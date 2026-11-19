#pragma once
// T328 (informe D60: in the seed 7 a clan never tilled: it knew tillage and had no seed, and the wild grain was beyond its home ground): a clan
// of the AI that may till and holds no seed of a crop it may sow goes for wild seed farther than it gathers as usual.
//
// When. economy.farming.wildSeedReach > 0 (data; 0 = off, as before); not the player's clan in the colony (FarmZones::Player); a technology
// of the tree names the verb farm:till and the clan knows it (with no tree, or no verb in the data, nothing changes); a living member may farm
// (Tribe::CanFarm); and the clan holds no seed of any crop it may sow (FarmZones::Knows) in its stores, nor growing in a field (a sown plot
// gives its seed back at harvest).
//
// What the member does (one line in Tribe::Decide, before the agenda, so the night, the dusk and the pyramid of needs still rule it):
//   - with a seed of such a crop in its bag: it brings it home ("deposit", above the usual load), so the store gets it and FarmZones marks a field;
//   - else, when no other member is already on it (on its way to a seed node, gathering there, looking for one, or carrying seed home): it goes
//     to the node it remembers that gives most seed for the walk, within the reach of the camp, without the home ground's falloff ("gather",
//     the tribe's own: approach, claim, swings and deposit as any gather);
//   - and when it remembers none: it walks out to look for one ("explore", rings out to the reach, its own trip count turning the bearing),
//     with the tribe's wait after a search that found nothing (m_Searches).
// A node's seed is its item or its extra (a berry bush's pea seed at 5 %): what a gather gives of it on average weighs the choice.
//
// Stateless: what the clan holds, knows and does decides; nothing is saved. CPU only, deterministic (maps in name order, no dice).
#include "GB/AI/Utility.h"
#include <set>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;
	struct ResourceNodeDef;

	class WildSeeds
	{
	public:
		static constexpr float kScore = 0.95f;      // the trip for seed: above a trade's usual gather, below hunger, thirst and the pyramid
		static constexpr float kExplore = 0.7f;     // looking for a place of it
		static constexpr float kHome = 1.3f;        // bringing it home: above a full bag's deposit (1.1)
		static constexpr int kRings = 4;            // the searches go out to reach / kRings, 2 reach / kRings ... reach

		// The seeds of the crops the clan may sow (FarmZones::Knows), in id order.
		static std::set<std::string> Sowable(const Tribe& tribe);
		// The clan wants wild seed now (see the header): the rule is on, it may till, someone may farm, and it holds none.
		static bool Wanted(const Tribe& tribe);
		// What one gather of a node of `def` gives of `seeds` on average (0: none).
		static float Yield(const ResourceNodeDef& def, const std::set<std::string>& seeds);

		// ---- the tribe's hook (one line in Tribe::Decide)
		static void Options(Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& opts);
	};
}
