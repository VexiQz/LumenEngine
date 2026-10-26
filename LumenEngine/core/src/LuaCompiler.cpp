#include "lumen/core/LuaCompiler.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include <algorithm>

namespace lumen::lua
{
	namespace
	{
		struct StateGuard
		{
			lua_State* L;
			StateGuard() : L(luaL_newstate()) {}
			~StateGuard()
			{
				if (L)
					lua_close(L);
			}
		};

		int DumpWriter(lua_State*, const void* p, size_t sz, void* ud)
		{
			auto* out = static_cast<std::vector<uint8_t>*>(ud);
			const uint8_t* bytes = static_cast<const uint8_t*>(p);
			out->insert(out->end(), bytes, bytes + sz);
			return 0;
		}

		// Extracts the quoted token from a Lua message of the form "... near 'tok'".
		std::string NearToken(const std::string& message)
		{
			const size_t pos = message.rfind("near ");
			if (pos == std::string::npos)
				return {};
			std::string rest = message.substr(pos + 5);
			if (rest.size() >= 2 && rest.front() == '\'' && rest.back() == '\'')
				return rest.substr(1, rest.size() - 2);
			return {};
		}

		// Lua reports errors as "<chunk>:<line>: <message>". Splits that into its parts.
		Diagnostic ParseLuaError(const std::string& raw, const std::string& chunkName, std::string_view source)
		{
			Diagnostic d;
			d.severity = Severity::Error;
			d.chunk = chunkName;
			d.line = 1;
			d.column = 1;
			d.message = raw;

			const std::string prefix = chunkName + ":";
			std::string rest = raw;
			if (str::StartsWith(rest, prefix))
			{
				rest.erase(0, prefix.size());
				size_t i = 0;
				while (i < rest.size() && std::isdigit((unsigned char)rest[i]))
					++i;
				if (i > 0 && i < rest.size() && rest[i] == ':')
				{
					int64_t line = 1;
					str::ParseInt(std::string_view(rest).substr(0, i), line);
					d.line = (int)std::max<int64_t>(1, line);
					d.message = std::string(str::Trim(std::string_view(rest).substr(i + 1)));
				}
			}

			// Locate the offending token inside that line for a useful column.
			const std::vector<std::string> lines = str::SplitLines(source);
			if (d.line >= 1 && (size_t)d.line <= lines.size())
			{
				const std::string& text = lines[(size_t)d.line - 1];
				const std::string tok = NearToken(d.message);
				if (d.message.find("near <eof>") != std::string::npos)
				{
					d.column = (int)text.size() + 1;
				}
				else if (!tok.empty())
				{
					const size_t pos = text.rfind(tok);
					if (pos != std::string::npos)
						d.column = (int)pos + 1;
				}
				else
				{
					// First non blank character of the line.
					size_t i = 0;
					while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
						++i;
					d.column = (int)i + 1;
				}
			}
			return d;
		}

		bool LoadChunk(lua_State* L, std::string_view source, const std::string& chunkName, Diagnostic* error)
		{
			const std::string name = "=" + chunkName;
			const int rc = luaL_loadbufferx(L, source.data(), source.size(), name.c_str(), "t");
			if (rc == LUA_OK)
				return true;
			if (error)
			{
				const char* msg = lua_tostring(L, -1);
				*error = ParseLuaError(msg ? msg : "unknown error", chunkName, source);
			}
			lua_pop(L, 1);
			return false;
		}
	} // namespace

	std::string VersionString()
	{
		return std::string(LUA_VERSION_MAJOR "." LUA_VERSION_MINOR "." LUA_VERSION_RELEASE).insert(0, "Lua ");
	}

	bool IsBytecode(const void* data, size_t size)
	{
		// "\x1bLua" + version byte (0x54 for 5.4) + format byte (0)
		if (size < 6)
			return false;
		const uint8_t* p = static_cast<const uint8_t*>(data);
		return p[0] == 0x1B && p[1] == 'L' && p[2] == 'u' && p[3] == 'a' && p[4] == 0x54 && p[5] == 0;
	}

	bool CompileString(std::string_view source, const std::string& chunkName, const CompileOptions& options,
		std::vector<uint8_t>& bytecode, Diagnostic* error)
	{
		StateGuard g;
		if (!g.L)
		{
			if (error)
			{
				error->severity = Severity::Error;
				error->message = "cannot create Lua state";
			}
			return false;
		}
		// Skip a shebang line like luac does (keep the newline so line numbers stay correct).
		std::string_view src = source;
		std::string patched;
		if (src.size() >= 2 && src[0] == '#' && src[1] == '!')
		{
			const size_t nl = src.find('\n');
			patched = "--" + std::string(src);
			if (nl == std::string_view::npos)
				patched = "--";
			src = patched;
		}
		if (!LoadChunk(g.L, src, chunkName, error))
			return false;
		bytecode.clear();
		if (lua_dump(g.L, DumpWriter, &bytecode, options.strip ? 1 : 0) != 0)
		{
			if (error)
			{
				error->severity = Severity::Error;
				error->message = "lua_dump failed";
				error->chunk = chunkName;
			}
			return false;
		}
		return true;
	}

