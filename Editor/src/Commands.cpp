#include "Commands.h"
#include <algorithm>
#include <cctype>

namespace GB::Editor
{
	void CommandRunner::Register(const std::string& name, const std::string& help, Handler handler)
	{
		m_Commands[name] = { help, std::move(handler) };
	}

	std::vector<std::string> CommandRunner::Tokenize(const std::string& line)
	{
		std::vector<std::string> out;
		std::string cur;
		bool quoted = false, any = false;
		for (char c : line)
		{
			if (c == '"') { quoted = !quoted; any = true; continue; }
			if (!quoted && c == '#') break;
			if (!quoted && std::isspace(static_cast<unsigned char>(c)))
			{
				if (any) { out.push_back(cur); cur.clear(); any = false; }
				continue;
			}
			cur += c;
			any = true;
		}
		if (any) out.push_back(cur);
		return out;
	}

	std::vector<std::string> CommandRunner::SplitScript(const std::string& text)
	{
		std::vector<std::string> lines;
		std::string cur;
		bool quoted = false;
		auto flush = [&]()
		{
			if (!Tokenize(cur).empty()) lines.push_back(cur);
			cur.clear();
		};
		bool comment = false;
		for (char c : text)
		{
			if (c == '"') quoted = !quoted;
			if (c == '\n' || c == '\r') { quoted = false; comment = false; flush(); continue; }
			if (!quoted && c == '#') comment = true;
			if (!quoted && !comment && c == ';') { flush(); continue; }
			cur += c;
		}
		flush();
		return lines;
	}

	CommandResult CommandRunner::Execute(const std::string& line)
	{
		const std::vector<std::string> words = Tokenize(line);
		if (words.empty()) return { true, "" };
		const auto it = m_Commands.find(words[0]);
		if (it == m_Commands.end())
		{
			std::string hint;
			for (const std::string& n : Complete(words[0])) hint += (hint.empty() ? " Did you mean: " : ", ") + n;
			return { false, "unknown command '" + words[0] + "'." + hint + " (type help)" };
		}
		return it->second.Run(std::vector<std::string>(words.begin() + 1, words.end()));
	}

	std::vector<std::string> CommandRunner::Names() const
	{
		std::vector<std::string> out;
		for (const auto& [name, entry] : m_Commands) out.push_back(name);
		return out;
	}

	std::string CommandRunner::Help(const std::string& name) const
	{
		const auto it = m_Commands.find(name);
		return it == m_Commands.end() ? "" : it->second.Help;
	}

	std::vector<std::string> CommandRunner::Complete(const std::string& prefix) const
	{
		std::vector<std::string> out;
		for (const auto& [name, entry] : m_Commands)
			if (!prefix.empty() && name.compare(0, prefix.size(), prefix) == 0) out.push_back(name);
		return out;
	}
}
