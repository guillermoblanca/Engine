#include "GB/Game/FoodStock.h"
#include "GB/Game/MealLog.h"
#include "GB/Game/Tribe.h"
#include <algorithm>

namespace GB
{
	// (The arithmetic is the old count in units, times a ratio that is exactly 1 for a plain meal: a clan without the diet does what it did.)
	float FoodStock::PerDay(const NeedsConfig& needs)
	{
		return (needs.DaySeconds / std::max(needs.HungerSeconds, 1.0f)) * (100.0f / static_cast<float>(std::max(needs.FoodHunger, 1)));
	}

	float FoodStock::Nutrition(const Tribe& tribe, const std::string& item)
	{
		return MealLog::Nutrition(tribe.Config.Social.Diet, tribe.Config.Social.On, tribe.Config.Needs.FoodHunger, item);
	}

	float FoodStock::Meals(const Tribe& tribe, const std::string& item)
	{
		return Nutrition(tribe, item) / static_cast<float>(std::max(tribe.Config.Needs.FoodHunger, 1));
	}

	float FoodStock::MealsIn(const Tribe& tribe, const Inventory& inv)
	{
		float meals = 0.0f;
		for (const ItemSlot& s : inv.Stacks())
			if (s.Count > 0 && tribe.IsFood(s.Id)) meals += static_cast<float>(s.Count) * Meals(tribe, s.Id);
		return meals;
	}

	float FoodStock::Days(const Tribe& tribe)
	{
		float meals = MealsIn(tribe, tribe.Store());
		int alive = 0;
		for (const TribeMember& m : tribe.Members())
		{
			if (m.Dead) continue;
			++alive;
			if (m.Body) meals += MealsIn(tribe, m.Body->Bag());
		}
		if (alive == 0) return 0.0f;
		return meals / (PerDay(tribe.Config.Needs) * static_cast<float>(alive));
	}

	float FoodStock::UnitsFor(const Tribe& tribe, const std::string& item, float days, float mouths)
	{
		const float meals = Meals(tribe, item);
		return days * PerDay(tribe.Config.Needs) * mouths / (meals > 0.0f ? meals : 1.0f);
	}
}
