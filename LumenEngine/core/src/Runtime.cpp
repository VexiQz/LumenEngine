#include "lumen/core/Runtime.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <mutex>

namespace lumen
{
	namespace
	{
		std::mutex g_mutex;
		bool g_initialized = false;
		RuntimeInfo g_info;
		Json g_config;
		constexpr int kRuntimeManifestVersion = 2;

		RuntimeLayout MakeLayout(const std::string& root)
		{
			RuntimeLayout l;
			l.root = root;
			l.lua = fs::Join(root, "lua");
			l.luaApi = fs::Join(root, "lua/api");
			l.luaCache = fs::Join(root, "cache/lua");
			l.include = fs::Join(root, "include");
			l.shaders = fs::Join(root, "shaders");
			l.shaderCache = fs::Join(root, "cache/shaders");
			l.templates = fs::Join(root, "templates");
			l.packages = fs::Join(root, "packages");
			l.imports = fs::Join(root, "cache/imports");
			l.logs = fs::Join(root, "logs");
			l.config = fs::Join(root, "config");
			l.toolchain = fs::Join(root, "toolchain");
			l.docs = fs::Join(root, "docs");
			l.tmp = fs::Join(root, "tmp");
			return l;
		}

		Json DefaultConfig()
		{
			Json c = Json::MakeObject();
			c["lsp"]["enabled"] = true;
			c["lsp"]["bind"] = "127.0.0.1";
			c["lsp"]["port"] = 6009; // 0 picks a free port
			c["lsp"]["announce"] = true;
			c["debugger"]["enabled"] = true;
			c["debugger"]["bind"] = "127.0.0.1";
			c["debugger"]["port"] = 6010;
			c["packages"]["offline"] = false;
			c["packages"]["registry"] = "config/registry.json";
			c["toolchain"]["cmake"] = "";
			c["toolchain"]["generator"] = "";
			c["toolchain"]["compiler"] = "";
			c["toolchain"]["useSystemTools"] = false;
			c["toolchain"]["autoDownload"] = true;
			c["editor"]["theme"] = "dark";
			c["editor"]["autosave"] = true;
			c["editor"]["recentProjects"] = Json::MakeArray();
			return c;
		}

		// Adds keys that exist in `defaults` but not in `target` (recursively); never overwrites user values.
		void MergeDefaults(Json& target, const Json& defaults)
		{
			if (!target.IsObject())
			{
				target = defaults;
				return;
			}
			for (const Json::Member& m : defaults.Members())
			{
				Json* existing = target.FindMutable(m.first);
				if (!existing)
					target[m.first] = m.second;
				else if (existing->IsObject() && m.second.IsObject())
					MergeDefaults(*existing, m.second);
			}
		}

		std::string ConfigPath()
		{
			return fs::Join(g_info.layout.config, "engine.json");
		}

		std::string ManifestPath(const std::string& root)
		{
			return fs::Join(root, "runtime.json");
		}

		std::string ChooseRoot(const RuntimeOptions& options, bool& fallback)
		{
			fallback = false;
			if (!options.explicitRoot.empty())
				return fs::Absolute(options.explicitRoot);
			if (auto env = platform::GetEnv("LUMEN_RUNTIME_DIR"); env && !env->empty())
				return fs::Absolute(*env);
			const std::string beside = fs::Join(platform::ExecutableDir(), "lumen_runtime");
			if (fs::IsDirectory(beside) ? fs::IsWritableDirectory(beside) : fs::IsWritableDirectory(platform::ExecutableDir()))
				return beside;
			if (options.allowUserDataFallback)
			{
				fallback = true;
				return fs::Join(platform::UserDataDir(), "lumen_runtime");
			}
			return beside;
		}

		struct PayloadFile
		{
			std::string path;
			std::string hash;                // hex of FNV-1a
			const EmbeddedFile* embedded = nullptr;
			std::string diskPath;            // when read from a source directory
		};

