#include "LuaEdit.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
}

namespace lumen
{
	namespace
	{
		const char* kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in", "local", "nil", "not", "or",
			"repeat", "return", "then", "true", "until", "while"};
		const char* kBuiltins[] = {"assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs", "load", "loadfile", "next", "pairs", "pcall", "print",
			"rawequal", "rawget", "rawlen", "rawset", "require", "select", "setmetatable", "tonumber", "tostring", "type", "xpcall", "string", "table", "math",
			"io", "os", "coroutine", "utf8", "debug", "package", "_G", "_ENV", "self"};

		struct Member { const char* qualifier; const char* name; const char* detail; };
		const Member kApi[] = {
			{"lumen", "scene", "scene queries"}, {"lumen", "input", "keyboard and mouse"}, {"lumen", "log", "logging"}, {"lumen", "debug", "debug drawing"},
			{"lumen", "events", "event bus"}, {"lumen", "mathx", "math helpers, easing, vectors"}, {"lumen", "random", "PCG32 random"},
			{"lumen", "noise", "perlin, simplex, worley, fbm"}, {"lumen", "geom", "geometry tests"}, {"lumen", "color", "colour spaces"},
			{"lumen", "json", "JSON encode/decode"}, {"lumen", "hash", "hashes, base64, uuid"}, {"lumen", "regex", "regular expressions"},
			{"lumen", "fs", "file system"}, {"lumen", "http", "HTTP client"}, {"lumen", "time", "dates and stopwatch"}, {"lumen", "net", "sockets"},
			{"lumen", "system", "host information"}, {"lumen", "zip", "zip archives"}, {"lumen", "compress", "deflate / gzip"}, {"lumen", "process", "child processes"},
			{"lumen", "vec2", "2D vector"}, {"lumen", "vec3", "3D vector"}, {"lumen", "vec4", "4D vector"}, {"lumen", "quat", "quaternion"}, {"lumen", "mat4", "4x4 matrix"},
			{"lumen", "quit", "close the game"}, {"lumen", "elapsed", "seconds since the game started"}, {"lumen", "delta_time", "last frame time"},
			{"lumen", "version", "engine version string"},
			{"scene", "find", "find(name) -> entity"}, {"scene", "find_all", "find_all(name) -> {entity}"}, {"scene", "find_by_tag", "find_by_tag(tag) -> {entity}"},
			{"scene", "spawn", "spawn(primitive, position, size, physics) -> entity"}, {"scene", "raycast", "raycast(origin, dir, max) -> hit"},
			{"scene", "load", "load(path) -> bool"}, {"scene", "gravity", "gravity([vec3])"},
			{"input", "down", "down(key) -> bool"}, {"input", "pressed", "pressed(key) -> bool (this frame)"}, {"input", "mouse_delta", "-> dx, dy"},
			{"input", "mouse_pos", "-> x, y"}, {"input", "wheel", "-> delta"}, {"input", "set_cursor_locked", "set_cursor_locked(bool)"}, {"input", "cursor_locked", "-> bool"},
			{"log", "info", "log.info(...)"}, {"log", "warn", "log.warn(...)"}, {"log", "error", "log.error(...)"}, {"log", "debug", "log.debug(...)"},
			{"debug", "text", "text(x, y, string)"}, {"debug", "line", "line(a, b)"},
			{"events", "on", "on(name, fn)"}, {"events", "emit", "emit(name, ...)"},
			{"entity", "position", "-> vec3"}, {"entity", "set_position", "(vec3)"}, {"entity", "rotation", "-> euler degrees"}, {"entity", "set_rotation", "(euler degrees)"},
			{"entity", "forward", "-> vec3"}, {"entity", "right", "-> vec3"}, {"entity", "up", "-> vec3"}, {"entity", "translate", "(vec3)"}, {"entity", "look_at", "(vec3)"},
			{"entity", "child", "child(name)"}, {"entity", "children", "-> {entity}"}, {"entity", "parent", "-> entity"}, {"entity", "destroy", "()"},
			{"entity", "velocity", "-> vec3"}, {"entity", "set_velocity", "(vec3)"}, {"entity", "add_force", "(vec3)"}, {"entity", "add_impulse", "(vec3)"},
			{"entity", "move", "move(dir, speed, jump) character controller"}, {"entity", "grounded", "-> bool"}, {"entity", "light", "-> light"}, {"entity", "set_color", "(vec4)"},
		};

		bool IsIdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
		bool IsId(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
	}

