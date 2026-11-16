#pragma once
#include <map>
#include <string>
#include <vector>

// T238 + T239 (documentation/01_diseno/MODO_DIOS_DECISIONES.md §3.b): a push from outside the clan on what its people choose, generic so the
// player mode and the ambitions (D26) can use it too. The god's director (WorldDirector, DirectorFocus.cpp) writes it on every update; the
// tribe only reads it, in two one-line hooks: Tribe::Decide (the focus, ApplyFocusBias, after the pyramid and before the priority graphs) and
// Tribe::ReviewRoles (the calling). It is not saved with the tribe: whoever writes it saves its own state and writes it again after a load.
// Empty, it changes nothing (no option is touched, no factor is added).
namespace GB
{
	class Tribe;
	struct TribeMember;
	struct UtilityOption;

	// The focus of a clan: the options of one group weigh more, person by person (never less than they did).
	struct TribeFocusBias
	{
		std::string Group;                         // "food", "build", "lore", "defense", "faith" ("" = no focus)
		std::vector<std::string> Kinds;            // the utility options of the group
		bool FoodGather = false;                   // a "gather" whose node gives food is of the group
		bool OtherGather = false;                  // a "gather" whose node gives anything else is
		std::map<std::string, float> Mult;         // member -> m(p) >= 1 (the director computes it once a day)
		bool Active() const { return !Group.empty() && !Mult.empty(); }
	};

	// A calling of one person to a trade: in the review of the trades its skill in it counts Skill points more.
	struct TribeCalling
	{
		std::string Role;
		float Skill = 0.0f;
		int Order = 0;                             // the oldest goes first (one change per review)
	};

	struct TribeBias
	{
		TribeFocusBias Focus;
		std::map<std::string, TribeCalling> Callings;   // member -> its calling
		float CallingSkill(const std::string& member, const std::string& role) const
		{
			const auto it = Callings.find(member);
			return it != Callings.end() && it->second.Role == role ? it->second.Skill : 0.0f;
		}
	};

	// The focus hook (Tribe::Decide): multiplies the options of the group by the member's m(p). Only the public API of Tribe.
	void ApplyFocusBias(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options);
	// The calling hook (Tribe::ReviewRoles): the member with the oldest calling whose trade it can take now (`canTake(member, role)`: the
	// review's own gates, its tool and the last hands of an essential trade); null when none. The calling is honoured whatever the skills say
	// (GBSim: +30 points alone left 2 of 3 callings of seeds 5/7/77 under SkillSwitch); its points still count in the rest of the review, so a
	// called member keeps the trade against the review's later moves.
	template <class Member, class CanTake>
	Member* PickCalling(const TribeBias& bias, const std::vector<Member*>& flexible, CanTake canTake)
	{
		Member* best = nullptr;
		int order = 0;
		for (Member* m : flexible)
		{
			const auto it = bias.Callings.find(m->Name);
			if (it == bias.Callings.end() || it->second.Role == m->Role || (best && it->second.Order >= order) || !canTake(*m, it->second.Role)) continue;
			best = m;
			order = it->second.Order;
		}
		return best;
	}
	// A member whose calling is the trade it has: the review does not move it away while the calling lasts.
	inline bool HeldByCalling(const TribeBias& bias, const std::string& member, const std::string& role) { return bias.CallingSkill(member, role) > 0.0f; }

	// Whether `gather` of this node gives food (the same test as Tribe::MaslowOptions: the member's memory of the node, its def, IsFood).
	bool GatherIsFood(const Tribe& tribe, const TribeMember& m, const std::string& node);
}
