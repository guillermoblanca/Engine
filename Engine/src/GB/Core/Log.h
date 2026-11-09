#pragma once

// The engine's log. The backend library is a private detail of Log.cpp: this header only exposes plain types and
// std::format, so GB does not publish the backend to its users (ARQUITECTURA_REVISION H10).
// Two channels: Core (the engine, prefix "GB") and Client (the applications, prefix "APP").
// Messages use std::format syntax ("{}", "{:.2f}"); the GB_CORE_* / GB_CLIENT_* macros below are the usual entry points.

#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

namespace GB
{
	class Log
	{
	public:
		enum class Channel : uint8_t { Core, Client };
		enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Fatal };   // ordered by severity

		// Called with every line written after Init(), already formatted as "[HH:MM:SS] message" (no line break).
		// Sinks run under the log's lock: a sink must not log.
		using Sink = std::function<void(Channel, Level, std::string_view)>;

		static void Init();
		static bool ShouldLog(Channel channel, Level level);
		static void Write(Channel channel, Level level, std::string_view message);   // the text is written as is (no formatting)

		template<class... Args>
		static void Format(Channel channel, Level level, std::format_string<Args...> format, Args&&... args)
		{
			if (ShouldLog(channel, level))
				Write(channel, level, std::format(format, std::forward<Args>(args)...));
		}

		static int AddSink(Sink sink);   // returns an id for RemoveSink
		static void RemoveSink(int id);
	};
}

// Core log macros
#define GB_CORE_TRACE(...)   ::GB::Log::Format(::GB::Log::Channel::Core, ::GB::Log::Level::Trace, __VA_ARGS__)
#define GB_CORE_INFO(...)    ::GB::Log::Format(::GB::Log::Channel::Core, ::GB::Log::Level::Info, __VA_ARGS__)
#define GB_CORE_WARN(...)    ::GB::Log::Format(::GB::Log::Channel::Core, ::GB::Log::Level::Warn, __VA_ARGS__)
#define GB_CORE_ERROR(...)   ::GB::Log::Format(::GB::Log::Channel::Core, ::GB::Log::Level::Error, __VA_ARGS__)
#define GB_CORE_FATAL(...)   ::GB::Log::Format(::GB::Log::Channel::Core, ::GB::Log::Level::Fatal, __VA_ARGS__)
// Client log macros
#define GB_CLIENT_TRACE(...) ::GB::Log::Format(::GB::Log::Channel::Client, ::GB::Log::Level::Trace, __VA_ARGS__)
#define GB_CLIENT_INFO(...)  ::GB::Log::Format(::GB::Log::Channel::Client, ::GB::Log::Level::Info, __VA_ARGS__)
#define GB_CLIENT_WARN(...)  ::GB::Log::Format(::GB::Log::Channel::Client, ::GB::Log::Level::Warn, __VA_ARGS__)
#define GB_CLIENT_ERROR(...) ::GB::Log::Format(::GB::Log::Channel::Client, ::GB::Log::Level::Error, __VA_ARGS__)
#define GB_CLIENT_FATAL(...) ::GB::Log::Format(::GB::Log::Channel::Client, ::GB::Log::Level::Fatal, __VA_ARGS__)
