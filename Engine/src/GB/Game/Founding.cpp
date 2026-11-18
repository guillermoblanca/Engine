// T310: the founding of the player's clan (see Founding.h).
#include "GB/Game/Founding.h"
#include "GB/Game/GameSeeds.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace GB
{
	void ColonyFounding::Start(const TribeConfig& config, uint32_t worldSeed)
	{
		*this = ColonyFounding();
		m_Started = true;
		m_WorldSeed = worldSeed;
		m_Rules = config.Founding;
		m_Society = config.Society;
		m_Society.Seed = StreamSeed(MasterSeedOf(worldSeed), SeedStream::Founding);
		m_Work = config.Work;
		for (const auto& [id, role] : config.Roles) m_RoleSkills[id] = role.Skill;
		for (int i = 0; i < m_Rules.Candidates; ++i) m_Candidates.push_back(Draw(i, 0));
	}

	FoundingCandidate ColonyFounding::Draw(int slot, uint32_t roll) const
	{
		FoundingCandidate c;
		c.Slot = slot;
		c.Roll = roll;
		c.Name = MemberName(m_Work, static_cast<size_t>(slot) + static_cast<size_t>(roll) * static_cast<size_t>(std::max(1, m_Rules.Candidates)));
		// the pool is half women, half men (slot by slot), so a drawn again candidate keeps its place's sex and the "group" can always be balanced
		c.Who = Society().Draft(m_Society, c.Name, roll, slot % 2 == 0 ? 1 : 0);
		float best = -1.0f;
		for (const auto& [gene, role] : m_Rules.Trades)   // (map order: ties go to the first gene by name)
		{
			const auto g = c.Who.Genes.find(gene);
			const float v = g == c.Who.Genes.end() ? 0.0f : g->second;
			c.Aptitude += v;
			if (v > best && m_RoleSkills.count(role)) { best = v; c.Trade = role; }
		}
		if (!c.Trade.empty()) c.Skill = m_RoleSkills.at(c.Trade);
		const uint64_t h = DeriveSeed(m_Society.Seed, c.Name, roll);
		const float u = static_cast<float>(static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0));
		c.SkillLevel = c.Skill.empty() ? 0.0f : m_Rules.SkillMin + (m_Rules.SkillMax - m_Rules.SkillMin) * u;
		return c;
	}

	std::string ColonyFounding::Reroll(int i)
	{
		if (i < 0 || i >= static_cast<int>(m_Candidates.size())) return "no candidate " + std::to_string(i) + " (0.." + std::to_string(static_cast<int>(m_Candidates.size()) - 1) + ")";
		FoundingCandidate& c = m_Candidates[static_cast<size_t>(i)];
		if (c.Locked) return c.Name + " is locked (game founding unlock " + std::to_string(i) + ")";
		c = Draw(i, c.Roll + 1);
		++m_Rolls;
		return {};
	}

	int ColonyFounding::RerollAll()
	{
		int n = 0;
		for (int i = 0; i < static_cast<int>(m_Candidates.size()); ++i)
			if (!m_Candidates[static_cast<size_t>(i)].Locked && Reroll(i).empty()) ++n;
		return n;
	}

	std::string ColonyFounding::Lock(int i, bool on)
	{
		if (i < 0 || i >= static_cast<int>(m_Candidates.size())) return "no candidate " + std::to_string(i);
		m_Candidates[static_cast<size_t>(i)].Locked = on;
		return {};
	}

	std::string ColonyFounding::Pick(const std::vector<int>& slots)
	{
		const int n = static_cast<int>(slots.size());
		if (n < m_Rules.PartyMin || n > m_Rules.PartyMax) return "pick " + std::to_string(m_Rules.PartyMin) + " to " + std::to_string(m_Rules.PartyMax) + " people, not " + std::to_string(n);
		std::vector<int> sorted = slots;
		std::sort(sorted.begin(), sorted.end());
		if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return "a candidate is picked twice";
		for (const int s : sorted) if (s < 0 || s >= static_cast<int>(m_Candidates.size())) return "no candidate " + std::to_string(s);
		m_Picked = sorted;
		return {};
	}

	std::vector<int> ColonyFounding::DefaultPick(int n) const
	{
		n = std::clamp(n, 0, static_cast<int>(m_Candidates.size()));
		std::vector<int> order(m_Candidates.size());
		std::iota(order.begin(), order.end(), 0);
		std::stable_sort(order.begin(), order.end(), [this](int a, int b) { return m_Candidates[static_cast<size_t>(a)].Aptitude > m_Candidates[static_cast<size_t>(b)].Aptitude; });
		std::vector<int> picked;
		if (n > 0)
		{
			// as many women as men; the odd one out is of the sex of the best
			const bool bestFemale = m_Candidates[static_cast<size_t>(order.front())].Who.Female;
			int women = bestFemale ? n - n / 2 : n / 2, men = n - women;
			for (const int s : order)
			{
				int& quota = m_Candidates[static_cast<size_t>(s)].Who.Female ? women : men;
				if (quota > 0) { --quota; picked.push_back(s); }
			}
			for (const int s : order)   // (a pool short of one sex: the best of the rest)
				if (static_cast<int>(picked.size()) < n && std::find(picked.begin(), picked.end(), s) == picked.end()) picked.push_back(s);
		}
		std::sort(picked.begin(), picked.end());
		return picked;
	}

	std::vector<int> ColonyFounding::PartyOf(int n) const
	{
		return static_cast<int>(m_Picked.size()) == n ? m_Picked : DefaultPick(n);
	}

	void ColonyFounding::Seat(Tribe& tribe, const FoundingCandidate& c, bool skill)
	{
		const auto m = std::find_if(tribe.Members().begin(), tribe.Members().end(), [&](const TribeMember& x) { return x.Name == c.Name; });
		if (m == tribe.Members().end()) return;
		if (Person* p = tribe.People().Find(c.Name)) *p = c.Who;
		if (skill && !c.Skill.empty() && c.SkillLevel > 0.0f) m->Skills[c.Skill] = c.SkillLevel;
		if (m->Body) m->Body->SetGrowth(tribe.People().Growth(tribe.Config.Society, c.Name));
	}

	std::string ColonyFounding::Report() const
	{
		std::string s;
		for (const FoundingCandidate& c : m_Candidates)
		{
			const bool picked = std::find(m_Picked.begin(), m_Picked.end(), c.Slot) != m_Picked.end();
			char line[160];
			auto gene = [&c](const char* id) { const auto g = c.Who.Genes.find(id); return g == c.Who.Genes.end() ? 0.0 : static_cast<double>(g->second); };
			std::snprintf(line, sizeof line, "%d%s%s %s, %s, %d years: %s (%s %.0f); strength %.2f, dexterity %.2f, perception %.2f, endurance %.2f", c.Slot, picked ? " [x]" : "", c.Locked ? " [locked]" : "",
				c.Name.c_str(), c.Who.Female ? "woman" : "man", static_cast<int>(c.Who.Age), c.Trade.empty() ? "-" : c.Trade.c_str(), c.Skill.empty() ? "-" : c.Skill.c_str(), static_cast<double>(c.SkillLevel),
				gene("strength"), gene("dexterity"), gene("perception"), gene("endurance"));
			s += (s.empty() ? "" : " | ") + std::string(line);
		}
		return s;
	}

	std::string ColonyFounding::KitText(int n) const
	{
		std::string s;
		for (const auto& [item, count] : FoundingKit(m_Rules, n, true)) s += (s.empty() ? "" : ", ") + item + " " + std::to_string(count);
		return s;
	}
}
