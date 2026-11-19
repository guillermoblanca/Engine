#pragma once
#include "GB/Gameplay/Items.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// T358 (N1) and T300 (N3, P-A8) of documentation/01_diseno/CIERRE_AGRICULTURA_2026-11-19.md §4.3: which recipes keep food and which cook it,
	// and how much of each the clan of the AI asks for. Pure CPU, deterministic (recipes in id order, ties to the lowest id), no scene and no
	// state: the tribe fills a Query from its store, its diet and its stations, and turns the answer into wants and steps of its production plan
	// (Tribe::Wants and Tribe::PlanNow, TribeEconomy.cpp). Nothing is named: every rule reads the shelf lives of the items, the nutrition and
	// group of the diet and the recipes of the data.
	//
	//   Keeps   (N1, E6) a recipe keeps a food when its output lasts longer than the food and no other perishable input lasts longer than the
	//           output (two lentils, good for a month, are not spent to save one leaf that is good for a day).
	//   Cooks   a recipe cooks when its output is food that sates at least what its food inputs sate together (cooked is worth more).
	//   IsMeal  (N3) a cooked meal: it cooks and some perishable input lasts longer than the output (grain into parched grain, porridge, bread;
	//           lentils into pottage). Smoked or dried food is kept, not cooked: it does not count as a meal.
	class ClanCooking
	{
	public:
		struct Query
		{
			const ItemLibrary* Items = nullptr;
			const RecipeBook* Recipes = nullptr;
			std::function<bool(const std::string&)> IsFood;           // nullptr: an item tagged Item.Food or below
			std::function<float(const std::string&)> Nutrition;       // what a unit sates (the diet); nullptr: every food the same
			std::function<std::string(const std::string&)> Group;     // the diet's group of a food; nullptr or "": the food is a group of its own
			std::function<bool(const Recipe&)> Known;                 // nullptr = the clan knows them all
			std::function<bool(const std::string&)> Station;          // nullptr = it has every station ("" is always had)
			int MaxBatches = 3;                                       // most batches one recipe is asked for at once (ProductionRules::MaxBatches)
		};

		// What the clan asks to cook: `Batches` of `Recipe`. `Chain`: one of its inputs is not in the store and is made first by the recipe the
		// production planner would pick (flour at the quern for bread), so it goes to the planner as a want of its output; else every input is in
		// the store and it is a step that can start now.
		struct Cook
		{
			const Recipe* Use = nullptr;
			int Batches = 0;
			bool Chain = false;
			std::string Why;
		};

		static float Life(const ItemDef& d);   // its shelf life, s; what never spoils: a very large number
		// (`life`: what the food has left, s; below 0, its shelf life)
		static bool Keeps(const Query& q, const Recipe& r, const std::string& food, float life = -1.0f);
		static bool Cooks(const Query& q, const Recipe& r);
		static bool IsMeal(const Query& q, const Recipe& r);
		// The recipe the production planner (GB/Game/Production.h) uses to make `item`: the first by id that the clan knows and has the station of.
		static const Recipe* PlannerPick(const Query& q, const std::string& item);
		// The ids of the outputs of every meal recipe of the book (known or not): what counts as a cooked meal in hand.
		static std::vector<std::string> MealFoods(const Query& q);

		// N3: the batches that bring the cooked meals in hand (`held`: meal food -> units) up to `wanted` units, from what `stock` holds (what is
		// asked for is taken out of it). Each batch goes to the group the clan holds fewest meals of (variety), then to the meal that keeps
		// longest (it is eaten before it goes off), then the one that sates most, then the lowest id.
		static std::vector<Cook> Meals(const Query& q, std::map<std::string, int>& stock, std::map<std::string, int>& held, int wanted);

		// N3: raw food about to go off (`soon`: food -> units with less than economy.production.meals.rawSoon s left, and the least life left of
		// them) is cooked by a recipe that keeps it (Keeps, with the life left in place of the shelf life) and cooks; of several, the one whose
		// output keeps longest, then sates most. What is asked for is taken out of `stock`, and a meal it makes is added to `held`.
		struct Soon
		{
			int Units = 0;
			float Left = 0.0f;
		};
		static std::vector<Cook> RawSoon(const Query& q, std::map<std::string, int>& stock, std::map<std::string, int>& held, const std::map<std::string, Soon>& soon);

		// T300 (the second pass, after the battery of 12 days: 48 parched grain made, 2 eaten): what is eaten first from `inv` when the clan keeps
		// cooked meals. Food was eaten by what goes off soonest, so berries and greens always came before the cooked meals and the grain and
		// lentils, which keep, were never eaten. Now: when nothing in `inv` has less than `rawSoon` s left, the cooked meal (IsMeal's output) that
		// goes off soonest; "" when something is about to go off or there is no cooked meal (the caller eats as before: the soonest to go off).
		static std::string EatFirst(const Query& q, const Inventory& inv, float rawSoon);
	};
}
