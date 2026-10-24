#include "Core/Log.h"
#include "Core/Assert.h"
#include "HAL/PlatformMisc.h"
#include "Core/Misc/ModuleContext.h"

// Our names for spdlog's levels. Only this file includes spdlog, so they cannot disagree.
#define SPDLOG_LEVEL_NAMES { "trace", "verbose", "info", "warning", "error", "critical", "off" }
#include <spdlog/spdlog.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if FP_PLATFORM_WINDOWS
	#include <spdlog/sinks/msvc_sink.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

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

		spdlog::level::level_enum ToSpdlog(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Verbose: return spdlog::level::debug;
				case LogLevel::Info:    return spdlog::level::info;
				case LogLevel::Warning: return spdlog::level::warn;
				case LogLevel::Error:   return spdlog::level::err;
			}
			return spdlog::level::info;
		}

		LogLevel FromSpdlog(spdlog::level::level_enum level)
		{
			if (level >= spdlog::level::err) return LogLevel::Error;
			if (level == spdlog::level::warn) return LogLevel::Warning;
			if (level == spdlog::level::info) return LogLevel::Info;
			return LogLevel::Verbose;
		}

		// Passes on only lines up to a level, so info goes to stdout and warnings to stderr.
		class UpToLevelSink : public spdlog::sinks::sink
		{
		public:
			UpToLevelSink(std::shared_ptr<spdlog::sinks::sink> inner, spdlog::level::level_enum highest)
				: m_Inner(std::move(inner)), m_Highest(highest) {}

			void log(const spdlog::details::log_msg& message) override
			{
				if (message.level <= m_Highest)
					m_Inner->log(message);
			}
			void flush() override { m_Inner->flush(); }
			void set_pattern(const std::string& pattern) override { m_Inner->set_pattern(pattern); }
			void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override { m_Inner->set_formatter(std::move(formatter)); }

		private:
			std::shared_ptr<spdlog::sinks::sink> m_Inner;
			spdlog::level::level_enum m_Highest;
		};

		// %* in a pattern: "[warning] " and the like for every level but info, which reads plainer bare.
		class LevelTagFlag : public spdlog::custom_flag_formatter
		{
		public:
			void format(const spdlog::details::log_msg& message, const std::tm&, spdlog::memory_buf_t& out) override
			{
				if (message.level == spdlog::level::info)
					return;
				const spdlog::string_view_t name = spdlog::level::to_string_view(message.level);
				out.push_back('[');
				out.append(name.data(), name.data() + name.size());
				out.push_back(']');
				out.push_back(' ');
			}

			std::unique_ptr<spdlog::custom_flag_formatter> clone() const override { return std::make_unique<LevelTagFlag>(); }
		};

		std::unique_ptr<spdlog::formatter> MakeFormatter(const char* pattern)
		{
			auto formatter = std::make_unique<spdlog::pattern_formatter>();
			formatter->add_flag<LevelTagFlag>('*').set_pattern(pattern);
			return formatter;
		}

		struct LogState;

		// Hands formatted lines to the sinks added with AddSink.
		class ForwardSink : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit ForwardSink(LogState& state) : m_State(state) {}

		protected:
			void sink_it_(const spdlog::details::log_msg& message) override;
			void flush_() override {}

		private:
			LogState& m_State;
		};

		struct LogState
		{
			std::mutex Mutex;
			Sink Sinks[MaxSinks];
			std::shared_ptr<spdlog::sinks::dist_sink_mt> Outputs;
			std::shared_ptr<spdlog::logger> Logger;

			LogState()
			{
				Outputs = std::make_shared<spdlog::sinks::dist_sink_mt>();
				auto out = std::make_shared<UpToLevelSink>(std::make_shared<spdlog::sinks::stdout_color_sink_mt>(), spdlog::level::info);
				auto err = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
				err->set_level(spdlog::level::warn);
				out->set_formatter(MakeFormatter("%^%*%$%v"));
				err->set_formatter(MakeFormatter("%^%*%$%v"));
				Outputs->add_sink(out);
				Outputs->add_sink(err);
#if FP_PLATFORM_WINDOWS
				// Shows up in the Visual Studio Output window.
				auto debugger = std::make_shared<spdlog::sinks::msvc_sink_mt>(false);
				debugger->set_formatter(MakeFormatter("%*%v\n"));
				Outputs->add_sink(debugger);
#endif
				auto forward = std::make_shared<ForwardSink>(*this);
				forward->set_formatter(MakeFormatter("[%H:%M:%S] %*%v"));
				Outputs->add_sink(forward);

				Logger = std::make_shared<spdlog::logger>("FirstPrinciples", Outputs);
				Logger->set_level(spdlog::level::debug);
				Logger->flush_on(spdlog::level::info);
			}
		};

		void ForwardSink::sink_it_(const spdlog::details::log_msg& message)
		{
			spdlog::memory_buf_t formatted;
			formatter_->format(message, formatted);
			std::string text(formatted.data(), formatted.size());
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				text.pop_back();
			const LogLevel level = FromSpdlog(message.level);
			std::lock_guard lock(m_State.Mutex);
			for (const Sink& sink : m_State.Sinks)
			{
				if (sink.Fn)
					sink.Fn(level, text.c_str(), sink.UserData);
			}
		}

		LogState& GetState()
		{
			static LogState state;
			return ResolveShared(&ModuleContext::Log, state);
		}
	}

	void WriteV(LogLevel level, const char* fmt, va_list args)
	{
		LogState& state = GetState();
		const spdlog::level::level_enum spdLevel = ToSpdlog(level);
		if (!state.Logger->should_log(spdLevel))
			return;

		// Most lines fit the stack buffer. Longer ones are formatted again at full size.
		char buffer[1024];
		va_list copy;
		va_copy(copy, args);
		const int length = std::vsnprintf(buffer, sizeof(buffer), fmt, args);
		if (length < 0)
		{
			va_end(copy);
			return;
		}
		if (length < static_cast<int>(sizeof(buffer)))
		{
			state.Logger->log(spdLevel, spdlog::string_view_t(buffer, static_cast<usize>(length)));
		}
		else
		{
			std::string text(static_cast<usize>(length) + 1, '\0');
			std::vsnprintf(text.data(), text.size(), fmt, copy);
			state.Logger->log(spdLevel, spdlog::string_view_t(text.data(), static_cast<usize>(length)));
		}
		va_end(copy);
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
		GetState().Logger->set_level(ToSpdlog(level));
	}

	bool OpenFile(const char* path)
	{
		try
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
			auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(path, 5 * 1024 * 1024, 3, true);
			file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] %v");
			GetState().Outputs->add_sink(file);
		}
		catch (const spdlog::spdlog_ex& error)
		{
			Write(LogLevel::Error, "Could not open log file %s: %s", path, error.what());
			return false;
		}
		Write(LogLevel::Verbose, "Logging to %s", path);
		return true;
	}

	void Flush()
	{
		GetState().Logger->flush();
	}

	void* GetSharedState()
	{
		return &GetState();
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
		Log::Flush();
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
		Log::Flush();
		if (PlatformMisc::IsDebuggerPresent())
			FP_DEBUGBREAK();
		std::abort();
	}
}
