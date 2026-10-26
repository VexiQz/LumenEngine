#include "lumen/core/Process.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <algorithm>

#if defined(LUMEN_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace lumen
{
	// ------------------------------------------------------------------------------------------------
	// helpers
	// ------------------------------------------------------------------------------------------------
	std::string QuoteArgument(const std::string& arg)
	{
		if (!arg.empty() && arg.find_first_of(" \t\"'$&|;<>()*?[]{}!`\\") == std::string::npos)
			return arg;
		std::string out = "\"";
		for (const char c : arg)
		{
			if (c == '"' || c == '\\')
				out += '\\';
			out += c;
		}
		out += '"';
		return out;
	}

	std::string CommandLineToString(const ProcessOptions& options)
	{
		std::string out = QuoteArgument(options.program);
		for (const std::string& a : options.args)
		{
			out += ' ';
			out += QuoteArgument(a);
		}
		return out;
	}

	static bool IsExecutableFile(const std::string& path)
	{
		if (!fs::IsFile(path))
			return false;
#if defined(LUMEN_PLATFORM_WINDOWS)
		return true;
#else
		return access(path.c_str(), X_OK) == 0;
#endif
	}

	std::string FindExecutable(const std::string& name, const std::vector<std::string>& extraDirs)
	{
		if (name.empty())
			return {};

#if defined(LUMEN_PLATFORM_WINDOWS)
		std::vector<std::string> exts = {"", ".exe", ".cmd", ".bat", ".com"};
		if (auto pathext = platform::GetEnv("PATHEXT"))
		{
			for (const std::string& e : str::Split(str::ToLower(*pathext), ';', false))
			{
				if (std::find(exts.begin(), exts.end(), e) == exts.end())
					exts.push_back(e);
			}
		}
		const char listSep = ';';
#else
		const std::vector<std::string> exts = {""};
		const char listSep = ':';
#endif

		const auto tryDir = [&](const std::string& dir) -> std::string {
			for (const std::string& ext : exts)
			{
				const std::string candidate = dir.empty() ? name + ext : fs::Join(dir, name + ext);
				if (IsExecutableFile(candidate))
					return fs::Absolute(candidate);
			}
			return {};
		};

		if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
			return tryDir({});

		for (const std::string& dir : extraDirs)
		{
			const std::string r = tryDir(dir);
			if (!r.empty())
				return r;
		}
		if (auto path = platform::GetEnv("PATH"))
		{
			for (const std::string& dir : str::Split(*path, listSep, false))
			{
				const std::string r = tryDir(dir);
				if (!r.empty())
					return r;
			}
		}
		return {};
	}

	// ------------------------------------------------------------------------------------------------
	// POSIX implementation
	// ------------------------------------------------------------------------------------------------
#if !defined(LUMEN_PLATFORM_WINDOWS)

	static void SetCloexec(int fd)
	{
		const int flags = fcntl(fd, F_GETFD);
		if (flags >= 0)
			fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
	}

	ProcessResult RunProcess(const ProcessOptions& options)
	{
		ProcessResult result;

		const std::string exe = FindExecutable(options.program);
		if (exe.empty())
		{
			result.startError = "executable not found: " + options.program;
			return result;
		}

		// Environment for the child: current environment with overrides applied.
		std::vector<std::string> envStrings;
		for (char** e = environ; e && *e; ++e)
			envStrings.emplace_back(*e);
		for (const auto& kv : options.env)
		{
			const std::string prefix = kv.first + "=";
			bool replaced = false;
			for (std::string& s : envStrings)
			{
				if (str::StartsWith(s, prefix))
				{
					s = prefix + kv.second;
					replaced = true;
					break;
				}
			}
			if (!replaced)
				envStrings.push_back(prefix + kv.second);
		}
		std::vector<char*> envp;
		for (std::string& s : envStrings)
			envp.push_back(s.data());
		envp.push_back(nullptr);

		std::vector<std::string> argStrings;
		argStrings.push_back(options.program);
		for (const std::string& a : options.args)
			argStrings.push_back(a);
		std::vector<char*> argv;
		for (std::string& s : argStrings)
			argv.push_back(s.data());
		argv.push_back(nullptr);

		const std::string cwd = options.workingDir;
		const bool useStdin = !options.stdinText.empty();

		int outPipe[2] = {-1, -1};
		int errPipe[2] = {-1, -1};
		int inPipe[2] = {-1, -1};
		if (pipe(outPipe) != 0)
		{
			result.startError = "pipe() failed";
			return result;
		}
		if (!options.mergeStderr && pipe(errPipe) != 0)
		{
			close(outPipe[0]);
			close(outPipe[1]);
			result.startError = "pipe() failed";
			return result;
		}
		if (useStdin && pipe(inPipe) != 0)
		{
			close(outPipe[0]);
			close(outPipe[1]);
			if (errPipe[0] >= 0)
			{
				close(errPipe[0]);
				close(errPipe[1]);
			}
			result.startError = "pipe() failed";
			return result;
		}

		const pid_t pid = fork();
		if (pid < 0)
		{
			result.startError = "fork() failed";
			close(outPipe[0]);
			close(outPipe[1]);
			if (errPipe[0] >= 0)
			{
				close(errPipe[0]);
				close(errPipe[1]);
			}
			if (inPipe[0] >= 0)
			{
				close(inPipe[0]);
				close(inPipe[1]);
			}
			return result;
		}

		if (pid == 0)
		{
			// Child: only async-signal-safe calls from here to exec.
			setpgid(0, 0);
			if (useStdin)
			{
				dup2(inPipe[0], STDIN_FILENO);
			}
			else
			{
				const int devnull = open("/dev/null", O_RDONLY);
				if (devnull >= 0)
				{
					dup2(devnull, STDIN_FILENO);
					close(devnull);
				}
			}
			dup2(outPipe[1], STDOUT_FILENO);
			dup2(options.mergeStderr ? outPipe[1] : errPipe[1], STDERR_FILENO);
			close(outPipe[0]);
			close(outPipe[1]);
			if (errPipe[0] >= 0)
			{
				close(errPipe[0]);
				close(errPipe[1]);
			}
			if (inPipe[0] >= 0)
			{
				close(inPipe[0]);
				close(inPipe[1]);
			}
			if (!cwd.empty() && chdir(cwd.c_str()) != 0)
				_exit(126);
			execve(exe.c_str(), argv.data(), envp.data());
			_exit(127);
		}

		// Parent.
		setpgid(pid, pid); // avoid the race with the child's own setpgid
		close(outPipe[1]);
		if (errPipe[1] >= 0)
			close(errPipe[1]);
		if (inPipe[0] >= 0)
			close(inPipe[0]);
		SetCloexec(outPipe[0]);
		if (errPipe[0] >= 0)
			SetCloexec(errPipe[0]);
		result.started = true;

		if (useStdin)
		{
			size_t written = 0;
			while (written < options.stdinText.size())
			{
				const ssize_t n = write(inPipe[1], options.stdinText.data() + written, options.stdinText.size() - written);
				if (n < 0)
				{
					if (errno == EINTR)
						continue;
					break; // EPIPE etc: child closed stdin
				}
				written += (size_t)n;
			}
			close(inPipe[1]);
		}

		const uint64_t start = platform::NowMs();
		bool outOpen = true;
		bool errOpen = errPipe[0] >= 0;
		char buffer[8192];

		const auto killTree = [&]() {
			kill(-pid, SIGKILL);
			kill(pid, SIGKILL);
		};

		while (outOpen || errOpen)
		{
			if (options.cancel && options.cancel->load())
			{
				result.cancelled = true;
				killTree();
				break;
			}
			if (options.timeoutMs > 0 && platform::NowMs() - start > options.timeoutMs)
			{
				result.timedOut = true;
				killTree();
				break;
			}

			struct pollfd fds[2];
			int count = 0;
			int outIndex = -1, errIndex = -1;
			if (outOpen)
			{
				fds[count] = {outPipe[0], POLLIN, 0};
				outIndex = count++;
			}
			if (errOpen)
			{
				fds[count] = {errPipe[0], POLLIN, 0};
				errIndex = count++;
			}
			const int pr = poll(fds, (nfds_t)count, 50);
			if (pr < 0)
			{
				if (errno == EINTR)
					continue;
				break;
			}
			if (pr == 0)
				continue;

			const auto drain = [&](int index, int fd, bool isErr, bool& open) {
				if (index < 0 || !(fds[index].revents & (POLLIN | POLLHUP | POLLERR)))
					return;
				const ssize_t n = read(fd, buffer, sizeof(buffer));
				if (n > 0)
				{
					const std::string_view chunk(buffer, (size_t)n);
					if (options.captureOutput)
					{
						if (isErr)
							result.error.append(chunk);
						else
							result.output.append(chunk);
					}
					if (options.onOutput)
						options.onOutput(chunk, isErr);
				}
				else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN))
				{
					open = false;
				}
			};
			drain(outIndex, outPipe[0], false, outOpen);
			drain(errIndex, errPipe[0], true, errOpen);
		}

		close(outPipe[0]);
		if (errPipe[0] >= 0)
			close(errPipe[0]);

		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		{
		}
		if (WIFEXITED(status))
			result.exitCode = WEXITSTATUS(status);
		else if (WIFSIGNALED(status))
			result.exitCode = 128 + WTERMSIG(status);
		return result;
	}

	// ------------------------------------------------------------------------------------------------
	// Windows implementation
	// ------------------------------------------------------------------------------------------------
