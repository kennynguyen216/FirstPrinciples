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

	namespace Log
	{
		using SinkFn = void (*)(LogLevel level, const char* message, void* userData);

		void Write(LogLevel level, const char* fmt, ...);
		void WriteV(LogLevel level, const char* fmt, va_list args);

		// Extra outputs (e.g. the editor's console panel). Stdout is always written.
		void AddSink(SinkFn sink, void* userData);
		void RemoveSink(SinkFn sink, void* userData);

		void SetMinimumLevel(LogLevel level);

		// For ModuleContext so loaded modules log through the host.
		void* GetSharedState();
	}
}

#define FP_LOG(...)     ::FP::Log::Write(::FP::LogLevel::Info, __VA_ARGS__)
#define FP_LOG_VERBOSE(...) ::FP::Log::Write(::FP::LogLevel::Verbose, __VA_ARGS__)
#define FP_LOG_WARN(...)  ::FP::Log::Write(::FP::LogLevel::Warning, __VA_ARGS__)
#define FP_LOG_ERROR(...) ::FP::Log::Write(::FP::LogLevel::Error, __VA_ARGS__)