	bool CompileFile(const std::string& path, const std::string& chunkName, const CompileOptions& options,
		std::vector<uint8_t>& bytecode, Diagnostic* error)
	{
		std::string source;
		if (!fs::ReadText(path, source))
		{
			if (error)
			{
				error->severity = Severity::Error;
				error->message = "cannot read file: " + path;
				error->chunk = chunkName;
			}
			return false;
		}
		return CompileString(source, chunkName, options, bytecode, error);
	}

	bool CheckSyntax(std::string_view source, const std::string& chunkName, Diagnostic* error)
	{
		StateGuard g;
		if (!g.L)
			return false;
		const bool ok = LoadChunk(g.L, source, chunkName, error);
		if (ok)
			lua_pop(g.L, 1);
		return ok;
	}

	// ------------------------------------------------------------------------------------------------
	// BytecodeCache
	// ------------------------------------------------------------------------------------------------
	void BytecodeCache::Configure(std::string directory, bool strip)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		dir_ = std::move(directory);
		strip_ = strip;
		if (!dir_.empty())
			fs::CreateDirs(dir_);
	}

	std::string BytecodeCache::PathFor(const std::string& hash) const
	{
		return fs::Join(dir_, hash.substr(0, 2), hash + ".luac");
	}

	bool BytecodeCache::GetOrCompileSource(std::string_view source, const std::string& chunkName, std::vector<uint8_t>& bytecode,
		Diagnostic* error, bool* cacheHit)
	{
		if (cacheHit)
			*cacheHit = false;

		// The chunk name is part of the key: it is embedded in the bytecode (debug info) unless stripped.
		Sha256 h;
		h.Update(source);
		h.Update(strip_ ? "|strip|" : "|debug|");
		h.Update(VersionString());
		if (!strip_)
			h.Update(chunkName);
		const std::string hash = h.FinalHex();

		if (!dir_.empty())
		{
			const std::string path = PathFor(hash);
			std::vector<uint8_t> cached;
			if (fs::ReadBinary(path, cached) && IsBytecode(cached.data(), cached.size()))
			{
				bytecode = std::move(cached);
				std::lock_guard<std::mutex> lock(mutex_);
				++hits_;
				if (cacheHit)
					*cacheHit = true;
				return true;
			}
		}

		CompileOptions opts;
		opts.strip = strip_;
		if (!CompileString(source, chunkName, opts, bytecode, error))
			return false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			++misses_;
		}
		if (!dir_.empty())
			fs::WriteAtomic(PathFor(hash), bytecode.data(), bytecode.size());
		return true;
	}

	bool BytecodeCache::GetOrCompile(const std::string& sourcePath, const std::string& chunkName, std::vector<uint8_t>& bytecode,
		Diagnostic* error, bool* cacheHit)
	{
		std::string source;
		if (!fs::ReadText(sourcePath, source))
		{
			if (error)
			{
				error->severity = Severity::Error;
				error->message = "cannot read file: " + sourcePath;
				error->chunk = chunkName;
			}
			return false;
		}
		return GetOrCompileSource(source, chunkName, bytecode, error, cacheHit);
	}

	size_t BytecodeCache::Prune(uint64_t olderThanMs)
	{
		if (dir_.empty())
			return 0;
		size_t removed = 0;
		const uint64_t now = platform::UnixTimeMs();
		fs::ListOptions opts;
		opts.recursive = true;
		std::vector<std::string> doomed;
		fs::Walk(dir_, opts, [&](const fs::DirEntry& e) {
			if (!e.isDirectory && e.mtimeMs + olderThanMs < now)
				doomed.push_back(e.path);
			return true;
		});
		for (const std::string& p : doomed)
		{
			if (fs::Remove(p))
				++removed;
		}
		return removed;
	}

	void BytecodeCache::Clear()
	{
		if (!dir_.empty())
		{
			fs::RemoveAll(dir_);
			fs::CreateDirs(dir_);
		}
	}

	// ------------------------------------------------------------------------------------------------
	// bundling
	// ------------------------------------------------------------------------------------------------
	BundleReport BundleScripts(const std::string& rootDir, const std::vector<std::string>& relativeDirs, const CompileOptions& options,
		PakWriter& pak, BytecodeCache* cache)
	{
		BundleReport report;
		BytecodeCache local;
		if (!cache)
		{
			local.Configure({}, options.strip);
			cache = &local;
		}

		for (const std::string& dir : relativeDirs)
		{
			const std::string absDir = fs::Join(rootDir, dir);
			fs::ListOptions lo;
			lo.recursive = true;
			lo.includeHidden = false;
			fs::Walk(absDir, lo, [&](const fs::DirEntry& e) {
				if (e.isDirectory || fs::ExtensionLower(e.path) != ".lua")
					return true;
				const std::string rel = fs::Relative(e.path, rootDir);
				std::string source;
				if (!fs::ReadText(e.path, source))
				{
					Diagnostic d;
					d.chunk = rel;
					d.message = "cannot read file";
					report.diagnostics.push_back(d);
					++report.filesFailed;
					return true;
				}
				std::vector<uint8_t> bytecode;
				Diagnostic err;
				if (!cache->GetOrCompileSource(source, rel, bytecode, &err))
				{
					report.diagnostics.push_back(err);
					++report.filesFailed;
					return true;
				}
				pak.Add(rel, bytecode, PakEntry_LuaBytecode);
				report.sourceBytes += source.size();
				report.bytecodeBytes += bytecode.size();
				++report.filesCompiled;
				return true;
			});
		}
		return report;
	}

} // namespace lumen::lua
