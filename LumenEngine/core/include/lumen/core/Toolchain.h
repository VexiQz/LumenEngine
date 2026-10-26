// Lumen Engine — build toolchain detection and automatic provisioning.
//
// Native game modules need CMake, a generator (Ninja) and a C/C++ compiler. The engine looks for them in this
// order: explicit overrides in config/engine.json, tools it installed earlier into `lumen_runtime/toolchain`,
// the system PATH (and Visual Studio on Windows). Anything missing is downloaded from pinned, checksum
// verified releases into `lumen_runtime/toolchain` — no administrator rights, nothing outside the runtime folder.
//
// Game modules talk to the engine through a plain C ABI (lumen_c.h), so the compiler that builds a module does
// not have to match the one that built the engine.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"

#include <atomic>
#include <functional>

namespace lumen
{
	struct ToolInfo
	{
		std::string name;      // "cmake", "ninja", "c compiler", "c++ compiler"
		std::string path;      // absolute executable path ("" = not found)
		std::string version;
		bool managed = false;  // installed by Lumen into lumen_runtime/toolchain
		bool Found() const { return !path.empty(); }
	};

	enum class CompilerKind { None, Msvc, Gcc, Clang, AppleClang, LlvmMingw };

	struct Toolchain
	{
		ToolInfo cmake;
		ToolInfo ninja;
		ToolInfo cc;
		ToolInfo cxx;
		CompilerKind compiler = CompilerKind::None;
		std::string generator;       // "Ninja", "Visual Studio 17 2022", "Unix Makefiles"
		std::string generatorPlatform; // "x64" for Visual Studio generators
		std::string binDirs;         // PATH additions needed to run the compiler (path-list separated)
		std::vector<std::pair<std::string, std::string>> env; // extra environment for build processes
		std::string description;     // human readable one line summary

		bool Ready() const { return cmake.Found() && cxx.Found() && cc.Found() && !generator.empty(); }
		std::vector<std::string> Missing() const;
		Json ToJson() const;
	};

	struct ToolchainProgress
	{
		std::string step;      // "Downloading CMake 3.31.6"
		uint64_t done = 0;
		uint64_t total = 0;
		float fraction = -1.0f; // overall 0..1 when known
	};

	class ToolchainManager
	{
	public:
		using Callback = std::function<void(const ToolchainProgress&)>;

		// `toolchainDir` is lumen_runtime/toolchain. `config` is the "toolchain" object of engine.json (may be null).
		explicit ToolchainManager(std::string toolchainDir, Json config = Json());

		// Looks for tools without touching the network.
		Toolchain Detect() const;
		// Detect(), then downloads what is missing (when allowed). Returns the final toolchain.
		Status Ensure(Toolchain& out, bool allowDownload, const Callback& progress = {}, std::atomic<bool>* cancel = nullptr);

		// Pinned downloads available for this host; useful for the editor's "Toolchain" panel.
		struct Recipe
		{
			std::string id;        // "cmake", "ninja", "llvm-mingw"
			std::string version;
			std::string url;
			std::string sha256;
			std::string archiveRoot; // folder created inside the archive (stripped)
			uint64_t sizeBytes = 0;
		};
		std::vector<Recipe> Recipes() const;

		const std::string& Directory() const { return dir_; }

	private:
		Status Install(const Recipe& recipe, const Callback& progress, std::atomic<bool>* cancel, float base, float span);
		std::string ManagedPath(const std::string& recipeId, const std::string& relativeExe) const;

		std::string dir_;
		Json config_;
	};

	// Runs `<exe> --version` style probes. Returns the first line of output ("" on failure).
	std::string ProbeVersion(const std::string& exe, const std::vector<std::string>& args = {"--version"});

} // namespace lumen
