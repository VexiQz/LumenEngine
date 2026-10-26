#include "lumen/core/Toolchain.h"

#include "lumen/core/Archive.h"
#include "lumen/core/Fs.h"
#include "lumen/core/Http.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Process.h"
#include "lumen/core/Sha256.h"
#include "lumen/core/Strings.h"

namespace lumen
{
	namespace
	{
#if defined(_WIN32)
		constexpr const char* kExe = ".exe";
		constexpr char kPathSep = ';';
#else
		constexpr const char* kExe = "";
		constexpr char kPathSep = ':';
#endif

		bool HostIsWindows()
		{
#ifdef _WIN32
			return true;
#else
			return false;
#endif
		}

		bool HostIsMac()
		{
#ifdef __APPLE__
			return true;
#else
			return false;
#endif
		}

		bool HostIsArm()
		{
#if defined(__aarch64__) || defined(_M_ARM64)
			return true;
#else
			return false;
#endif
		}

		std::string FirstLine(const std::string& s)
		{
			const size_t nl = s.find_first_of("\r\n");
			return std::string(str::Trim(nl == std::string::npos ? s : s.substr(0, nl)));
		}

		// "cmake version 3.31.6" -> "3.31.6"
		std::string ExtractVersion(const std::string& line)
		{
			for (size_t i = 0; i < line.size(); ++i)
			{
				if (std::isdigit((unsigned char)line[i]) && (i == 0 || !std::isalnum((unsigned char)line[i - 1])))
				{
					size_t j = i;
					while (j < line.size() && (std::isdigit((unsigned char)line[j]) || line[j] == '.'))
						++j;
					if (j - i >= 3)
						return line.substr(i, j - i);
				}
			}
			return line;
		}

		std::string VswhereInstallPath()
		{
#ifdef _WIN32
			const std::string pf = platform::GetEnv("ProgramFiles(x86)").value_or("C:/Program Files (x86)");
			const std::string vswhere = fs::Join(pf, "Microsoft Visual Studio/Installer/vswhere.exe");
			if (!fs::IsFile(vswhere))
				return {};
			ProcessOptions po;
			po.program = vswhere;
			po.args = {"-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"};
			po.timeoutMs = 20000;
			const ProcessResult r = RunProcess(po);
			return r.Success() ? FirstLine(r.output) : std::string();
#else
			return {};
#endif
		}

		std::string VsGeneratorName(const std::string& installPath)
		{
			// installationPath ends with ".../2022/Community" etc.
			if (installPath.find("/2022/") != std::string::npos || installPath.find("\\2022\\") != std::string::npos)
				return "Visual Studio 17 2022";
			if (installPath.find("/2019/") != std::string::npos || installPath.find("\\2019\\") != std::string::npos)
				return "Visual Studio 16 2019";
			if (installPath.find("/18/") != std::string::npos || installPath.find("\\18\\") != std::string::npos)
				return "Visual Studio 18 2026";
			return "Visual Studio 17 2022";
		}
	} // namespace

	std::string ProbeVersion(const std::string& exe, const std::vector<std::string>& args)
	{
		ProcessOptions po;
		po.program = exe;
		po.args = args;
		po.timeoutMs = 15000;
		const ProcessResult r = RunProcess(po);
		if (!r.started)
			return {};
		return FirstLine(r.output);
	}

	std::vector<std::string> Toolchain::Missing() const
	{
		std::vector<std::string> m;
		if (!cmake.Found()) m.push_back("cmake");
		if (generator.empty() || (generator == "Ninja" && !ninja.Found())) m.push_back("ninja");
		if (!cc.Found() || !cxx.Found()) m.push_back("c/c++ compiler");
		return m;
	}

	Json Toolchain::ToJson() const
	{
		Json j = Json::MakeObject();
		const auto tool = [](const ToolInfo& t) {
			Json o = Json::MakeObject();
			o["path"] = t.path;
			o["version"] = t.version;
			o["managed"] = t.managed;
			return o;
		};
		j["cmake"] = tool(cmake);
		j["ninja"] = tool(ninja);
		j["cc"] = tool(cc);
		j["cxx"] = tool(cxx);
		j["generator"] = generator;
		j["description"] = description;
		return j;
	}

	ToolchainManager::ToolchainManager(std::string toolchainDir, Json config) : dir_(std::move(toolchainDir)), config_(std::move(config)) {}

