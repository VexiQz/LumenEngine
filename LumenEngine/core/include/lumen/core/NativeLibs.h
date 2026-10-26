// Lumen Engine — native runtime libraries (window/input, Vulkan, audio, video) per operating system.
//
// Libraries are described by a catalog (built in, extendable through `config/libs.json`). For every library the
// catalog says, per platform, whether it is
//   - downloadable (pinned url + SHA-256, or a rolling release verified against the release's checksum file),
//   - provided by the operating system (probed with dlopen/LoadLibrary, with an install hint when missing),
//   - built from source as a package (see Packages), or
//   - shipped with the engine itself.
// Downloaded files land in `lumen_runtime/libs/<host>/`; the launcher puts that folder in the library search path,
// and DeployTo() copies the same files next to an exported game's executable.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"

#include <atomic>
#include <map>
#include <functional>

namespace lumen
{
	enum class LibCategory { Platform, Render, Audio, Video, Lua, Tool };
	const char* LibCategoryName(LibCategory c);

	struct LibFileMap
	{
		std::string from; // path inside the archive (glob allowed: "bin/*.dll")
		std::string to;   // file name placed in the libs folder ("" keeps the original name)
	};

	struct LibSource
	{
		std::string url;
		std::string sha256;       // pinned; empty for rolling releases
		std::string checksumUrl;  // rolling releases: file with "<hash>  <name>" lines
		std::string checksumName; // name to look for in checksumUrl
		std::vector<LibFileMap> files;
		int stripComponents = 0;
		uint64_t sizeBytes = 0;
	};

	struct LibPlatformInfo
	{
		bool supported = true;
		std::vector<std::string> systemNames;  // names probed on the system ("libSDL2-2.0.so.0", "vulkan-1.dll")
		std::string installHint;               // shown when the library is neither managed nor on the system
		bool hasDownload = false;
		LibSource download;
		std::string package;                   // registry package that builds it from source
	};

	struct LibEntry
	{
		std::string id;          // "sdl2", "ffmpeg", "openal"
		std::string title;
		std::string description;
		std::string version;
		std::string license;
		LibCategory category = LibCategory::Platform;
		bool requiredForEngine = false;  // engine cannot start without it on platforms where it is `required`
		std::vector<std::string> requiredOn; // host os names ("linux")
		std::map<std::string, LibPlatformInfo> platforms; // key: "windows-x64", "linux-x64", ... or "windows-*" wildcard
	};

	enum class LibState { NotNeeded, Present, Managed, Missing, Unsupported };

	struct LibStatus
	{
		LibState state = LibState::Missing;
		std::string where;       // path or system library name
		std::string hint;
	};

	class NativeLibs
	{
	public:
		using Progress = std::function<void(const std::string& step, uint64_t done, uint64_t total)>;

		// `libsDir` is lumen_runtime/libs ; the host sub folder is appended internally.
		explicit NativeLibs(std::string libsDir, Json userCatalog = Json());

		const std::vector<LibEntry>& Catalog() const { return catalog_; }
		const LibEntry* Find(const std::string& id) const;

		LibStatus Probe(const std::string& id) const;
		// Downloads / extracts one library for the host platform. Already installed libraries are left alone.
		Status Install(const std::string& id, const Progress& progress = {}, std::atomic<bool>* cancel = nullptr);
		// Installs everything the engine requires on this host (and the given optional ids). Returns the first error
		// but keeps going so one failing library does not block the others; `report` lists per library results.
		Status EnsureRequired(const std::vector<std::string>& optionalIds, const Progress& progress = {}, std::vector<std::string>* report = nullptr,
			std::atomic<bool>* cancel = nullptr);

		// Folder holding the managed libraries of this host ("<libsDir>/<host>").
		std::string HostDir() const;
		// Environment variables that make the dynamic loader find managed libraries (LD_LIBRARY_PATH, PATH, ...).
		std::vector<std::pair<std::string, std::string>> LoaderEnvironment() const;
		// Windows: adds HostDir() to the DLL search path of this process. No-op elsewhere.
		void AddToLoaderPath() const;
		// Copies every managed library (and the given extra ids) beside an exported executable.
		Status DeployTo(const std::string& directory, std::vector<std::string>* copied = nullptr) const;

		// One line per library, for logs and the editor's Libraries panel.
		std::string Describe() const;

	private:
		static std::string HostKey();
		const LibPlatformInfo* PlatformFor(const LibEntry& e) const;
		bool IsInstalled(const LibEntry& e, std::vector<std::string>* files = nullptr) const;
		std::string MarkerPath(const std::string& id) const;
		void LoadBuiltinCatalog();
		void MergeUserCatalog(const Json& j);

		std::string libsDir_;
		std::vector<LibEntry> catalog_;
	};

} // namespace lumen
