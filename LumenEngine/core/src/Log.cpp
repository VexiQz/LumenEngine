#include "lumen/core/Log.h"

#include "lumen/core/Fs.h"
#include "lumen/core/Platform.h"
#include "lumen/core/Strings.h"

#include <cstdarg>
#include <cstdio>

#if defined(LUMEN_PLATFORM_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace lumen
{
	const char* LogLevelName(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Trace: return "TRACE";
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info: return "INFO";
		case LogLevel::Warn: return "WARN";
		case LogLevel::Error: return "ERROR";
		case LogLevel::Fatal: return "FATAL";
		}
		return "?";
	}

	// ------------------------------------------------------------------------------------------------
	// ConsoleLogSink
	// ------------------------------------------------------------------------------------------------
	static bool StreamIsTerminal(FILE* f)
	{
#if defined(LUMEN_PLATFORM_WINDOWS)
		DWORD mode = 0;
		return GetConsoleMode((HANDLE)_get_osfhandle(_fileno(f)), &mode) != 0;
#else
		return isatty(fileno(f)) != 0;
#endif
	}

	ConsoleLogSink::ConsoleLogSink(bool color) : color_(color)
	{
		if (color_ && !StreamIsTerminal(stderr))
			color_ = false;
#if defined(LUMEN_PLATFORM_WINDOWS)
		if (color_)
		{
			// Enable ANSI escape sequence processing in the Windows console.
			HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
			DWORD mode = 0;
			if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
			{
				if (!SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/))
					color_ = false;
			}
		}