	std::string ToolchainManager::ManagedPath(const std::string& id, const std::string& relativeExe) const
	{
		const std::string root = fs::Join(dir_, id);
		if (!fs::IsDirectory(root))
			return {};
		// The folder holds exactly one version directory (we remove old ones when installing).
		for (const fs::DirEntry& e : fs::ListDir(root))
		{
			if (!e.isDirectory)
				continue;
			const std::string exe = fs::Join(e.path, relativeExe);
			if (fs::IsFile(exe))
				return exe;
		}
		return {};
	}

	// ----------------------------------------------------------------------------------------------------
	// Pinned recipes. Checksums were computed from the official release assets.
	// ----------------------------------------------------------------------------------------------------
	std::vector<ToolchainManager::Recipe> ToolchainManager::Recipes() const
	{
		std::vector<Recipe> r;
		const std::string cmakeVer = "3.31.6";
		const std::string cmakeBase = "https://github.com/Kitware/CMake/releases/download/v" + cmakeVer + "/cmake-" + cmakeVer + "-";
		if (HostIsWindows())
		{
			if (!HostIsArm())
				r.push_back({"cmake", cmakeVer, cmakeBase + "windows-x86_64.zip", "d163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836", "cmake-" + cmakeVer + "-windows-x86_64", 0});
			else
				r.push_back({"cmake", cmakeVer, cmakeBase + "windows-arm64.zip", "fa648fd417f44e6cb08928964a480ade0d18df421f9b623639dba22f9b301e4e", "cmake-" + cmakeVer + "-windows-arm64", 0});
			r.push_back({"ninja", "1.12.1", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip", "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a", "", 0});
			if (!HostIsArm())
				r.push_back({"llvm-mingw", "20250114", "https://github.com/mstorsjo/llvm-mingw/releases/download/20250114/llvm-mingw-20250114-ucrt-x86_64.zip",
					"9395d95c089611e6cd728c9a07219ffc5ac706e0c719833a2f7e64a706a81541", "llvm-mingw-20250114-ucrt-x86_64", 149303636});
		}
		else if (HostIsMac())
		{
			r.push_back({"cmake", cmakeVer, cmakeBase + "macos-universal.tar.gz", "330b9514f5112e5ed4fb08b8b05803b776fd9b539a6ae12927d14dcc0ee2ba8d", "cmake-" + cmakeVer + "-macos-universal", 0});
			r.push_back({"ninja", "1.12.1", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-mac.zip", "89a287444b5b3e98f88a945afa50ce937b8ffd1dcc59c555ad9b1baf855298c9", "", 0});
		}
		else
		{
			if (!HostIsArm())
			{
				r.push_back({"cmake", cmakeVer, cmakeBase + "linux-x86_64.tar.gz", "5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf", "cmake-" + cmakeVer + "-linux-x86_64", 0});
				r.push_back({"ninja", "1.12.1", "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-linux.zip", "6f98805688d19672bd699fbbfa2c2cf0fc054ac3df1f0e6a47664d963d530255", "", 0});
			}
			else
				r.push_back({"cmake", cmakeVer, cmakeBase + "linux-aarch64.tar.gz", "b4cc788d63112b2749b40627e719eb5d3b8ed8f00c36d77189f4019cfe64bc9e", "cmake-" + cmakeVer + "-linux-aarch64", 0});
		}
		return r;
	}

	// ----------------------------------------------------------------------------------------------------
	Toolchain ToolchainManager::Detect() const
	{
		Toolchain tc;
		const std::string cfgCmake = config_.GetString("cmake");
		const std::string cfgNinja = config_.GetString("ninja");
		const std::string cfgCompiler = config_.GetString("compiler");   // path to the C++ compiler
		const std::string cfgGenerator = config_.GetString("generator");
		const std::string preferred = str::ToLower(config_.GetString("preferred", "auto")); // auto|msvc|llvm-mingw|gcc|clang
		// Managed-first policy: Lumen always uses its own downloaded cmake/ninja/compiler (reproducible, nothing for the
		// end user to install). System tools are used only when "useSystemTools": true is set explicitly, or — on
		// Linux/macOS, where no managed compiler recipe exists yet — as the compiler fallback (see below).
		const bool useSystem = config_.GetBool("useSystemTools", false);
		const auto sys = [&](const std::string& n) { return useSystem ? FindExecutable(n) : std::string(); };

		// --- cmake
		tc.cmake.name = "cmake";
		if (!cfgCmake.empty() && fs::IsFile(cfgCmake))
			tc.cmake.path = fs::Normalize(cfgCmake);
		if (tc.cmake.path.empty())
		{
			const std::string rel = HostIsMac() ? "CMake.app/Contents/bin/cmake" : std::string("bin/cmake") + kExe;
			const std::string managed = ManagedPath("cmake", rel);
			if (!managed.empty())
			{
				tc.cmake.path = managed;
				tc.cmake.managed = true;
			}
		}
		if (tc.cmake.path.empty())
			tc.cmake.path = sys("cmake");
		if (tc.cmake.Found())
		{
			tc.cmake.version = ExtractVersion(ProbeVersion(tc.cmake.path));
			if (tc.cmake.version.empty())
				tc.cmake.path.clear(); // not runnable
		}

		// --- ninja
		tc.ninja.name = "ninja";
		if (!cfgNinja.empty() && fs::IsFile(cfgNinja))
			tc.ninja.path = fs::Normalize(cfgNinja);
		if (tc.ninja.path.empty())
		{
			const std::string managed = ManagedPath("ninja", std::string("ninja") + kExe);
			if (!managed.empty())
			{
				tc.ninja.path = managed;
				tc.ninja.managed = true;
			}
		}
		if (tc.ninja.path.empty())
			tc.ninja.path = sys("ninja");
		if (tc.ninja.Found())
		{
			tc.ninja.version = ProbeVersion(tc.ninja.path);
			if (tc.ninja.version.empty())
				tc.ninja.path.clear();
		}

		// --- compiler
		tc.cc.name = "c compiler";
		tc.cxx.name = "c++ compiler";
		const auto setCompiler = [&](const std::string& cc, const std::string& cxx, CompilerKind kind, bool managed, const std::string& versionLine) {
			tc.cc.path = cc;
			tc.cxx.path = cxx;
			tc.cc.managed = tc.cxx.managed = managed;
			tc.cc.version = tc.cxx.version = ExtractVersion(versionLine);
			tc.compiler = kind;
		};
		const auto tryManagedMingw = [&]() {
			const std::string cxx = ManagedPath("llvm-mingw", std::string("bin/clang++") + kExe);
			const std::string cc = ManagedPath("llvm-mingw", std::string("bin/clang") + kExe);
			if (!cxx.empty() && !cc.empty())
			{
				setCompiler(cc, cxx, CompilerKind::LlvmMingw, true, ProbeVersion(cxx));
				tc.binDirs = fs::Parent(cxx);
				return true;
			}
			return false;
		};
		const auto trySystem = [&](const char* ccName, const char* cxxName, CompilerKind kind) {
			const std::string cc = sys(ccName);
			const std::string cxx = sys(cxxName);
			if (!cc.empty() && !cxx.empty())
			{
				std::string v = ProbeVersion(cxx);
				if (v.empty())
					return false;
				if (kind == CompilerKind::Clang && v.find("Apple") != std::string::npos)
					kind = CompilerKind::AppleClang;
				setCompiler(cc, cxx, kind, false, v);
				return true;
			}
			return false;
		};

		bool haveCompiler = false;
		if (!cfgCompiler.empty() && fs::IsFile(cfgCompiler))
		{
			std::string cc = cfgCompiler;
			const std::string stem = fs::Stem(cfgCompiler);
			if (stem.size() > 2 && str::EndsWith(stem, "++"))
				cc = fs::Join(fs::Parent(cfgCompiler), stem.substr(0, stem.size() - 2) + kExe);
			CompilerKind kind = stem.find("clang") != std::string::npos ? CompilerKind::Clang : CompilerKind::Gcc;
			setCompiler(fs::IsFile(cc) ? cc : cfgCompiler, cfgCompiler, kind, false, ProbeVersion(cfgCompiler));
			haveCompiler = true;
		}

#ifdef _WIN32
		std::string vsPath;
		if (!haveCompiler && (preferred == "auto" || preferred == "msvc"))
		{
			vsPath = useSystem ? VswhereInstallPath() : std::string();
			if (!vsPath.empty())
			{
				tc.compiler = CompilerKind::Msvc;
				tc.generator = cfgGenerator.empty() ? VsGeneratorName(vsPath) : cfgGenerator;
				tc.generatorPlatform = HostIsArm() ? "ARM64" : "x64";
				tc.cc.path = tc.cxx.path = fs::Normalize(vsPath); // resolved by the VS generator itself
				tc.cc.version = tc.cxx.version = "Visual Studio";
				haveCompiler = true;
			}
		}
		if (!haveCompiler && (preferred == "auto" || preferred == "llvm-mingw"))
			haveCompiler = tryManagedMingw();
		if (!haveCompiler && (preferred == "auto" || preferred == "clang"))
			haveCompiler = trySystem("clang", "clang++", CompilerKind::Clang);
		if (!haveCompiler && (preferred == "auto" || preferred == "gcc"))
			haveCompiler = trySystem("gcc", "g++", CompilerKind::Gcc);
#else
		// No managed compiler recipe for this host: fall back to the system compiler even in managed mode.
		const bool sysCompilerFallback = !useSystem;
		const auto sysCc = [&](const char* ccName, const char* cxxName, CompilerKind kind) {
			if (useSystem)
				return trySystem(ccName, cxxName, kind);
			const std::string cc = FindExecutable(ccName), cxx = FindExecutable(cxxName);
			if (cc.empty() || cxx.empty())
				return false;
			const std::string v = ProbeVersion(cxx);
			if (v.empty())
				return false;
			if (kind == CompilerKind::Clang && v.find("Apple") != std::string::npos)
				kind = CompilerKind::AppleClang;
			setCompiler(cc, cxx, kind, false, v);
			return true;
		};
		(void)sysCompilerFallback;
		if (!haveCompiler && (preferred == "auto" || preferred == "clang") && HostIsMac())
			haveCompiler = sysCc("clang", "clang++", CompilerKind::Clang);
		if (!haveCompiler && (preferred == "auto" || preferred == "gcc"))
			haveCompiler = sysCc("gcc", "g++", CompilerKind::Gcc);
		if (!haveCompiler && (preferred == "auto" || preferred == "clang"))
			haveCompiler = sysCc("clang", "clang++", CompilerKind::Clang);
		if (!haveCompiler && (preferred == "auto" || preferred == "clang"))
			haveCompiler = sysCc("cc", "c++", CompilerKind::Gcc);
#endif

		if (tc.generator.empty())
		{
			if (!cfgGenerator.empty())
				tc.generator = cfgGenerator;
			else if (tc.ninja.Found())
				tc.generator = "Ninja";
#ifndef _WIN32
			else if (!sys("make").empty())
				tc.generator = "Unix Makefiles";
#endif
		}

		// description
		std::string comp;
		switch (tc.compiler)
		{
		case CompilerKind::Msvc: comp = "MSVC (" + tc.generator + ")"; break;
		case CompilerKind::Gcc: comp = "GCC " + tc.cxx.version; break;
		case CompilerKind::Clang: comp = "Clang " + tc.cxx.version; break;
		case CompilerKind::AppleClang: comp = "Apple Clang " + tc.cxx.version; break;
		case CompilerKind::LlvmMingw: comp = "LLVM-MinGW " + tc.cxx.version; break;
		default: comp = "no compiler"; break;
		}
		tc.description = "CMake " + (tc.cmake.Found() ? tc.cmake.version : std::string("missing")) + ", " + (tc.generator.empty() ? "no generator" : tc.generator) + ", " + comp;

		// PATH additions for managed tools (compilers locate their runtime relative to themselves, but CMake needs ninja on PATH).
		std::string dirs = tc.binDirs;
		if (tc.ninja.Found() && tc.ninja.managed)
			dirs += (dirs.empty() ? "" : std::string(1, kPathSep)) + fs::Parent(tc.ninja.path);
		tc.binDirs = dirs;
		return tc;
	}

	// ----------------------------------------------------------------------------------------------------
	Status ToolchainManager::Install(const Recipe& recipe, const Callback& progress, std::atomic<bool>* cancel, float base, float span)
	{
		fs::CreateDirs(dir_);
		const std::string cacheDir = fs::Join(dir_, "downloads");
		fs::CreateDirs(cacheDir);
		const std::string archiveFile = fs::Join(cacheDir, fs::FileName(recipe.url));
		const std::string label = recipe.id + " " + recipe.version;

		bool have = fs::IsFile(archiveFile) && str::EqualsIgnoreCase(Sha256::HashFileHex(archiveFile), recipe.sha256);
		if (!have)
		{
			http::DownloadOptions d;
			d.url = recipe.url;
			d.destPath = archiveFile;
			d.sha256 = recipe.sha256;
			d.cancel = cancel;
			d.progress = [&](uint64_t done, uint64_t total) {
				if (!progress)
					return;
				const uint64_t t = total ? total : recipe.sizeBytes;
				ToolchainProgress p;
				p.step = "Downloading " + label;
				p.done = done;
				p.total = t;
				p.fraction = t ? base + span * 0.8f * std::min(1.0f, (float)done / (float)t) : base;
				progress(p);
			};
			Status s = http::Download(d);
			if (!s)
				return s;
		}
		if (progress)
			progress({"Extracting " + label, 0, 0, base + span * 0.85f});

		const std::string target = fs::Join(dir_, recipe.id);
		const std::string tmp = fs::Join(dir_, "tmp-" + fs::UniqueName(recipe.id));
		fs::RemoveAll(tmp);
		archive::ExtractOptions eo;
		eo.stripComponents = 0;
		eo.cancel = cancel;
		Status s = archive::Extract(archiveFile, tmp, eo);
		if (!s)
		{
			fs::RemoveAll(tmp);
			return s;
		}
		if (cancel && cancel->load())
		{
			fs::RemoveAll(tmp);
			return Status::Fail("cancelled");
		}
		// Normalise to <target>/<version>/ with the archive root folder stripped.
		std::string root = tmp;
		if (!recipe.archiveRoot.empty() && fs::IsDirectory(fs::Join(tmp, recipe.archiveRoot)))
			root = fs::Join(tmp, recipe.archiveRoot);
		fs::RemoveAll(target);
		fs::CreateDirs(target);
		const std::string versionDir = fs::Join(target, recipe.version);
		if (!fs::Rename(root, versionDir) && !fs::CopyDir(root, versionDir, true))
		{
			fs::RemoveAll(tmp);
			return Status::Fail("cannot install " + label);
		}
		fs::RemoveAll(tmp);
#ifndef _WIN32
		// Zip archives do not always carry the executable bit through our extractor for tools without mode info.
		if (recipe.id == "ninja")
			RunProcess({"chmod", {"+x", fs::Join(versionDir, "ninja")}});
#endif
		if (progress)
			progress({"Installed " + label, 0, 0, base + span});
		return Status::Ok();
	}

	Status ToolchainManager::Ensure(Toolchain& out, bool allowDownload, const Callback& progress, std::atomic<bool>* cancel)
	{
		out = Detect();
		if (out.Ready())
			return Status::Ok();
		const std::vector<std::string> missing = out.Missing();
		if (!allowDownload)
			return Status::Fail("missing build tools: " + str::Join(missing, ", "));

		const std::vector<Recipe> recipes = Recipes();
		std::vector<const Recipe*> todo;
		std::string cannotProvision;
		const auto find = [&](const char* id) -> const Recipe* {
			for (const Recipe& r : recipes)
				if (r.id == id)
					return &r;
			return nullptr;
		};
		for (const std::string& m : missing)
		{
			const char* id = m == "cmake" ? "cmake" : m == "ninja" ? "ninja" : "llvm-mingw";
			const Recipe* r = find(id);
			if (!r)
			{
				if (m == "c/c++ compiler")
#ifdef __APPLE__
					cannotProvision = "no C/C++ compiler found: run `xcode-select --install`";
#else
					cannotProvision = "no C/C++ compiler found: install gcc/g++ or clang (e.g. `sudo apt install build-essential`)";
#endif
				else
					cannotProvision = "cannot provision " + m + " on this platform";
				continue;
			}
			if (std::find(todo.begin(), todo.end(), r) == todo.end())
				todo.push_back(r);
		}
		// A Visual Studio / Makefile based setup does not need ninja.
		const float step = todo.empty() ? 1.0f : 1.0f / (float)todo.size();
		float base = 0;
		for (const Recipe* r : todo)
		{
			if (cancel && cancel->load())
				return Status::Fail("cancelled");
			LUMEN_INFO("toolchain", "installing %s %s", r->id.c_str(), r->version.c_str());
			Status s = Install(*r, progress, cancel, base, step);
			if (!s)
				return s;
			base += step;
		}
		out = Detect();
		if (!out.Ready() && !cannotProvision.empty())
			return Status::Fail(cannotProvision);
		if (!out.Ready())
			return Status::Fail("toolchain still incomplete after installation: " + str::Join(out.Missing(), ", "));
		return Status::Ok();
	}

} // namespace lumen
