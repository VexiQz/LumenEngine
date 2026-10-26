// Lumen Engine — project model and the .lum project file.
//
// A project is a folder with a `<Name>.lum` JSON manifest in its root. Format history:
//   v1  (phase 1)  name, engine, startupScene, mainScript, graphics{}, window{}, nativeModules, packages[]
//   v2  (current)  adds scripts/shaders/native/physics/audio/culling/build sections and richer package specs
// Files written by v1 builds load unchanged (missing sections take their defaults) and are upgraded
// to v2 on the next save. Unknown keys are preserved in `extra` so newer engines do not lose data.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"

namespace lumen
{
	constexpr const char* kProjectFormatName = "lumen-project";
	constexpr int kProjectFormatVersion = 2;
	constexpr const char* kProjectFileExtension = ".lum";

	// --- dependency (package) description -------------------------------------------------------------
	enum class PackageSource
	{
		Git,     // git url (+ optional ref: branch / tag / commit)
		Archive, // .zip / .tar.gz / .tar.xz / .tar.bz2 downloaded over http(s)
		Path,    // local directory (copied or referenced in place)
		Registry // name@version resolved through the built-in registry
	};

	struct DependencySpec
	{
		std::string name;
		std::string version;       // free form ("1.2.3", "latest"); informational for git/archive
		PackageSource source = PackageSource::Registry;
		std::string url;           // git url / archive url / local path
		std::string ref;           // git branch, tag or commit
		std::string sha256;        // expected archive checksum (hex); empty: trust on first use
		std::string subdir;        // use this sub folder of the package as its root
		std::vector<std::string> lua;      // package relative dirs added to package.path
		std::vector<std::string> include;  // package relative include dirs for native modules
		std::vector<std::string> shaders;  // package relative shader include dirs
		std::vector<std::string> defines;  // preprocessor definitions for native modules
		std::vector<std::string> cmakeTargets; // when the package has a CMakeLists.txt: targets linked into the module
		std::vector<std::string> platforms;    // empty: all; otherwise "windows", "linux", "macos"
		bool optional = false;
		bool linkInPlace = false;  // Path source only: do not copy, reference the original directory

		Json ToJson() const;
		static DependencySpec FromJson(const Json& j);
		// Legacy v1 entry {name, version, source}
		static DependencySpec FromLegacy(const Json& j);
	};

	// --- project sections -------------------------------------------------------------------------------
	struct GraphicsSettings
	{
		std::string preferredApi = "vulkan";
		std::vector<std::string> apis = {"vulkan", "dx12"};
		bool vsync = true;
		bool culling = true;           // frustum + distance + screen size culling
		bool occlusionCulling = true;  // GPU occlusion culling (Wicked)
		bool meshletOcclusionCulling = true;
		int msaa = 1;                  // 1 / 2 / 4 / 8
		bool shadows = true;
		bool reflections = true;
		float resolutionScale = 1.0f;
		std::string antiAliasing = "taa"; // none | fxaa | taa
		std::string tonemap = "aces";     // reinhard | aces | uchimura
		float exposure = 1.0f;
		float saturation = 1.0f;
		float contrast = 1.0f;
		bool bloom = true;
		float bloomThreshold = 1.0f;
		bool dither = true;               // removes banding in smooth sky gradients
		bool eyeAdaptation = false;
	};

	struct WindowSettings
	{
		std::string title;
		int width = 1600;
		int height = 900;
		bool fullscreen = false;
		bool borderless = false;
		bool resizable = true;
	};

	struct ScriptSettings
	{
		std::vector<std::string> dirs = {"scripts"};
		bool hotReload = true;
		bool strip = false;  // strip debug info from release bytecode
		bool debug = true;   // allow the Lua debugger to attach
	};

	struct ShaderSettings
	{
		std::vector<std::string> dirs = {"shaders"};
		std::vector<std::string> includeDirs;
		std::vector<std::string> defines;
		bool optimize = true;
		bool debugInfo = false;
		bool builtinLibrary = true;                 // Lumen's own material / light shaders (runtime shader library)
		std::vector<std::string> disabledBuiltins;  // names of library shaders switched off ("water", "toon", ...)
	};

