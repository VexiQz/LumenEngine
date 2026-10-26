#include "Diagnostics.h"

#include "lumen/core/Log.h"
#include "lumen/core/Platform.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdlib>
#include <exception>
#endif

namespace lumen::diagnostics
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		std::mutex g_mutex;
		std::string g_crashDumpPath;
		std::string g_hangDumpPath;
		std::string g_stage = "diagnostics not initialized";
		Clock::time_point g_lastProgress = Clock::now();
		std::atomic<bool> g_running{false};
		std::atomic<uint64_t> g_completedFrames{0};
		std::atomic_flag g_crashHandlerActive = ATOMIC_FLAG_INIT;
		std::thread g_watchdog;
		bool g_stallReported = false;

#ifdef _WIN32
		bool WriteDump(EXCEPTION_POINTERS* exceptionInfo, const std::string& path)
		{
			if (path.empty())
				return false;

			const std::wstring widePath = platform::Utf8ToWide(path);
			HANDLE file = CreateFileW(widePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return false;

			MINIDUMP_EXCEPTION_INFORMATION exception = {};
			exception.ThreadId = GetCurrentThreadId();
			exception.ExceptionPointers = exceptionInfo;
			exception.ClientPointers = FALSE;
			const BOOL written = MiniDumpWriteDump(
				GetCurrentProcess(),
				GetCurrentProcessId(),
				file,
				MiniDumpNormal,
				exceptionInfo ? &exception : nullptr,
				nullptr,
				nullptr);
			CloseHandle(file);
			return written != FALSE;
		}

		std::string ModuleForAddress(void* address)
		{
			MEMORY_BASIC_INFORMATION info = {};
			if (!address || VirtualQuery(address, &info, sizeof(info)) == 0 || !info.AllocationBase)
				return "unknown";

			wchar_t path[MAX_PATH] = {};
			if (GetModuleFileNameW(static_cast<HMODULE>(info.AllocationBase), path, MAX_PATH) == 0)
				return "unknown";
			return platform::WideToUtf8(path);
		}

		void LogCrash(const char* reason, DWORD code = 0, void* address = nullptr)
		{
			std::string stage;
			if (g_mutex.try_lock())
			{
				stage = g_stage;
				g_mutex.unlock();
			}
			else
				stage = "diagnostic stage unavailable (lock busy)";
			const std::string module = ModuleForAddress(address);
			LUMEN_FATAL("diagnostics", "%s; code=0x%08lX; address=%p; module=%s; stage=%s", reason, code, address, module.c_str(), stage.c_str());
			const bool dumpWritten = WriteDump(nullptr, g_crashDumpPath);
			LUMEN_FATAL("diagnostics", "crash minidump %s: %s", dumpWritten ? "written" : "failed", g_crashDumpPath.c_str());
			Log::Flush();
		}

		LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
		{
			if (g_crashHandlerActive.test_and_set())
				return EXCEPTION_CONTINUE_SEARCH;

			const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionCode : 0;
			void* address = exceptionInfo && exceptionInfo->ExceptionRecord ? exceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;
			std::string stage;
			if (g_mutex.try_lock())
			{
				stage = g_stage;
				g_mutex.unlock();
			}
			else
				stage = "diagnostic stage unavailable (lock busy)";
			const std::string module = ModuleForAddress(address);
			LUMEN_FATAL("diagnostics", "unhandled Windows exception; code=0x%08lX; address=%p; module=%s; stage=%s",
				code, address, module.c_str(), stage.c_str());
			const bool dumpWritten = WriteDump(exceptionInfo, g_crashDumpPath);
			LUMEN_FATAL("diagnostics", "crash minidump %s: %s", dumpWritten ? "written" : "failed", g_crashDumpPath.c_str());
			Log::Flush();
			return EXCEPTION_EXECUTE_HANDLER;
		}

		void TerminateHandler()
		{
			LogCrash("std::terminate called");
			std::abort();
		}

		void AbortSignalHandler(int signal)
		{
			LogCrash("abort signal received", static_cast<DWORD>(signal));
			std::_Exit(128 + signal);
		}
#endif

		void Watchdog()
		{
			while (g_running.load())
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));
				if (!g_running.load())
					break;

				std::string stage;
				uint64_t frames = 0;
				int64_t stalledSeconds = 0;
				{
					std::lock_guard<std::mutex> lock(g_mutex);
					stalledSeconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - g_lastProgress).count();
					if (stalledSeconds < 30 || g_stallReported)
						continue;
					g_stallReported = true;
					stage = g_stage;
					frames = g_completedFrames.load();
				}

				LUMEN_ERROR("diagnostics", "no completed frame for %lld seconds; last stage='%s'; completed_frames=%llu",
					static_cast<long long>(stalledSeconds), stage.c_str(), static_cast<unsigned long long>(frames));
#ifdef _WIN32
				const bool dumpWritten = WriteDump(nullptr, g_hangDumpPath);
				LUMEN_ERROR("diagnostics", "hang minidump %s: %s", dumpWritten ? "written" : "failed", g_hangDumpPath.c_str());
#endif
				Log::Flush();
			}
		}
	}

	void Install(const std::string& logDirectory)
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_crashDumpPath = logDirectory + "/lumen_crash.dmp";
			g_hangDumpPath = logDirectory + "/lumen_hang.dmp";
			g_stage = "diagnostics installed";
			g_lastProgress = Clock::now();
		}
		Log::InstallFileSink(logDirectory + "/diagnostics.log", 4ull << 20, 3);
#ifdef _WIN32
		SetUnhandledExceptionFilter(UnhandledExceptionHandler);
		std::set_terminate(TerminateHandler);
		std::signal(SIGABRT, AbortSignalHandler);
#endif
		g_running.store(true);
		g_watchdog = std::thread(Watchdog);
		Stage("diagnostics ready");
	}

	void Stage(const char* name)
	{
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_stage = name ? name : "(unnamed stage)";
			g_lastProgress = Clock::now();
			g_stallReported = false;
		}
		LUMEN_INFO("diagnostics", "stage: %s", name ? name : "(unnamed stage)");
		Log::Flush();
	}

	void FrameCompleted(uint64_t frame)
	{
		g_completedFrames.store(frame);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_stage = "application frame loop";
			g_lastProgress = Clock::now();
			g_stallReported = false;
		}
		if (frame == 1 || frame % 300 == 0)
		{
			LUMEN_INFO("diagnostics", "frame completed: %llu", static_cast<unsigned long long>(frame));
			Log::Flush();
		}
	}

	void Stop()
	{
		g_running.store(false);
		if (g_watchdog.joinable())
			g_watchdog.join();
	}
}
