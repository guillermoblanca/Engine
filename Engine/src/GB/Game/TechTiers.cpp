#include "GB/Game/TechTiers.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		const std::vector<TierStep>* Steps(const Tribe& t)
		{
			const TechTree* tree = t.Knowledge().Techs();
			return tree && !tree->Rules.Tiers.empty() ? &tree->Rules.Tiers : nullptr;
		}

		int Living(const Tribe& t)
		{
			return static_cast<int>(std::count_if(t.Members().begin(), t.Members().end(), [](const TribeMember& m) { return !m.Dead; }));
		}

		// How many times a period of `length` seconds ended between the clan's founding (the world's clock less the clan's own) and now.
		int Ended(const Tribe& t, double length)
		{
			if (!(length > 0.0)) return 0;
			const double now = t.WorldSeconds();
			const double founded = std::max(0.0, now - t.Time());
			return static_cast<int>(std::floor(now / length) - std::floor(founded / length));
		}

		std::string Words(const TierGoal& g)
		{
			if (g.Type == "building") return g.Building;
			if (g.Type == "population") return "people";
			return g.Type;
		}

		bool StepMet(const Tribe& t, const TierStep& step)
		{
			int met = 0;
			for (const TierGoal& g : step.Goals) if (TechTiers::Met(t, g)) ++met;
			const int need = step.Need > 0 ? step.Need : static_cast<int>(step.Goals.size());
			return met >= need;
		}
	}

	int TechTiers::Open(const Tribe& tribe)
	{
		return tribe.Knowledge().OpenTier();
	}

	int TechTiers::Missing(const Tribe& tribe, const std::string& tech)
	{
		const int tier = tribe.Knowledge().TierOf(tech);
		return tier > Open(tribe) ? tier : 0;
	}

	bool TechTiers::Met(const Tribe& t, const TierGoal& g, int* value)
	{
		int v = 0;
		int target = g.Count;
		if (g.Type == "population") v = Living(t);
		else if (g.Type == "building" || g.Type == "buildings")
		{
			std::string name = g.Building;   // the sites name the building by its def's name
			if (g.Type == "building")
			{
				const std::map<std::string, BuildingDef>* defs = t.Buildings();
				const BuildingDef* def = nullptr;
				if (defs)
					if (const auto it = defs->find(g.Building); it != defs->end()) def = &it->second;
				if (!def) { if (value) *value = 0; return false; }   // a building this world does not have: never met
				name = def->Name;
			}
			for (const Tribe::SettlementSite& s : t.SettlementSites())
				if (s.State.Done && (g.Type == "buildings" || s.Building == name)) ++v;
		}
		else if (g.Type == "techs") v = static_cast<int>(t.Knowledge().Clan().size());
		else if (g.Type == "fire") { v = t.HasFire() ? 1 : 0; target = 1; }
		else if (g.Type == "seasons") v = Ended(t, static_cast<double>(t.Config.Seasons.SeasonSeconds));
		else if (g.Type == "winters") v = Ended(t, 4.0 * static_cast<double>(t.Config.Seasons.SeasonSeconds));   // (the clock starts in spring: a year ends with a winter)
		if (value) *value = v;
		return v >= target;
	}

	int TechTiers::Reached(const Tribe& tribe)
	{
		const std::vector<TierStep>* steps = Steps(tribe);
		if (!steps) return kNoTierGate;
		int reached = 0;
		for (const TierStep& step : *steps)
		{
			if (step.Tier > reached + 1) break;
			if (step.Tier <= tribe.Knowledge().OpenTier()) { reached = step.Tier; continue; }   // (open stays open: no need to meet it again)
			if (!StepMet(tribe, step)) break;
			reached = step.Tier;
		}
		return reached;
	}

	void TechTiers::Update(Tribe& tribe, std::vector<std::string>& goals)
	{
		if (!Steps(tribe)) return;
		Raise(tribe);
		const ClanKnowledge& k = tribe.m_Knowledge;
		goals.erase(std::remove_if(goals.begin(), goals.end(), [&](const std::string& id) { return !k.TierOpen(id); }), goals.end());
	}

	void TechTiers::Raise(Tribe& tribe)
	{
		if (!Steps(tribe)) return;
		ClanKnowledge& k = tribe.m_Knowledge;
		const int was = k.OpenTier();
		if (const int now = Reached(tribe); k.RaiseTier(now))
			for (int n = was + 1; n <= now; ++n)
			{
				const std::string text = "opens tier " + std::to_string(n) + " of the tree of technologies";
				tribe.TellClan(text);
				tribe.EmitEvent("tier", std::string(), std::string(), std::to_string(n), text);
			}
	}

	bool TechTiers::Refresh(Tribe& tribe)
	{
		const int was = Open(tribe);
		Raise(tribe);
		tribe.UpdateGoals();
		return Open(tribe) != was;
	}

	std::string TechTiers::Text(const Tribe& tribe)
	{
		const std::vector<TierStep>* steps = Steps(tribe);
		if (!steps) return "no tiers";
		const int open = Open(tribe);
		std::string s = "tier " + std::to_string(open) + " open";
		for (const TierStep& step : *steps)
		{
			if (step.Tier != open + 1) continue;
			int met = 0;
			std::string parts;
			for (const TierGoal& g : step.Goals)
			{
				int v = 0;
				if (Met(tribe, g, &v)) ++met;
				const int target = g.Type == "fire" ? 1 : g.Count;
				parts += (parts.empty() ? "" : ", ") + Words(g) + " " + std::to_string(std::min(v, target)) + "/" + std::to_string(target);
			}
			const int need = step.Need > 0 ? step.Need : static_cast<int>(step.Goals.size());
			s += "; tier " + std::to_string(step.Tier) + ": " + std::to_string(met) + "/" + std::to_string(need) + " goals (" + parts + ")";
		}
		return s;
	}
}
