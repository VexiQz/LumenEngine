#include "lumen/core/GameBuilder.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/LuaCompiler.h"
#include "lumen/core/Platform.h"
#include "lumen/core/SceneDoc.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <set>

namespace lumen
{
	namespace
	{
		void BLog(const BuildOptions& o, const std::string& s)
		{
			LUMEN_INFO("build", "%s", s.c_str());
			if (o.onLog) o.onLog(s);
		}

		bool IsExcludedTopLevel(const std::string& rel, const Project& p)
		{
			static const char* skip[] = {".lumen/", ".git/", ".svn/", ".vs/", ".vscode/", "build/", "native/build/", "packages/.cache/"};
			for (const char* s : skip)
				if (str::StartsWith(rel, s)) return true;
			const std::string out = p.Settings().build.outputDir;
			if (!out.empty() && str::StartsWith(rel, out + "/")) return true;
			return false;
		}

		bool IsNativeSource(const std::string& rel)
		{
			if (!str::StartsWith(rel, "native/")) return false;
			const std::string ext = fs::ExtensionLower(rel);
			return ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cxx" || ext == ".txt" || ext == ".cmake";
		}
	}

	std::vector<std::string> CollectGameFiles(const Project& project, bool includeNative)
	{
		std::vector<std::string> out;
		fs::ListOptions lo;
		lo.recursive = true;
		lo.ignore = project.Settings().assets.ignore;
		const std::string root = project.Root();
		fs::Walk(root, lo, [&](const fs::DirEntry& e) {
			if (e.isDirectory) return true;
			const std::string rel = fs::Relative(e.path, root);
			if (IsExcludedTopLevel(rel, project) || IsNativeSource(rel)) return true;
			if (fs::ExtensionLower(rel) == ".lum" && rel != fs::FileName(project.FilePath())) return true; // other manifests
			if (fs::MatchesAny(project.Settings().assets.ignore, rel) || fs::MatchesAny(project.Settings().assets.ignore, fs::FileName(rel))) return true;
			out.push_back(rel);
			return true;
		});
		(void)includeNative;
		std::sort(out.begin(), out.end());
		return out;
	}

