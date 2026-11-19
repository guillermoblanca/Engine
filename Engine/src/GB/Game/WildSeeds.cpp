// T328: a clan that may till and has no seed goes for wild seed past its home ground (see WildSeeds.h).
#include "GB/Game/WildSeeds.h"
#include "GB/Game/FarmZones.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Transplant.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kMinYield = 0.25f;   // a node worth the trip gives at least this much seed a gather (not a berry bush's 5 % pea)
		constexpr float kGolden = 2.3999632f;

		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }

		// The kind of the node a member works or walks to ("" when it is not one it remembers).
		std::string KindOf(const TribeMember& m, const std::string& node)
		{
			const MemoryEntry* e = m.Mind.Memory.Find(node);
			return e ? e->Kind : std::string();
		}
	}

	std::set<std::string> WildSeeds::Sowable(const Tribe& tribe)
	{
		std::set<std::string> out;
		for (const auto& [id, crop] : tribe.Config.Econ.Crops) if (FarmZones::Knows(tribe, id)) out.insert(crop.Seed);
		return out;
	}

	float WildSeeds::Yield(const ResourceNodeDef& def, const std::set<std::string>& seeds)
	{
		float y = 0.0f;
		if (seeds.count(def.Item)) y += static_cast<float>(std::max(1, def.PerGather));
		if (!def.ExtraItem.empty() && seeds.count(def.ExtraItem)) y += static_cast<float>(def.ExtraPerGather) * def.ExtraChance;
		return y;
	}

	bool WildSeeds::Wanted(const Tribe& tribe)
	{
		if (tribe.Config.Econ.Farming.WildSeedReach <= 0.0f || tribe.m_Zones.Player()) return false;   // (the player's clan in the colony: its people do what the player says)
		// the verb farm:till named by the tree and known (no tree, or no verb in the data: off, so an old setup runs as before)
		const TechTree* tree = tribe.Knowledge().Techs();
		const std::string verb = TechVerbs::Ruling(tree, "farm", "till");
		if (verb.empty() || !TechVerbs::Knows(tribe, verb)) return false;
		bool can = false;
		for (const TribeMember& m : tribe.m_Members) can = can || (!m.Dead && m.Body && !m.Player && tribe.CanFarm(m));
		if (!can) return false;
		const std::set<std::string> seeds = Sowable(tribe);
		if (seeds.empty()) return false;
		for (const std::string& s : seeds) if (tribe.Stored(s) > 0) return false;
		for (const Field& f : tribe.m_Econ.Fields)
			if (f.Count("sown") + f.Count("sprout") + f.Count("grown") > 0) return false;   // (what grows gives its seed back)
		return true;
	}

	void WildSeeds::Options(Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& opts)
	{
		if (m.Player || m.Dead || !m.Body || !Wanted(tribe)) return;
		const float reach = tribe.Config.Econ.Farming.WildSeedReach;
		const std::set<std::string> seeds = Sowable(tribe);
		const glm::vec2 camp = tribe.Config.Economy.Camp;
		const glm::vec2 pos = tribe.Where(m);
		char note[160];

		// it carries some: home with it
		int carried = 0;
		for (const std::string& s : seeds) carried += m.Body->Bag().Count(s);
		if (carried > 0)
		{
			UtilityOption o;
			o.Kind = "deposit";
			o.Where = tribe.CampSpot(m);
			o.Target = tribe.DepositPlace(m, o.Where);
			if (o.Target == "-") return;
			o.Score = kHome;
			Factor(o, "seed for the fields", kHome);
			std::snprintf(note, sizeof note, "%d wild seed in the bag and none in the store", carried);
			o.Note = note;
			opts.push_back(o);
			return;
		}

		// one at a time: another member already on it
		for (const TribeMember& o : tribe.m_Members)
		{
			if (&o == &m || o.Dead || !o.Body) continue;
			for (const std::string& s : seeds) if (o.Body->Bag().Count(s) > 0) return;
			std::string kind;
			if (o.Job == "gather" || o.Job == "approach") kind = KindOf(o, o.Detail);
			else if (o.Job == "explore") kind = o.Detail;
			const auto def = kind.empty() ? tribe.m_Defs.end() : tribe.m_Defs.find(kind);
			if (def != tribe.m_Defs.end() && Yield(def->second, seeds) >= kMinYield) return;
		}

		// the node it remembers that gives most seed for the walk, within the reach of the camp
		const Inventory& bag = m.Body->Bag();
		const MemoryEntry* best = nullptr;
		float bestCost = 0.0f, bestYield = 0.0f;
		std::string bestKind;   // (also the kind to look for when it knows none)
		float kindYield = 0.0f;
		for (const auto& [kind, def] : tribe.m_Defs)
		{
			const float y = Yield(def, seeds);
			if (y < kMinYield || !tribe.m_Knowledge.CanGather(m.Name, kind)) continue;
			if (!def.RequiresTag.empty() && bag.CountTagged(*tribe.m_Items, def.RequiresTag) == 0) continue;
			if (y > kindYield) { kindYield = y; bestKind = kind; }
		}
		if (bestKind.empty()) return;
		for (const auto& [name, e] : m.Mind.Memory.All())
		{
			const auto def = tribe.m_Defs.find(e.Kind);
			if (def == tribe.m_Defs.end() || e.Amount == 0 || glm::distance(e.Pos, camp) > reach) continue;
			const float y = Yield(def->second, seeds);
			if (y < kMinYield || !tribe.m_Knowledge.CanGather(m.Name, e.Kind) || tribe.m_Claims.HeldByOther(name, m.Name)) continue;
			if (!Transplant::Held(tribe, name, e.Pos).empty()) continue;   // T366: not on the land another clan holds (taking there is theft: war in seed 7)
			if (!def->second.RequiresTag.empty() && bag.CountTagged(*tribe.m_Items, def->second.RequiresTag) == 0) continue;
			const float cost = (glm::distance(pos, e.Pos) + 1.0f) / y;
			if (!best || cost < bestCost) { best = &e; bestCost = cost; bestYield = y; }   // (name order breaks a tie)
		}
		if (best)
		{
			UtilityOption o;
			o.Kind = "gather";
			o.Target = best->Name;
			o.Where = best->Pos;
			const float far = 0.5f + 0.5f * Falloff(glm::distance(pos, best->Pos), reach);
			o.Score = kScore * far;
			Factor(o, "no seed to sow", kScore);
			Factor(o, "distance", far);
			std::snprintf(note, sizeof note, "%s: wild seed (%.2f a gather), the clan has none to sow", best->Kind.c_str(), static_cast<double>(bestYield));
			o.Note = note;
			opts.push_back(o);
			return;
		}

		// it knows of none: it goes to look, out to the reach (the tribe's wait after a search that found nothing holds)
		const std::string& item = tribe.m_Defs.at(bestKind).Item;
		if (const auto s = tribe.m_Searches.find(item); s != tribe.m_Searches.end() && s->second.second > tribe.m_Time) return;
		const int trip = m.Mind.ExploreCount;
		const float radius = reach * static_cast<float>(trip % kRings + 1) / static_cast<float>(kRings);
		const float angle = kGolden * static_cast<float>(trip) + 1.7f * static_cast<float>(tribe.IndexOf(m));
		UtilityOption o;
		o.Kind = "explore";
		o.Target = bestKind;
		// not into a lake or up a cliff, nor into the land another clan holds (T366: what it finds there it would take, and that is theft): the
		// next bearing; none: it does not go
		bool found = false;
		for (int k = 0; k < 8 && !found; ++k)
		{
			o.Where = camp + glm::vec2(std::cos(angle + 0.7f * static_cast<float>(k)), std::sin(angle + 0.7f * static_cast<float>(k))) * radius;
			found = m.Body->CanReach(o.Where) && Transplant::Held(tribe, std::string(), o.Where).empty();
		}
		if (!found) return;
		o.Score = kExplore;
		Factor(o, "no seed to sow", kExplore);
		o.Note = "knows no " + bestKind + " within " + std::to_string(static_cast<int>(reach)) + " m and the clan has no seed to sow";
		opts.push_back(o);
	}
}
