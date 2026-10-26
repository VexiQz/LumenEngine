#include "lumen/core/Packages.h"

#include "lumen/core/Archive.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Http.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Process.h"
#include "lumen/core/Runtime.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <deque>
#include <set>

namespace lumen
{
	namespace
	{
		// Built-in registry: curated, widely used libraries. Entries only describe where the code lives and how it
		// is consumed; nothing is downloaded until a project asks for it. Users extend this through
		// <runtime>/config/registry.json (same schema).
		const char* kBuiltinRegistry = R"JSON({
"packages": {
  "lume":        {"description": "rxi/lume: collection of functions for Lua, geared towards game development", "kind": "lua", "git": "https://github.com/rxi/lume.git", "ref": "master", "lua": ["."]},
  "classic":     {"description": "rxi/classic: tiny class module for Lua", "kind": "lua", "git": "https://github.com/rxi/classic.git", "ref": "master", "lua": ["."]},
  "json":        {"description": "rxi/json.lua: pure Lua JSON", "kind": "lua", "git": "https://github.com/rxi/json.lua.git", "ref": "master", "lua": ["."]},
  "flux":        {"description": "rxi/flux: fast, lightweight tweening library for Lua", "kind": "lua", "git": "https://github.com/rxi/flux.git", "ref": "master", "lua": ["."]},
  "middleclass": {"description": "kikito/middleclass: object orientation for Lua", "kind": "lua", "git": "https://github.com/kikito/middleclass.git", "ref": "master", "lua": ["."]},
  "inspect":     {"description": "kikito/inspect.lua: human readable representation of Lua tables", "kind": "lua", "git": "https://github.com/kikito/inspect.lua.git", "ref": "master", "lua": ["."]},
  "nlohmann-json": {"description": "JSON for Modern C++ (header only)", "kind": "native", "git": "https://github.com/nlohmann/json.git", "ref": "v3.11.3", "include": ["single_include"]},
  "glm":         {"description": "OpenGL Mathematics (header only)", "kind": "native", "git": "https://github.com/g-truc/glm.git", "ref": "1.0.1", "include": ["."]},
  "entt":        {"description": "Fast and reliable entity component system (header only)", "kind": "native", "git": "https://github.com/skypjack/entt.git", "ref": "v3.13.2", "include": ["src"]},
  "fmt":         {"description": "{fmt}: formatting library (header only mode)", "kind": "native", "git": "https://github.com/fmtlib/fmt.git", "ref": "11.0.2", "include": ["include"], "defines": ["FMT_HEADER_ONLY=1"]},
  "stb":         {"description": "stb single-file public domain libraries", "kind": "native", "git": "https://github.com/nothings/stb.git", "ref": "master", "include": ["."]},
  "magic-enum":  {"description": "Static reflection for enums (header only)", "kind": "native", "git": "https://github.com/Neargye/magic_enum.git", "ref": "v0.9.6", "include": ["include"]}
}})JSON";
	} // namespace

	bool PackageManager::IsValidName(const std::string& name)
	{
		if (name.empty() || name.size() > 64 || name[0] == '.' || name[0] == '-')
			return false;
		for (char c : name)
		{
			const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
			if (!ok)
				return false;
		}
		return name.find("..") == std::string::npos;
	}

	// ----------------------------------------------------------------------------------------------------
	std::vector<RegistryEntry> PackageManager::Registry() const
	{
		std::vector<RegistryEntry> out;
		std::set<std::string> seen;
		const auto load = [&](const Json& root) {
			const Json& pk = root.Get("packages");
			for (const Json::Member& m : pk.Members())
			{
				if (!IsValidName(m.first))
					continue;
				const Json& e = m.second;
				RegistryEntry r;
				r.name = m.first;
				r.description = e.GetString("description");
				r.kind = e.GetString("kind", "lua");
				r.spec.name = m.first;
				r.spec.version = e.GetString("version", e.GetString("ref", "latest"));
				if (!e.GetString("git").empty())
				{
					r.spec.source = PackageSource::Git;
					r.spec.url = e.GetString("git");
				}
				else
				{
					r.spec.source = PackageSource::Archive;
					r.spec.url = e.GetString("archive");
					r.spec.sha256 = e.GetString("sha256");
				}
				r.spec.ref = e.GetString("ref");
				r.spec.subdir = e.GetString("subdir");
				r.spec.lua = e.GetStringArray("lua");
				r.spec.include = e.GetStringArray("include");
				r.spec.shaders = e.GetStringArray("shaders");
				r.spec.defines = e.GetStringArray("defines");
				r.spec.cmakeTargets = e.GetStringArray("cmakeTargets");
				// A user entry replaces a built-in one with the same name.
				for (RegistryEntry& existing : out)
				{
					if (existing.name == r.name)
					{
						existing = r;
						r.name.clear();
						break;
					}
				}
				if (!r.name.empty())
					out.push_back(std::move(r));
			}
		};
		Json builtin;
		Json::Parse(kBuiltinRegistry, builtin, nullptr);
		load(builtin);
		if (!userRegistry_.empty())
		{
			std::string text;
			Json user;
			if (fs::ReadText(userRegistry_, text) && Json::Parse(text, user, nullptr, Json::ParseOptions{true, true, 64}))
				load(user);
		}
		std::sort(out.begin(), out.end(), [](const RegistryEntry& a, const RegistryEntry& b) { return a.name < b.name; });
		return out;
	}

	std::vector<RegistryEntry> PackageManager::SearchRegistry(const std::string& query) const
	{
		std::vector<std::pair<int, RegistryEntry>> scored;
		for (const RegistryEntry& e : Registry())
		{
			int s = query.empty() ? 1 : std::max(str::FuzzyScore(query, e.name) * 2, str::FuzzyScore(query, e.description));
			if (s > 0)
				scored.push_back({s, e});
		}
		std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		std::vector<RegistryEntry> out;
		for (auto& p : scored)
			out.push_back(std::move(p.second));
		return out;
	}

	DependencySpec PackageManager::ResolveRegistry(const DependencySpec& spec) const
	{
		if (spec.source != PackageSource::Registry)
			return spec;
		for (const RegistryEntry& e : Registry())
		{
			if (e.name == spec.name)
			{
				DependencySpec r = e.spec;
				if (!spec.version.empty() && spec.version != "latest")
				{
					r.version = spec.version;
					if (r.source == PackageSource::Git)
						r.ref = spec.version; // name@1.2.3 selects that tag
				}
				if (!spec.ref.empty())
					r.ref = spec.ref;
				r.optional = spec.optional;
				r.platforms = spec.platforms;
				for (const std::string& d : spec.defines)
					r.defines.push_back(d);
				return r;
			}
		}
		DependencySpec unknown = spec;
		unknown.url.clear();
		return unknown;
	}

	// ----------------------------------------------------------------------------------------------------
	Status PackageManager::Open(const Project& project, const std::string& userRegistryPath)
	{
		if (!project.IsValid())
			return Status::Fail("invalid project");
		project_ = project;
		userRegistry_ = userRegistryPath;
		if (userRegistry_.empty() && Runtime::IsInitialized())
			userRegistry_ = fs::Join(Runtime::Layout().config, "registry.json");
		open_ = true;
		installed_.clear();
		return LoadLock();
	}

	Status PackageManager::LoadLock()
	{
		lock_ = Json::MakeObject();
		installed_.clear();
		std::string text;
		if (!fs::ReadText(project_.LockFilePath(), text))
			return Status::Ok();
		Json j;
		if (!Json::Parse(text, j, nullptr) || !j.IsObject())
		{
			LUMEN_WARN("packages", "ignoring unreadable lock file");
			return Status::Ok();
		}
		lock_ = j;
		for (const Json& p : j.Get("packages").Items())
		{
			InstalledPackage ip;
			ip.spec = DependencySpec::FromJson(p.Get("spec"));
			ip.dir = p.GetString("dir");
			ip.resolvedRef = p.GetString("resolved");
			ip.contentHash = p.GetString("contentHash");
			ip.dependencies = p.GetStringArray("dependencies");
			ip.requestedBy = p.GetString("requestedBy");
			ip.description = p.GetString("description");
			if (IsValidName(ip.spec.name))
				installed_.push_back(std::move(ip));
		}
		return Status::Ok();
	}

	Status PackageManager::SaveLock() const
	{
		Json j = Json::MakeObject();
		j["format"] = "lumen-lock";
		j["version"] = 1;
		Json arr = Json::MakeArray();
		std::vector<const InstalledPackage*> sorted;
		for (const InstalledPackage& p : installed_)
			sorted.push_back(&p);
		std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->spec.name < b->spec.name; });
		for (const InstalledPackage* p : sorted)
		{
			Json o = Json::MakeObject();
			o["spec"] = p->spec.ToJson();
			o["dir"] = p->dir;
			o["resolved"] = p->resolvedRef;
			o["contentHash"] = p->contentHash;
			Json deps = Json::MakeArray();
			for (const std::string& d : p->dependencies)
				deps.Push(d);
			o["dependencies"] = std::move(deps);
			if (!p->requestedBy.empty())
				o["requestedBy"] = p->requestedBy;
			if (!p->description.empty())
				o["description"] = p->description;
			arr.Push(std::move(o));
		}
		j["packages"] = std::move(arr);
		if (!fs::WriteTextIfChanged(project_.LockFilePath(), j.Dump(2) + "\n"))
			return Status::Fail("cannot write " + project_.LockFilePath());
		return Status::Ok();
	}

	const InstalledPackage* PackageManager::Find(const std::string& name) const
	{
		for (const InstalledPackage& p : installed_)
			if (p.spec.name == name)
				return &p;
		return nullptr;
	}

	std::string PackageManager::HashDirectory(const std::string& dir)
	{
		std::vector<std::pair<std::string, uint64_t>> files;
		fs::ListOptions lo;
		lo.recursive = true;
		lo.includeHidden = true;
		fs::Walk(dir, lo, [&](const fs::DirEntry& e) {
			if (!e.isDirectory)
				files.push_back({fs::Relative(e.path, dir), e.size});
			return true;
		});
		std::sort(files.begin(), files.end());
		Sha256 h;
		for (const auto& f : files)
		{
			h.Update(f.first);
			h.Update("\n", 1);
			h.Update(Sha256::HashFileHex(fs::Join(dir, f.first)));
			h.Update("\n", 1);
		}
		return h.FinalHex();
	}

	// ----------------------------------------------------------------------------------------------------
	// Installers
	// ----------------------------------------------------------------------------------------------------
	namespace
	{
		bool PlatformMatches(const std::vector<std::string>& platforms)
		{
			if (platforms.empty())
				return true;
			const std::string host = platform::HostTriple();
			for (const std::string& p : platforms)
			{
				if (p == "windows" && host.find("windows") != std::string::npos) return true;
				if (p == "linux" && host.find("linux") != std::string::npos) return true;
				if ((p == "macos" || p == "mac" || p == "darwin") && (host.find("macos") != std::string::npos || host.find("darwin") != std::string::npos)) return true;
			}
			return false;
		}

		// Extracts "owner/repo" from a github https URL, or "" for other hosts.
		std::string GithubRepo(const std::string& url)
		{
			const std::string prefix = "https://github.com/";
			if (!str::StartsWith(url, prefix))
				return {};
			std::string rest = url.substr(prefix.size());
			if (str::EndsWith(rest, ".git"))
				rest.resize(rest.size() - 4);
			while (!rest.empty() && rest.back() == '/')
				rest.pop_back();
			const std::vector<std::string> parts = str::Split(rest, '/', false);
			if (parts.size() != 2)
				return {};
			return parts[0] + "/" + parts[1];
		}

		bool IsHex40(const std::string& s)
		{
			if (s.size() != 40)
				return false;
			for (char c : s)
				if (!std::isxdigit((unsigned char)c))
					return false;
			return true;
		}

		bool SafeRef(const std::string& ref)
		{
			if (ref.empty() || ref.size() > 128 || ref[0] == '-')
				return false;
			for (char c : ref)
				if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '/' || c == '+'))
					return false;
			return ref.find("..") == std::string::npos;
		}
	} // namespace

	Status PackageManager::InstallGit(const DependencySpec& spec, const std::string& destDir, std::string& resolved, const Callback& cb, std::atomic<bool>* cancel)
	{
		if (!http::IsSafeUrl(spec.url))
			return Status::Fail("unsafe git url: " + spec.url);
		if (!spec.ref.empty() && !SafeRef(spec.ref))
			return Status::Fail("unsafe git ref: " + spec.ref);
		const std::string git = FindExecutable("git");
		const std::string tmp = fs::Join(project_.MetaDir(), "tmp", fs::UniqueName("pkg-" + spec.name));
		fs::RemoveAll(tmp);
		fs::CreateDirs(fs::Parent(tmp));

		bool done = false;
		if (!git.empty())
		{
			if (cb) cb({PackageEvent::Kind::Downloading, spec.name, "git clone " + spec.url});
			const auto run = [&](std::vector<std::string> args, const std::string& cwd) {
				ProcessOptions po;
				po.program = git;
				po.args = std::move(args);
				po.workingDir = cwd;
				po.timeoutMs = 10 * 60 * 1000;
				po.cancel = cancel;
				po.env = {{"GIT_TERMINAL_PROMPT", "0"}, {"GIT_ASKPASS", "echo"}, {"GCM_INTERACTIVE", "never"}};
				return RunProcess(po);
			};
			ProcessResult r;
			if (spec.ref.empty() || !IsHex40(spec.ref))
			{
				std::vector<std::string> args = {"clone", "--depth", "1", "--quiet", "--no-tags", "--single-branch", "--config", "core.symlinks=false"};
				if (!spec.ref.empty() && spec.ref != "latest")
				{
					args.push_back("--branch");
					args.push_back(spec.ref);
				}
				args.push_back("--");
				args.push_back(spec.url);
				args.push_back(fs::ToNative(tmp));
				r = run(args, "");
			}
			else
			{
				// A commit hash cannot be cloned directly: init + fetch that commit + checkout.
				fs::CreateDirs(tmp);
				r = run({"init", "--quiet"}, tmp);
				if (r.Success()) r = run({"remote", "add", "origin", spec.url}, tmp);
				if (r.Success()) r = run({"fetch", "--depth", "1", "--quiet", "origin", spec.ref}, tmp);
				if (r.Success()) r = run({"-c", "core.symlinks=false", "checkout", "--quiet", "FETCH_HEAD"}, tmp);
			}
			if (r.cancelled)
			{
				fs::RemoveAll(tmp);
				return Status::Fail("cancelled");
			}
			if (r.Success())
			{
				ProcessResult head = run({"rev-parse", "HEAD"}, tmp);
				resolved = head.Success() ? std::string(str::Trim(head.output)) : spec.ref;
				fs::RemoveAll(fs::Join(tmp, ".git"));
				done = true;
			}
			else
			{
				LUMEN_WARN("packages", "git clone failed for %s: %s", spec.url.c_str(), std::string(str::Trim(r.output)).c_str());
				fs::RemoveAll(tmp);
			}
		}
		if (!done)
		{
			// Fallback: GitHub archive download (no git needed).
			const std::string repo = GithubRepo(spec.url);
			if (repo.empty())
				return Status::Fail("git is not available and " + spec.url + " is not a GitHub repository");
			const std::string ref = spec.ref.empty() || spec.ref == "latest" ? "HEAD" : spec.ref;
			const std::string archiveUrl = "https://github.com/" + repo + "/archive/" + ref + ".zip";
			const std::string zip = fs::Join(project_.MetaDir(), "tmp", fs::UniqueName("dl") + ".zip");
			http::DownloadOptions d;
			d.url = archiveUrl;
			d.destPath = zip;
			d.cancel = cancel;
			d.progress = [&](uint64_t a, uint64_t b) { if (cb) cb({PackageEvent::Kind::Downloading, spec.name, archiveUrl, a, b}); };
			Status s = http::Download(d);
			if (!s)
				return s;
			if (cb) cb({PackageEvent::Kind::Extracting, spec.name, ""});
			archive::ExtractOptions eo;
			eo.stripComponents = 1;
			s = archive::ExtractZip(zip, tmp, eo);
			fs::Remove(zip);
			if (!s)
			{
				fs::RemoveAll(tmp);
				return s;
			}
			resolved = "archive:" + ref;
		}
		// Move into place.
		fs::RemoveAll(destDir);
		fs::CreateDirs(fs::Parent(destDir));
		const std::string source = spec.subdir.empty() ? tmp : fs::Join(tmp, fs::Normalize(spec.subdir));
		if (!spec.subdir.empty() && (!fs::IsSubPath(source, tmp) || !fs::IsDirectory(source)))
		{
			fs::RemoveAll(tmp);
			return Status::Fail("subdir \"" + spec.subdir + "\" not found in " + spec.name);
		}
		if (!fs::Rename(source, destDir))
		{
			if (!fs::CopyDir(source, destDir, true))
			{
				fs::RemoveAll(tmp);
				return Status::Fail("cannot install into " + destDir);
			}
		}
		fs::RemoveAll(tmp);
		return Status::Ok();
	}

	Status PackageManager::InstallArchive(const DependencySpec& spec, const std::string& destDir, std::string& resolved, const Callback& cb, std::atomic<bool>* cancel)
	{
		if (!http::IsSafeUrl(spec.url))
			return Status::Fail("unsafe archive url: " + spec.url);
		std::string cacheDir = Runtime::IsInitialized() ? fs::Join(Runtime::Layout().packages, "cache") : fs::Join(project_.MetaDir(), "cache");
		fs::CreateDirs(cacheDir);
		std::string ext = ".zip";
		const std::string lower = str::ToLower(spec.url);
		for (const char* e : {".tar.gz", ".tgz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tar", ".zip"})
			if (str::EndsWith(lower.substr(0, lower.find_first_of("?#")), e))
			{
				ext = e;
				break;
			}
		const std::string cached = fs::Join(cacheDir, Sha256::HashHex(spec.url).substr(0, 24) + ext);
		bool have = fs::IsFile(cached) && (spec.sha256.empty() || str::EqualsIgnoreCase(Sha256::HashFileHex(cached), spec.sha256));
		if (!have)
		{
			http::DownloadOptions d;
			d.url = spec.url;
			d.destPath = cached;
			d.sha256 = spec.sha256;
			d.cancel = cancel;
			d.progress = [&](uint64_t a, uint64_t b) { if (cb) cb({PackageEvent::Kind::Downloading, spec.name, spec.url, a, b}); };
			Status s = http::Download(d);
			if (!s)
				return s;
		}
		resolved = Sha256::HashFileHex(cached);
		if (cb) cb({PackageEvent::Kind::Extracting, spec.name, ""});
		const std::string tmp = fs::Join(project_.MetaDir(), "tmp", fs::UniqueName("pkg-" + spec.name));
		fs::RemoveAll(tmp);
		archive::ExtractOptions eo;
		// Strip a single top level directory when the archive has exactly one.
		eo.stripComponents = 0;
		Status s = archive::Extract(cached, tmp, eo);
		if (!s)
		{
			fs::RemoveAll(tmp);
			return s;
		}
		std::string root = tmp;
		{
			const std::vector<fs::DirEntry> top = fs::ListDir(tmp);
			if (top.size() == 1 && top[0].isDirectory)
				root = top[0].path;
		}
		if (!spec.subdir.empty())
		{
			root = fs::Join(root, fs::Normalize(spec.subdir));
			if (!fs::IsSubPath(root, tmp) || !fs::IsDirectory(root))
			{
				fs::RemoveAll(tmp);
				return Status::Fail("subdir \"" + spec.subdir + "\" not found in " + spec.name);
			}
		}
		fs::RemoveAll(destDir);
		fs::CreateDirs(fs::Parent(destDir));
		if (!fs::Rename(root, destDir) && !fs::CopyDir(root, destDir, true))
		{
			fs::RemoveAll(tmp);
			return Status::Fail("cannot install into " + destDir);
		}
		fs::RemoveAll(tmp);
		return Status::Ok();
	}

	Status PackageManager::InstallPath(const DependencySpec& spec, const std::string& destDir, std::string& resolved)
	{
		std::string src = spec.url;
		if (!fs::IsAbsolute(src))
			src = project_.Resolve(src);
		if (!fs::IsDirectory(src))
			return Status::Fail("local package path not found: " + spec.url);
		if (!spec.subdir.empty())
			src = fs::Join(src, spec.subdir);
		if (spec.linkInPlace)
		{
			resolved = "path:" + fs::Canonical(src);
			return Status::Ok();
		}
		fs::RemoveAll(destDir);
		if (!fs::CopyDir(src, destDir, true, [](const std::string& rel) { return !str::StartsWith(rel, ".git/") && rel != ".git"; }))
			return Status::Fail("cannot copy " + src);
		resolved = "path:" + fs::Canonical(src);
		return Status::Ok();
	}

	void PackageManager::ReadPackageManifest(InstalledPackage& pkg, std::vector<Request>& transitive) const
	{
		const std::string base = pkg.spec.linkInPlace ? pkg.spec.url : project_.Resolve(pkg.dir);
		const std::string manifest = fs::Join(fs::IsAbsolute(base) ? base : project_.Resolve(base), "lumen-package.json");
		std::string text;
		Json j;
		if (!fs::ReadText(manifest, text) || !Json::Parse(text, j, nullptr, Json::ParseOptions{true, true, 64}) || !j.IsObject())
			return;
		pkg.description = j.GetString("description", pkg.description);
		if (pkg.spec.version.empty() || pkg.spec.version == "latest")
			pkg.spec.version = j.GetString("version", pkg.spec.version);
		// The project entry wins; the manifest fills what the entry leaves empty.
		const auto fill = [&](std::vector<std::string>& dst, const char* key) {
			if (dst.empty())
				dst = j.GetStringArray(key);
		};
		fill(pkg.spec.lua, "lua");
		fill(pkg.spec.include, "include");
		fill(pkg.spec.shaders, "shaders");
		fill(pkg.spec.defines, "defines");
		fill(pkg.spec.cmakeTargets, "cmakeTargets");
		for (const Json& d : j.Get("dependencies").Items())
		{
			DependencySpec ds = DependencySpec::FromJson(d);
			if (!IsValidName(ds.name))
				continue;
			pkg.dependencies.push_back(ds.name);
			transitive.push_back({ds, pkg.spec.name});
		}
	}

	Status PackageManager::Install(const Request& request, const Callback& cb, bool offline, std::atomic<bool>* cancel, bool force)
	{
		DependencySpec spec = ResolveRegistry(request.spec);
		if (!IsValidName(spec.name))
			return Status::Fail("invalid package name \"" + spec.name + "\"");
		if (!PlatformMatches(spec.platforms))
			return Status::Ok();
		if (spec.source == PackageSource::Registry || (spec.url.empty() && spec.source != PackageSource::Path))
			return Status::Fail("package \"" + spec.name + "\" is not in the registry and has no source url");

		const std::string relDir = "packages/" + spec.name;
		const std::string destDir = project_.Resolve(relDir);

		// Up to date?
		if (!force)
		{
			for (const InstalledPackage& p : installed_)
			{
				if (p.spec.name == spec.name && p.spec.url == spec.url && p.spec.ref == spec.ref && p.spec.subdir == spec.subdir &&
					(spec.linkInPlace || (fs::IsDirectory(destDir) && !p.contentHash.empty() && HashDirectory(destDir) == p.contentHash)))
				{
					if (cb) cb({PackageEvent::Kind::UpToDate, spec.name, p.resolvedRef});
					return Status::Ok();
				}
			}
		}
		if (offline && spec.source != PackageSource::Path)
			return Status::Fail("package \"" + spec.name + "\" is missing or modified and offline mode forbids downloading it");

		if (cb) cb({PackageEvent::Kind::Resolving, spec.name, spec.url + (spec.ref.empty() ? "" : "@" + spec.ref)});
		std::string resolved;
		Status s;
		switch (spec.source)
		{
		case PackageSource::Git: s = InstallGit(spec, destDir, resolved, cb, cancel); break;
		case PackageSource::Archive: s = InstallArchive(spec, destDir, resolved, cb, cancel); break;
		case PackageSource::Path: s = InstallPath(spec, destDir, resolved); break;
		default: s = Status::Fail("unsupported package source");
		}
		if (!s)
			return s;

		InstalledPackage ip;
		ip.spec = spec;
		ip.dir = spec.linkInPlace ? std::string() : relDir;
		ip.resolvedRef = resolved;
		ip.requestedBy = request.requestedBy;
		std::vector<Request> transitive;
		ReadPackageManifest(ip, transitive);
		ip.contentHash = spec.linkInPlace ? std::string() : HashDirectory(destDir);

		installed_.erase(std::remove_if(installed_.begin(), installed_.end(), [&](const InstalledPackage& p) { return p.spec.name == spec.name; }), installed_.end());
		installed_.push_back(ip);
		if (cb) cb({PackageEvent::Kind::Installed, spec.name, resolved});

		for (const Request& t : transitive)
		{
			const InstalledPackage* existing = Find(t.spec.name);
			if (existing)
			{
				if (!t.spec.ref.empty() && existing->spec.ref != t.spec.ref && cb)
					cb({PackageEvent::Kind::Warning, t.spec.name, "version conflict: " + spec.name + " wants " + t.spec.ref + ", keeping " + existing->spec.ref});
				continue;
			}
			s = Install(t, cb, offline, cancel, false);
			if (!s && !t.spec.optional)
				return Status::Fail("dependency \"" + t.spec.name + "\" of \"" + spec.name + "\": " + s.message);
		}
		return Status::Ok();
	}

	Status PackageManager::Sync(const Callback& callback, bool offline, std::atomic<bool>* cancel)
	{
		if (!open_)
			return Status::Fail("package manager not open");
		std::vector<std::string> errors;
		std::set<std::string> wanted;
		for (const DependencySpec& d : project_.Settings().packages)
		{
			if (cancel && cancel->load())
				return Status::Fail("cancelled");
			wanted.insert(d.name);
			// Projects only need to list the package once; "optional" ones may fail without failing the sync.
			Status s = Install({d, ""}, callback, offline, cancel, false);
			if (!s)
			{
				if (callback) callback({d.optional ? PackageEvent::Kind::Warning : PackageEvent::Kind::Error, d.name, s.message});
				if (!d.optional)
					errors.push_back(d.name + ": " + s.message);
			}
		}
		// Prune packages nobody asks for anymore.
		std::set<std::string> reachable;
		std::deque<std::string> queue(wanted.begin(), wanted.end());
		while (!queue.empty())
		{
			const std::string n = queue.front();
			queue.pop_front();
			if (!reachable.insert(n).second)
				continue;
			if (const InstalledPackage* p = Find(n))
				for (const std::string& d : p->dependencies)
					queue.push_back(d);
		}
		for (size_t i = 0; i < installed_.size();)
		{
			if (!reachable.count(installed_[i].spec.name))
			{
				if (!installed_[i].dir.empty())
					fs::RemoveAll(project_.Resolve(installed_[i].dir));
				if (callback) callback({PackageEvent::Kind::Removed, installed_[i].spec.name, "no longer required"});
				installed_.erase(installed_.begin() + (ptrdiff_t)i);
			}
			else
				++i;
		}
		Status ls = SaveLock();
		if (!errors.empty())
			return Status::Fail(str::Join(errors, "\n"));
		return ls;
	}

	Status PackageManager::Add(const DependencySpec& specIn, const Callback& callback)
	{
		if (!open_)
			return Status::Fail("package manager not open");
		if (!IsValidName(specIn.name))
			return Status::Fail("invalid package name");
		DependencySpec spec = specIn;
		Status s = Install({spec, ""}, callback, false, nullptr, true);
		if (!s)
			return s;
		auto& list = project_.Settings().packages;
		bool replaced = false;
		for (DependencySpec& d : list)
			if (d.name == spec.name)
			{
				d = specIn;
				replaced = true;
			}
		if (!replaced)
			list.push_back(specIn);
		s = project_.Save();
		if (!s)
			return s;
		return SaveLock();
	}

	Status PackageManager::AddFromRegistry(const std::string& name, const Callback& callback)
	{
		DependencySpec spec;
		std::string n = name;
		const size_t at = n.find('@');
		if (at != std::string::npos)
		{
			spec.version = n.substr(at + 1);
			n.resize(at);
		}
		spec.name = n;
		spec.source = PackageSource::Registry;
		bool found = false;
		for (const RegistryEntry& e : Registry())
			found |= e.name == n;
		if (!found)
			return Status::Fail("\"" + n + "\" is not in the registry");
		return Add(spec, callback);
	}

	Status PackageManager::Remove(const std::string& name, const Callback& callback)
	{
		auto& list = project_.Settings().packages;
		const size_t before = list.size();
		list.erase(std::remove_if(list.begin(), list.end(), [&](const DependencySpec& d) { return d.name == name; }), list.end());
		if (list.size() == before && !Find(name))
			return Status::Fail("package \"" + name + "\" is not installed");
		Status s = project_.Save();
		if (!s)
			return s;
		s = Sync(callback, true);
		return s;
	}

	Status PackageManager::Update(const std::string& name, const Callback& callback)
	{
		for (const DependencySpec& d : project_.Settings().packages)
		{
			if (d.name == name)
			{
				Status s = Install({d, ""}, callback, false, nullptr, true);
				if (!s)
					return s;
				return SaveLock();
			}
		}
		return Status::Fail("package \"" + name + "\" is not a direct dependency of the project");
	}

	std::vector<std::string> PackageManager::Verify() const
	{
		std::vector<std::string> problems;
		for (const InstalledPackage& p : installed_)
		{
			if (p.spec.linkInPlace)
			{
				if (!fs::IsDirectory(p.spec.url))
					problems.push_back(p.spec.name + ": linked directory is missing");
				continue;
			}
			const std::string dir = project_.Resolve(p.dir);
			if (!fs::IsDirectory(dir))
				problems.push_back(p.spec.name + ": missing (run package sync)");
			else if (!p.contentHash.empty() && HashDirectory(dir) != p.contentHash)
				problems.push_back(p.spec.name + ": files were modified after installation");
		}
		return problems;
	}

	PackageEnvironment PackageManager::Environment() const
	{
		PackageEnvironment env;
		const auto addUnique = [](std::vector<std::string>& v, const std::string& s) {
			if (std::find(v.begin(), v.end(), s) == v.end())
				v.push_back(s);
		};
		for (const InstalledPackage& p : installed_)
		{
			std::string base = p.spec.linkInPlace ? p.spec.url : project_.Resolve(p.dir);
			if (!fs::IsAbsolute(base))
				base = project_.Resolve(base);
			if (!fs::IsDirectory(base))
				continue;
			const auto dirs = [&](const std::vector<std::string>& rel, std::vector<std::string>& out, const std::string& fallback) {
				std::vector<std::string> list = rel;
				if (list.empty() && !fallback.empty())
					list.push_back(fallback);
				for (const std::string& r : list)
				{
					const std::string d = fs::Normalize(fs::Join(base, r));
					if (fs::IsSubPath(d, base) && fs::IsDirectory(d))
						addUnique(out, d);
				}
			};
			dirs(p.spec.lua, env.luaPaths, fs::IsDirectory(fs::Join(base, "lua")) ? "lua" : ".");
			dirs(p.spec.shaders, env.shaderDirs, fs::IsDirectory(fs::Join(base, "shaders")) ? "shaders" : "");
			dirs(p.spec.include, env.includeDirs, fs::IsDirectory(fs::Join(base, "include")) ? "include" : "");
			for (const std::string& d : p.spec.defines)
				addUnique(env.defines, d);
			if (!p.spec.cmakeTargets.empty() && fs::IsFile(fs::Join(base, "CMakeLists.txt")))
				env.cmakePackages.push_back({p.spec.name, base, p.spec.cmakeTargets});
		}
		return env;
	}

} // namespace lumen
