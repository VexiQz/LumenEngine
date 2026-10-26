// Lumen Engine — package manager.
//
// Dependencies listed in the .lum project are downloaded into `<project>/packages/<name>` (git clone, archive,
// local path or registry name), their own dependencies are resolved transitively, the result is recorded in
// `lumen.lock.json` and aggregated into one environment (Lua paths, shader include dirs, C/C++ include dirs,
// defines, CMake sub projects) that the script system, shader compiler and native build consume.
//
// A package may ship a `lumen-package.json` describing itself; without one the entry of the project (or the
// registry) provides all information. The manager never executes code from a package.
#pragma once

#include "lumen/core/Common.h"
#include "lumen/core/Json.h"
#include "lumen/core/Project.h"

#include <atomic>
#include <functional>

namespace lumen
{
	struct PackageEvent
	{
		enum class Kind { Resolving, Downloading, Extracting, Installed, UpToDate, Removed, Warning, Error };
		Kind kind = Kind::Resolving;
		std::string package;
		std::string message;
		uint64_t done = 0;
		uint64_t total = 0;
	};

	struct InstalledPackage
	{
		DependencySpec spec;       // as requested (after registry resolution)
		std::string dir;           // project relative ("packages/lume")
		std::string resolvedRef;   // commit hash / archive sha256
		std::string contentHash;   // hash over all files of the package directory
		std::vector<std::string> dependencies; // names of packages it depends on
		std::string requestedBy;   // "" = the project itself
		std::string description;
	};

	struct CMakePackage
	{
		std::string name;
		std::string dir; // absolute
		std::vector<std::string> targets;
	};

	// Everything the rest of the engine needs to know about the installed packages.
	struct PackageEnvironment
	{
		std::vector<std::string> luaPaths;        // absolute directories
		std::vector<std::string> shaderDirs;      // absolute
		std::vector<std::string> includeDirs;     // absolute, C/C++
		std::vector<std::string> defines;
		std::vector<CMakePackage> cmakePackages;
		std::vector<std::string> libraries;       // prebuilt libs / system libs
		std::vector<std::string> nativeSources;   // absolute .c/.cpp files compiled into the game module
	};

	struct RegistryEntry
	{
		std::string name;
		std::string description;
		DependencySpec spec;
		std::string kind; // "lua", "native", "shader", "assets"
	};

	class PackageManager
	{
	public:
		using Callback = std::function<void(const PackageEvent&)>;

		Status Open(const Project& project, const std::string& userRegistryPath = {});

		// Installs everything the project lists (and what those need). Packages that are already installed and
		// match the lock file are left alone. `offline` forbids network access.
		Status Sync(const Callback& callback = {}, bool offline = false, std::atomic<bool>* cancel = nullptr);
		// Adds (or replaces) a dependency in the project, installs it and saves the project file.
		Status Add(const DependencySpec& spec, const Callback& callback = {});
		Status AddFromRegistry(const std::string& name, const Callback& callback = {});
		Status Remove(const std::string& name, const Callback& callback = {});
		// Reinstalls a package from its source (re-resolves branches).
		Status Update(const std::string& name, const Callback& callback = {});
		// Verifies package directories against the lock file; returns problems.
		std::vector<std::string> Verify() const;

		const std::vector<InstalledPackage>& Installed() const { return installed_; }
		const InstalledPackage* Find(const std::string& name) const;
		PackageEnvironment Environment() const;

		std::vector<RegistryEntry> Registry() const;
		std::vector<RegistryEntry> SearchRegistry(const std::string& query) const;

		// Validates a package name (lower case letters, digits, '-', '_', '.'; no path tricks).
		static bool IsValidName(const std::string& name);

	private:
		struct Request
		{
			DependencySpec spec;
			std::string requestedBy;
		};
		Status Install(const Request& request, const Callback& cb, bool offline, std::atomic<bool>* cancel, bool force);
		Status InstallGit(const DependencySpec& spec, const std::string& destDir, std::string& resolved, const Callback& cb, std::atomic<bool>* cancel);
		Status InstallArchive(const DependencySpec& spec, const std::string& destDir, std::string& resolved, const Callback& cb, std::atomic<bool>* cancel);
		Status InstallPath(const DependencySpec& spec, const std::string& destDir, std::string& resolved);
		DependencySpec ResolveRegistry(const DependencySpec& spec) const;
		Status LoadLock();
		Status SaveLock() const;
		static std::string HashDirectory(const std::string& dir);
		void ReadPackageManifest(InstalledPackage& pkg, std::vector<Request>& transitive) const;

		bool open_ = false;
		Project project_;
		std::string userRegistry_;
		std::vector<InstalledPackage> installed_;
		Json lock_;
	};

} // namespace lumen
