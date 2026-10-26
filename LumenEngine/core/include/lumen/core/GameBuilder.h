// Lumen Engine — release builds: turns a project into a standalone game folder.
//
//   <output>/<GameName>.exe   copy of lumen-player (the runtime without the editor)
//   <output>/data.lpak        project manifest, scenes, scripts (precompiled), assets, Lua libraries, shader library
//   <output>/*.dll|*.so       runtime libraries that sit beside the player
//
// The player unpacks data.lpak into a cache folder once per build (keyed by the pak checksum) and runs the
// project from there with exactly the same code paths as the editor's Play mode.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Pak.h"
#include "lumen/core/Project.h"

namespace lumen
{
	struct BuildOptions
	{
		std::string outputDir;       // "" = <project>/build/<name>
		std::string playerExe;       // lumen-player executable to copy ("" = pak only)
		std::string runtimeDataDir;  // runtime_data (lua/, shaders/, fonts/, config/ are shipped)
		bool stripLua = true;        // drop Lua debug info (smaller; error positions are lost)
		bool includeNative = true;   // ship the compiled native module when it exists
		std::function<void(const std::string&)> onLog;
		std::function<void(float)> onProgress;
		const bool* cancel = nullptr;
	};

	struct BuildReport
	{
		std::string outputDir;
		std::string pakPath;
		std::string exePath;
		size_t files = 0;
		size_t scripts = 0;
		size_t scenes = 0;
		uint64_t pakBytes = 0;
		std::vector<std::string> warnings;
	};

	// Pak layout: game/<project relative path> and runtime/<runtime_data relative path>.
	constexpr const char* kPakGamePrefix = "game/";
	constexpr const char* kPakRuntimePrefix = "runtime/";
	constexpr const char* kPakManifest = "game/game.lum";

	// Collects the files that belong to a release build (project relative, '/' separated).
	std::vector<std::string> CollectGameFiles(const Project& project, bool includeNative);

	Status BuildGame(const Project& project, const BuildOptions& options, BuildReport& report);

	// Player side: unpacks `pak` below `dstDir` (game/ -> dstDir/game, runtime/ -> dstDir/runtime). Files whose size and
	// checksum already match are skipped, so repeated launches are cheap. Returns the .lum path of the extracted project.
	Status ExtractGamePackage(const PakReader& pak, const std::string& dstDir, std::string& projectFile);
}
