#pragma once
#include <deque>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <string>

namespace GB::Editor
{
	struct LogLine
	{
		spdlog::level::level_enum Level = spdlog::level::info;
		std::string Text;
	};

	// Keeps the last log lines for the console panel (an spdlog sink).
	class LogBuffer : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		std::vector<LogLine> Snapshot();
		size_t Version() const { return m_Version; }
		void Clear();
		void Add(spdlog::level::level_enum level, const std::string& text);   // lines of the editor itself (command results)

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override;
		void flush_() override {}

	private:
		std::deque<LogLine> m_Lines;
		size_t m_Version = 0;
		static constexpr size_t kMax = 1000;
	};
}
