#pragma once
// PROTOTYPE of TL4 (documentation/01_diseno/TALLERES.md: which recipes each workshop makes), 2026-11-20, to try in the colony. For each standing
// building of the clan with a station, each recipe of that station is allowed (auto: as before), forbidden, or "keep N" (allowed, and a stock goal
// of N of its product: StockGoals, which is by item and not by building). The production planner skips a recipe only when every standing building
// of its station forbids it (then it tries another recipe for the same thing, as when a station is missing); with no building of its station (the
// camp's improvised one) nothing is forbidden. Who makes it goes to the nearest building of its station that allows it, and to the work point of
// a module's slot (HouseModules) when that building is a module.
//
//   game recipe <building>                         its recipes and their state
//   game recipe <building> <recipe> on|off|keep <n>
//
// The tribe owns one (Tribe::RecipeChoices()); its hooks are one predicate of the planner's query, the meals' check and the place a maker goes
// to (TribeEconomy.cpp). With no choice nothing changes and nothing is saved ("recipeChoices"), so the AI and GBSim are as before. CPU only.
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct Recipe;

	class RecipeChoices
	{
	public:
		static constexpr int kOff = -1;   // a stored state: -1 forbidden, n > 0 keep n (auto is not stored)
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English
		};

		// The recipes made at a station (by id).
		static std::vector<const Recipe*> RecipesOf(const Tribe& tribe, const std::string& station);
		static bool Known(const Tribe& tribe, const Recipe& r);   // some living member knows how to make it (Tribe::ClanKnows)
		// 0 auto, kOff forbidden, n keep n
		int State(const std::string& building, const std::string& recipe) const;
		Reply Set(Tribe& tribe, const std::string& building, const std::string& recipe, const std::string& mode, int n = 0);
		std::string List(const Tribe& tribe, const std::string& building) const;
		void Forget(Tribe& tribe, const std::string& building);   // the building is gone: its choices (and the goals they set) go

		// ---- the tribe's hooks
		// The planner may use the recipe: some standing building of its station allows it (or there is none).
		bool Allows(const Tribe& tribe, const Recipe& r) const;
		// Where to make it: the nearest standing building of its station that allows it (its module's work point, else its front). False (and
		// `at` untouched) with no choice and no module, as before.
		bool Spot(const Tribe& tribe, const Recipe& r, const glm::vec2& from, glm::vec2& at) const;

		bool Empty() const { return m_Choices.empty(); }
		void Clear() { m_Choices.clear(); }
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		std::map<std::string, std::map<std::string, int>> m_Choices;   // building -> recipe -> kOff or n
	};
}
