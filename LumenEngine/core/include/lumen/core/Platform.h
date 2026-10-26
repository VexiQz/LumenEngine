// Lumen Engine — operating system abstraction (paths, environment, shared libraries, time).
#pragma once

#include "lumen/core/Common.h"

namespace lumen::platform
{
	enum class Os
	{
		Windows,
		Linux,
		MacOS
	};

#if defined(LUMEN_PLATFORM_WINDOWS)
	constexpr Os kHostOs = Os::Windows;
#elif defined(LUMEN_PLATFORM_MACOS)
	constexpr Os kHostOs = Os::MacOS;
#else
	constexpr Os kHostOs = Os::Linux;
#endif

	// "windows-x64", "linux-x64", "macos-arm64" ...
	std::string HostTriple();
	const char* OsName();

	// Absolute path of the running executable (UTF-8, forward slashes).
	std::string ExecutablePath();
	std::string ExecutableDir();
	// Per user writable data directory of the engine.
	//   Windows: %LOCALAPPDATA%/LumenEngine   Linux: ~/.local/share/LumenEngine   macOS: ~/Library/Application Support/LumenEngine
	std::string UserDataDir();
	std::string UserHomeDir();
	std::string TempDir();
	std::string CurrentDir();
	bool SetCurrentDir(const std::string& dir);

	std::optional<std::string> GetEnv(const std::string& name);
	bool SetEnv(const std::string& name, const std::string& value);

	const char* SharedLibraryExtension(); // ".dll" / ".so" / ".dylib"
	const char* SharedLibraryPrefix();    // ""     / "lib"
	const char* ExecutableExtension();    // ".exe" / ""
	std::string SharedLibraryFileName(const std::string& baseName);
	std::string ExecutableFileName(const std::string& baseName);

	uint32_t LogicalCpuCount();
	// Monotonic clock in milliseconds.
	uint64_t NowMs();
	// Wall clock, milliseconds since the Unix epoch (UTC).
	uint64_t UnixTimeMs();
	// ISO-8601 UTC timestamp, e.g. 2026-10-20T07:14:03Z
	std::string UtcTimestamp(uint64_t unixMs = 0);
	void SleepMs(uint32_t ms);

	// Reveals a file/folder in the system file manager.
	bool OpenInFileManager(const std::string& path);
	// Opens a file or URL with the default application.
	bool OpenWithDefaultApp(const std::string& pathOrUrl);

	// Returns the process id of the running process.
	uint32_t CurrentProcessId();

#if defined(LUMEN_PLATFORM_WINDOWS)
	std::wstring Utf8ToWide(std::string_view s);
	std::string WideToUtf8(std::wstring_view s);
#endif

	// A dynamically loaded shared library (dlopen / LoadLibrary).
	class SharedLibrary
	{
	public:
		SharedLibrary() = default;
		~SharedLibrary() { Close(); }
		SharedLibrary(const SharedLibrary&) = delete;
		SharedLibrary& operator=(const SharedLibrary&) = delete;
		SharedLibrary(SharedLibrary&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
		SharedLibrary& operator=(SharedLibrary&& other) noexcept
		{
			if (this != &other)
			{
				Close();
				handle_ = other.handle_;
				other.handle_ = nullptr;
			}
			return *this;
		}

		bool Open(const std::string& path, std::string* error = nullptr);
		void Close();
		bool IsOpen() const { return handle_ != nullptr; }
		void* Symbol(const char* name) const;
		template <typename F>
		F SymbolAs(const char* name) const
		{
			return reinterpret_cast<F>(Symbol(name));
		}

	private:
		void* handle_ = nullptr;
	};

} // namespace lumen::platform