	void LuaHighlight(const std::vector<std::string>& lines, std::vector<std::vector<LuaSpan>>& out)
	{
		static const std::set<std::string> kw(std::begin(kKeywords), std::end(kKeywords));
		static const std::set<std::string> bi(std::begin(kBuiltins), std::end(kBuiltins));
		out.assign(lines.size(), {});
		int longLevel = -1;     // inside a long bracket (string or comment) with this level
		bool longIsComment = false;
		for (size_t li = 0; li < lines.size(); ++li)
		{
			const std::string& s = lines[li];
			std::vector<LuaSpan>& spans = out[li];
			int i = 0;
			const int n = (int)s.size();
			auto closeLong = [&](int from) -> int {
				const std::string close = "]" + std::string(longLevel, '=') + "]";
				const size_t p = s.find(close, from);
				return p == std::string::npos ? -1 : (int)(p + close.size());
			};
			if (longLevel >= 0)
			{
				const int e = closeLong(0);
				spans.push_back({0, e < 0 ? n : e, longIsComment ? LuaTok::Comment : LuaTok::String});
				if (e < 0) continue;
				longLevel = -1;
				i = e;
			}
			while (i < n)
			{
				const char c = s[i];
				if (c == '-' && i + 1 < n && s[i + 1] == '-')
				{
					int j = i + 2, lvl = -1;
					if (j < n && s[j] == '[')
					{
						int k = j + 1, eq = 0;
						while (k < n && s[k] == '=') { ++eq; ++k; }
						if (k < n && s[k] == '[') lvl = eq;
					}
					if (lvl >= 0)
					{
						longLevel = lvl;
						longIsComment = true;
						const int e = closeLong(j + lvl + 2);
						spans.push_back({i, e < 0 ? n : e, LuaTok::Comment});
						if (e < 0) { i = n; break; }
						longLevel = -1;
						i = e;
						continue;
					}
					spans.push_back({i, n, LuaTok::Comment});
					break;
				}
				if (c == '"' || c == '\'')
				{
					int j = i + 1;
					while (j < n && s[j] != c) { if (s[j] == '\\') ++j; ++j; }
					j = std::min(n, j + 1);
					spans.push_back({i, j, LuaTok::String});
					i = j;
					continue;
				}
				if (c == '[' && i + 1 < n && (s[i + 1] == '[' || s[i + 1] == '='))
				{
					int k = i + 1, eq = 0;
					while (k < n && s[k] == '=') { ++eq; ++k; }
					if (k < n && s[k] == '[')
					{
						longLevel = eq;
						longIsComment = false;
						const int e = closeLong(k + 1);
						spans.push_back({i, e < 0 ? n : e, LuaTok::String});
						if (e < 0) { i = n; break; }
						longLevel = -1;
						i = e;
						continue;
					}
				}
				if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1])))
				{
					int j = i + 1;
					while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '.' || ((s[j] == '+' || s[j] == '-') && (s[j - 1] == 'e' || s[j - 1] == 'E')))) ++j;
					spans.push_back({i, j, LuaTok::Number});
					i = j;
					continue;
				}
				if (IsIdStart(c))
				{
					int j = i + 1;
					while (j < n && IsId(s[j])) ++j;
					const std::string w = s.substr(i, j - i);
					LuaTok k = LuaTok::Text;
					if (kw.count(w)) k = LuaTok::Keyword;
					else if (w == "lumen") k = LuaTok::Lumen;
					else if (bi.count(w)) k = LuaTok::Builtin;
					spans.push_back({i, j, k});
					i = j;
					continue;
				}
				if (std::strchr("+-*/%^#&~|<>=(){}[];:,.", c))
				{
					spans.push_back({i, i + 1, LuaTok::Operator});
					++i;
					continue;
				}
				++i;
			}
		}
	}

	std::vector<LuaDiag> LuaCheck(const std::string& source, const std::string& chunkName)
	{
		std::vector<LuaDiag> out;
		lua_State* L = luaL_newstate();
		if (!L)
			return out;
		const std::string name = "=" + chunkName;
		if (luaL_loadbufferx(L, source.data(), source.size(), name.c_str(), "t") != LUA_OK)
		{
			const std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "syntax error";
			LuaDiag d;
			// "<chunk>:<line>: message"
			const size_t a = msg.find(':');
			if (a != std::string::npos)
			{
				size_t b = msg.find(':', a + 1);
				if (b != std::string::npos)
				{
					d.line = std::max(1, atoi(msg.substr(a + 1, b - a - 1).c_str()));
					d.message = msg.substr(b + 1);
					while (!d.message.empty() && d.message[0] == ' ') d.message.erase(0, 1);
				}
			}
			if (d.message.empty()) { d.line = 1; d.message = msg; }
			out.push_back(d);
		}
		lua_close(L);
		return out;
	}

	std::vector<LuaCompletion> LuaComplete(const std::string& source, const std::string& prefix, const std::string& qualifier)
	{
		std::vector<LuaCompletion> out;
		auto match = [&](const std::string& w) {
			if (w.size() < prefix.size()) return false;
			for (size_t i = 0; i < prefix.size(); ++i)
				if (std::tolower((unsigned char)w[i]) != std::tolower((unsigned char)prefix[i])) return false;
			return true;
		};
		if (!qualifier.empty())
		{
			std::string q = qualifier;
			const size_t dot = q.rfind('.');
			if (dot != std::string::npos) q = q.substr(dot + 1);
			if (q == "self" || q == "e" || q == "ent") q = "entity";
			for (const Member& m : kApi)
				if (q == m.qualifier && match(m.name))
					out.push_back({m.name, m.detail});
			return out;
		}
		std::set<std::string> seen;
		for (const char* k : kKeywords) if (match(k) && seen.insert(k).second) out.push_back({k, "keyword"});
		for (const char* k : kBuiltins) if (match(k) && seen.insert(k).second) out.push_back({k, "Lua"});
		if (match("lumen") && seen.insert("lumen").second) out.push_back({"lumen", "engine API"});
		// identifiers declared in this file
		size_t i = 0;
		while (i < source.size())
		{
			if (IsIdStart(source[i]))
			{
				size_t j = i + 1;
				while (j < source.size() && IsId(source[j])) ++j;
				const std::string w = source.substr(i, j - i);
				if (w.size() > prefix.size() && match(w) && seen.insert(w).second) out.push_back({w, "file"});
				i = j;
			}
			else
				++i;
		}
		if (out.size() > 60) out.resize(60);
		return out;
	}
}