		std::vector<PayloadFile> CollectPayload(const RuntimeOptions& options)
		{
			std::vector<PayloadFile> files;
			const auto add = [&](PayloadFile f) {
				for (PayloadFile& existing : files)
				{
					if (existing.path == f.path)
					{
						existing = std::move(f);
						return;
					}
				}
				files.push_back(std::move(f));
			};
			for (size_t i = 0; i < options.embedded.count; ++i)
			{
				const EmbeddedFile& e = options.embedded.files[i];
				PayloadFile f;
				f.path = e.path;
				f.hash = str::ToHex(e.hash);
				f.embedded = &e;
				add(std::move(f));
			}
			for (const std::string& dir : options.sourceDirs)
			{
				fs::ListOptions lo;
				lo.recursive = true;
				fs::Walk(dir, lo, [&](const fs::DirEntry& e) {
					if (e.isDirectory)
						return true;
					PayloadFile f;
					f.path = fs::Relative(e.path, dir);
					f.diskPath = e.path;
					std::vector<uint8_t> bytes;
					if (!fs::ReadBinary(e.path, bytes))
						return true;
					f.hash = str::ToHex(str::Hash64(bytes.data(), bytes.size()));
					add(std::move(f));
					return true;
				});
			}
			return files;
		}

		bool WritePayload(const PayloadFile& f, const std::string& dst)
		{
			if (f.embedded)
				return fs::WriteBinary(dst, f.embedded->data, (size_t)f.embedded->size);
			return fs::CopyFile(f.diskPath, dst, true);
		}

		std::string DiskHash(const std::string& path)
		{
			std::vector<uint8_t> bytes;
			if (!fs::ReadBinary(path, bytes))
				return {};
			return str::ToHex(str::Hash64(bytes.data(), bytes.size()));
		}
	} // namespace

