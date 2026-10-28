#include "GB/Game/Knowledge.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr const char* kPrefix = "tech.";   // how a technology is named in a save, beside other kinds of knowledge ("place.", "recipe.")

		std::string Percent(float v)
		{
			char b[16];
			std::snprintf(b, sizeof b, "%d%%", static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)));
			return b;
		}

		// 64-bit FNV-1a over the bytes of the parts, with a separator so "ab"+"c" is not "a"+"bc".
		uint64_t Mix(uint64_t h, const std::string& s)
		{
			for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
			h ^= 0xffu;
			h *= 1099511628211ull;
			return h;
		}
	}

	void ClanKnowledge::SetTechs(const TechTree* techs)
	{
		m_Techs = techs;
		if (m_Techs)
			for (const std::string& t : m_Techs->Starting())
			{
				m_Clan.insert(t);
				for (auto& [name, k] : m_Agents) k.Known.insert(t);
			}
	}

	void ClanKnowledge::Clear()
	{
		m_Agents.clear();
		m_Clan.clear();
		m_ClanOther.clear();
		m_LessonTimer = 0.0f;
		if (m_Techs)
			for (const std::string& t : m_Techs->Starting()) m_Clan.insert(t);
	}

	void ClanKnowledge::Join(const std::string& name)
	{
		if (m_Agents.count(name)) return;
		AgentKnowledge& k = m_Agents[name];
		if (m_Techs)
			for (const std::string& t : m_Techs->Starting()) k.Known.insert(t);
	}

	void ClanKnowledge::Leave(const std::string& name) { m_Agents.erase(name); }

	const AgentKnowledge* ClanKnowledge::Of(const std::string& name) const
	{
		const auto it = m_Agents.find(name);
		return it == m_Agents.end() ? nullptr : &it->second;
	}

	bool ClanKnowledge::Knows(const std::string& name, const std::string& tech) const
	{
		if (!m_Techs) return true;
		const AgentKnowledge* k = Of(name);
		if (k) return k->Known.count(tech) > 0;
		const TechDef* t = m_Techs->Find(tech);   // someone the clan has not met knows what anyone knows from the start
		return t && t->Known;
	}

	bool ClanKnowledge::CanLearn(const std::string& name, const std::string& tech) const
	{
		if (!m_Techs) return false;
		const TechDef* t = m_Techs->Find(tech);
		if (!t || Knows(name, tech)) return false;
		return std::all_of(t->Requires.begin(), t->Requires.end(), [&](const std::string& q) { return Knows(name, q); });
	}

	bool ClanKnowledge::CanMake(const std::string& name, const std::string& recipe) const
	{
		if (!m_Techs) return true;
		const std::vector<std::string> locks = m_Techs->ForRecipe(recipe);
		return locks.empty() || std::any_of(locks.begin(), locks.end(), [&](const std::string& t) { return Knows(name, t); });
	}

	bool ClanKnowledge::CanGather(const std::string& name, const std::string& node) const
	{
		if (!m_Techs) return true;
		const std::vector<std::string> locks = m_Techs->ForNode(node);
		return locks.empty() || std::any_of(locks.begin(), locks.end(), [&](const std::string& t) { return Knows(name, t); });
	}

	std::set<std::string> ClanKnowledge::KnownRecipes(const std::string& name, const RecipeBook& book) const
	{
		std::set<std::string> out;
		for (const std::string& id : book.Ids()) if (CanMake(name, id)) out.insert(id);
		return out;
	}

	void ClanKnowledge::Learn(const std::string& name, AgentKnowledge& k, const std::string& tech)
	{
		(void)name;
		k.Known.insert(tech);
		k.Progress.erase(tech);
		k.Tries.erase(tech);
	}

	std::string ClanKnowledge::Grant(const std::string& name, const std::string& tech)
	{
		if (!m_Techs) return "there are no technologies loaded (everything is known)";
		if (!m_Techs->Find(tech)) return "no technology '" + tech + "'";
		const auto it = m_Agents.find(name);
		if (it == m_Agents.end()) return "'" + name + "' is not in the clan";
		Learn(name, it->second, tech);
		return {};
	}

	std::string ClanKnowledge::GrantClan(const std::string& tech)
	{
		if (!m_Techs) return "there are no technologies loaded (everything is known)";
		if (!m_Techs->Find(tech)) return "no technology '" + tech + "'";
		for (auto& [name, k] : m_Agents) Learn(name, k, tech);
		m_Clan.insert(tech);
		return {};
	}

	std::string ClanKnowledge::Name(const std::string& tech) const
	{
		const TechDef* t = m_Techs ? m_Techs->Find(tech) : nullptr;
		return t && !t->Name.empty() ? t->Name : tech;
	}

	// ------------------------------------------------------------------ discovery
	double ClanKnowledge::Roll(const std::string& name, const std::string& tech, int salt, int n) const
	{
		uint64_t h = 14695981039346656037ull;
		h = Mix(h, std::to_string(m_Techs ? m_Techs->Rules.Seed : 1u));
		h = Mix(h, name);
		h = Mix(h, tech);
		h = Mix(h, std::to_string(salt) + ":" + std::to_string(n));
		h ^= h >> 29;   // the low bits of FNV are weak: fold the high ones in before taking 53 of them
		h *= 0xbf58476d1ce4e5b9ull;
		h ^= h >> 32;
		return static_cast<double>(h >> 11) / 9007199254740992.0;
	}

	std::string ClanKnowledge::NextExperiment(const std::string& name, double now, const Inventory& store, const Inventory& bag, const RecipeBook& book, std::string* recipe) const
	{
		if (!m_Techs) return {};
		const AgentKnowledge* k = Of(name);
		if (!k || now < k->NextTry) return {};
		for (const auto& [id, t] : m_Techs->Techs)
		{
			if (t.Discovery.Experiment <= 0.0f || !CanLearn(name, id)) continue;
			for (const std::string& rid : t.Recipes)
			{
				const Recipe* r = book.Find(rid);
				if (!r) continue;
				const bool have = std::all_of(r->Inputs.begin(), r->Inputs.end(), [&](const auto& in) { return store.Count(in.first) + bag.Count(in.first) >= in.second; });
				if (!have) continue;
				if (recipe) *recipe = rid;
				return id;
			}
			if (t.Recipes.empty())   // an idea: nothing to hold
			{
				if (recipe) recipe->clear();
				return id;
			}
		}
		return {};
	}

	std::vector<KnowledgeEvent> ClanKnowledge::Experiment(const std::string& name, double now, Inventory& store, Inventory& bag, const RecipeBook& book)
	{
		std::vector<KnowledgeEvent> out;
		std::string rid;
		const std::string tech = NextExperiment(name, now, store, bag, book, &rid);
		if (tech.empty()) return out;
		AgentKnowledge& k = m_Agents[name];
		const TechDef& t = *m_Techs->Find(tech);
		const int tries = k.Tries[tech];   // failures so far
		const double chance = std::min(1.0, static_cast<double>(t.Discovery.Experiment) * (1.0 + static_cast<double>(m_Techs->Rules.Growth) * tries));
		k.NextTry = now + static_cast<double>(t.Discovery.Seconds);
		const std::string with = rid.empty() ? std::string() : " with the materials of the " + rid;
		if (Roll(name, tech, 1, tries) < chance)
		{
			Learn(name, k, tech);
			out.push_back({ name, "discovers " + Name(tech) + " by experimenting" + with + " (try " + std::to_string(tries + 1) + ")" });
			return out;
		}
		k.Tries[tech] = tries + 1;
		std::string spoilt;
		if (const Recipe* r = rid.empty() ? nullptr : book.Find(rid); r && !r->Inputs.empty() && Roll(name, tech, 2, tries) < static_cast<double>(t.Discovery.Waste))
		{
			const std::string& first = r->Inputs.begin()->first;
			if (store.Remove(first, 1) == 1 || bag.Remove(first, 1) == 1) spoilt = "; one " + first + " is spoilt";
		}
		out.push_back({ name, "tries to work out " + Name(tech) + with + " and fails (try " + std::to_string(tries + 1) + ")" + spoilt });
		return out;
	}

	void ClanKnowledge::Understand(const std::string& name, AgentKnowledge& k, const std::string& tech, float amount, const std::string& how, std::vector<KnowledgeEvent>& out)
	{
		float& p = k.Progress[tech];
		p = std::min(1.0f, p + amount);
		if (p >= 1.0f - 1e-5f)
		{
			Learn(name, k, tech);
			out.push_back({ name, "learns " + Name(tech) + " " + how });
		}
		else out.push_back({ name, "understands " + Percent(p) + " of " + Name(tech) + " " + how });
	}

	std::vector<KnowledgeEvent> ClanKnowledge::Witness(const std::string& doer, const std::string& tech, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents)
	{
		std::vector<KnowledgeEvent> out;
		const TechDef* t = m_Techs ? m_Techs->Find(tech) : nullptr;
		if (!t || t->Discovery.Observe <= 0.0f) return out;
		for (const KnowledgeAgent& a : agents)
		{
			if (a.Name == doer || !a.Alive || glm::distance(a.Pos, where) > m_Techs->Rules.ObserveRadius || !CanLearn(a.Name, tech)) continue;
			const auto it = m_Agents.find(a.Name);
			if (it == m_Agents.end()) continue;
			Understand(a.Name, it->second, tech, t->Discovery.Observe, "by watching " + doer, out);
		}
		return out;
	}

	std::vector<KnowledgeEvent> ClanKnowledge::WitnessRecipe(const std::string& doer, const std::string& recipe, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents)
	{
		if (!m_Techs) return {};
		for (const std::string& t : m_Techs->ForRecipe(recipe))   // the technology it used: the first it knows
			if (Knows(doer, t)) return Witness(doer, t, where, agents);
		return {};
	}

	std::vector<KnowledgeEvent> ClanKnowledge::WitnessNode(const std::string& doer, const std::string& node, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents)
	{
		if (!m_Techs) return {};
		for (const std::string& t : m_Techs->ForNode(node))
			if (Knows(doer, t)) return Witness(doer, t, where, agents);
		return {};
	}

	std::vector<KnowledgeEvent> ClanKnowledge::Share(const std::string& name)
	{
		std::vector<KnowledgeEvent> out;
		const AgentKnowledge* k = Of(name);
		if (!m_Techs || !k) return out;
		for (const std::string& t : k->Known)
			if (m_Clan.insert(t).second) out.push_back({ name, "shows the clan how " + Name(t) + " is done" });
		return out;
	}

	bool ClanKnowledge::Tick(float dt)
	{
		if (!m_Techs) return false;
		m_LessonTimer += dt;
		if (m_LessonTimer < m_Techs->Rules.LessonSeconds) return false;
		m_LessonTimer -= m_Techs->Rules.LessonSeconds;
		if (m_LessonTimer >= m_Techs->Rules.LessonSeconds) m_LessonTimer = 0.0f;   // a huge step holds one lesson, not a backlog
		return true;
	}

	std::vector<KnowledgeEvent> ClanKnowledge::Lesson(const std::vector<KnowledgeAgent>& agents)
	{
		std::vector<KnowledgeEvent> out;
		if (!m_Techs) return out;
		// what the learners can be taught is what someone else at the camp knows now (before this lesson changes anything)
		for (const KnowledgeAgent& learner : agents)
		{
			if (!learner.Alive || !learner.AtCamp || !learner.Idle) continue;
			const auto me = m_Agents.find(learner.Name);
			if (me == m_Agents.end()) continue;
			for (const auto& [id, t] : m_Techs->Techs)
			{
				if (t.Discovery.Teach <= 0.0f || !CanLearn(learner.Name, id)) continue;
				auto canTeach = [&](const KnowledgeAgent& a) { return a.Alive && a.AtCamp && a.Name != learner.Name && Knows(a.Name, id) && m_Agents.count(a.Name); };
				// its mentors first (H10: parents, then those it likes best), in their order; else the first who can
				auto teacher = agents.end();
				for (const std::string& mentor : learner.Mentors)
				{
					const auto it = std::find_if(agents.begin(), agents.end(), [&](const KnowledgeAgent& a) { return a.Name == mentor; });
					if (it != agents.end() && canTeach(*it)) { teacher = it; break; }
				}
				if (teacher == agents.end()) teacher = std::find_if(agents.begin(), agents.end(), canTeach);
				if (teacher == agents.end()) continue;
				Understand(learner.Name, me->second, id, t.Discovery.Teach * learner.Learn * teacher->Teach, "from " + teacher->Name + " at the camp", out);
				out.back().Other = teacher->Name;
				break;   // one technology per lesson
			}
		}
		// the clan forgets what nobody alive knows (the technologies of the start stay: they are everywhere)
		for (auto it = m_Clan.begin(); it != m_Clan.end();)
		{
			const TechDef* t = m_Techs->Find(*it);
			const bool kept = !t || t->Known || std::any_of(agents.begin(), agents.end(), [&](const KnowledgeAgent& a) { return a.Alive && Knows(a.Name, *it); });
			if (kept) { ++it; continue; }
			out.push_back({ std::string(), "the clan forgets " + Name(*it) + ": nobody alive knows it any more" });
			it = m_Clan.erase(it);
		}
		return out;
	}

	// ------------------------------------------------------------------ for people
	std::string ClanKnowledge::Status(const std::string& name) const
	{
		if (!m_Techs) return name + " knows everything (no technologies loaded)";
		const AgentKnowledge* k = Of(name);
		if (!k) return "'" + name + "' is not in the clan";
		std::string s = name + " knows:";
		for (const std::string& t : k->Known) s += " " + t;
		if (k->Known.empty()) s += " nothing";
		if (!k->Progress.empty())
		{
			s += "; learning:";
			for (const auto& [t, p] : k->Progress) s += " " + t + " " + Percent(p);
		}
		if (!k->Tries.empty())
		{
			s += "; tried:";
			for (const auto& [t, n] : k->Tries) s += " " + t + " x" + std::to_string(n);
		}
		std::string can;
		for (const auto& [id, t] : m_Techs->Techs) if (CanLearn(name, id)) can += " " + id;
		if (!can.empty()) s += "; could learn:" + can;
		return s;
	}

	std::string ClanKnowledge::ClanStatus() const
	{
		if (!m_Techs) return "the clan knows everything (no technologies loaded)";
		std::string s = "the clan knows:";
		for (const std::string& t : m_Clan) s += " " + t;
		std::string lost;
		for (const auto& [id, t] : m_Techs->Techs) if (!m_Clan.count(id)) lost += " " + id;
		if (!lost.empty()) s += "; not yet:" + lost;
		return s;
	}

	// ------------------------------------------------------------------ saving
	std::vector<std::string> ClanKnowledge::Names(const std::string& name) const
	{
		std::vector<std::string> out;
		const AgentKnowledge* k = Of(name);
		if (!m_Techs || !k) return out;
		for (const std::string& t : k->Known) out.push_back(kPrefix + t);
		out.insert(out.end(), k->Other.begin(), k->Other.end());
		return out;
	}

	void ClanKnowledge::SetNames(const std::string& name, const std::vector<std::string>& names)
	{
		if (names.empty() || !m_Agents.count(name)) return;
		AgentKnowledge& k = m_Agents[name];
		k.Known.clear();
		k.Other.clear();
		const std::string prefix = kPrefix;
		for (const std::string& n : names)
		{
			if (n.compare(0, prefix.size(), prefix) == 0 && n.size() > prefix.size()) k.Known.insert(n.substr(prefix.size()));
			else k.Other.push_back(n);
		}
	}

	std::string ClanKnowledge::ToJson() const
	{
		Json known = Json::array();
		for (const std::string& t : m_Clan) known.push_back(kPrefix + t);
		for (const std::string& o : m_ClanOther) known.push_back(o);
		Json agents = Json::object();
		for (const auto& [name, k] : m_Agents)
		{
			if (k.Progress.empty() && k.Tries.empty() && k.NextTry == 0.0) continue;   // what it knows travels with its actor
			Json a = Json::object();
			if (!k.Progress.empty()) a["progress"] = k.Progress;
			if (!k.Tries.empty()) a["tries"] = k.Tries;
			if (k.NextTry != 0.0) a["nextTry"] = k.NextTry;
			agents[name] = a;
		}
		Json j = { { "known", known }, { "lesson", m_LessonTimer } };
		if (!agents.empty()) j["agents"] = agents;
		return j.dump();
	}

	bool ClanKnowledge::FromJson(const std::string& text)
	{
		const Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::set<std::string> clan;
		std::vector<std::string> other;
		float lesson = 0.0f;
		std::map<std::string, AgentKnowledge> agents = m_Agents;
		for (auto& [name, k] : agents) { k.Progress.clear(); k.Tries.clear(); k.NextTry = 0.0; }   // only what the section says is half done
		try
		{
			const std::string prefix = kPrefix;
			if (const auto it = j.find("known"); it != j.end())
			{
				if (!it->is_array()) return false;
				for (const Json& n : *it)
				{
					if (!n.is_string()) return false;
					const std::string s = n.get<std::string>();
					if (s.compare(0, prefix.size(), prefix) == 0 && s.size() > prefix.size()) clan.insert(s.substr(prefix.size()));
					else other.push_back(s);
				}
			}
			if (const auto it = j.find("lesson"); it != j.end())
			{
				if (!it->is_number()) return false;
				lesson = it->get<float>();
			}
			if (const auto it = j.find("agents"); it != j.end())
			{
				if (!it->is_object()) return false;
				for (const auto& [name, a] : it->items())
				{
					const auto k = agents.find(name);
					if (k == agents.end() || !a.is_object()) continue;
					if (const auto p = a.find("progress"); p != a.end())
						for (const auto& [t, v] : p->items()) { if (!v.is_number()) return false; k->second.Progress[t] = std::clamp(v.get<float>(), 0.0f, 1.0f); }
					if (const auto p = a.find("tries"); p != a.end())
						for (const auto& [t, v] : p->items()) { if (!v.is_number_integer() || v.get<long long>() < 0) return false; k->second.Tries[t] = v.get<int>(); }
					if (const auto p = a.find("nextTry"); p != a.end()) { if (!p->is_number()) return false; k->second.NextTry = p->get<double>(); }
				}
			}
		}
		catch (const Json::exception&) { return false; }
		if (!clan.empty() || !other.empty()) { m_Clan = std::move(clan); m_ClanOther = std::move(other); }
		if (m_Techs)
			for (const std::string& t : m_Techs->Starting()) m_Clan.insert(t);   // never forgotten (and a save from before the technologies has none)
		m_LessonTimer = lesson;
		m_Agents = std::move(agents);
		return true;
	}
}
