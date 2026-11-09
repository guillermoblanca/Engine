#include "GB/Core/Log.h"

// The only file of GB that sees spdlog (ARQUITECTURA_REVISION H10).
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>
#include <string>
#include <vector>

namespace GB
{
	namespace
	{
		std::shared_ptr<spdlog::logger> s_CoreLogger;
		std::shared_ptr<spdlog::logger> s_ClientLogger;

		spdlog::level::level_enum ToSpdlog(Log::Level level)
		{
			switch (level)
			{
			case Log::Level::Trace: return spdlog::level::trace;
			case Log::Level::Debug: return spdlog::level::debug;
			case Log::Level::Info:  return spdlog::level::info;
			case Log::Level::Warn:  return spdlog::level::warn;
			case Log::Level::Error: return spdlog::level::err;
			case Log::Level::Fatal: return spdlog::level::critical;
			}
			return spdlog::level::info;
		}

		Log::Level FromSpdlog(spdlog::level::level_enum level)
		{
			switch (level)
			{
			case spdlog::level::trace:    return Log::Level::Trace;
			case spdlog::level::debug:    return Log::Level::Debug;
			case spdlog::level::warn:     return Log::Level::Warn;
			case spdlog::level::err:      return Log::Level::Error;
			case spdlog::level::critical: return Log::Level::Fatal;
			default:                      return Log::Level::Info;
			}
		}

		// Forwards every line to the sinks added with Log::AddSink (the editor's console), formatted with its own pattern.
		class CallbackSink : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			int Add(Log::Sink sink)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				m_Sinks.push_back({ ++m_NextId, std::move(sink) });
				return m_NextId;
			}

			void Remove(int id)
			{
				std::lock_guard<std::mutex> lock(mutex_);
				for (size_t i = 0; i < m_Sinks.size(); ++i)
					if (m_Sinks[i].Id == id) { m_Sinks.erase(m_Sinks.begin() + static_cast<std::ptrdiff_t>(i)); return; }
			}

		protected:
			void sink_it_(const spdlog::details::log_msg& msg) override
			{
				if (m_Sinks.empty())
					return;
				spdlog::memory_buf_t formatted;
				formatter_->format(msg, formatted);
				std::string_view text(formatted.data(), formatted.size());
				while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
				const Log::Channel channel = std::string_view(msg.logger_name.data(), msg.logger_name.size()) == "APP" ? Log::Channel::Client : Log::Channel::Core;
				for (const Entry& e : m_Sinks)
					e.Callback(channel, FromSpdlog(msg.level), text);
			}
			void flush_() override {}

		private:
			struct Entry { int Id = 0; Log::Sink Callback; };
			std::vector<Entry> m_Sinks;
			int m_NextId = 0;
		};

		std::shared_ptr<CallbackSink> s_Callbacks;

		spdlog::logger* LoggerOf(Log::Channel channel)
		{
			return channel == Log::Channel::Client ? s_ClientLogger.get() : s_CoreLogger.get();
		}
	}

	void Log::Init()
	{
		if (s_CoreLogger)
			return;

		spdlog::set_pattern("%^[%T] %n: %v%$");

		s_CoreLogger = spdlog::stdout_color_mt("GB");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_ClientLogger->set_level(spdlog::level::trace);

		// The sinks of Log::AddSink (the editor's console: "[HH:MM:SS] message"). Its pattern goes after the global one,
		// which would overwrite it.
		s_Callbacks = std::make_shared<CallbackSink>();
		s_Callbacks->set_pattern("[%H:%M:%S] %v");
		s_CoreLogger->sinks().push_back(s_Callbacks);
		s_ClientLogger->sinks().push_back(s_Callbacks);
	}

	bool Log::ShouldLog(Channel channel, Level level)
	{
		const spdlog::logger* logger = LoggerOf(channel);
		return logger && logger->should_log(ToSpdlog(level));
	}

	void Log::Write(Channel channel, Level level, std::string_view message)
	{
		// before Init() there is no logger: the line is dropped
		if (spdlog::logger* logger = LoggerOf(channel))
			logger->log(ToSpdlog(level), "{}", message);
	}

	int Log::AddSink(Sink sink)
	{
		return s_Callbacks ? s_Callbacks->Add(std::move(sink)) : 0;
	}

	void Log::RemoveSink(int id)
	{
		if (s_Callbacks)
			s_Callbacks->Remove(id);
	}
}
