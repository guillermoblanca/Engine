#include "GB/Game/FoodStock.h"
#include "GB/Game/MealLog.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Spoilage.h"
#include <algorithm>
#include <utility>
#include <vector>

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

	// T360 (N4): a unit counts only if it lasts until its turn. The food is eaten by what goes off soonest; a stack whose turn comes when its
	// life (in the store, at the rate the store and the season age it) is over counts only for what is eaten before (1000 berries about to
	// go off were 1000 meals of the winter's reserve). What keeps counts whole, as before.
	float FoodStock::Days(const Tribe& tribe)
	{
		int alive = 0;
		for (const TribeMember& m : tribe.Members()) if (!m.Dead) ++alive;
		if (alive == 0) return 0.0f;
		const float rate = PerDay(tribe.Config.Needs) * static_cast<float>(alive);   // meals the clan eats a day
		float ageing = tribe.Config.Econ.Main().SpoilRate * tribe.StoreKeep();
		if (const auto f = tribe.Config.Econ.Spoilage.Seasons.find(SeasonClock::Name(tribe.CurrentSeason())); f != tribe.Config.Econ.Spoilage.Seasons.end()) ageing *= f->second;
		const float day = std::max(1.0f, tribe.Config.Needs.DaySeconds);
		const ItemLibrary* items = tribe.Items();
		std::vector<std::pair<float, float>> stacks;   // (days left, meals)
		auto add = [&](const Inventory& inv)
		{
			for (const ItemSlot& s : inv.Stacks())
			{
				if (s.Count <= 0 || !tribe.IsFood(s.Id)) continue;
				const float left = items ? LifeLeft(*items, s) : 1e30f;
				const float days = left >= 1e29f || ageing <= 0.0f ? 1e30f : left / (ageing * day);
				stacks.push_back({ days, static_cast<float>(s.Count) * Meals(tribe, s.Id) });
			}
		};
		add(tribe.Store());
		for (const TribeMember& m : tribe.Members()) if (!m.Dead && m.Body) add(m.Body->Bag());
		std::stable_sort(stacks.begin(), stacks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		float eaten = 0.0f;
		for (const auto& [left, meals] : stacks)
		{
			if (left >= 1e29f) { eaten += meals; continue; }
			eaten += std::clamp(left * rate - eaten, 0.0f, meals);   // (what is eaten of it before it goes off)
		}
		return eaten / rate;
	}

	float FoodStock::UnitsFor(const Tribe& tribe, const std::string& item, float days, float mouths)
	{
		const float meals = Meals(tribe, item);
		return days * PerDay(tribe.Config.Needs) * mouths / (meals > 0.0f ? meals : 1.0f);
	}
}
