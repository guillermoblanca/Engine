#include "Lang.h"
#include "GB/Log.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace GB::Editor
{
	namespace
	{
		std::string Spaced(std::string id)
		{
			std::replace(id.begin(), id.end(), '_', ' ');
			return id;
		}

		std::string Lower(std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}
	}

	bool Lang::Load(const std::string& dir, const std::string& code)
	{
		m_Code = code;
		m_On = false;
		m_Ui.clear();
		m_Names.clear();
		m_Entities.clear();
		m_Phrases.clear();
		m_Cache.clear();
		m_Missing.clear();
		if (code == "en" || code.empty()) return true;
		std::ifstream in(dir + "/" + code + ".json");
		const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
		if (!in.is_open() || j.is_discarded() || !j.is_object())
		{
			GB_CLIENT_WARN("no language file {}/{}.json (or it does not parse): the interface stays in English", dir, code);
			m_Code = "en";
			return false;
		}
		if (const auto ui = j.find("ui"); ui != j.end() && ui->is_object())
			for (const auto& [k, v] : ui->items()) if (v.is_string()) m_Ui[k] = v.get<std::string>();
		if (const auto names = j.find("names"); names != j.end() && names->is_object())
			for (const auto& [kind, table] : names->items())
				if (table.is_object())
					for (const auto& [id, v] : table.items()) if (v.is_string()) m_Names[kind][id] = v.get<std::string>();
		if (const auto ent = j.find("entity"); ent != j.end() && ent->is_object())
			for (const auto& [k, v] : ent->items()) if (v.is_string()) m_Entities[Lower(k)] = v.get<std::string>();
		if (const auto phrases = j.find("phrases"); phrases != j.end() && phrases->is_array())
			for (const auto& p : *phrases)
			{
				if (!p.is_object() || !p.contains("re") || !p.contains("to")) continue;
				try { m_Phrases.push_back({ std::regex(p["re"].get<std::string>(), std::regex::ECMAScript | std::regex::optimize), p["to"].get<std::string>() }); }
				catch (const std::exception& e) { GB_CLIENT_WARN("language {}: the pattern '{}' is skipped ({})", code, p["re"].dump(), e.what()); }
			}
		m_On = true;
		return true;
	}

	std::string Lang::Fill(std::string pattern, const std::string* values, size_t count)
	{
		for (size_t i = 0; i < count; ++i)
		{
			const std::string key = "{" + std::to_string(i) + "}";
			for (size_t at = pattern.find(key); at != std::string::npos; at = pattern.find(key, at + values[i].size())) pattern.replace(at, key.size(), values[i]);
		}
		return pattern;
	}

	std::string Lang::Text(std::string_view english) const
	{
		if (m_On)
		{
			if (const auto it = m_Ui.find(std::string(english)); it != m_Ui.end()) return it->second;
			if (m_Missing.size() < 500) m_Missing.emplace(english);
		}
		return std::string(english);
	}

	std::string Lang::Name(std::string_view kind, const std::string& id, const std::string& fallback) const
	{
		if (m_On)
			if (const auto table = m_Names.find(kind); table != m_Names.end())
				if (const auto it = table->second.find(id); it != table->second.end()) return it->second;
		return fallback.empty() ? Spaced(id) : fallback;
	}

	std::string Lang::Entity(const std::string& name) const
	{
		if (!m_On || name.empty()) return name;
		// its number apart, "wild " away (the scene's mark of what grows by itself), then the table
		std::string base = name, number;
		if (const size_t sp = base.find_last_of(' '); sp != std::string::npos && sp + 1 < base.size()
			&& std::all_of(base.begin() + static_cast<std::ptrdiff_t>(sp) + 1, base.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
		{
			number = base.substr(sp);
			base = base.substr(0, sp);
		}
		std::string key = Lower(Spaced(base));
		if (key.rfind("wild ", 0) == 0) key = key.substr(5);
		if (const auto it = m_Entities.find(key); it != m_Entities.end()) return it->second + number;
		// an id of the data written as a detail ("cooking" while experimenting, "dried_berry" while making): its name
		for (const char* kind : { "tech", "item", "building" })
			if (const auto table = m_Names.find(std::string_view(kind)); table != m_Names.end())
				if (const auto n = table->second.find(name); n != table->second.end()) return n->second;
		return name;
	}

	std::string Lang::Doing(const std::string& job, const std::string& detail) const
	{
		const std::string what = Name("doing", job.empty() ? std::string("idle") : job);
		if (detail.empty()) return what;
		if (job == "talk")
		{
			// "<kind> <other> [<about>|listening]" (Tribe::StartSocial); the English of the kinds is what the clan's log says
			static const std::map<std::string, std::string> kEnglish = { { "chat", "chats with" }, { "story", "tells a story to" }, { "gossip", "gossips with" },
				{ "teach", "shows something to" }, { "console", "consoles" }, { "settle", "makes peace with" }, { "greet", "greets" }, { "share", "shares food with" },
				{ "groom", "grooms" }, { "play", "plays with" } };
			std::vector<std::string> words;
			for (size_t at = 0; at < detail.size();)
			{
				const size_t sp = std::min(detail.find(' ', at), detail.size());
				if (sp > at) words.push_back(detail.substr(at, sp - at));
				at = sp + 1;
			}
			if (words.size() >= 2)
			{
				const auto english = kEnglish.find(words[0]);
				const std::string kind = Name("talk", words[0], english != kEnglish.end() ? english->second : words[0]);
				if (words.size() > 2 && words[2] == "listening") return Text("listens to {0}", words[1]) + " (" + Name("talkabout", words[0], words[0]) + ")";
				return kind + " " + words[1] + (words.size() > 2 ? " (" + Entity(words[2]) + ")" : std::string());
			}
		}
		if (job == "envoy") return what + " " + Name("clan", detail, detail);   // (the clan it goes to)
		if (job == "rite") return what + " " + Name("rite", detail, detail);
		return what + " " + Entity(detail);
	}

	std::string Lang::Apply(const Phrase& p, const std::smatch& m) const
	{
		// {n}: a captured group as it is; {item:n}, {building:n}... a name of that kind by the captured id; {ent:n}: a thing of the scene;
		// {line:n}: the captured text translated again (a reason inside a sentence)
		std::string out;
		const std::string& to = p.To;
		for (size_t i = 0; i < to.size();)
		{
			if (to[i] != '{') { out += to[i++]; continue; }
			const size_t close = to.find('}', i);
			if (close == std::string::npos) { out += to.substr(i); break; }
			const std::string inside = to.substr(i + 1, close - i - 1);
			const size_t colon = inside.find(':');
			const std::string kind = colon == std::string::npos ? std::string() : inside.substr(0, colon);
			const std::string digits = colon == std::string::npos ? inside : inside.substr(colon + 1);
			const bool number = !digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
			const size_t n = number ? static_cast<size_t>(std::stoul(digits)) : 0;
			if (!number || n >= m.size()) { out += to.substr(i, close - i + 1); i = close + 1; continue; }
			const std::string g = m[n].str();
			if (kind.empty()) out += g;
			else if (kind == "ent") out += Entity(g);
			else if (kind == "line") out += Line(g);
			else out += Name(kind, g);
			i = close + 1;
		}
		return out;
	}

	std::string Lang::Line(const std::string& english) const
	{
		if (!m_On || english.empty()) return english;
		if (const auto it = m_Cache.find(english); it != m_Cache.end()) return it->second;
		std::string out = english;
		std::smatch m;
		for (const Phrase& p : m_Phrases)
			if (std::regex_match(english, m, p.Re)) { out = Apply(p, m); break; }
		if (m_Cache.size() > 4000) m_Cache.clear();   // (the log is a ring: old lines are not asked again)
		m_Cache.emplace(english, out);
		return out;
	}

	bool Lang::Covers(const std::string& english) const
	{
		std::smatch m;
		for (const Phrase& p : m_Phrases) if (std::regex_match(english, m, p.Re)) return true;
		return false;
	}
}
