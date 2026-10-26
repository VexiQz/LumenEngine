// Lumen Engine — child process execution with live output capture, timeout and cancellation.
//
// Used by the package manager (git / curl / tar), the native module builder (cmake / compiler) and
// the editor's build panel.
#pragma once

#include "lumen/core/Common.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace lumen
{
	struct ProcessOptions
	{
		std::string program;                 // executable name or path (resolved through PATH)
		std::vector<std::string> args;
		std::string workingDir;              // empty: inherit
		std::vector<std::pair<std::string, std::string>> env; // overrides / additions
		bool mergeStderr = true;             // true: stderr is interleaved into the stdout stream
		bool captureOutput = true;           // false: output is only delivered to onOutput
		uint32_t timeoutMs = 0;              // 0: no timeout
		std::atomic<bool>* cancel = nullptr; // set to true from another thread to kill the process
		// Called from the thread running RunProcess() with raw chunks as they arrive.
		std::function<void(std::string_view chunk, bool isStderr)> onOutput;
		// Optional text sent to the process' stdin (then stdin is closed).
		std::string stdinText;
	};

	struct ProcessResult
	{
		bool started = false;
		int exitCode = -1;
		bool timedOut = false;
		bool cancelled = false;
		std::string output; // stdout (+ stderr when merged)
		std::string error;  // stderr when not merged
		std::string startError;

		bool Success() const { return started && !timedOut && !cancelled && exitCode == 0; }
	};

	// Runs a process to completion.
	ProcessResult RunProcess(const ProcessOptions& options);

	// Locates an executable in PATH (and `extraDirs`). On Windows ".exe/.cmd/.bat/.com" are tried.
	// Returns "" when not found. A name that already contains a directory is returned as is when it exists.
	std::string FindExecutable(const std::string& name, const std::vector<std::string>& extraDirs = {});

	// Quotes one argument for display / shell usage ("a b" -> "\"a b\"").
	std::string QuoteArgument(const std::string& arg);
	// Space separated, quoted command line (for logs).
	std::string CommandLineToString(const ProcessOptions& options);

	// A process running on a background thread.
	class AsyncProcess
	{
	public:
		// Starts running immediately. `onFinished` is invoked on the worker thread.
		static std::shared_ptr<AsyncProcess> Start(ProcessOptions options, std::function<void(const ProcessResult&)> onFinished = {});
		~AsyncProcess();

		void Cancel();
		bool IsRunning() const { return running_.load(); }
		// Blocks until the process has finished and returns its result.
		const ProcessResult& Wait();
		// Output produced so far (thread safe snapshot).
		std::string OutputSoFar() const;

	private:
		AsyncProcess() = default;
		std::thread thread_;
		std::atomic<bool> running_{false};
		std::atomic<bool> cancel_{false};
		mutable std::mutex mutex_;
		std::string live_;
		ProcessResult result_;
	};

} // namespace lumen
