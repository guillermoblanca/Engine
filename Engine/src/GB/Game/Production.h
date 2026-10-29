#pragma once
#include "GB/Gameplay/Items.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// Production chains (H18): from what a clan wants to the recipes it has to run, intermediates included. Pure CPU, deterministic (wants in the
	// order given, recipes by id), no scene: the tribe feeds it and posts the result on its job board (TribeEconomy.cpp).
	//
	// A want is an item and a number of units (the tool of a trade a member lacks, a better tool once the clan knows it, a building material, a
	// stock target of an intermediate such as cordage). For each one, what the stock already holds is set aside first (a unit counts for one want
	// only); what is missing is made by the first recipe (by id) that makes the item, that the clan knows and whose station it has, in as many
	// batches as it takes; and each input of that recipe becomes a want of its own, one level deeper, up to `Depth`. An input no recipe makes is a
	// raw shortage (it must be gathered). A want whose only recipes the clan does not know yet is not made, but with `Experiment` its inputs are
	// still wanted (raw or made), so the clan has at hand what it needs to try it and work the technology out (E5): this is how a clan that knows
	// cordage makes it so that flint knapping, which needs cordage, can be found by experimenting.

	struct ProductionWant
	{
		std::string Item;
		int Units = 1;
		std::string Why;                         // for people: "Biel lacks a flint_axe", "keeps 2 cordage"
		bool Make = true;                        // false: someone else makes the item itself (a member its own tool): only its inputs are planned
	};

	struct ProductionStep
	{
		std::string Recipe;
		std::string Output;
		int Batches = 1;                         // times the recipe runs (Batches x OutputCount units)
		int Depth = 0;                           // 0 = it makes a want, 1 = an input of one, ...
		bool Ready = false;                      // the stock holds every input of one batch now (it can start)
		std::string Why;                         // "cordage for flint_axe (Biel lacks a flint_axe)"
	};

	struct ProductionPlan
	{
		std::vector<ProductionStep> Steps;       // in the order found (a want, then what it needs)
		std::map<std::string, int> Raw;          // raw shortages: item -> units to gather
		std::vector<std::string> Blocked;        // wants nothing can make now ("flint_axe: does not know how", "dried_berry: no drying_rack")
	};

	struct ProductionQuery
	{
		int Depth = 3;                           // the deepest level of inputs planned (0 = only the wants themselves)
		bool Experiment = true;                  // also want the inputs of a recipe the clan does not know yet (to try it)
		int MaxBatches = 3;                      // most batches one step asks for at once (the rest waits for the next plan)
		std::function<bool(const Recipe&)> Known;        // the clan knows how to make it (nullptr = it knows all)
		std::function<bool(const Recipe&)> Learnable;    // someone could find it now by trying (its technology's requirements are known; nullptr = yes):
		                                                 // only then are its inputs wanted to try it (smelting's ore waits for pottery)
		std::function<bool(const std::string&)> Station; // the clan has that station ("" is always had; nullptr = it has them all)
	};

	// Plans the wants against `stock` (item -> units at hand). Deterministic for the same input.
	ProductionPlan PlanProduction(const std::vector<ProductionWant>& wants, const std::map<std::string, int>& stock, const RecipeBook& recipes, const ProductionQuery& query);

	// The recipes that make `item`, by id.
	std::vector<const Recipe*> RecipesFor(const RecipeBook& recipes, const std::string& item);

	// For people: one line per step, then the raw shortages and what is blocked.
	std::string PlanReport(const ProductionPlan& plan);
}
