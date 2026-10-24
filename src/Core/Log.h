#pragma once

#include "HAL/Platform.h"

#include <cstdarg>

namespace FP
{
	enum class LogLevel : uint8
	{
		Verbose,
		Info,
		Warning,
		Error,
	};

	// Logging backed by spdlog. Lines go to the console (warnings and errors to stderr), to a log file
	// once OpenFile is called, to the debugger output on Windows and to any sinks added here.
	namespace Log
	{
		using SinkFn = void (*)(LogLevel level, const char* message, void* userData);

		void Write(LogLevel level, const char* fmt, ...);
		void WriteV(LogLevel level, const char* fmt, va_list args);

		// Extra outputs (e.g. the editor's console panel). message comes with a timestamp and no newline.
		// Sinks are called from whichever thread logged.
		void AddSink(SinkFn sink, void* userData);
		void RemoveSink(SinkFn sink, void* userData);

		void SetMinimumLevel(LogLevel level);

		// Also writes every line to path, with the date and thread. The file starts fresh each run and
		// the last three runs are kept next to it as name.1.log, name.2.log and name.3.log.
		bool OpenFile(const char* path);

		// Writes out anything buffered. Only verbose lines ever wait in the buffer.
		void Flush();

		// For ModuleContext so loaded modules log through the host.
		void* GetSharedState();
	}
}

#define FP_LOG(...)     ::FP::Log::Write(::FP::LogLevel::Info, __VA_ARGS__)
#define FP_LOG_VERBOSE(...) ::FP::Log::Write(::FP::LogLevel::Verbose, __VA_ARGS__)
#define FP_LOG_WARN(...)  ::FP::Log::Write(::FP::LogLevel::Warning, __VA_ARGS__)
#define FP_LOG_ERROR(...) ::FP::Log::Write(::FP::LogLevel::Error, __VA_ARGS__)