	Status Runtime::Initialize(const RuntimeOptions& options)
	{
		std::lock_guard<std::mutex> lock(g_mutex);

		RuntimeInfo info;
		bool fallback = false;
		const std::string root = ChooseRoot(options, fallback);
		info.usedUserDataFallback = fallback;
		info.exeDir = platform::ExecutableDir();
		info.engineVersion = LUMEN_VERSION_STRING;
		info.layout = MakeLayout(root);

		if (!fs::CreateDirs(root))
			return Status::Fail("cannot create runtime folder: " + root);

		const std::string manifestPath = ManifestPath(root);
		Json manifest;
		bool haveManifest = false;
		{
			std::string text;
			if (fs::ReadText(manifestPath, text))
				haveManifest = Json::Parse(text, manifest, nullptr) && manifest.IsObject();
		}
		info.firstRun = !haveManifest;

		const RuntimeLayout& l = info.layout;
		for (const std::string& d : {l.lua, l.luaApi, l.luaCache, l.include, l.shaders, l.shaderCache, l.templates, l.packages, l.imports,
				 l.logs, l.config, l.toolchain, l.docs, l.tmp,
				 fs::Join(l.toolchain, "cmake"), fs::Join(l.toolchain, "ninja"),
				 fs::Join(l.toolchain, "llvm-mingw"), fs::Join(l.toolchain, "downloads")})
		{
			if (!fs::CreateDirs(d))
				return Status::Fail("cannot create runtime folder: " + d);
		}

		Json installed = manifest.Get("files");
		if (!installed.IsObject())
			installed = Json::MakeObject();

		const std::vector<PayloadFile> payload = CollectPayload(options);
		for (const PayloadFile& f : payload)
		{
			const std::string dst = fs::Join(root, f.path);
			const std::string recorded = installed.GetString(f.path);
			if (!fs::IsFile(dst))
			{
				if (WritePayload(f, dst))
				{
					installed[f.path] = f.hash;
					++info.filesInstalled;
				}
				else
				{
					LUMEN_WARN("runtime", "cannot install %s", f.path.c_str());
				}
				continue;
			}
			if (recorded == f.hash && fs::FileSize(dst) > 0)
				continue; // already current (cheap check: manifest says so)
			const std::string diskHash = DiskHash(dst);
			if (diskHash == f.hash)
			{
				installed[f.path] = f.hash;
				continue;
			}
			if (!recorded.empty() && recorded == diskHash)
			{
				// Untouched by the user, the engine ships a newer version: update.
				if (WritePayload(f, dst))
				{
					installed[f.path] = f.hash;
					++info.filesUpdated;
				}
			}
			else
			{
				// The user modified it (or it predates tracking): keep it, drop the new version beside it.
				WritePayload(f, dst + ".lumen-new");
				++info.filesKeptModified;
			}
		}

		Json out = Json::MakeObject();
		out["format"] = "lumen-runtime";
		out["version"] = kRuntimeManifestVersion;
		out["engine"] = LUMEN_VERSION_STRING;
		out["host"] = platform::HostTriple();
		out["createdAt"] = manifest.GetString("createdAt", platform::UtcTimestamp());
		out["updatedAt"] = platform::UtcTimestamp();
		out["exeDir"] = info.exeDir;
		out["lua"] = "5.4 (embedded compiler + VM)";
		Json layout = Json::MakeObject();
		layout["lua"] = "lua";
		layout["api"] = "lua/api";
		layout["luaCache"] = "cache/lua";
		layout["include"] = "include";
		layout["shaders"] = "shaders";
		layout["shaderCache"] = "cache/shaders";
		layout["templates"] = "templates";
		layout["packages"] = "packages";
		layout["imports"] = "cache/imports";
		layout["cache"] = "cache";
		layout["logs"] = "logs";
		layout["config"] = "config";
		layout["toolchain"] = "toolchain";
		layout["docs"] = "docs";
		layout["tmp"] = "tmp";
		out["layout"] = std::move(layout);
		out["files"] = std::move(installed);
		fs::WriteTextIfChanged(manifestPath, out.Dump(2) + "\n");

		g_info = info;

		// Engine configuration: user file merged over defaults.
		g_config = Json::MakeObject();
		{
			std::string text;
			if (fs::ReadText(ConfigPath(), text))
				Json::Parse(text, g_config, nullptr, Json::ParseOptions{true, true, 256});
		}
		MergeDefaults(g_config, DefaultConfig());
		fs::WriteTextIfChanged(ConfigPath(), g_config.Dump(2) + "\n");

		g_initialized = true;

		if (options.installLogFile)
			Log::InstallFileSink(fs::Join(l.logs, options.logName + ".log"));

		if (info.firstRun)
			LUMEN_INFO("runtime", "first launch: created %s (%d files)", root.c_str(), info.filesInstalled);
		else if (info.filesInstalled || info.filesUpdated)
			LUMEN_INFO("runtime", "runtime refreshed: %d installed, %d updated, %d kept (modified)", info.filesInstalled, info.filesUpdated,
				info.filesKeptModified);
		if (fallback)
			LUMEN_WARN("runtime", "engine directory is read-only; runtime lives in %s", root.c_str());
		return Status::Ok();
	}

	bool Runtime::IsInitialized()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		return g_initialized;
	}

	const RuntimeInfo& Runtime::Info()
	{
		return g_info;
	}

	Json& Runtime::Config()
	{
		return g_config;
	}

	Status Runtime::SaveConfig()
	{
		if (!g_initialized)
			return Status::Fail("runtime not initialized");
		if (!fs::WriteTextIfChanged(ConfigPath(), g_config.Dump(2) + "\n"))
			return Status::Fail("cannot write " + ConfigPath());
		return Status::Ok();
	}

	Status Runtime::Repair(const EmbeddedTable& embedded, int* restored)
	{
		if (!g_initialized)
			return Status::Fail("runtime not initialized");
		int count = 0;
		for (size_t i = 0; i < embedded.count; ++i)
		{
			const EmbeddedFile& e = embedded.files[i];
			const std::string dst = fs::Join(g_info.layout.root, e.path);
			if (!fs::IsFile(dst) || DiskHash(dst) != str::ToHex(e.hash))
			{
				if (fs::WriteBinary(dst, e.data, (size_t)e.size))
					++count;
			}
		}
		if (restored)
			*restored = count;
		return Status::Ok();
	}

} // namespace lumen
