#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB::Editor
{
	struct CommandResult
	{
		bool Ok = true;
		std::string Message;
	};

	// A tiny command interpreter: "name arg arg ..." (quotes group words, # starts a comment). It is the
	// automation surface of the editor: people type in the console, agents and tests run scripts.
	class CommandRunner
	{
	public:
		using Handler = std::function<CommandResult(const std::vector<std::string>& args)>;

		void Register(const std::string& name, const std::string& help, Handler handler);
		CommandResult Execute(const std::string& line);
		std::vector<std::string> Names() const;
		std::string Help(const std::string& name) const;
		// Completes a prefix of a command name (for the console input).
		std::vector<std::string> Complete(const std::string& prefix) const;

		static std::vector<std::string> Tokenize(const std::string& line);
		// Splits a script into command lines (newlines and ';').
		static std::vector<std::string> SplitScript(const std::string& text);

	private:
		struct Entry { std::string Help; Handler Run; };
		std::map<std::string, Entry> m_Commands;
	};
}
