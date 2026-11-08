#include "GB/Game/Clans.h"
#include <algorithm>
#include <functional>

// V7 (docs/VISION_MUNDO_VIVO.md): what the clans become as they grow. A settlement climbs the ranks of the data (camp, village, town, city) by its
// people and its finished buildings, and a rank it reaches is history. Clans bound by alliances are a polity (a confederation, named after the
// biggest of them); one that forms is history too. Both are read from the state of the clans each time (nothing to save): the first look of a
// run, or after a load, only learns where things stand.
namespace GB
{
	int ClanWorld::RankIndex(const Clan& c) const
	{
		if (!c.Folk || Rules.Ranks.empty()) return -1;
		const int people = Alive(c), built = c.Folk->BuildingCount();
		int at = -1;
		for (size_t i = 0; i < Rules.Ranks.size(); ++i)
			if (people >= Rules.Ranks[i].People && built >= Rules.Ranks[i].Buildings) at = static_cast<int>(i);
		return at;
	}

	std::string ClanWorld::RankOf(ClanId id) const
	{
		const Clan* c = Find(id);
		const int at = c ? RankIndex(*c) : -1;
		return at < 0 ? std::string() : Rules.Ranks[static_cast<size_t>(at)].Name;
	}

	std::vector<std::vector<ClanId>> ClanWorld::Polities() const
	{
		std::vector<ClanId> ids;
		for (const auto& c : m_Clans) if (Alive(*c) > 0) ids.push_back(c->Id);
		std::sort(ids.begin(), ids.end());
		std::map<ClanId, ClanId> root;
		for (ClanId id : ids) root[id] = id;
		std::function<ClanId(ClanId)> find = [&](ClanId x) { return root[x] == x ? x : root[x] = find(root[x]); };
		for (size_t i = 0; i < ids.size(); ++i)
			for (size_t j = i + 1; j < ids.size(); ++j)
				if (HasPact(ids[i], ids[j], "alliance")) root[find(ids[j])] = find(ids[i]);
		std::map<ClanId, std::vector<ClanId>> groups;
		for (ClanId id : ids) groups[find(id)].push_back(id);
		std::vector<std::vector<ClanId>> out;
		for (auto& [r, g] : groups) if (g.size() >= 2) out.push_back(g);
		return out;
	}

	std::string ClanWorld::PolityName(const std::vector<ClanId>& members) const
	{
		const Clan* big = nullptr;
		for (ClanId id : members)
			if (const Clan* c = Find(id); c && (!big || Alive(*c) > Alive(*big))) big = c;
		return big ? "the confederation of the " + big->Name : std::string();
	}

	std::string ClanWorld::Ally(ClanId a, ClanId b)
	{
		const Clan* ca = Find(a);
		const Clan* cb = Find(b);
		if (!ca || !cb || a == b) return "two different clans are needed";
		ClanRelation& r = RelationRef(a, b);
		r.Pacts.erase(std::remove_if(r.Pacts.begin(), r.Pacts.end(), [](const Pact& p) { return p.Kind == "alliance"; }), r.Pacts.end());
		r.Pacts.push_back({ "alliance", m_Time, m_Time + static_cast<double>(Rules.Pacts.Days * DaySeconds()) });
		Relate(a, b, "pact", 1.0f, ca->Name + " and " + cb->Name + " agree an alliance");
		StepPolity();
		return {};
	}

	void ClanWorld::StepPolity()
	{
		auto event = [&](const std::string& kind, ClanId clan, ClanId other, const std::string& text)
		{
			HistoryEvent h;
			h.At = m_Past.HasClock() ? m_Past.Clock() : m_Time;
			h.Kind = kind;
			h.Clan = clan;
			h.Other = other;
			h.Text = text;
			m_Past.Add(std::move(h));
		};
		// the ranks: told when one is climbed (the first look only learns them)
		for (const auto& c : m_Clans)
		{
			const int at = RankIndex(*c);
			const auto was = m_Ranks.find(c->Id);
			if (was != m_Ranks.end() && at > was->second) event("rank", c->Id, -1, c->Name + " become a " + Rules.Ranks[static_cast<size_t>(at)].Name);
			m_Ranks[c->Id] = at;
			if (c->Folk)
			{
				std::set<std::string> reached;
				for (int i = 0; i <= at; ++i) reached.insert(Rules.Ranks[static_cast<size_t>(i)].Id);
				c->Folk->SetRanks(std::move(reached));
			}
		}
		// the polities: told when a new one is there (a bigger one, or one that was not)
		std::set<std::string> now;
		for (const std::vector<ClanId>& g : Polities())
		{
			std::string key;
			for (ClanId id : g) key += std::to_string(id) + ",";
			now.insert(key);
			if (m_PolitiesSeen && !m_Polities.count(key))
			{
				std::string names;
				for (size_t i = 0; i < g.size(); ++i)
					if (const Clan* c = Find(g[i])) names += (i == 0 ? "" : i + 1 == g.size() ? " and " : ", ") + c->Name;
				event("polity", g.front(), g.size() > 1 ? g[1] : -1, names + " form " + PolityName(g));
			}
		}
		m_Polities = std::move(now);
		m_PolitiesSeen = true;
	}
}