#else

	static std::wstring BuildCommandLine(const std::string& program, const std::vector<std::string>& args)
	{
		const auto quote = [](const std::string& arg) -> std::wstring {
			const std::wstring w = platform::Utf8ToWide(arg);
			if (!w.empty() && w.find_first_of(L" \t\n\v\"") == std::wstring::npos)
				return w;
			std::wstring out = L"\"";
			size_t backslashes = 0;
			for (const wchar_t c : w)
			{
				if (c == L'\\')
				{
					++backslashes;
					continue;
				}
				if (c == L'"')
				{
					out.append(backslashes * 2 + 1, L'\\');
					out += L'"';
				}
				else
				{
					out.append(backslashes, L'\\');
					out += c;
				}
				backslashes = 0;
			}
			out.append(backslashes * 2, L'\\');
			out += L'"';
			return out;
		};
		std::wstring line = quote(program);
		for (const std::string& a : args)
		{
			line += L' ';
			line += quote(a);
		}
		return line;
	}

	struct WinPipeReader
	{
		HANDLE handle = nullptr;
		bool isErr = false;
		std::string data;
		std::mutex* callbackMutex = nullptr;
		const ProcessOptions* options = nullptr;
		bool capture = true;
		std::thread thread;
	};

	static void PipeReadLoop(WinPipeReader* r)
	{
		char buffer[8192];
		DWORD got = 0;
		while (ReadFile(r->handle, buffer, sizeof(buffer), &got, nullptr) && got > 0)
		{
			const std::string_view chunk(buffer, got);
			if (r->capture)
				r->data.append(chunk);
			if (r->options->onOutput)
			{
				std::lock_guard<std::mutex> lock(*r->callbackMutex);
				r->options->onOutput(chunk, r->isErr);
			}
		}
	}

	ProcessResult RunProcess(const ProcessOptions& options)
	{
		ProcessResult result;
		const std::string exe = FindExecutable(options.program);
		if (exe.empty())
		{
			result.startError = "executable not found: " + options.program;
			return result;
		}

		SECURITY_ATTRIBUTES sa{};
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;

		HANDLE outRead = nullptr, outWrite = nullptr, errRead = nullptr, errWrite = nullptr, inRead = nullptr, inWrite = nullptr;
		if (!CreatePipe(&outRead, &outWrite, &sa, 0))
		{
			result.startError = "CreatePipe failed";
			return result;
		}
		SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
		if (!options.mergeStderr)
		{
			CreatePipe(&errRead, &errWrite, &sa, 0);
			SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
		}
		CreatePipe(&inRead, &inWrite, &sa, 0);
		SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdInput = inRead;
		si.hStdOutput = outWrite;
		si.hStdError = options.mergeStderr ? outWrite : errWrite;

		// Environment block
		std::wstring envBlock;
		std::vector<std::wstring> envEntries;
		if (!options.env.empty())
		{
			LPWCH current = GetEnvironmentStringsW();
			for (LPWCH p = current; p && *p; p += wcslen(p) + 1)
				envEntries.emplace_back(p);
			FreeEnvironmentStringsW(current);
			for (const auto& kv : options.env)
			{
				const std::wstring prefix = platform::Utf8ToWide(kv.first) + L"=";
				bool replaced = false;
				for (std::wstring& e : envEntries)
				{
					if (e.size() >= prefix.size() && _wcsnicmp(e.c_str(), prefix.c_str(), prefix.size()) == 0)
					{
						e = prefix + platform::Utf8ToWide(kv.second);
						replaced = true;
						break;
					}
				}
				if (!replaced)
					envEntries.push_back(prefix + platform::Utf8ToWide(kv.second));
			}
			std::sort(envEntries.begin(), envEntries.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
			for (const std::wstring& e : envEntries)
			{
				envBlock += e;
				envBlock += L'\0';
			}
			envBlock += L'\0';
		}

		std::wstring commandLine = BuildCommandLine(exe, options.args);
		const std::wstring wcwd = options.workingDir.empty() ? std::wstring() : platform::Utf8ToWide(fs::ToNative(options.workingDir));

		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		if (job)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
			info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
		}

		PROCESS_INFORMATION pi{};
		const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
			CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, options.env.empty() ? nullptr : envBlock.data(),
			wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);

		CloseHandle(outWrite);
		if (errWrite)
			CloseHandle(errWrite);
		CloseHandle(inRead);

		if (!ok)
		{
			result.startError = StrFormat("CreateProcess failed (%lu)", (unsigned long)GetLastError());
			CloseHandle(outRead);
			if (errRead)
				CloseHandle(errRead);
			CloseHandle(inWrite);
			if (job)
				CloseHandle(job);
			return result;
		}
		if (job)
			AssignProcessToJobObject(job, pi.hProcess);
		ResumeThread(pi.hThread);
		result.started = true;

		if (!options.stdinText.empty())
		{
			DWORD written = 0;
			WriteFile(inWrite, options.stdinText.data(), (DWORD)options.stdinText.size(), &written, nullptr);
		}
		CloseHandle(inWrite);

		std::mutex callbackMutex;
		WinPipeReader outReader;
		outReader.handle = outRead;
		outReader.isErr = false;
		outReader.callbackMutex = &callbackMutex;
		outReader.options = &options;
		outReader.capture = options.captureOutput;
		outReader.thread = std::thread(PipeReadLoop, &outReader);

		WinPipeReader errReader;
		if (errRead)
		{
			errReader.handle = errRead;
			errReader.isErr = true;
			errReader.callbackMutex = &callbackMutex;
			errReader.options = &options;
			errReader.capture = options.captureOutput;
			errReader.thread = std::thread(PipeReadLoop, &errReader);
		}

		const uint64_t start = platform::NowMs();
		while (true)
		{
			const DWORD wait = WaitForSingleObject(pi.hProcess, 50);
			if (wait == WAIT_OBJECT_0)
				break;
			const bool cancelled = options.cancel && options.cancel->load();
			const bool timedOut = options.timeoutMs > 0 && platform::NowMs() - start > options.timeoutMs;
			if (cancelled || timedOut)
			{
				result.cancelled = cancelled;
				result.timedOut = !cancelled && timedOut;
				if (job)
					TerminateJobObject(job, 1);
				else
					TerminateProcess(pi.hProcess, 1);
				WaitForSingleObject(pi.hProcess, 5000);
				break;
			}
		}

		DWORD exitCode = 0;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		result.exitCode = (int)exitCode;
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		if (job)
			CloseHandle(job); // kills stragglers that still hold the pipe, so readers terminate

		outReader.thread.join();
		CloseHandle(outRead);
		result.output = std::move(outReader.data);
		if (errRead)
		{
			errReader.thread.join();
			CloseHandle(errRead);
			result.error = std::move(errReader.data);
		}
		return result;
	}

