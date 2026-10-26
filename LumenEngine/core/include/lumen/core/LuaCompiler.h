// Lumen Engine — embedded Lua 5.4 compiler (the engine's own "luac").
//
// The compiler is the very same Lua that executes scripts at runtime (Wicked Engine ships Lua 5.4),
// so bytecode produced here is always loadable by the player. It is used for
//   * syntax checking in the editor and the CLI,
//   * a bytecode cache (compile once, load fast, hot-reload only what changed),
//   * release bundles (scripts compiled, optionally stripped, packed into an LPAK).
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Pak.h"

#include <mutex>

namespace lumen::lua
{
	// Numbering matches the Language Server Protocol.
	enum class Severity : int
	{
		Error = 1,
		Warning = 2,
		Information = 3,
		Hint = 4
	};

	struct Diagnostic
	{
		Severity severity = Severity::Error;
		int line = 1;   // 1-based
		int column = 1; // 1-based
		std::string message;
		std::string chunk; // chunk / file name the diagnostic belongs to
	};

	struct CompileOptions
	{
		bool strip = false; // remove debug information (line numbers, local names): smaller, but errors lose their positions
	};

	// "Lua 5.4.8"
	std::string VersionString();

	// True when `data` starts with the Lua binary chunk signature of this Lua version.
	bool IsBytecode(const void* data, size_t size);

	// Compiles Lua source into a binary chunk. `chunkName` is what error messages and tracebacks show
	// (usually the project relative file path). Returns false and fills `error` on a syntax error.
	bool CompileString(std::string_view source, const std::string& chunkName, const CompileOptions& options,
		std::vector<uint8_t>& bytecode, Diagnostic* error = nullptr);
	bool CompileFile(const std::string& path, const std::string& chunkName, const CompileOptions& options,
		std::vector<uint8_t>& bytecode, Diagnostic* error = nullptr);

	// Syntax check only.
	bool CheckSyntax(std::string_view source, const std::string& chunkName, Diagnostic* error = nullptr);

	// Content addressed bytecode cache: <dir>/<xx>/<sha256>.luac
	class BytecodeCache
	{
	public:
		BytecodeCache() = default;
		BytecodeCache(std::string directory, bool strip) : dir_(std::move(directory)), strip_(strip) {}
		void Configure(std::string directory, bool strip);
		const std::string& Directory() const { return dir_; }

		// Returns bytecode for the source file, compiling and storing it when it is not cached yet.
		bool GetOrCompile(const std::string& sourcePath, const std::string& chunkName, std::vector<uint8_t>& bytecode,
			Diagnostic* error = nullptr, bool* cacheHit = nullptr);
		bool GetOrCompileSource(std::string_view source, const std::string& chunkName, std::vector<uint8_t>& bytecode,
			Diagnostic* error = nullptr, bool* cacheHit = nullptr);
		// Removes cached files that were not used since `olderThanMs` ago.
		size_t Prune(uint64_t olderThanMs);
		void Clear();

		uint64_t Hits() const { return hits_; }
		uint64_t Misses() const { return misses_; }

	private:
		std::string PathFor(const std::string& hash) const;
		std::string dir_;
		bool strip_ = false;
		uint64_t hits_ = 0;
		uint64_t misses_ = 0;
		std::mutex mutex_;
	};

	struct BundleReport
	{
		size_t filesCompiled = 0;
		size_t filesFailed = 0;
		uint64_t sourceBytes = 0;
		uint64_t bytecodeBytes = 0;
		std::vector<Diagnostic> diagnostics;
	};

	// Compiles every .lua file below `rootDir/<dir>` for each dir in `relativeDirs` and adds it to `pak`
	// under its project relative path (e.g. "scripts/player.lua") flagged as bytecode. Non Lua files
	// are ignored. Files with syntax errors are reported and skipped.
	BundleReport BundleScripts(const std::string& rootDir, const std::vector<std::string>& relativeDirs,
		const CompileOptions& options, PakWriter& pak, BytecodeCache* cache = nullptr);

} // namespace lumen::lua
