#include "LogBuffer.h"
#include <vector>

namespace GB::Editor
{
	void LogBuffer::Sink(GB::Log::Level level, std::string_view text)
	{
		Add(level, std::string(text));
	}

	std::vector<LogLine> LogBuffer::Snapshot()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return std::vector<LogLine>(m_Lines.begin(), m_Lines.end());
	}

	void LogBuffer::Clear()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Lines.clear();
		++m_Version;
	}

	void LogBuffer::Add(GB::Log::Level level, const std::string& text)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Lines.push_back({ level, text });
		if (m_Lines.size() > kMax) m_Lines.pop_front();
		++m_Version;
	}
}