#endif // platform

	// ------------------------------------------------------------------------------------------------
	// AsyncProcess
	// ------------------------------------------------------------------------------------------------
	std::shared_ptr<AsyncProcess> AsyncProcess::Start(ProcessOptions options, std::function<void(const ProcessResult&)> onFinished)
	{
		std::shared_ptr<AsyncProcess> p(new AsyncProcess());
		p->running_.store(true);
		AsyncProcess* raw = p.get();

		// The worker keeps the object alive through `p` until it finished.
		auto userCallback = options.onOutput;
		options.cancel = &raw->cancel_;
		options.onOutput = [raw, userCallback](std::string_view chunk, bool isErr) {
			{
				std::lock_guard<std::mutex> lock(raw->mutex_);
				raw->live_.append(chunk);
			}
			if (userCallback)
				userCallback(chunk, isErr);
		};
		p->thread_ = std::thread([p, options = std::move(options), onFinished = std::move(onFinished)]() mutable {
			ProcessResult r = RunProcess(options);
			{
				std::lock_guard<std::mutex> lock(p->mutex_);
				p->result_ = r;
			}
			p->running_.store(false);
			if (onFinished)
				onFinished(r);
		});
		return p;
	}

	AsyncProcess::~AsyncProcess()
	{
		cancel_.store(true);
		if (thread_.joinable())
		{
			if (thread_.get_id() == std::this_thread::get_id())
				thread_.detach(); // last reference dropped from inside the worker
			else
				thread_.join();
		}
	}

	void AsyncProcess::Cancel()
	{
		cancel_.store(true);
	}

	const ProcessResult& AsyncProcess::Wait()
	{
		if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
			thread_.join();
		return result_;
	}

	std::string AsyncProcess::OutputSoFar() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return live_;
	}

} // namespace lumen