	Status BuildGame(const Project& project, const BuildOptions& options, BuildReport& report)
	{
		if (!project.IsValid()) return Status::Fail("no project");
		const ProjectSettings& ps = project.Settings();
		const std::string startup = project.Resolve(ps.startupScene);
		if (!fs::IsFile(startup)) return Status::Fail("startup scene not found: " + ps.startupScene);

		report = BuildReport{};
		const std::string name = fs::SanitizeFileName(ps.name.empty() ? "Game" : ps.name);
		std::string out = options.outputDir.empty() ? fs::Join(project.BuildDir(), name) : fs::Absolute(options.outputDir);
		// A previous build is replaced, but never anything outside the project / explicit directory.
		if (fs::IsDirectory(out) && options.outputDir.empty()) fs::RemoveAll(out);
		if (!fs::CreateDirs(out)) return Status::Fail("cannot create " + out);
		report.outputDir = out;

		PakWriter pak;
		const std::vector<std::string> files = CollectGameFiles(project, options.includeNative);
		BLog(options, "collecting " + std::to_string(files.size()) + " project files");
		size_t i = 0;
		for (const std::string& rel : files)
		{
			if (options.cancel && *options.cancel) return Status::Fail("cancelled");
			if (options.onProgress) options.onProgress(0.7f * float(i++) / float(std::max<size_t>(1, files.size())));
			const std::string abs = project.Resolve(rel);
			const std::string ext = fs::ExtensionLower(rel);
			if (rel == fs::FileName(project.FilePath()))
			{
				// The manifest travels without editor only state; the player reads it as game/game.lum.
				const std::string text = project.ToJson().Dump(1);
				pak.Add(kPakManifest, text.data(), text.size(), PakEntry_Text);
				continue;
			}
			if (ext == ".lua")
			{
				std::vector<uint8_t> bc;
				lua::Diagnostic d;
				lua::CompileOptions co;
				co.strip = options.stripLua;
				if (!lua::CompileFile(abs, rel, co, bc, &d))
					return Status::Fail(rel + ":" + std::to_string(d.line) + ": " + d.message);
				pak.Add(std::string(kPakGamePrefix) + rel, bc, PakEntry_LuaBytecode);
				++report.scripts;
				continue;
			}
			if (ext == ".scene")
			{
				SceneDoc doc;
				Status s = SceneDoc::Load(abs, doc);
				if (!s)
				{
					report.warnings.push_back(rel + ": " + s.message);
					continue;
				}
				for (const std::string& problem : doc.Validate()) report.warnings.push_back(rel + ": " + problem);
				++report.scenes;
			}
			if (!pak.AddFile(std::string(kPakGamePrefix) + rel, abs, ext == ".scene" || ext == ".json" || ext == ".mat" ? PakEntry_Text : PakEntry_None))
				report.warnings.push_back("cannot read " + rel);
		}
		if (!pak.AddFile(std::string(kPakGamePrefix) + ps.startupScene, startup, PakEntry_Text))
			return Status::Fail("cannot read the startup scene");

		// Native module: the compiled library only (sources stay out of the release).
		if (options.includeNative && ps.native.enabled)
		{
			const std::string lib = fs::Join(project.NativeBinDir(), platform::SharedLibraryFileName(ps.native.moduleName));
			if (fs::IsFile(lib))
			{
				pak.AddFile(std::string(kPakGamePrefix) + ".lumen/bin/" + fs::FileName(lib), lib);
				BLog(options, "native module " + fs::FileName(lib));
			}
			else
				report.warnings.push_back("native module is not built (Build > release first), the game ships without it");
		}

		// Runtime payload: Lua libraries, shader library, fonts and the default config.
		if (!options.runtimeDataDir.empty() && fs::IsDirectory(options.runtimeDataDir))
		{
			static const char* dirs[] = {"lua/lumen", "shaders", "fonts", "config"};
			for (const char* d : dirs)
			{
				const std::string base = fs::Join(options.runtimeDataDir, d);
				if (!fs::IsDirectory(base)) continue;
				fs::ListOptions lo;
				lo.recursive = true;
				fs::Walk(base, lo, [&](const fs::DirEntry& e) {
					if (e.isDirectory) return true;
					const std::string rel = fs::Relative(e.path, options.runtimeDataDir);
					if (str::EndsWith(rel, ".md") || str::EndsWith(rel, "registry.json")) return true;
					if (fs::ExtensionLower(rel) == ".lua")
					{
						std::vector<uint8_t> bc;
						lua::CompileOptions co;
						co.strip = options.stripLua;
						if (lua::CompileFile(e.path, rel, co, bc, nullptr))
						{
							pak.Add(std::string(kPakRuntimePrefix) + rel, bc, PakEntry_LuaBytecode);
							return true;
						}
					}
					pak.AddFile(std::string(kPakRuntimePrefix) + rel, e.path);
					return true;
				});
			}
		}
		else
			report.warnings.push_back("runtime_data not found: Lua libraries and shaders are not packed");

		report.files = pak.Count();
		report.pakPath = fs::Join(out, "data.lpak");
		if (options.onProgress) options.onProgress(0.8f);
		Status s = pak.Save(report.pakPath);
		if (!s) return s;
		report.pakBytes = fs::FileSize(report.pakPath);
		BLog(options, "data.lpak: " + std::to_string(report.files) + " files, " + std::to_string(report.pakBytes / 1024) + " KiB");

		// Player executable + the libraries it needs.
		if (!options.playerExe.empty())
		{
			if (!fs::IsFile(options.playerExe)) return Status::Fail("player executable not found: " + options.playerExe);
			report.exePath = fs::Join(out, platform::ExecutableFileName(name));
			if (!fs::CopyFile(options.playerExe, report.exePath)) return Status::Fail("cannot copy the player to " + report.exePath);
			const std::string playerDir = fs::Parent(options.playerExe);
			for (const fs::DirEntry& e : fs::ListDir(playerDir))
			{
				if (e.isDirectory) continue;
				const std::string ext = fs::ExtensionLower(e.path);
				if (ext == ".dll" || ext == ".so" || (ext == ".json" && fs::FileName(e.path) == "player.json"))
					fs::CopyFile(e.path, fs::Join(out, fs::FileName(e.path)));
			}
			BLog(options, "player: " + fs::FileName(report.exePath));
		}
		else
			report.warnings.push_back("no player executable given: only data.lpak was written");
		if (options.onProgress) options.onProgress(1.0f);
		return Status::Ok();
	}

	Status ExtractGamePackage(const PakReader& pak, const std::string& dstDir, std::string& projectFile)
	{
		if (!pak.IsOpen()) return Status::Fail("package is not open");
		projectFile.clear();
		for (const PakEntry& e : pak.Entries())
		{
			std::string rel;
			if (str::StartsWith(e.path, kPakGamePrefix)) rel = "game/" + e.path.substr(strlen(kPakGamePrefix));
			else if (str::StartsWith(e.path, kPakRuntimePrefix)) rel = "runtime/" + e.path.substr(strlen(kPakRuntimePrefix));
			else continue;
			if (rel.find("..") != std::string::npos) return Status::Fail("unsafe path in package: " + e.path);
			const std::string dst = fs::Join(dstDir, rel);
			if (fs::IsFile(dst) && fs::FileSize(dst) == e.size)
			{
				// Same size: trust it when the recorded checksum file says so (cheap and good enough for a cache).
				const std::string stamp = dst + ".h";
				std::string h;
				if (fs::ReadText(stamp, h) && h == std::to_string(e.hash)) continue;
			}
			std::vector<uint8_t> data;
			if (!pak.Read(e.path, data)) return Status::Fail("corrupt package entry: " + e.path);
			fs::CreateDirs(fs::Parent(dst));
			if (!fs::WriteBinary(dst, data.data(), data.size())) return Status::Fail("cannot write " + dst);
			fs::WriteText(dst + ".h", std::to_string(e.hash));
		}
		const std::string manifest = fs::Join(dstDir, "game/game.lum");
		if (!fs::IsFile(manifest)) return Status::Fail("package has no game.lum");
		projectFile = manifest;
		return Status::Ok();
	}
}
