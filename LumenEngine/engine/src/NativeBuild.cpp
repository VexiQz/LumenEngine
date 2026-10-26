#include "NativeBuild.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Process.h"
#include "lumen/core/Strings.h"

namespace lumen
{
	namespace
	{
		std::string Q(const std::string& s) { return "\"" + str::ReplaceAll(s, "\\", "/") + "\""; }

		void ParseDiagnostics(const std::string& output, std::vector<std::string>& out)
		{
			for (const std::string& line : str::SplitLines(output))
			{
				// gcc/clang: file:line:col: error: msg    msvc: file(line): error Cxxxx: msg
				if (line.find(": error") != std::string::npos || line.find(": fatal error") != std::string::npos || line.find("undefined reference") != std::string::npos ||
					line.find(": warning") != std::string::npos)
					out.push_back(line);
			}
			if (out.size() > 200) out.resize(200);
		}
	}

	NativeBuildResult BuildNativeModule(const Project& project, const Toolchain& tc, const NativeBuildOptions& opt)
	{
		NativeBuildResult res;
		const NativeSettings& ns = project.Settings().native;
		if (!ns.enabled) { res.status = Status::Fail("native modules are disabled in the project settings"); return res; }
		if (!tc.Ready()) { res.status = Status::Fail("toolchain is incomplete: " + str::Join(tc.Missing(), ", ")); return res; }

		const std::string dir = fs::Join(project.MetaDir(), "native");
		const std::string buildDir = fs::Join(project.NativeBuildDir(), opt.release ? "release" : "debug");
		const std::string outDir = project.NativeBinDir();
		fs::CreateDirs(dir);
		fs::CreateDirs(buildDir);
		fs::CreateDirs(outDir);

		std::string cm;
		cm += "cmake_minimum_required(VERSION 3.16)\nproject(" + ns.moduleName + " C CXX)\n";
		cm += "set(CMAKE_CXX_STANDARD " + ns.cxxStandard + ")\nset(CMAKE_CXX_STANDARD_REQUIRED ON)\nset(CMAKE_POSITION_INDEPENDENT_CODE ON)\n";
		cm += "set(CMAKE_C_VISIBILITY_PRESET hidden)\nset(CMAKE_CXX_VISIBILITY_PRESET hidden)\n";
		cm += "set(LUMEN_SOURCES)\n";
		for (const std::string& pat : ns.sources)
			cm += "file(GLOB_RECURSE _g CONFIGURE_DEPENDS " + Q(fs::Join(project.Root(), pat)) + ")\nlist(APPEND LUMEN_SOURCES ${_g})\n";
		for (const std::string& s : opt.packages.nativeSources)
			cm += "list(APPEND LUMEN_SOURCES " + Q(s) + ")\n";
		cm += "if(NOT LUMEN_SOURCES)\n  message(FATAL_ERROR \"no native sources found (see native.sources in the .lum file)\")\nendif()\n";
		cm += "add_library(" + ns.moduleName + " SHARED ${LUMEN_SOURCES})\n";
		cm += "set_target_properties(" + ns.moduleName + " PROPERTIES OUTPUT_NAME " + ns.moduleName + " PREFIX \"" + platform::SharedLibraryPrefix() +
			"\" LIBRARY_OUTPUT_DIRECTORY " + Q(outDir) + " RUNTIME_OUTPUT_DIRECTORY " + Q(outDir) + " LIBRARY_OUTPUT_DIRECTORY_DEBUG " + Q(outDir) +
			" LIBRARY_OUTPUT_DIRECTORY_RELEASE " + Q(outDir) + " RUNTIME_OUTPUT_DIRECTORY_DEBUG " + Q(outDir) + " RUNTIME_OUTPUT_DIRECTORY_RELEASE " + Q(outDir) + ")\n";
		cm += "target_include_directories(" + ns.moduleName + " PRIVATE " + Q(opt.includeDir);
		for (const std::string& d : ns.includeDirs) cm += " " + Q(fs::Join(project.Root(), d));
		for (const std::string& d : opt.packages.includeDirs) cm += " " + Q(d);
		cm += ")\n";
		std::string defs;
		for (const std::string& d : ns.defines) defs += " " + d;
		for (const std::string& d : opt.packages.defines) defs += " " + d;
		if (!defs.empty()) cm += "target_compile_definitions(" + ns.moduleName + " PRIVATE" + defs + ")\n";
		if (!ns.compileOptions.empty())
		{
			cm += "target_compile_options(" + ns.moduleName + " PRIVATE";
			for (const std::string& o : ns.compileOptions) cm += " " + o;
			cm += ")\n";
		}
		for (const CMakePackage& p : opt.packages.cmakePackages)
		{
			cm += "add_subdirectory(" + Q(p.dir) + " " + Q(fs::Join(buildDir, "pkg_" + p.name)) + " EXCLUDE_FROM_ALL)\n";
			for (const std::string& t : p.targets) cm += "target_link_libraries(" + ns.moduleName + " PRIVATE " + t + ")\n";
		}
		std::string libs;
		for (const std::string& l : ns.linkLibraries) libs += " " + l;
		for (const std::string& l : opt.packages.libraries) libs += " " + Q(l);
		if (!libs.empty()) cm += "target_link_libraries(" + ns.moduleName + " PRIVATE" + libs + ")\n";
		// Lua symbols are provided by the host executable.
		cm += "if(APPLE)\n  target_link_options(" + ns.moduleName + " PRIVATE -undefined dynamic_lookup)\nendif()\n";
		if (!fs::WriteTextIfChanged(fs::Join(dir, "CMakeLists.txt"), cm)) { res.status = Status::Fail("cannot write CMakeLists.txt"); return res; }

		std::vector<std::pair<std::string, std::string>> env = tc.env;
		if (!tc.binDirs.empty())
		{
			std::string path = tc.binDirs;
#ifdef _WIN32
			path += ";";
#else
			path += ":";
#endif
			const char* cur = getenv("PATH");
			if (cur) path += cur;
			env.push_back({"PATH", path});
		}
		auto run = [&](std::vector<std::string> args, std::string& output) {
			ProcessOptions po;
			po.program = tc.cmake.path;
			po.args = std::move(args);
			po.workingDir = dir;
			po.env = env;
			po.cancel = opt.cancel;
			po.onOutput = [&](std::string_view chunk, bool) { if (opt.onOutput) opt.onOutput(std::string(chunk)); };
			ProcessResult r = RunProcess(po);
			output = r.output;
			return r;
		};
		std::string out;
		std::vector<std::string> cfg = {"-S", dir, "-B", buildDir, "-G", tc.generator};
		if (!tc.generatorPlatform.empty()) { cfg.push_back("-A"); cfg.push_back(tc.generatorPlatform); }
		const std::string type = opt.release ? "Release" : (ns.buildType == "auto" ? "Debug" : ns.buildType);
		cfg.push_back("-DCMAKE_BUILD_TYPE=" + type);
		if (tc.ninja.Found() && tc.generator == "Ninja") cfg.push_back("-DCMAKE_MAKE_PROGRAM=" + tc.ninja.path);
		if (tc.cc.Found()) cfg.push_back("-DCMAKE_C_COMPILER=" + tc.cc.path);
		if (tc.cxx.Found()) cfg.push_back("-DCMAKE_CXX_COMPILER=" + tc.cxx.path);
		ProcessResult r = run(cfg, out);
		if (!r.Success())
		{
			ParseDiagnostics(out, res.errors);
			res.status = Status::Fail(r.cancelled ? "cancelled" : "CMake configure failed" + (r.startError.empty() ? std::string() : ": " + r.startError));
			return res;
		}
		r = run({"--build", buildDir, "--config", type, "--parallel"}, out);
		ParseDiagnostics(out, res.errors);
		if (!r.Success())
		{
			res.status = Status::Fail(r.cancelled ? "cancelled" : "build failed (" + std::to_string(res.errors.size()) + " diagnostics)");
			return res;
		}
		res.library = fs::Join(outDir, platform::SharedLibraryFileName(ns.moduleName));
		if (!fs::Exists(res.library))
		{
			res.status = Status::Fail("build finished but " + res.library + " was not produced");
			return res;
		}
		res.status = Status::Ok();
		return res;
	}