	struct NativeSettings
	{
		bool enabled = true;
		std::string moduleName = "game";
		std::vector<std::string> sources = {"native/**/*.cpp", "native/**/*.cc", "native/**/*.c"};
		std::vector<std::string> includeDirs = {"native"};
		std::vector<std::string> defines;
		std::vector<std::string> linkLibraries;
		std::vector<std::string> compileOptions;
		std::vector<std::string> linkOptions;
		std::string cxxStandard = "17";
		std::string buildType = "auto"; // auto: Debug in the editor, Release for builds
		bool hotReload = true;
	};

	struct PhysicsSettings
	{
		Float3 gravity{0.0f, -10.0f, 0.0f};
		float frameRate = 120.0f;
		int accuracy = 8;
		bool interpolation = true;
		bool debugDraw = false;
		std::vector<std::string> layers = {"Default", "Static", "Dynamic", "Character", "Trigger", "Water", "UI", "Ignore Raycast"};
		// layerMatrix[i] is a bit mask of layers that layer i collides with.
		std::vector<uint32_t> layerMatrix;
	};

	struct AudioSettings
	{
		float master = 1.0f;
		float music = 0.8f;
		float sfx = 1.0f;
		float ui = 1.0f;
		float voice = 1.0f;
	};

	struct AssetSettings
	{
		std::vector<std::string> ignore = {"*.tmp", "*.bak", "*~", ".git", ".svn", ".lumen", "build", "*.meta.tmp"};
	};

	struct BuildSettings
	{
		std::string outputDir = "build";
		bool packScripts = true;
		bool packScenes = true;
		bool packShaders = true;
		bool includeRuntime = true;
		std::vector<std::string> copyDirs = {"textures", "models", "audio", "video", "fonts", "animations", "prefabs", "materials"};
	};

	struct ProjectSettings
	{
		int formatVersion = kProjectFormatVersion;
		std::string name = "Untitled";
		std::string id;                  // GUID
		std::string description;
		std::string company;
		std::string gameVersion = "0.1.0";
		std::string engineVersion = LUMEN_VERSION_STRING;
		std::string startupScene = "scenes/main.scene";
		std::string mainScript = "scripts/main.lua";
		GraphicsSettings graphics;
		WindowSettings window;
		ScriptSettings scripts;
		ShaderSettings shaders;
		NativeSettings native;
		PhysicsSettings physics;
		AudioSettings audio;
		AssetSettings assets;
		BuildSettings build;
		std::vector<DependencySpec> packages;
		Json extra; // unknown top level keys, preserved
	};

	struct ProjectCreateOptions
	{
		std::string directory;      // folder to create the project in (created when missing)
		std::string name;
		std::string templateName = "default";
		bool nativeModule = true;
		std::string runtimeRoot;    // lumen_runtime folder used to find templates ("" = built-in files only)
	};

	class Project
	{
	public:
		// Loads a .lum file, or the first .lum found in a directory.
		static Status Load(const std::string& pathOrDirectory, Project& out);
		// Creates a new project on disk: manifest, folder tree, starter content, default scene.
		static Status Create(const ProjectCreateOptions& options, Project& out);

		Status Save() const;
		// Checks paths and sections; returns human readable problems.
		std::vector<std::string> Validate() const;

		bool IsValid() const { return !root_.empty(); }
		const std::string& Root() const { return root_; }
		const std::string& FilePath() const { return file_; }
		ProjectSettings& Settings() { return settings_; }
		const ProjectSettings& Settings() const { return settings_; }

		// Project relative -> absolute, and back. ToRelative returns "" when the path is outside the project.
		std::string Resolve(const std::string& relative) const;
		std::string ToRelative(const std::string& absolute) const;
		bool Contains(const std::string& absolute) const;

		// Well known folders (absolute).
		std::string MetaDir() const;      // <root>/.lumen   (caches, generated files; not part of the project content)
		std::string PackagesDir() const;  // <root>/packages (downloaded dependencies live here)
		std::string NativeBuildDir() const;
		std::string NativeBinDir() const;
		std::string ShaderCacheDir() const;
		std::string LuaCacheDir() const;
		std::string LockFilePath() const; // <root>/lumen.lock.json
		std::string BuildDir() const;

		Json ToJson() const;
		static Status FromJson(const Json& j, ProjectSettings& out);

	private:
		std::string root_;
		std::string file_;
		ProjectSettings settings_;
	};

	// Random RFC-4122 style identifier ("9f3a21c4-...").
	std::string GenerateGuid();

} // namespace lumen
