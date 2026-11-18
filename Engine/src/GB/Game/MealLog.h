#pragma once
#include <map>
#include <string>

namespace GB
{
	struct DietRules;

	// T277 (documentation/01_diseno/ALIMENTACION.md §10.2 P-A10): what a clan's people ate, by food and by where it came from. Only counts:
	// nothing in the simulation reads it (GBSim --report prints it; the telemetry gets an "ate" event per meal, written by Tribe::Eat). Not
	// saved: it counts from the start of a run (or of a load).
	class MealLog
	{
	public:
		struct Tally
		{
			long Units = 0;
			double Nutrition = 0.0;   // what the meals sated (the hunger points of NeedName::Hunger)
		};

		// Where a meal came from: "bag" (what it carried), "pantry" (its household's), "store@<building>" (the store building it went to eat at)
		// or "store" (the clan's store, by the fire).
		static std::string Source(bool pantry, bool bag, const std::string& shed);
		// What a meal of `item` sates: what Tribe::Ate gives (the diet's nutrition of the food; `foodHunger` for one not listed, or with the
		// social life off).
		static float Nutrition(const DietRules& diet, bool social, int foodHunger, const std::string& item);

		void Record(const std::string& item, const std::string& source, float nutrition);
		const std::map<std::string, Tally>& ByItem() const { return m_ByItem; }
		const std::map<std::string, Tally>& BySource() const { return m_BySource; }
		const Tally& Total() const { return m_Total; }
		void Clear() { *this = MealLog(); }

	private:
		std::map<std::string, Tally> m_ByItem, m_BySource;
		Tally m_Total;
	};
}