	Status NativeModuleHost::Open(const std::string& path, lua_State* L, const void* api)
	{
		std::string err;
		// Work on a copy so the original can be rebuilt while the game runs (Windows locks loaded DLLs).
		const std::string copy = fs::Join(fs::Parent(path), ".loaded_" + fs::FileName(path));
		fs::CopyFile(path, copy, true);
		if (!lib_.Open(copy, &err))
			return Status::Fail("cannot load " + path + ": " + err);
		auto open = lib_.SymbolAs<int (*)(lua_State*, const void*)>("lumen_module_open");
		if (!open)
		{
			lib_.Close();
			return Status::Fail("module does not export lumen_module_open");
		}
		close_ = lib_.SymbolAs<void (*)(lua_State*)>("lumen_module_close");
		update_ = lib_.SymbolAs<void (*)(double)>("lumen_module_update");
		fixed_ = lib_.SymbolAs<void (*)(double)>("lumen_module_fixed_update");
		if (open(L, api) != 0)
		{
			lib_.Close();
			return Status::Fail("lumen_module_open reported an error");
		}
		return Status::Ok();
	}

	void NativeModuleHost::Close(lua_State* L)
	{
		if (!lib_.IsOpen())
			return;
		if (close_)
			close_(L);
		lib_.Close();
		close_ = nullptr;
		update_ = fixed_ = nullptr;
	}
}
