// T324: the player's research queue (see ResearchQueue.h).
#include "GB/Game/ResearchQueue.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		std::string Percent(float p) { return std::to_string(static_cast<int>(std::floor(std::clamp(p, 0.0f, 1.0f) * 100.0f + 0.5f))) + "%"; }
	}

	std::string ResearchQueue::Name(const Tribe& tribe, const std::string& tech)
	{
		return tribe.Knowledge().Name(tech);
	}

	bool ResearchQueue::Researchable(const Tribe& tribe, const std::string& tech)
	{
		const ClanKnowledge& k = tribe.Knowledge();
		const TechTree* tree = k.Techs();
		const TechDef* t = tree ? tree->Find(tech) : nullptr;
		if (!t || k.Clan().count(tech) || t->Discovery.Experiment <= 0.0f || !k.Physical(tech)) return false;
		return std::all_of(t->Requires.begin(), t->Requires.end(), [&](const std::string& r) { return k.Clan().count(r) != 0; });
	}

	std::string ResearchQueue::Current(const Tribe& tribe) const
	{
		for (const std::string& tech : m_Queue) if (Researchable(tribe, tech)) return tech;
		return {};
	}

	std::vector<std::string> ResearchQueue::Open(const Tribe& tribe, const std::vector<std::string>& except)
	{
		std::vector<std::string> out;
		const TechTree* tree = tribe.Knowledge().Techs();
		if (!tree) return out;
		for (const auto& [id, t] : tree->Techs)
			if (Researchable(tribe, id) && std::find(except.begin(), except.end(), id) == except.end()) out.push_back(id);
		return out;
	}

	int ResearchQueue::Hidden(const Tribe& tribe)
	{
		const ClanKnowledge& k = tribe.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) return 0;
		int n = 0;
		for (const auto& [id, t] : tree->Techs)
			if (!k.Clan().count(id) && !Researchable(tribe, id)) ++n;
		return n;
	}

	std::string ResearchQueue::Status(const Tribe& tribe, const std::string& tech, bool current)
	{
		const ClanKnowledge& k = tribe.Knowledge();
		const TechDef* t = k.Techs() ? k.Techs()->Find(tech) : nullptr;
		if (!t) return "not in the tree";
		if (k.Clan().count(tech)) return "known";
		for (const std::string& r : t->Requires)
			if (!k.Clan().count(r)) return "waits for " + r;
		if (t->Discovery.Experiment <= 0.0f) return "cannot be found by trying";
		if (!k.Physical(tech)) return "the world does not let it";
		return current ? "researching" : "next";
	}

	ResearchQueue::Reply ResearchQueue::Add(Tribe& tribe, const std::string& tech)
	{
		const TechTree* tree = tribe.Knowledge().Techs();
		if (!tree) return { false, "this world has no technologies" };
		if (!tree->Find(tech)) return { false, "no technology '" + tech + "'" };
		if (tribe.Knowledge().Clan().count(tech)) return { false, "the clan already knows " + tech };
		if (std::find(m_Queue.begin(), m_Queue.end(), tech) != m_Queue.end()) return { false, tech + " is already in the research queue" };
		if (m_Queue.size() >= kMost) return { false, "the research queue is full (" + std::to_string(kMost) + ")" };
		m_Queue.push_back(tech);
		tribe.UpdateGoals();
		std::string text = "research queue: " + tech + " added (" + std::to_string(m_Queue.size()) + " in the queue)";
		if (const std::string s = Status(tribe, tech, Current(tribe) == tech); s != "researching" && s != "next") text += "; " + s;
		return { true, text };
	}

	ResearchQueue::Reply ResearchQueue::Remove(Tribe& tribe, const std::string& tech)
	{
		const auto it = std::find(m_Queue.begin(), m_Queue.end(), tech);
		if (it == m_Queue.end()) return { false, tech + " is not in the research queue" };
		m_Queue.erase(it);
		tribe.UpdateGoals();
		return { true, "research queue: " + tech + " removed" };
	}

	ResearchQueue::Reply ResearchQueue::Clear(Tribe& tribe)
	{
		const bool had = !m_Queue.empty();
		m_Queue.clear();
		tribe.UpdateGoals();
		return { true, had ? "research queue cleared: the clan chooses what to seek" : "the research queue is empty" };
	}

	ResearchQueue::Reply ResearchQueue::Top(Tribe& tribe, const std::string& tech)
	{
		const auto it = std::find(m_Queue.begin(), m_Queue.end(), tech);
		if (it == m_Queue.end())
		{
			Reply r = Add(tribe, tech);
			if (!r.Ok) return r;
			std::rotate(m_Queue.begin(), m_Queue.end() - 1, m_Queue.end());
			tribe.UpdateGoals();
			return { true, "research queue: " + tech + " added first" };
		}
		std::rotate(m_Queue.begin(), it, it + 1);
		tribe.UpdateGoals();
		return { true, "research queue: " + tech + " first" };
	}

	std::string ResearchQueue::List(const Tribe& tribe) const
	{
		std::string text;
		if (m_Queue.empty()) text = "research queue empty: the clan chooses what to seek";
		else
		{
			std::map<std::string, float> progress;
			for (const ClanKnowledge::ResearchLine& l : tribe.Knowledge().Research()) progress[l.Tech] = l.Progress;
			const std::string current = Current(tribe);
			text = "research queue:";
			for (size_t i = 0; i < m_Queue.size(); ++i)
			{
				const std::string& tech = m_Queue[i];
				const auto p = progress.find(tech);
				text += (i == 0 ? " " : "; ") + std::to_string(i + 1) + ". " + tech + " " + Percent(p == progress.end() ? 0.0f : p->second) + " "
					+ Status(tribe, tech, tech == current);
			}
		}
		const std::vector<std::string> open = Open(tribe, m_Queue);
		text += " | can research:";
		if (open.empty()) text += " nothing now";
		for (size_t i = 0; i < open.size(); ++i) text += (i == 0 ? " " : ", ") + open[i];
		if (const int hidden = Hidden(tribe); hidden > 0) text += "; ? " + std::to_string(hidden) + " more";
		return text;
	}

	void ResearchQueue::Lead(Tribe& tribe, std::vector<std::string>& goals)
	{
		if (m_Queue.empty()) return;
		const ClanKnowledge& k = tribe.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) return;
		for (auto it = m_Queue.begin(); it != m_Queue.end();)
		{
			if (!tree->Find(*it)) { it = m_Queue.erase(it); continue; }
			if (k.Clan().count(*it))
			{
				tribe.NoteClan("has learnt " + Name(tribe, *it) + ", from the research queue");
				it = m_Queue.erase(it);
				continue;
			}
			++it;
		}
		for (const std::string& tech : m_Queue)
			if (Researchable(tribe, tech) && std::find(goals.begin(), goals.end(), tech) == goals.end()) goals.push_back(tech);
	}

	std::string ResearchQueue::ToJson() const
	{
		return Json{ { "version", 1 }, { "queue", m_Queue } }.dump();
	}

	bool ResearchQueue::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("queue") || !j["queue"].is_array() || j["queue"].size() > kMost) return false;
		std::vector<std::string> queue;
		for (const Json& t : j["queue"])
		{
			if (!t.is_string() || t.get<std::string>().empty()) return false;
			if (std::find(queue.begin(), queue.end(), t.get<std::string>()) != queue.end()) return false;
			queue.push_back(t.get<std::string>());
		}
		m_Queue = std::move(queue);
		return true;
	}
}
