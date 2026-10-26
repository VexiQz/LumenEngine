#include "lumen/core/Platform.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Strings.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <thread>

#if defined(LUMEN_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(LUMEN_PLATFORM_MACOS)
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace lumen::platform
{
#if defined(LUMEN_PLATFORM_WINDOWS)
	std::wstring Utf8ToWide(std::string_view s)
	{
		if (s.empty())
			return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring out((size_t)n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
		return out;
	}

	std::string WideToUtf8(std::wstring_view s)
	{
		if (s.empty())
			return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
		std::string out((size_t)n, '\0');
		WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
		return out;
	}
#endif

	const char* OsName()
	{
		switch (kHostOs)
		{
		case Os::Windows: return "windows";
		case Os::MacOS: return "macos";
		default: return "linux";
		}
	}

	std::string HostTriple()
	{
#if defined(__x86_64__) || defined(_M_X64)
		const char* arch = "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
		const char* arch = "arm64";
#elif defined(__i386__) || defined(_M_IX86)
		const char* arch = "x86";
#else
		const char* arch = "unknown";
#endif
		return std::string(OsName()) + "-" + arch;
	}

	std::string ExecutablePath()
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		std::wstring buf(1024, L'\0');
		while (true)
		{
			const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
			if (n == 0)
				return {};
			if (n < buf.size())
			{
				buf.resize(n);
				break;
			}
			buf.resize(buf.size() * 2);
		}
		return fs::Normalize(WideToUtf8(buf));
#elif defined(LUMEN_PLATFORM_MACOS)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buf(size, '\0');
		if (_NSGetExecutablePath(buf.data(), &size) != 0)
			return {};
		buf.resize(std::strlen(buf.c_str()));
		return fs::Canonical(buf);
#else
		std::string buf(PATH_MAX, '\0');
		const ssize_t n = readlink("/proc/self/exe", buf.data(), buf.size());
		if (n <= 0)
			return {};
		buf.resize((size_t)n);
		return fs::Normalize(buf);
#endif
	}

	std::string ExecutableDir()
	{
		return fs::Parent(ExecutablePath());
	}

	std::string UserHomeDir()
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		if (auto v = GetEnv("USERPROFILE"))
			return fs::Normalize(*v);
		return {};
#else
		if (auto v = GetEnv("HOME"))
			return fs::Normalize(*v);
		if (const passwd* pw = getpwuid(getuid()))
			return fs::Normalize(pw->pw_dir);
		return {};
#endif
	}

	std::string UserDataDir()
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		if (auto v = GetEnv("LOCALAPPDATA"))
			return fs::Join(fs::Normalize(*v), "LumenEngine");
		return fs::Join(UserHomeDir(), "AppData/Local/LumenEngine");
#elif defined(LUMEN_PLATFORM_MACOS)
		return fs::Join(UserHomeDir(), "Library/Application Support/LumenEngine");
#else
		if (auto v = GetEnv("XDG_DATA_HOME"); v && !v->empty())
			return fs::Join(fs::Normalize(*v), "LumenEngine");
		return fs::Join(UserHomeDir(), ".local/share/LumenEngine");
#endif
	}

	std::string TempDir()
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		std::wstring buf(MAX_PATH + 1, L'\0');
		const DWORD n = GetTempPathW((DWORD)buf.size(), buf.data());
		buf.resize(n);
		return fs::Normalize(WideToUtf8(buf));
#else
		if (auto v = GetEnv("TMPDIR"); v && !v->empty())
			return fs::Normalize(*v);
		return "/tmp";
#endif
	}

	std::string CurrentDir()
	{
		return fs::CurrentDir();
	}

	bool SetCurrentDir(const std::string& dir)
	{
		std::error_code ec;
		std::filesystem::current_path(fs::ToPath(dir), ec);
		return !ec;
	}

	std::optional<std::string> GetEnv(const std::string& name)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		const std::wstring wname = Utf8ToWide(name);
		const DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
		if (n == 0)
			return std::nullopt;
		std::wstring buf(n, L'\0');
		const DWORD got = GetEnvironmentVariableW(wname.c_str(), buf.data(), n);
		buf.resize(got);
		return WideToUtf8(buf);
#else
		const char* v = std::getenv(name.c_str());
		if (!v)
			return std::nullopt;
		return std::string(v);
