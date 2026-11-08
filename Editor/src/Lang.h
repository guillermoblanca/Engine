#pragma once
// The language of the game's interface (the editor's own panels stay in English). The words are data: assets/ui/lang/<lang>.json, with
//   "ui":      the interface's texts by their English ("Eat": "Comer"; "{0} beds for {1}": "{0} camas para {1}"),
//   "names":   the names of the world by kind and id ("item": { "berry": "Bayas" }, "building", "tech", "role", "need", "season"...),
//   "entity":  the names of things in the world without their number ("fiber grass": "hierba de fibra"; "wild fiber grass 9" -> "hierba de fibra 9"),
//   "phrases": what the simulation writes (the clan's log, the reasons of the board, the prompt) as patterns: { "re": "...", "to": "... {1} {item:2}" }.
// Only what is drawn goes through here: `game status`, the commands' answers and the logs stay English (the tests read them). Whatever has no
// translation is shown as it is, so a missing word never hides anything. A translated text is never a printf format: {0}, {1}... are replaced here.
#include <cstdio>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace GB::Editor
{
	class Lang
	{
	public:
		// Reads <dir>/<code>.json; "en" (or a file that is not there) means the English of the code. Returns false when the file could not be read.
		bool Load(const std::string& dir, const std::string& code);
		const std::string& Code() const { return m_Code; }
		bool On() const { return m_On; }

		// The interface's text for `english`, with {0}, {1}... replaced by the arguments.
		std::string Text(std::string_view english) const;
		template <class... A> std::string Text(std::string_view english, const A&... args) const
		{
			const std::string values[] = { Arg(args)... };
			return Fill(Text(english), values, sizeof...(A));
		}
		// A name of the world: `kind` is "item", "building", "tech", "role"...; `fallback` (the data's English name) when there is none, else the id spaced.
		std::string Name(std::string_view kind, const std::string& id, const std::string& fallback = std::string()) const;
		// A number for the player: `decimals` after the mark, which is a comma in Spanish ("3,2") and a point in English.
		std::string Num(float v, int decimals = 1) const
		{
			char s[32];
			std::snprintf(s, sizeof s, "%.*f", decimals, static_cast<double>(v));
			std::string out = s;
			if (m_Code == "es") for (char& c : out) if (c == '.') c = ',';
			return out;
		}
		// A key as the player reads it ("GraveAccent" -> "`", "Equal" -> "=", "Escape" -> "Esc"): the "key" names, else its ImGui name.
		std::string Key(const std::string& imguiName) const { return Name("key", imguiName, imguiName); }
		// A thing of the world by its name in the scene ("wild fiber grass 9", "Shelter 1", "Rabbit 2").
		std::string Entity(const std::string& name) const;
		// What a member is doing, as a label: its job and the job's detail ("gather" + "wild berry bush 4"; "talk" + "greet Mara listening" ->
		// "saluda a Mara" / "escucha a Mara (saludo)": the detail of a talk is the kind, the other one and what about, not a thing of the scene).
		std::string Doing(const std::string& job, const std::string& detail) const;
		// A line the simulation wrote (the log, a job's reason, the prompt): the first pattern that matches, else the line as it is. Cached.
		std::string Line(const std::string& english) const;
		// Whether a pattern covers the line (`lang check` counts the coverage of the log).
		bool Covers(const std::string& english) const;
		// The interface's texts asked for and not in the file (`lang missing`): what is left to translate.
		const std::set<std::string>& Missing() const { return m_Missing; }

		// The first letter in capitals (a name as a label: "bayas" -> "Bayas"); also the accented ones of Latin-1 written in UTF-8 ("ámbar" -> "Ámbar").
		static std::string Cap(std::string s)
		{
			if (s.empty()) return s;
			const unsigned char c = static_cast<unsigned char>(s[0]);
			if (c >= 'a' && c <= 'z') s[0] = static_cast<char>(c - 32);
			else if (c == 0xC3 && s.size() > 1 && static_cast<unsigned char>(s[1]) >= 0xA0 && static_cast<unsigned char>(s[1]) <= 0xBE) s[1] = static_cast<char>(static_cast<unsigned char>(s[1]) - 0x20);
			return s;
		}
		static std::string Arg(const std::string& s) { return s; }
		static std::string Arg(const char* s) { return s ? s : ""; }
		template <class N, class = std::enable_if_t<std::is_integral_v<N>>> static std::string Arg(N n) { return std::to_string(n); }

	private:
		struct Phrase { std::regex Re; std::string To; };
		static std::string Fill(std::string pattern, const std::string* values, size_t count);
		std::string Apply(const Phrase& p, const std::smatch& m) const;

		std::string m_Code = "en";
		bool m_On = false;
		std::unordered_map<std::string, std::string> m_Ui;
		std::map<std::string, std::unordered_map<std::string, std::string>, std::less<>> m_Names;
		std::unordered_map<std::string, std::string> m_Entities;
		std::vector<Phrase> m_Phrases;
		mutable std::unordered_map<std::string, std::string> m_Cache;
		mutable std::set<std::string> m_Missing;
	};
}
