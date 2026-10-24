#include "Core/Log.h"
#include "Core/Assert.h"
#include "HAL/PlatformMisc.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace FP::Log
{
	namespace
	{
		struct Sink
		{
			SinkFn Fn = nullptr;
			void* UserData = nullptr;
		};

		constexpr int MaxSinks = 8;

		struct LogState
		{
			std::mutex Mutex;
			Sink Sinks[MaxSinks];
			LogLevel MinLevel = LogLevel::Verbose;
		};

		LogState& GetState()
		{
			static LogState state;
			return state;
		}

		const char* LevelPrefix(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Verbose: return "[verbose] ";
				case LogLevel::Info:    return "";
				case LogLevel::Warning: return "[warning] ";
				case LogLevel::Error:   return "[error] ";
			}
			return "";
		}
	}

	void WriteV(LogLevel level, const char* fmt, va_list args)
	{
		LogState& state = GetState();
		if (level < state.MinLevel)
			return;

		char buffer[2048];
		const int prefixLen = std::snprintf(buffer, sizeof(buffer), "%s", LevelPrefix(level));
		std::vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, fmt, args);

		std::lock_guard lock(state.Mutex);
		FILE* stream = level >= LogLevel::Warning ? stderr : stdout;
		std::fputs(buffer, stream);
		std::fputc('\n', stream);
		std::fflush(stream);
		PlatformMisc::DebugOutput(buffer);

		for (const Sink& sink : state.Sinks)
		{
			if (sink.Fn)
				sink.Fn(level, buffer, sink.UserData);
		}
	}

	void Write(LogLevel level, const char* fmt, ...)
	{
		va_list args;
		va_start(args, fmt);
		WriteV(level, fmt, args);
		va_end(args);
	}

	void AddSink(SinkFn fn, void* userData)
	{
		LogState& state = GetState();
		std::lock_guard lock(state.Mutex);
		for (Sink& sink : state.Sinks)
		{
			if (!sink.Fn)
			{
				sink = { fn, userData };
				return;
			}
		}
	}

	void RemoveSink(SinkFn fn, void* userData)
	{
		LogState& state = GetState();
		std::lock_guard lock(state.Mutex);
		for (Sink& sink : state.Sinks)
		{
			if (sink.Fn == fn && sink.UserData == userData)
				sink = {};
		}
	}

	void SetMinimumLevel(LogLevel level)
	{
		GetState().MinLevel = level;
	}
}

namespace FP::Detail
{
	bool ReportAssertFailure(const char* expr, const char* file, int line, const char* fmt, ...)
	{
		char message[1024] = {};
		if (fmt)
		{
			va_list args;
			va_start(args, fmt);
			std::vsnprintf(message, sizeof(message), fmt, args);
			va_end(args);
		}
		Log::Write(LogLevel::Error, "Assertion failed: %s %s\n  at %s:%d", expr, message, file, line);
		return PlatformMisc::IsDebuggerPresent();
	}

	void FatalError(const char* file, int line, const char* fmt, ...)
	{
		char message[1024];
		va_list args;
		va_start(args, fmt);
		std::vsnprintf(message, sizeof(message), fmt, args);
		va_end(args);
		Log::Write(LogLevel::Error, "Fatal error: %s\n  at %s:%d", message, file, line);
		if (PlatformMisc::IsDebuggerPresent())
			FP_DEBUGBREAK();
		std::abort();
	}
}
