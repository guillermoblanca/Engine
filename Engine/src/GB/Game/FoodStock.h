#pragma once
#include <string>

namespace GB
{
	struct NeedsConfig;
	class Inventory;
	class Tribe;

	// T346 (documentation/01_diseno/ALIMENTACION.md §16.3 P-A1): the food a clan has in hand, measured by what it sates, not by its units. A
	// unit of food sates its diet's nutrition (MealLog::Nutrition: SocialRules::Diet, NeedsConfig::FoodHunger for a food not listed or with the
	// social life off). It is counted in plain meals (what FoodHunger sates: a unit of grain at 12 is 12/25 of one), and a member eats PerDay of
	// them a day (NeedName::Hunger empties 100 in HungerSeconds). So forty units of raw grain are fewer days than forty of cooked meat (45). With
	// the social life off, or with foods that sate FoodHunger, it is the old count in units, to the bit. Only food counts (Tribe::IsFood: water
	// and drinks do not). It only reads; nothing here is kept.
	class FoodStock
	{
	public:
		// The plain meals a member eats in a day.
		static float PerDay(const NeedsConfig& needs);
		// What a unit of `item` sates for the clan (MealLog::Nutrition with its rules), and that in plain meals.
		static float Nutrition(const Tribe& tribe, const std::string& item);
		static float Meals(const Tribe& tribe, const std::string& item);
		// The plain meals of the food in `inv`, all of it.
		static float MealsIn(const Tribe& tribe, const Inventory& inv);
		// Tribe::FoodDays: the food of the store and of the bags of the living over what the living eat a day (0 with nobody alive).
		static float Days(const Tribe& tribe);
		// The units of `item` that feed `mouths` for `days`.
		static float UnitsFor(const Tribe& tribe, const std::string& item, float days, float mouths);
	};
}
