// lumen.fs, lumen.http, lumen.process, lumen.time, lumen.log, lumen.system
#include "LuaUtil.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Http.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Process.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <map>
#include <memory>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef CopyFile
#undef CopyFile
#endif
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace lumen::lua::detail
{
	// ============================================================== fs
	namespace
	{
#define FS_CHECK(L, idx)                                         \
	std::string path = Str(L, idx);                              \
	if (!PathAllowed(L, path))                                   \
		return Fail(L, "access denied: " + path);

		int FExists(lua_State* L) { FS_CHECK(L, 1) lua_pushboolean(L, fs::Exists(path)); return 1; }
		int FIsFile(lua_State* L) { FS_CHECK(L, 1) lua_pushboolean(L, fs::IsFile(path)); return 1; }
		int FIsDir(lua_State* L) { FS_CHECK(L, 1) lua_pushboolean(L, fs::IsDirectory(path)); return 1; }
		int FSize(lua_State* L) { FS_CHECK(L, 1) if (!fs::IsFile(path)) return Fail(L, "no such file: " + path); lua_pushinteger(L, (lua_Integer)fs::FileSize(path)); return 1; }
		int FMtime(lua_State* L) { FS_CHECK(L, 1) if (!fs::Exists(path)) return Fail(L, "no such path: " + path); lua_pushnumber(L, (lua_Number)fs::LastWriteTimeMs(path) / 1000.0); return 1; }
		int FRead(lua_State* L)
		{
			FS_CHECK(L, 1)
			std::vector<uint8_t> d;
			if (!fs::ReadBinary(path, d))
				return Fail(L, "cannot read " + path);
			lua_pushlstring(L, (const char*)d.data(), d.size());
			return 1;
		}
		int FReadLines(lua_State* L)
		{
			FS_CHECK(L, 1)
			std::string t;
			if (!fs::ReadText(path, t))
				return Fail(L, "cannot read " + path);
			lua_newtable(L);
			lua_Integer i = 1;
			for (const std::string& l : str::SplitLines(t))
			{
				Push(L, l);
				lua_rawseti(L, -2, i++);
			}
			return 1;
		}
		int FWrite(lua_State* L)
		{
			FS_CHECK(L, 1)
			const std::string_view d = View(L, 2);
			fs::CreateDirs(fs::Parent(fs::Absolute(path)));
			const bool ok = OptBool(L, 3, false) ? fs::WriteBinary(path, d.data(), d.size()) : fs::WriteAtomic(path, d.data(), d.size());
			if (!ok)
				return Fail(L, "cannot write " + path);
			lua_pushboolean(L, 1);
			return 1;
		}
		int FAppend(lua_State* L)
		{
			FS_CHECK(L, 1)
			const std::string_view d = View(L, 2);
			fs::CreateDirs(fs::Parent(fs::Absolute(path)));
			FILE* f = fopen(fs::ToNative(path).c_str(), "ab");
			if (!f)
				return Fail(L, "cannot open " + path);
			const bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
			fclose(f);
			if (!ok)
				return Fail(L, "write failed: " + path);
			lua_pushboolean(L, 1);
			return 1;
		}
		int FMkdir(lua_State* L) { FS_CHECK(L, 1) if (!fs::CreateDirs(path)) return Fail(L, "cannot create " + path); lua_pushboolean(L, 1); return 1; }
		int FRemove(lua_State* L) { FS_CHECK(L, 1) if (!fs::Remove(path)) return Fail(L, "cannot remove " + path); lua_pushboolean(L, 1); return 1; }
		int FRemoveAll(lua_State* L)
		{
			FS_CHECK(L, 1)
			// refuse to wipe a filesystem root or the home directory by accident
			const std::string abs = fs::Canonical(path);
			if (abs.size() <= 3 || fs::PathEquals(abs, fs::Canonical(platform::UserHomeDir())))
				return Fail(L, "refusing to remove " + abs);
			if (!fs::RemoveAll(path))
				return Fail(L, "cannot remove " + path);
			lua_pushboolean(L, 1);
			return 1;
		}
		int FRename(lua_State* L)
		{
			FS_CHECK(L, 1)
			const std::string to = Str(L, 2);
			if (!PathAllowed(L, to))
				return Fail(L, "access denied: " + to);
			if (!fs::Rename(path, to))
				return Fail(L, "cannot rename " + path);
			lua_pushboolean(L, 1);
			return 1;
		}
		int FCopy(lua_State* L)
		{
			FS_CHECK(L, 1)
			const std::string to = Str(L, 2);
			if (!PathAllowed(L, to))
				return Fail(L, "access denied: " + to);
			fs::CreateDirs(fs::Parent(fs::Absolute(to)));
			const bool ok = fs::IsDirectory(path) ? fs::CopyDir(path, to, OptBool(L, 3, true)) : fs::CopyFile(path, to, OptBool(L, 3, true));
			if (!ok)
				return Fail(L, "cannot copy " + path);
			lua_pushboolean(L, 1);
			return 1;
		}
		// fs.list(dir, {recursive=, pattern=glob, files=, dirs=, hidden=}) -> array of {path,name,dir,size,mtime}
		int FList(lua_State* L)
		{
			FS_CHECK(L, 1)
			fs::ListOptions lo;
			lo.recursive = FieldBool(L, 2, "recursive", false);
			lo.includeHidden = FieldBool(L, 2, "hidden", true);
			const std::string pattern = FieldStr(L, 2, "pattern");
			const bool wantFiles = FieldBool(L, 2, "files", true), wantDirs = FieldBool(L, 2, "dirs", true);
			if (!fs::IsDirectory(path))
				return Fail(L, "not a directory: " + path);
			std::vector<fs::DirEntry> list = fs::ListDir(path, lo);
			std::sort(list.begin(), list.end(), [](const fs::DirEntry& a, const fs::DirEntry& b) { return a.path < b.path; });
			lua_newtable(L);
			lua_Integer i = 1;
			for (const fs::DirEntry& e : list)
			{
				if (e.isDirectory ? !wantDirs : !wantFiles)
					continue;
				const std::string rel = fs::Relative(e.path, path);
				if (!pattern.empty() && !fs::GlobMatch(pattern, rel) && !fs::GlobMatch(pattern, fs::FileName(rel)))
					continue;
				lua_newtable(L);
				Push(L, e.path);
				lua_setfield(L, -2, "path");
				Push(L, rel);
				lua_setfield(L, -2, "relative");
				Push(L, fs::FileName(e.path));
				lua_setfield(L, -2, "name");
				lua_pushboolean(L, e.isDirectory);
				lua_setfield(L, -2, "dir");
				lua_pushinteger(L, (lua_Integer)e.size);
				lua_setfield(L, -2, "size");
				lua_pushnumber(L, (lua_Number)e.mtimeMs / 1000.0);
				lua_setfield(L, -2, "mtime");
				lua_rawseti(L, -2, i++);
			}
			return 1;
		}
		int FGlob(lua_State* L)
		{
			lua_pushboolean(L, fs::GlobMatch(View(L, 1), View(L, 2)));
			return 1;
		}
		int FJoin(lua_State* L)
		{
			const int n = lua_gettop(L);
			std::string r;
			for (int i = 1; i <= n; ++i)
				r = i == 1 ? Str(L, 1) : fs::Join(r, Str(L, i));
			Push(L, fs::Normalize(r));
			return 1;
		}
		int FParent(lua_State* L) { Push(L, fs::Parent(Str(L, 1))); return 1; }
		int FName(lua_State* L) { Push(L, fs::FileName(Str(L, 1))); return 1; }
		int FStem(lua_State* L) { Push(L, fs::Stem(Str(L, 1))); return 1; }
		int FExt(lua_State* L) { Push(L, fs::Extension(Str(L, 1))); return 1; }
		int FAbsolute(lua_State* L) { Push(L, fs::Absolute(Str(L, 1))); return 1; }
		int FNormalize(lua_State* L) { Push(L, fs::Normalize(Str(L, 1))); return 1; }
		int FRelative(lua_State* L) { Push(L, fs::Relative(Str(L, 1), Str(L, 2))); return 1; }
		int FCwd(lua_State* L) { Push(L, fs::CurrentDir()); return 1; }
		int FTemp(lua_State* L) { Push(L, platform::TempDir()); return 1; }
		int FTempDir(lua_State* L) { Push(L, fs::CreateTempDir(lua_isnoneornil(L, 1) ? "lumen" : Str(L, 1))); return 1; }
		int FSanitize(lua_State* L) { Push(L, fs::SanitizeFileName(Str(L, 1))); return 1; }
		int FFindUp(lua_State* L) { Push(L, fs::FindUp(Str(L, 1), Str(L, 2))); return 1; }

		// polling watcher: detects created / modified / deleted files below a directory
		struct Watcher
		{
			std::string root;
			std::map<std::string, std::pair<uint64_t, uint64_t>> state;
			std::string pattern;
			void Scan(std::map<std::string, std::pair<uint64_t, uint64_t>>& out) const
			{
				fs::ListOptions lo;
				lo.recursive = true;
				fs::Walk(root, lo, [&](const fs::DirEntry& e) {
					if (!e.isDirectory && (pattern.empty() || fs::GlobMatch(pattern, fs::Relative(e.path, root)) || fs::GlobMatch(pattern, fs::FileName(e.path))))
						out[e.path] = {e.mtimeMs, e.size};
					return true;
				});
			}
		};
		int FWatch(lua_State* L)
		{
			FS_CHECK(L, 1)
			Watcher* w = NewUdata<Watcher>(L, "lumen.fs.watcher");
			w->root = path;
			w->pattern = FieldStr(L, 2, "pattern");
			w->Scan(w->state);
			return 1;
		}
		int WPoll(lua_State* L)
		{
			Watcher* w = static_cast<Watcher*>(luaL_checkudata(L, 1, "lumen.fs.watcher"));
			std::map<std::string, std::pair<uint64_t, uint64_t>> now;
			w->Scan(now);
			lua_newtable(L);
			lua_Integer i = 1;
			auto add = [&](const std::string& p, const char* ev) {
				lua_newtable(L);
				Push(L, p);
				lua_setfield(L, -2, "path");
				lua_pushstring(L, ev);
				lua_setfield(L, -2, "event");
				lua_rawseti(L, -2, i++);
			};
			for (auto& kv : now)
			{
				auto it = w->state.find(kv.first);
				if (it == w->state.end())
					add(kv.first, "created");
				else if (it->second != kv.second)
					add(kv.first, "modified");
			}
			for (auto& kv : w->state)
				if (!now.count(kv.first))
					add(kv.first, "deleted");
			w->state = std::move(now);
			return 1;
		}
	} // namespace

	int OpenFs(lua_State* L)
	{
		static const luaL_Reg wm[] = {{"poll", WPoll}, {nullptr, nullptr}};
		MakeClass(L, "lumen.fs.watcher", wm, nullptr, GcUdata<Watcher>);
		static const luaL_Reg fns[] = {{"exists", FExists}, {"is_file", FIsFile}, {"is_dir", FIsDir}, {"size", FSize}, {"mtime", FMtime}, {"read", FRead},
			{"read_lines", FReadLines}, {"write", FWrite}, {"append", FAppend}, {"mkdir", FMkdir}, {"remove", FRemove}, {"remove_all", FRemoveAll},
			{"rename", FRename}, {"copy", FCopy}, {"list", FList}, {"glob", FGlob}, {"join", FJoin}, {"parent", FParent}, {"name", FName},
			{"stem", FStem}, {"ext", FExt}, {"absolute", FAbsolute}, {"normalize", FNormalize}, {"relative", FRelative}, {"cwd", FCwd},
			{"temp_dir", FTemp}, {"make_temp_dir", FTempDir}, {"sanitize", FSanitize}, {"find_up", FFindUp}, {"watch", FWatch}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== http
	namespace
	{
		struct AsyncReq
		{
			std::atomic<bool> done{false};
			std::atomic<bool> cancel{false};
			bool ok = false;
			int status = 0;
			std::string body, error;
			uint64_t downloaded = 0, total = 0;
			std::atomic<uint64_t> progressDone{0}, progressTotal{0};
		};
		struct ReqHandle
		{
			std::shared_ptr<AsyncReq> req;
		};

		bool NetAllowed(lua_State* L) { return Policy(L).allowNetwork; }

		std::vector<std::string> ReadHeaders(lua_State* L, int idx)
		{
			std::vector<std::string> h;
			if (!lua_istable(L, idx))
				return h;
			lua_getfield(L, idx, "headers");
			if (lua_istable(L, -1))
			{
				lua_pushnil(L);
				while (lua_next(L, -2))
				{
					if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1))
						h.push_back(std::string(lua_tostring(L, -2)) + ": " + lua_tostring(L, -1));
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
			return h;
		}

		int PushResult(lua_State* L, bool ok, int status, const std::string& body, const std::string& error)
		{
			if (!ok)
				return Fail(L, error);
			lua_newtable(L);
			lua_pushinteger(L, status);
			lua_setfield(L, -2, "status");
			lua_pushboolean(L, status >= 200 && status < 300);
			lua_setfield(L, -2, "ok");
			Push(L, body);
			lua_setfield(L, -2, "body");
			return 1;
		}

		int HGet(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			const std::string url = Str(L, 1);
			if (!http::IsSafeUrl(url, FieldBool(L, 2, "allow_http", false)))
				return Fail(L, "unsafe url (https required): " + url);
			http::GetResult r = http::Get(url, ReadHeaders(L, 2), (uint32_t)(FieldNum(L, 2, "timeout", 30) * 1000));
			return PushResult(L, r.ok, r.status, r.body, r.error);
		}
		int HGetAsync(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			const std::string url = Str(L, 1);
			const bool allowHttp = FieldBool(L, 2, "allow_http", false);
			if (!http::IsSafeUrl(url, allowHttp))
				return Fail(L, "unsafe url (https required): " + url);
			const std::vector<std::string> headers = ReadHeaders(L, 2);
			const uint32_t timeout = (uint32_t)(FieldNum(L, 2, "timeout", 30) * 1000);
			ReqHandle* h = NewUdata<ReqHandle>(L, "lumen.http.request");
			h->req = std::make_shared<AsyncReq>();
			std::shared_ptr<AsyncReq> r = h->req;
			std::thread([r, url, headers, timeout] {
				http::GetResult g = http::Get(url, headers, timeout);
				r->ok = g.ok;
				r->status = g.status;
				r->body = std::move(g.body);
				r->error = std::move(g.error);
				r->done = true;
			}).detach();
			return 1;
		}
		int HDownload(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			http::DownloadOptions o;
			o.url = Str(L, 1);
			o.destPath = Str(L, 2);
			if (!PathAllowed(L, o.destPath))
				return Fail(L, "access denied: " + o.destPath);
			o.sha256 = FieldStr(L, 3, "sha256");
			o.allowInsecureHttp = FieldBool(L, 3, "allow_http", false);
			o.headers = ReadHeaders(L, 3);
			if (!http::IsSafeUrl(o.url, o.allowInsecureHttp))
				return Fail(L, "unsafe url (https required): " + o.url);
			Status s = http::Download(o);
			if (!s)
				return Fail(L, s.message);
			lua_pushboolean(L, 1);
			return 1;
		}
		int HDownloadAsync(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			http::DownloadOptions o;
			o.url = Str(L, 1);
			o.destPath = Str(L, 2);
			if (!PathAllowed(L, o.destPath))
				return Fail(L, "access denied: " + o.destPath);
			o.sha256 = FieldStr(L, 3, "sha256");
			o.allowInsecureHttp = FieldBool(L, 3, "allow_http", false);
			o.headers = ReadHeaders(L, 3);
			if (!http::IsSafeUrl(o.url, o.allowInsecureHttp))
				return Fail(L, "unsafe url (https required): " + o.url);
			ReqHandle* h = NewUdata<ReqHandle>(L, "lumen.http.request");
			h->req = std::make_shared<AsyncReq>();
			std::shared_ptr<AsyncReq> r = h->req;
			o.cancel = &r->cancel;
			o.progress = [r](uint64_t d, uint64_t t) {
				r->progressDone = d;
				r->progressTotal = t;
			};
			std::thread([r, o] {
				Status s = http::Download(o);
				r->ok = s.ok;
				r->error = s.message;
				r->status = s.ok ? 200 : 0;
				r->done = true;
			}).detach();
			return 1;
		}
		int RPoll(lua_State* L)
		{
			ReqHandle* h = static_cast<ReqHandle*>(luaL_checkudata(L, 1, "lumen.http.request"));
			if (!h->req->done)
			{
				lua_pushnil(L);
				lua_pushnil(L);
				lua_newtable(L);
				lua_pushinteger(L, (lua_Integer)h->req->progressDone.load());
				lua_setfield(L, -2, "done");
				lua_pushinteger(L, (lua_Integer)h->req->progressTotal.load());
				lua_setfield(L, -2, "total");
				return 3; // nil, nil, progress
			}
			return PushResult(L, h->req->ok, h->req->status, h->req->body, h->req->error);
		}
		int RDone(lua_State* L)
		{
			lua_pushboolean(L, static_cast<ReqHandle*>(luaL_checkudata(L, 1, "lumen.http.request"))->req->done.load());
			return 1;
		}
		int RCancel(lua_State* L)
		{
			static_cast<ReqHandle*>(luaL_checkudata(L, 1, "lumen.http.request"))->req->cancel = true;
			return 0;
		}
		int HUrlEncode(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::string out;
			for (unsigned char c : s)
			{
				if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
					out += (char)c;
				else
					out += StrFormat("%%%02X", c);
			}
			Push(L, out);
			return 1;
		}
		int HUrlDecode(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			std::string out;
			for (size_t i = 0; i < s.size(); ++i)
			{
				if (s[i] == '%' && i + 2 < s.size() + 0 && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2]))
				{
					out += (char)strtol(std::string(s.substr(i + 1, 2)).c_str(), nullptr, 16);
					i += 2;
				}
				else if (s[i] == '+')
					out += ' ';
				else
					out += s[i];
			}
			Push(L, out);
			return 1;
		}
		int HParseUrl(lua_State* L)
		{
			std::string u = Str(L, 1);
			lua_newtable(L);
			auto set = [&](const char* k, const std::string& v) { Push(L, v); lua_setfield(L, -2, k); };
			size_t p = u.find("://");
			if (p == std::string::npos)
				return Fail(L, "not an absolute url");
			set("scheme", u.substr(0, p));
			std::string rest = u.substr(p + 3);
			std::string fragment, query;
			if (size_t h = rest.find('#'); h != std::string::npos) { fragment = rest.substr(h + 1); rest.resize(h); }
			if (size_t q = rest.find('?'); q != std::string::npos) { query = rest.substr(q + 1); rest.resize(q); }
			std::string host = rest, path = "/";
			if (size_t s = rest.find('/'); s != std::string::npos) { host = rest.substr(0, s); path = rest.substr(s); }
			if (size_t at = host.rfind('@'); at != std::string::npos) { set("userinfo", host.substr(0, at)); host = host.substr(at + 1); }
			if (size_t c = host.rfind(':'); c != std::string::npos && host.find(']') == std::string::npos) { lua_pushinteger(L, atoi(host.c_str() + c + 1)); lua_setfield(L, -2, "port"); host.resize(c); }
			set("host", host);
			set("path", path);
			set("query", query);
			set("fragment", fragment);
			lua_newtable(L);
			for (const std::string& kv : str::Split(query, '&', false))
			{
				const size_t e = kv.find('=');
				const std::string k = e == std::string::npos ? kv : kv.substr(0, e), v = e == std::string::npos ? "" : kv.substr(e + 1);
				Push(L, k);
				Push(L, v);
				lua_settable(L, -3);
			}
			lua_setfield(L, -2, "params");
			return 1;
		}
	} // namespace

	int OpenHttp(lua_State* L)
	{
		static const luaL_Reg rm[] = {{"poll", RPoll}, {"done", RDone}, {"cancel", RCancel}, {nullptr, nullptr}};
		MakeClass(L, "lumen.http.request", rm, nullptr, GcUdata<ReqHandle>);
		static const luaL_Reg fns[] = {{"get", HGet}, {"get_async", HGetAsync}, {"download", HDownload}, {"download_async", HDownloadAsync},
			{"url_encode", HUrlEncode}, {"url_decode", HUrlDecode}, {"parse_url", HParseUrl}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== process
	namespace
	{
		ProcessOptions ReadProc(lua_State* L, int progIdx, int argsIdx, int optIdx)
		{
			ProcessOptions o;
			o.program = Str(L, progIdx);
			if (lua_istable(L, argsIdx))
			{
				const lua_Integer n = (lua_Integer)lua_rawlen(L, argsIdx);
				for (lua_Integer i = 1; i <= n; ++i)
				{
					lua_rawgeti(L, argsIdx, i);
					o.args.push_back(lua_isstring(L, -1) ? std::string(lua_tostring(L, -1)) : std::string());
					lua_pop(L, 1);
				}
			}
			o.workingDir = FieldStr(L, optIdx, "cwd");
			o.timeoutMs = (uint32_t)(FieldNum(L, optIdx, "timeout", 0) * 1000);
			o.stdinText = FieldStr(L, optIdx, "stdin");
			o.mergeStderr = FieldBool(L, optIdx, "merge_stderr", true);
			if (lua_istable(L, optIdx))
			{
				lua_getfield(L, optIdx, "env");
				if (lua_istable(L, -1))
				{
					lua_pushnil(L);
					while (lua_next(L, -2))
					{
						if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1))
							o.env.push_back({lua_tostring(L, -2), lua_tostring(L, -1)});
						lua_pop(L, 1);
					}
				}
				lua_pop(L, 1);
			}
			return o;
		}
		int PushProcResult(lua_State* L, const ProcessResult& r)
		{
			lua_newtable(L);
			lua_pushboolean(L, r.Success());
			lua_setfield(L, -2, "ok");
			lua_pushboolean(L, r.started);
			lua_setfield(L, -2, "started");
			lua_pushinteger(L, r.exitCode);
			lua_setfield(L, -2, "code");
			lua_pushboolean(L, r.timedOut);
			lua_setfield(L, -2, "timed_out");
			Push(L, r.output);
			lua_setfield(L, -2, "output");
			Push(L, r.error);
			lua_setfield(L, -2, "stderr");
			if (!r.startError.empty())
			{
				Push(L, r.startError);
				lua_setfield(L, -2, "error");
			}
			return 1;
		}
		int PRun(lua_State* L)
		{
			if (!Policy(L).allowProcess)
				return Fail(L, "starting processes is disabled");
			ProcessOptions o = ReadProc(L, 1, 2, 3);
			return PushProcResult(L, RunProcess(o));
		}
		struct ProcHandle
		{
			std::shared_ptr<AsyncProcess> p;
		};
		int PSpawn(lua_State* L)
		{
			if (!Policy(L).allowProcess)
				return Fail(L, "starting processes is disabled");
			ProcessOptions o = ReadProc(L, 1, 2, 3);
			ProcHandle* h = NewUdata<ProcHandle>(L, "lumen.process");
			h->p = AsyncProcess::Start(o);
			return 1;
		}
		ProcHandle* CheckP(lua_State* L) { return static_cast<ProcHandle*>(luaL_checkudata(L, 1, "lumen.process")); }
		int PmRunning(lua_State* L) { lua_pushboolean(L, CheckP(L)->p->IsRunning()); return 1; }
		int PmOutput(lua_State* L) { Push(L, CheckP(L)->p->OutputSoFar()); return 1; }
		int PmKill(lua_State* L) { CheckP(L)->p->Cancel(); return 0; }
		int PmWait(lua_State* L) { return PushProcResult(L, CheckP(L)->p->Wait()); }
		int PWhich(lua_State* L)
		{
			const std::string p = FindExecutable(Str(L, 1));
			if (p.empty())
				lua_pushnil(L);
			else
				Push(L, p);
			return 1;
		}
		int PQuote(lua_State* L) { Push(L, QuoteArgument(Str(L, 1))); return 1; }
	} // namespace

	int OpenProcess(lua_State* L)
	{
		static const luaL_Reg pm[] = {{"running", PmRunning}, {"output", PmOutput}, {"kill", PmKill}, {"wait", PmWait}, {nullptr, nullptr}};
		MakeClass(L, "lumen.process", pm, nullptr, GcUdata<ProcHandle>);
		static const luaL_Reg fns[] = {{"run", PRun}, {"spawn", PSpawn}, {"which", PWhich}, {"quote", PQuote}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== time
	namespace
	{
		int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d)
		{
			y -= m <= 2;
			const int64_t era = (y >= 0 ? y : y - 399) / 400;
			const unsigned yoe = (unsigned)(y - era * 400);
			const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return era * 146097 + (int64_t)doe - 719468;
		}
		void CivilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d)
		{
			z += 719468;
			const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
			const unsigned doe = (unsigned)(z - era * 146097);
			const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
			y = (int64_t)yoe + era * 400;
			const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
			const unsigned mp = (5 * doy + 2) / 153;
			d = doy - (153 * mp + 2) / 5 + 1;
			m = mp < 10 ? mp + 3 : mp - 9;
			y += m <= 2;
		}
		struct DateParts { int64_t year; int month, day, hour, min, sec, wday, yday; };
		DateParts Split(int64_t t)
		{
			int64_t days = t / 86400;
			int64_t rem = t % 86400;
			if (rem < 0) { rem += 86400; --days; }
			DateParts p;
			unsigned m, d;
			CivilFromDays(days, p.year, m, d);
			p.month = (int)m;
			p.day = (int)d;
			p.hour = (int)(rem / 3600);
			p.min = (int)(rem % 3600 / 60);
			p.sec = (int)(rem % 60);
			p.wday = (int)(((days % 7) + 11) % 7); // 0 = Sunday (1970-01-01 was a Thursday)
			p.yday = (int)(days - DaysFromCivil(p.year, 1, 1));
			return p;
		}
		int64_t LocalOffset(int64_t t)
		{
			time_t tt = (time_t)t;
			struct tm lt;
#ifdef _WIN32
			localtime_s(&lt, &tt);
#else
			localtime_r(&tt, &lt);
#endif
			struct tm gt;
#ifdef _WIN32
			gmtime_s(&gt, &tt);
#else
			gmtime_r(&tt, &gt);
#endif
			gt.tm_isdst = lt.tm_isdst;
			return (int64_t)(difftime(mktime(&lt), mktime(&gt)));
		}

		int TNow(lua_State* L)
		{
			lua_pushnumber(L, std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());
			return 1;
		}
		int TMono(lua_State* L)
		{
			lua_pushnumber(L, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
			return 1;
		}
		int TSleep(lua_State* L)
		{
			const double s = luaL_checknumber(L, 1);
			if (s > 0)
				std::this_thread::sleep_for(std::chrono::duration<double>(std::min(s, 3600.0)));
			return 0;
		}
		// time.date(t, utc) -> {year,month,day,hour,min,sec,wday,yday}
		int TDate(lua_State* L)
		{
			int64_t t = lua_isnoneornil(L, 1) ? (int64_t)std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() : (int64_t)luaL_checknumber(L, 1);
			const bool utc = OptBool(L, 2, false);
			if (!utc)
				t += LocalOffset(t);
			const DateParts p = Split(t);
			lua_newtable(L);
			auto set = [&](const char* k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
			set("year", p.year); set("month", p.month); set("day", p.day); set("hour", p.hour); set("min", p.min); set("sec", p.sec);
			set("wday", p.wday + 1); set("yday", p.yday + 1);
			return 1;
		}
		// time.make{year,month,day,hour,min,sec} -> unix seconds (UTC); utc=false interprets as local time
		int TMake(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const int64_t y = (int64_t)FieldNum(L, 1, "year", 1970);
			int64_t mon = (int64_t)FieldNum(L, 1, "month", 1) - 1;
			const int64_t yy = y + (mon >= 0 ? mon / 12 : -((11 - mon) / 12));
			mon = ((mon % 12) + 12) % 12;
			int64_t t = DaysFromCivil(yy, (unsigned)mon + 1, 1) * 86400 + (int64_t)(FieldNum(L, 1, "day", 1) - 1) * 86400 + (int64_t)FieldNum(L, 1, "hour", 0) * 3600 +
				(int64_t)FieldNum(L, 1, "min", 0) * 60 + (int64_t)FieldNum(L, 1, "sec", 0);
			if (!OptBool(L, 2, true))
				t -= LocalOffset(t);
			lua_pushinteger(L, t);
			return 1;
		}
		// strftime-like: %Y %m %d %H %M %S %y %b %B %a %A %j %p %I %Z(UTC) %%
		int TFormat(lua_State* L)
		{
			const std::string fmt = lua_isnoneornil(L, 1) ? "%Y-%m-%d %H:%M:%S" : Str(L, 1);
			int64_t t = lua_isnoneornil(L, 2) ? (int64_t)std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() : (int64_t)luaL_checknumber(L, 2);
			const bool utc = OptBool(L, 3, false);
			if (!utc)
				t += LocalOffset(t);
			const DateParts p = Split(t);
			static const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
			static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
			std::string out;
			for (size_t i = 0; i < fmt.size(); ++i)
			{
				if (fmt[i] != '%' || i + 1 >= fmt.size())
				{
					out += fmt[i];
					continue;
				}
				switch (fmt[++i])
				{
				case 'Y': out += StrFormat("%04lld", (long long)p.year); break;
				case 'y': out += StrFormat("%02d", (int)(p.year % 100)); break;
				case 'm': out += StrFormat("%02d", p.month); break;
				case 'd': out += StrFormat("%02d", p.day); break;
				case 'e': out += StrFormat("%2d", p.day); break;
				case 'H': out += StrFormat("%02d", p.hour); break;
				case 'I': out += StrFormat("%02d", p.hour % 12 == 0 ? 12 : p.hour % 12); break;
				case 'M': out += StrFormat("%02d", p.min); break;
				case 'S': out += StrFormat("%02d", p.sec); break;
				case 'p': out += p.hour < 12 ? "AM" : "PM"; break;
				case 'B': out += months[p.month - 1]; break;
				case 'b': out += std::string(months[p.month - 1]).substr(0, 3); break;
				case 'A': out += days[p.wday]; break;
				case 'a': out += std::string(days[p.wday]).substr(0, 3); break;
				case 'j': out += StrFormat("%03d", p.yday + 1); break;
				case 'Z': out += utc ? "UTC" : ""; break;
				case 'F': out += StrFormat("%04lld-%02d-%02d", (long long)p.year, p.month, p.day); break;
				case 'T': out += StrFormat("%02d:%02d:%02d", p.hour, p.min, p.sec); break;
				case '%': out += '%'; break;
				default: out += '%'; out += fmt[i];
				}
			}
			Push(L, out);
			return 1;
		}
		// ISO 8601 "2026-10-21T14:08:00Z" / "+05:00" / date only
		int TParseIso(lua_State* L)
		{
			const std::string s = Str(L, 1);
			int Y, M, D, h = 0, m = 0, sec = 0, n = 0;
			if (sscanf(s.c_str(), "%d-%d-%d%n", &Y, &M, &D, &n) < 3)
				return Fail(L, "not an ISO 8601 date");
			size_t pos = (size_t)n;
			int64_t off = 0;
			if (pos < s.size() && (s[pos] == 'T' || s[pos] == ' '))
			{
				int n2 = 0;
				const int got = sscanf(s.c_str() + pos + 1, "%d:%d%n", &h, &m, &n2);
				if (got < 2)
					return Fail(L, "bad time part");
				pos += 1 + (size_t)n2;
				if (pos < s.size() && s[pos] == ':')
				{
					int n3 = 0;
					sscanf(s.c_str() + pos + 1, "%d%n", &sec, &n3);
					pos += 1 + (size_t)n3;
				}
				if (pos < s.size() && s[pos] == '.')
				{
					++pos;
					while (pos < s.size() && isdigit((unsigned char)s[pos]))
						++pos;
				}
				if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
				{
					int oh = 0, om = 0;
					sscanf(s.c_str() + pos + 1, "%d:%d", &oh, &om);
					off = (oh * 3600 + om * 60) * (s[pos] == '-' ? -1 : 1);
				}
			}
			lua_pushinteger(L, DaysFromCivil(Y, (unsigned)M, (unsigned)D) * 86400 + h * 3600 + m * 60 + sec - off);
			return 1;
		}
		int TIso(lua_State* L)
		{
			const int64_t t = lua_isnoneornil(L, 1) ? (int64_t)std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count() : (int64_t)luaL_checknumber(L, 1);
			const DateParts p = Split(t);
			Push(L, StrFormat("%04lld-%02d-%02dT%02d:%02d:%02dZ", (long long)p.year, p.month, p.day, p.hour, p.min, p.sec));
			return 1;
		}
		int TOffset(lua_State* L)
		{
			lua_pushinteger(L, LocalOffset(lua_isnoneornil(L, 1) ? (int64_t)time(nullptr) : (int64_t)luaL_checknumber(L, 1)));
			return 1;
		}
		int TDuration(lua_State* L)
		{
			double s = luaL_checknumber(L, 1);
			const bool neg = s < 0;
			s = std::fabs(s);
			const int64_t t = (int64_t)s;
			const int64_t d = t / 86400, h = t % 86400 / 3600, m = t % 3600 / 60, sec = t % 60;
			std::string out;
			if (d) out += StrFormat("%lldd ", (long long)d);
			if (d || h) out += StrFormat("%lldh ", (long long)h);
			if (d || h || m) out += StrFormat("%lldm ", (long long)m);
			out += StrFormat("%llds", (long long)sec);
			Push(L, (neg ? "-" : "") + out);
			return 1;
		}
		// Stopwatch
		struct Stopwatch
		{
			std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
			double accumulated = 0;
			bool running = true;
			double Elapsed() const
			{
				return accumulated + (running ? std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() : 0.0);
			}
		};
		Stopwatch* CheckSw(lua_State* L) { return static_cast<Stopwatch*>(luaL_checkudata(L, 1, "lumen.stopwatch")); }
		int TStopwatch(lua_State* L)
		{
			const bool startStopped = OptBool(L, 1, false); // read before the new userdata occupies the stack
			Stopwatch* s = NewUdata<Stopwatch>(L, "lumen.stopwatch");
			s->running = !startStopped;
			s->start = std::chrono::steady_clock::now();
			return 1;
		}
		int SwElapsed(lua_State* L) { lua_pushnumber(L, CheckSw(L)->Elapsed()); return 1; }
		int SwStop(lua_State* L) { Stopwatch* s = CheckSw(L); if (s->running) { s->accumulated = s->Elapsed(); s->running = false; } lua_pushnumber(L, s->accumulated); return 1; }
		int SwStart(lua_State* L) { Stopwatch* s = CheckSw(L); if (!s->running) { s->start = std::chrono::steady_clock::now(); s->running = true; } return 0; }
		int SwReset(lua_State* L) { Stopwatch* s = CheckSw(L); s->accumulated = 0; s->start = std::chrono::steady_clock::now(); return 0; }
	} // namespace

	int OpenTime(lua_State* L)
	{
		static const luaL_Reg sm[] = {{"elapsed", SwElapsed}, {"stop", SwStop}, {"start", SwStart}, {"reset", SwReset}, {nullptr, nullptr}};
		MakeClass(L, "lumen.stopwatch", sm, nullptr, GcUdata<Stopwatch>);
		static const luaL_Reg fns[] = {{"now", TNow}, {"monotonic", TMono}, {"sleep", TSleep}, {"date", TDate}, {"make", TMake}, {"format", TFormat},
			{"parse_iso", TParseIso}, {"iso", TIso}, {"utc_offset", TOffset}, {"duration", TDuration}, {"stopwatch", TStopwatch}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== log
	namespace
	{
		int DoLog(lua_State* L, LogLevel lv, const std::string& channel, int first)
		{
			std::string msg;
			const int n = lua_gettop(L);
			for (int i = first; i <= n; ++i)
			{
				if (i > first)
					msg += ' ';
				size_t len;
				const char* s = luaL_tolstring(L, i, &len);
				msg.append(s, len);
				lua_pop(L, 1);
			}
			Log::Write(lv, channel, msg);
			return 0;
		}
		int LDebug(lua_State* L) { return DoLog(L, LogLevel::Debug, "lua", 1); }
		int LInfo(lua_State* L) { return DoLog(L, LogLevel::Info, "lua", 1); }
		int LWarn(lua_State* L) { return DoLog(L, LogLevel::Warn, "lua", 1); }
		int LError(lua_State* L) { return DoLog(L, LogLevel::Error, "lua", 1); }
		// log.channel("name") -> table with debug/info/warn/error bound to the channel
		int CDebug(lua_State* L) { return DoLog(L, LogLevel::Debug, Str(L, lua_upvalueindex(1)), 1); }
		int CInfo(lua_State* L) { return DoLog(L, LogLevel::Info, Str(L, lua_upvalueindex(1)), 1); }
		int CWarn(lua_State* L) { return DoLog(L, LogLevel::Warn, Str(L, lua_upvalueindex(1)), 1); }
		int CError(lua_State* L) { return DoLog(L, LogLevel::Error, Str(L, lua_upvalueindex(1)), 1); }
		int LChannel(lua_State* L)
		{
			luaL_checkstring(L, 1);
			lua_newtable(L);
			const std::pair<const char*, lua_CFunction> f[] = {{"debug", CDebug}, {"info", CInfo}, {"warn", CWarn}, {"error", CError}};
			for (auto& p : f)
			{
				lua_pushvalue(L, 1);
				lua_pushcclosure(L, p.second, 1);
				lua_setfield(L, -2, p.first);
			}
			return 1;
		}
		int LSetLevel(lua_State* L)
		{
			const std::string n = Str(L, 1);
			LogLevel lv = LogLevel::Info;
			if (n == "debug") lv = LogLevel::Debug;
			else if (n == "info") lv = LogLevel::Info;
			else if (n == "warn") lv = LogLevel::Warn;
			else if (n == "error") lv = LogLevel::Error;
			else return luaL_error(L, "unknown level '%s'", n.c_str());
			Log::SetMinLevel(lv);
			return 0;
		}
	} // namespace

	int OpenLog(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"debug", LDebug}, {"info", LInfo}, {"warn", LWarn}, {"error", LError}, {"channel", LChannel}, {"set_level", LSetLevel}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ============================================================== system
	namespace
	{
		int SHost(lua_State* L) { Push(L, platform::HostTriple()); return 1; }
		int SOs(lua_State* L) { const std::string t = platform::HostTriple(); Push(L, t.substr(0, t.find('-'))); return 1; }
		int SArch(lua_State* L) { const std::string t = platform::HostTriple(); Push(L, t.substr(t.find('-') + 1)); return 1; }
		int SCpus(lua_State* L) { lua_pushinteger(L, platform::LogicalCpuCount()); return 1; }
		int SGetEnv(lua_State* L)
		{
			auto v = platform::GetEnv(Str(L, 1));
			if (v)
				Push(L, *v);
			else
				lua_pushnil(L);
			return 1;
		}
		int SSetEnv(lua_State* L)
		{
			if (!Policy(L).allowProcess)
				return Fail(L, "changing the environment is disabled");
			lua_pushboolean(L, platform::SetEnv(Str(L, 1), Str(L, 2)));
			return 1;
		}
		int SHome(lua_State* L) { Push(L, platform::UserHomeDir()); return 1; }
		int SUserData(lua_State* L) { Push(L, platform::UserDataDir()); return 1; }
		int SExeDir(lua_State* L) { Push(L, platform::ExecutableDir()); return 1; }
		int SOpen(lua_State* L)
		{
			if (!Policy(L).allowProcess)
				return Fail(L, "opening applications is disabled");
			lua_pushboolean(L, platform::OpenWithDefaultApp(Str(L, 1)));
			return 1;
		}
		int SReveal(lua_State* L)
		{
			if (!Policy(L).allowProcess)
				return Fail(L, "opening applications is disabled");
			lua_pushboolean(L, platform::OpenInFileManager(Str(L, 1)));
			return 1;
		}
		int SPid(lua_State* L) { lua_pushinteger(L, platform::CurrentProcessId()); return 1; }
		// total / available physical memory in bytes
		int SMemory(lua_State* L)
		{
			uint64_t total = 0, avail = 0;
#ifdef _WIN32
			MEMORYSTATUSEX m;
			m.dwLength = sizeof(m);
			if (GlobalMemoryStatusEx(&m))
			{
				total = m.ullTotalPhys;
				avail = m.ullAvailPhys;
			}
#elif defined(__linux__)
			std::string t;
			if (fs::ReadText("/proc/meminfo", t))
			{
				for (const std::string& l : str::SplitLines(t))
				{
					unsigned long long kb;
					if (sscanf(l.c_str(), "MemTotal: %llu kB", &kb) == 1) total = kb * 1024;
					else if (sscanf(l.c_str(), "MemAvailable: %llu kB", &kb) == 1) avail = kb * 1024;
				}
			}
#endif
			lua_pushinteger(L, (lua_Integer)total);
			lua_pushinteger(L, (lua_Integer)avail);
			return 2;
		}
		int SEnvAll(lua_State* L)
		{
			lua_newtable(L);
#ifndef _WIN32
			for (char** e = environ; e && *e; ++e)
			{
				const char* eq = strchr(*e, '=');
				if (!eq)
					continue;
				lua_pushlstring(L, *e, (size_t)(eq - *e));
				lua_pushstring(L, eq + 1);
				lua_settable(L, -3);
			}
#endif
			return 1;
		}
	} // namespace

	int OpenSystem(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"host", SHost}, {"os", SOs}, {"arch", SArch}, {"cpu_count", SCpus}, {"getenv", SGetEnv}, {"setenv", SSetEnv},
			{"environment", SEnvAll}, {"home_dir", SHome}, {"user_data_dir", SUserData}, {"exe_dir", SExeDir}, {"open", SOpen}, {"reveal", SReveal},
			{"pid", SPid}, {"memory", SMemory}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