#endif
	}

	bool SetEnv(const std::string& name, const std::string& value)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		return SetEnvironmentVariableW(Utf8ToWide(name).c_str(), Utf8ToWide(value).c_str()) != 0;
#else
		return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
	}

	const char* SharedLibraryExtension()
	{
		switch (kHostOs)
		{
		case Os::Windows: return ".dll";
		case Os::MacOS: return ".dylib";
		default: return ".so";
		}
	}

	const char* SharedLibraryPrefix()
	{
		return kHostOs == Os::Windows ? "" : "lib";
	}

	const char* ExecutableExtension()
	{
		return kHostOs == Os::Windows ? ".exe" : "";
	}

	std::string SharedLibraryFileName(const std::string& baseName)
	{
		return std::string(SharedLibraryPrefix()) + baseName + SharedLibraryExtension();
	}

	std::string ExecutableFileName(const std::string& baseName)
	{
		return baseName + ExecutableExtension();
	}

	uint32_t LogicalCpuCount()
	{
		const unsigned n = std::thread::hardware_concurrency();
		return n == 0 ? 1u : (uint32_t)n;
	}

	uint64_t NowMs()
	{
		using namespace std::chrono;
		return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
	}

	uint64_t UnixTimeMs()
	{
		using namespace std::chrono;
		return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	}

	std::string UtcTimestamp(uint64_t unixMs)
	{
		if (unixMs == 0)
			unixMs = UnixTimeMs();
		const std::time_t t = (std::time_t)(unixMs / 1000);
		std::tm tmv{};
#if defined(LUMEN_PLATFORM_WINDOWS)
		gmtime_s(&tmv, &t);
#else
		gmtime_r(&t, &tmv);
#endif
		char buf[32];
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
		return buf;
	}

	void SleepMs(uint32_t ms)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	uint32_t CurrentProcessId()
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		return (uint32_t)GetCurrentProcessId();
#else
		return (uint32_t)getpid();
#endif
	}

	bool OpenWithDefaultApp(const std::string& pathOrUrl)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", Utf8ToWide(pathOrUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return (INT_PTR)r > 32;
#else
		pid_t pid = fork();
		if (pid < 0)
			return false;
		if (pid == 0)
		{
#if defined(LUMEN_PLATFORM_MACOS)
			execlp("open", "open", pathOrUrl.c_str(), (char*)nullptr);
#else
			execlp("xdg-open", "xdg-open", pathOrUrl.c_str(), (char*)nullptr);
#endif
			_exit(127);
		}
		return true;
#endif
	}

	bool OpenInFileManager(const std::string& path)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		std::wstring args = L"/select,\"" + Utf8ToWide(fs::ToNative(path)) + L"\"";
		if (fs::IsDirectory(path))
			return OpenWithDefaultApp(path);
		const HINSTANCE r = ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
		return (INT_PTR)r > 32;
#else
		return OpenWithDefaultApp(fs::IsDirectory(path) ? path : fs::Parent(path));
#endif
	}

	bool SharedLibrary::Open(const std::string& path, std::string* error)
	{
		Close();
#if defined(LUMEN_PLATFORM_WINDOWS)
		handle_ = (void*)LoadLibraryW(Utf8ToWide(fs::ToNative(path)).c_str());
		if (!handle_ && error)
		{
			const DWORD code = GetLastError();
			wchar_t* msg = nullptr;
			FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, nullptr);
			*error = StrFormat("LoadLibrary failed (%lu): %s", (unsigned long)code, msg ? WideToUtf8(msg).c_str() : "unknown error");
			if (msg)
				LocalFree(msg);
		}
#else
		handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!handle_ && error)
		{
			const char* e = dlerror();
			*error = e ? e : "dlopen failed";
		}
#endif
		return handle_ != nullptr;
	}

	void SharedLibrary::Close()
	{
		if (!handle_)
			return;
#if defined(LUMEN_PLATFORM_WINDOWS)
		FreeLibrary((HMODULE)handle_);
#else
		dlclose(handle_);
#endif
		handle_ = nullptr;
	}

	void* SharedLibrary::Symbol(const char* name) const
	{
		if (!handle_)
			return nullptr;
#if defined(LUMEN_PLATFORM_WINDOWS)
		return (void*)GetProcAddress((HMODULE)handle_, name);
#else
		return dlsym(handle_, name);
#endif
	}

} // namespace lumen::platform