#endif
	}

	void ConsoleLogSink::Write(const LogRecord& r)
	{
		const char* start = "";
		const char* end = "";
		if (color_)
		{
			end = "\x1b[0m";
			switch (r.level)
			{
			case LogLevel::Trace: start = "\x1b[90m"; break;
			case LogLevel::Debug: start = "\x1b[36m"; break;
			case LogLevel::Info: start = "\x1b[0m"; break;
			case LogLevel::Warn: start = "\x1b[33m"; break;
			case LogLevel::Error: start = "\x1b[31m"; break;
			case LogLevel::Fatal: start = "\x1b[1;41;97m"; break;
			}
		}
		FILE* out = r.level >= LogLevel::Warn ? stderr : stdout;
		std::fprintf(out, "%s[%s][%s] %s%s\n", start, LogLevelName(r.level), r.channel.c_str(), r.message.c_str(), end);
	}

	void ConsoleLogSink::Flush()
	{
		std::fflush(stdout);
		std::fflush(stderr);
	}

	// ------------------------------------------------------------------------------------------------
	// FileLogSink
	// ------------------------------------------------------------------------------------------------
	static FILE* OpenForAppend(const std::string& path)
	{
		fs::CreateDirs(fs::Parent(path));
#if defined(LUMEN_PLATFORM_WINDOWS)
		return _wfopen(platform::Utf8ToWide(fs::ToNative(path)).c_str(), L"ab");
#else
		return std::fopen(path.c_str(), "ab");
#endif
	}

	FileLogSink::FileLogSink(std::string path, uint64_t maxBytes, int keepFiles)
		: path_(std::move(path)), maxBytes_(maxBytes), keepFiles_(keepFiles < 1 ? 1 : keepFiles)
	{
		file_ = OpenForAppend(path_);
		written_ = fs::FileSize(path_);
	}

	FileLogSink::~FileLogSink()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (file_)
			std::fclose((FILE*)file_);
		file_ = nullptr;
	}

	void FileLogSink::Rotate()
	{
		if (file_)
			std::fclose((FILE*)file_);
		file_ = nullptr;
		const std::string stem = fs::ReplaceExtension(path_, "");
		const std::string ext = fs::Extension(path_);
		fs::Remove(StrFormat("%s.%d%s", stem.c_str(), keepFiles_, ext.c_str()));
		for (int i = keepFiles_ - 1; i >= 1; --i)
			fs::Rename(StrFormat("%s.%d%s", stem.c_str(), i, ext.c_str()), StrFormat("%s.%d%s", stem.c_str(), i + 1, ext.c_str()));
		fs::Rename(path_, StrFormat("%s.%d%s", stem.c_str(), 1, ext.c_str()));
		file_ = OpenForAppend(path_);
		written_ = 0;
	}

	void FileLogSink::Write(const LogRecord& r)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!file_)
			return;
		const std::string ts = platform::UtcTimestamp(r.unixMs);
		const int n = std::fprintf((FILE*)file_, "%s [%s][%s] %s\n", ts.c_str(), LogLevelName(r.level), r.channel.c_str(), r.message.c_str());
		if (n > 0)
			written_ += (uint64_t)n;
		std::fflush((FILE*)file_);
		if (written_ >= maxBytes_)
			Rotate();
	}

	void FileLogSink::Flush()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (file_)
			std::fflush((FILE*)file_);
	}

	// ------------------------------------------------------------------------------------------------
	// RingLogSink
	// ------------------------------------------------------------------------------------------------
	void RingLogSink::Write(const LogRecord& record)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (records_.size() < capacity_)
		{
			records_.push_back(record);
		}
		else if (capacity_ > 0)
		{
			records_[head_] = record;
			head_ = (head_ + 1) % capacity_;
		}
	}

	std::vector<LogRecord> RingLogSink::Snapshot() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<LogRecord> out;
		out.reserve(records_.size());
		for (size_t i = 0; i < records_.size(); ++i)
			out.push_back(records_[(head_ + i) % records_.size()]);
		return out;
	}

	std::vector<LogRecord> RingLogSink::Since(uint64_t sequence) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<LogRecord> out;
		for (size_t i = 0; i < records_.size(); ++i)
		{
			const LogRecord& r = records_[(head_ + i) % records_.size()];
			if (r.sequence > sequence)
				out.push_back(r);
		}
		return out;
	}

	uint64_t RingLogSink::LastSequence() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (records_.empty())
			return 0;
		const size_t last = (head_ + records_.size() - 1) % records_.size();
		return records_[last].sequence;
	}

	void RingLogSink::Clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		records_.clear();
		head_ = 0;
	}

	// ------------------------------------------------------------------------------------------------
	// Log
	// ------------------------------------------------------------------------------------------------
	namespace
	{
		struct SinkEntry
		{
			int id;
			std::shared_ptr<LogSink> sink;
		};

		std::mutex g_mutex;
		std::vector<SinkEntry> g_sinks;
		std::atomic<int> g_nextSinkId{1};
		std::atomic<uint64_t> g_sequence{0};
		std::atomic<int> g_minLevel{(int)LogLevel::Info};
	} // namespace

	void Log::SetMinLevel(LogLevel level)
	{
		g_minLevel.store((int)level);
	}

	LogLevel Log::MinLevel()
	{
		return (LogLevel)g_minLevel.load();
	}

	void Log::Write(LogLevel level, std::string_view channel, std::string_view message)
	{
		if ((int)level < g_minLevel.load())
			return;
		LogRecord record;
		record.sequence = g_sequence.fetch_add(1) + 1;
		record.unixMs = platform::UnixTimeMs();
		record.level = level;
		record.channel.assign(channel);
		record.message.assign(message);

		std::vector<std::shared_ptr<LogSink>> sinks;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			sinks.reserve(g_sinks.size());
			for (const SinkEntry& e : g_sinks)
				sinks.push_back(e.sink);
		}
		for (const auto& sink : sinks)
			sink->Write(record);
	}

	void Log::Writef(LogLevel level, const char* channel, const char* fmt, ...)
	{
		if ((int)level < g_minLevel.load())
			return;
		va_list args;
		va_start(args, fmt);
		va_list copy;
		va_copy(copy, args);
		const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
		va_end(copy);
		std::string message;
		if (needed > 0)
		{
			message.resize((size_t)needed + 1);
			std::vsnprintf(message.data(), message.size(), fmt, args);
			message.resize((size_t)needed);
		}
		va_end(args);
		Write(level, channel ? channel : "", message);
	}

	int Log::AddSink(std::shared_ptr<LogSink> sink)
	{
		if (!sink)
			return 0;
		std::lock_guard<std::mutex> lock(g_mutex);
		const int id = g_nextSinkId.fetch_add(1);
		g_sinks.push_back({id, std::move(sink)});
		return id;
	}

	void Log::RemoveSink(int id)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (size_t i = 0; i < g_sinks.size(); ++i)
		{
			if (g_sinks[i].id == id)
			{
				g_sinks.erase(g_sinks.begin() + (ptrdiff_t)i);
				return;
			}
		}
	}

	void Log::ClearSinks()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_sinks.clear();
	}

	void Log::Flush()
	{
		std::vector<std::shared_ptr<LogSink>> sinks;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			for (const SinkEntry& e : g_sinks)
				sinks.push_back(e.sink);
		}
		for (const auto& sink : sinks)
			sink->Flush();
	}

	int Log::InstallConsoleSink(bool color)
	{
		return AddSink(std::make_shared<ConsoleLogSink>(color));
	}

	std::shared_ptr<FileLogSink> Log::InstallFileSink(const std::string& path, uint64_t maxBytes, int keepFiles)
	{
		auto sink = std::make_shared<FileLogSink>(path, maxBytes, keepFiles);
		AddSink(sink);
		return sink;
	}

	std::shared_ptr<RingLogSink> Log::InstallRingSink(size_t capacity)
	{
		auto sink = std::make_shared<RingLogSink>(capacity);
		AddSink(sink);
		return sink;
	}

} // namespace lumen
