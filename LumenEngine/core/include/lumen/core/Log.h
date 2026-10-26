// Lumen Engine — thread safe logging with pluggable sinks (console, rotating file, ring buffer for the editor).
#pragma once

#include "lumen/core/Common.h"

#include <atomic>
#include <mutex>

namespace lumen
{
	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Debug,
		Info,
		Warn,
		Error,
		Fatal
	};

	const char* LogLevelName(LogLevel level);

	struct LogRecord
	{
		uint64_t sequence = 0;
		uint64_t unixMs = 0;
		LogLevel level = LogLevel::Info;
		std::string channel;
		std::string message;
	};

	class LogSink
	{
	public:
		virtual ~LogSink() = default;
		virtual void Write(const LogRecord& record) = 0;
		virtual void Flush() {}
	};

	// Writes to stdout/stderr, optionally with ANSI colours.
	class ConsoleLogSink : public LogSink
	{
	public:
		explicit ConsoleLogSink(bool color = true);
		void Write(const LogRecord& record) override;
		void Flush() override;

	private:
		bool color_;
	};

	// Appends to a file and rotates it ("name.log" -> "name.1.log" -> ...) when it grows beyond maxBytes.
	class FileLogSink : public LogSink
	{
	public:
		FileLogSink(std::string path, uint64_t maxBytes = 4ull << 20, int keepFiles = 5);
		~FileLogSink() override;
		void Write(const LogRecord& record) override;
		void Flush() override;
		const std::string& Path() const { return path_; }

	private:
		void Rotate();
		std::string path_;
		uint64_t maxBytes_;
		int keepFiles_;
		uint64_t written_ = 0;
		void* file_ = nullptr; // FILE*
		std::mutex mutex_;
	};

	// Keeps the last N records in memory; the editor console polls it with Since(lastSeenSequence).
	class RingLogSink : public LogSink
	{
	public:
		explicit RingLogSink(size_t capacity = 8192) : capacity_(capacity) {}
		void Write(const LogRecord& record) override;
		std::vector<LogRecord> Snapshot() const;
		// Records with sequence > `sequence`, oldest first.
		std::vector<LogRecord> Since(uint64_t sequence) const;
		uint64_t LastSequence() const;
		void Clear();

	private:
		size_t capacity_;
		mutable std::mutex mutex_;
		std::vector<LogRecord> records_;
		size_t head_ = 0; // index of the oldest record once the buffer is full
	};

	class CallbackLogSink : public LogSink
	{
	public:
		explicit CallbackLogSink(std::function<void(const LogRecord&)> fn) : fn_(std::move(fn)) {}
		void Write(const LogRecord& record) override
		{
			if (fn_)
				fn_(record);
		}

	private:
		std::function<void(const LogRecord&)> fn_;
	};

	class Log
	{
	public:
		static void SetMinLevel(LogLevel level);
		static LogLevel MinLevel();

		static void Write(LogLevel level, std::string_view channel, std::string_view message);
		static void Writef(LogLevel level, const char* channel, const char* fmt, ...) LUMEN_PRINTF_LIKE(3, 4);

		// Returns an id usable with RemoveSink().
		static int AddSink(std::shared_ptr<LogSink> sink);
		static void RemoveSink(int id);
		static void ClearSinks();
		static void Flush();

		// Convenience installers.
		static int InstallConsoleSink(bool color = true);
		static std::shared_ptr<FileLogSink> InstallFileSink(const std::string& path, uint64_t maxBytes = 4ull << 20, int keepFiles = 5);
		static std::shared_ptr<RingLogSink> InstallRingSink(size_t capacity = 8192);
	};

} // namespace lumen

#define LUMEN_TRACE(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Trace, channel, __VA_ARGS__)
#define LUMEN_DEBUG(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Debug, channel, __VA_ARGS__)
#define LUMEN_INFO(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Info, channel, __VA_ARGS__)
#define LUMEN_WARN(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Warn, channel, __VA_ARGS__)
#define LUMEN_ERROR(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Error, channel, __VA_ARGS__)
#define LUMEN_FATAL(channel, ...) ::lumen::Log::Writef(::lumen::LogLevel::Fatal, channel, __VA_ARGS__)
