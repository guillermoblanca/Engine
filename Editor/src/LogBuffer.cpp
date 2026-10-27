#include "LogBuffer.h"
#include <vector>

namespace GB::Editor
{
	void LogBuffer::sink_it_(const spdlog::details::log_msg& msg)
	{
		spdlog::memory_buf_t formatted;
		formatter_->format(msg, formatted);
		std::string text(formatted.data(), formatted.size());
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
		m_Lines.push_back({ msg.level, std::move(text) });
		if (m_Lines.size() > kMax) m_Lines.pop_front();
		++m_Version;
	}

	std::vector<LogLine> LogBuffer::Snapshot()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return std::vector<LogLine>(m_Lines.begin(), m_Lines.end());
	}

	void LogBuffer::Clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		m_Lines.clear();
		++m_Version;
	}

	void LogBuffer::Add(spdlog::level::level_enum level, const std::string& text)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		m_Lines.push_back({ level, text });
		if (m_Lines.size() > kMax) m_Lines.pop_front();
		++m_Version;
	}
}
