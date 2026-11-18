#pragma once
// T307 (C2 of documentation/01_diseno/MODO_COLONIA.md §4.3, "metas de existencias"): "keep N of <item>" (or of a tag: "Item.Food.Preserved")
// for the player's clan. What the clan has of it is what its store holds and its living members carry. While it has less than the goal the
// clan is asked for the rest:
//   - an item a recipe makes: a want of the production plan (Wants, from Tribe::Wants), so the planner posts the "make" of it once its inputs
//     are in the store and asks the gathers for the raw inputs it lacks (the same jobs, scores and execution as any other want);
//   - an item a node gives: the same want, which the planner leaves to the gathers (its raw shortage);
//   - meat, hide... (what a hunt gives): a "hunt" job for the nearest animal of the rules that gives it (Post).
// Each of those jobs is ranked by the person's table of priorities (WorkPriorities.h) like any other: making an axe is crafting, the meat is
// food. A goal that cannot be met says why in `game stock` (Status): it does not know how to make it, it has no station to make it at, it
// lacks an input nothing it knows gives, there is no animal to hunt or no way to get it.
//
// The tribe owns one (Tribe::Goals()) and calls it from one line in Tribe::Wants (TribeEconomy.cpp) and one in PostJobs (TribeWork.cpp). With
// no goal nothing here runs and nothing is saved ("stock"), so a run (GBSim, the clans of the AI) is the same as before. CPU only, deterministic.
#include "GB/AI/Utility.h"
#include "GB/Game/Production.h"
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;

	class StockGoals
	{
	public:
		static constexpr int kMost = 9999;
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers and the tests read it)
		};

		// Keep `n` (1..kMost) of an item or a tag ("Item." first). Refused: an unknown item or a tag no item carries.
		Reply Set(const Tribe& tribe, const std::string& what, int n);
		Reply Off(const std::string& what);
		const std::map<std::string, int>& All() const { return m_Goals; }

		// What the clan has of it (the store and the bags of the living).
		static int Have(const Tribe& tribe, const std::string& what);
		// The item a goal asks for: the item itself, or for a tag the best one the clan can come by ("" = none).
		static std::string ItemFor(const Tribe& tribe, const std::string& what);
		// "ok", "making", "gathering", "gathering stone", "making cordage", "hunting", "does not know how to make it", "no workbench to make it at",
		// "lacks flint", "no animal to hunt", "no way to get it".
		static std::string Status(const Tribe& tribe, const std::string& what, int goal);
		// "stone_axe 1 / 3 making; Item.Food.Preserved 0 / 10 no drying_rack to make it at" ("no stock goals" with none): `game stock`.
		std::string List(const Tribe& tribe) const;
		// The same, for `game status` ("" with no goal).
		std::string Line(const Tribe& tribe) const { return m_Goals.empty() ? std::string() : List(tribe); }

		// ---- the tribe's hooks
		// Tribe::Wants: one want for each goal short of its number.
		void Wants(const Tribe& tribe, std::vector<ProductionWant>& out) const;
		// PostJobs: a hunt for a goal only an animal gives.
		void Post(Tribe& tribe) const;
		// Decide: the "make" of a goal already met is not started again (the board drops the job at its next refresh, seconds later: without
		// this a maker faster than the refresh made one or two past the goal).
		void Options(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options) const;

		bool Empty() const { return m_Goals.empty(); }
		void Clear() { m_Goals.clear(); }

		// Saving (the tribe's "stock" section, only written when there is a goal).
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		std::map<std::string, int> m_Goals;   // item or tag -> units to keep
	};
}
