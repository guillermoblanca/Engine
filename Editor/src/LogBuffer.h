#pragma once
#include "GB/Core/Log.h"
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace GB::Editor
{
	struct LogLine
	{
		GB::Log::Level Level = GB::Log::Level::Info;
		std::string Text;
	};

	// Keeps the last log lines for the console panel. EditorApp feeds it from a GB::Log sink (Sink(), lines already
	// formatted as "[HH:MM:SS] message") and with the editor's own lines (Add, command results).
	class LogBuffer
	{
	public:
		std::vector<LogLine> Snapshot();
		size_t Version() const { return m_Version; }
		void Clear();
		void Add(GB::Log::Level level, const std::string& text);   // lines of the editor itself (command results)
		void Sink(GB::Log::Level level, std::string_view text);     // a line of GB::Log

	private:
		std::mutex m_Mutex;
		std::deque<LogLine> m_Lines;
		size_t m_Version = 0;
		static constexpr size_t kMax = 1000;
	};
}
