#include "GB/Game/Production.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>

namespace GB
{
	std::vector<const Recipe*> RecipesFor(const RecipeBook& recipes, const std::string& item)
	{
		std::vector<const Recipe*> out;
		for (const std::string& id : recipes.Ids())   // (Ids is in id order)
			if (const Recipe* r = recipes.Find(id); r && r->Output == item) out.push_back(r);
		return out;
	}

	namespace
	{
		struct Planner
		{
			const RecipeBook& Recipes;
			const ProductionQuery& Query;
			std::map<std::string, int> Avail;
			ProductionPlan Plan;

			bool Known(const Recipe& r) const { return !Query.Known || Query.Known(r); }
			bool HasStation(const std::string& s) const { return s.empty() || !Query.Station || Query.Station(s); }

			void Blocked(const std::string& text)
			{
				if (std::find(Plan.Blocked.begin(), Plan.Blocked.end(), text) == Plan.Blocked.end()) Plan.Blocked.push_back(text);
			}

			// Wants `units` of `item` at `depth`: sets aside what is at hand, then makes the rest (and wants its inputs one level deeper).
			void Want(const std::string& item, int units, int depth, const std::string& why, bool emit = true)
			{
				int& have = Avail[item];
				const int take = std::min(have, units);
				have -= take;
				units -= take;
				if (units <= 0) return;
				const std::vector<const Recipe*> makers = RecipesFor(Recipes, item);
				if (makers.empty()) { Plan.Raw[item] += units; return; }   // nothing makes it: it is gathered
				const Recipe* use = nullptr;
				const Recipe* unknown = nullptr;
				std::string station;
				for (const Recipe* r : makers)
				{
					if (!HasStation(r->Station)) { if (station.empty()) station = r->Station; continue; }
					if (!Known(*r)) { if (!unknown) unknown = r; continue; }
					use = r;
					break;
				}
				if (!use)
				{
					if (unknown)
					{
						Blocked(item + ": does not know how to make it (" + unknown->Id + ")");
						// to try it (E5) the clan needs one batch of its inputs at hand
						if (Query.Experiment && depth < Query.Depth && (!Query.Learnable || Query.Learnable(*unknown)))
							for (const auto& [input, n] : unknown->Inputs) Want(input, n, depth + 1, input + " to try " + unknown->Id);
					}
					else Blocked(item + ": no " + station + " to make it at");
					return;
				}
				const int per = std::max(1, use->OutputCount);
				const int batches = std::clamp((units + per - 1) / per, 1, std::max(1, Query.MaxBatches));
				// ready: one batch of every input is at hand now (before this step's inputs are set aside below)
				bool ready = true;
				for (const auto& [input, n] : use->Inputs) ready = ready && Avail[input] >= n;
				auto step = std::find_if(Plan.Steps.begin(), Plan.Steps.end(), [&](const ProductionStep& s) { return s.Recipe == use->Id; });
				if (!emit) {}   // (made by someone else: only its inputs below)
				else if (step == Plan.Steps.end())
					Plan.Steps.push_back({ use->Id, use->Output, batches, depth, ready, why });
				else
				{
					step->Batches = std::min(std::max(1, Query.MaxBatches), step->Batches + batches);
					step->Ready = step->Ready || ready;
					step->Depth = std::min(step->Depth, depth);
				}
				if (depth >= Query.Depth) return;   // deep enough: what its inputs need is not planned
				for (const auto& [input, n] : use->Inputs) Want(input, n * batches, depth + 1, input + " for " + use->Id + " (" + why + ")");
			}
		};
	}

	ProductionPlan PlanProduction(const std::vector<ProductionWant>& wants, const std::map<std::string, int>& stock, const RecipeBook& recipes, const ProductionQuery& query)
	{
		Planner p{ recipes, query, stock, {} };
		for (const ProductionWant& w : wants)
			if (!w.Item.empty() && w.Units > 0) p.Want(w.Item, w.Units, 0, w.Why, w.Make);
		return std::move(p.Plan);
	}

	const Recipe* BestToolRecipe(const RecipeBook& recipes, const ItemLibrary& items, const std::string& tag, int above, const ToolRecipeQuery& query, bool& known)
	{
		auto tagged = [&](const ItemDef& d) { return !tag.empty() && std::any_of(d.Tags.begin(), d.Tags.end(), [&](const std::string& t) { return GameplayTag::TagMatches(tag, t); }); };
		auto ready = [&](const Recipe& r) { return query.Have && std::all_of(r.Inputs.begin(), r.Inputs.end(), [&](const auto& in) { return query.Have(in.first) >= in.second; }); };
		const Recipe* best = nullptr;
		const ItemDef* bestDef = nullptr;
		bool bestReady = false;
		known = false;
		for (int pass = 0; pass < 2 && !best; ++pass)
			for (const std::string& id : recipes.Ids())
			{
				const Recipe* r = recipes.Find(id);
				const ItemDef* d = r ? items.Find(r->Output) : nullptr;
				if (!d || !tagged(*d) || d->Power <= above || (query.Station && !query.Station(r->Station))) continue;
				if (pass == 0 && query.Known && !query.Known(*r)) continue;
				const bool isReady = pass == 0 && ready(*r);   // (P-F10: only among the known; a recipe to try stays the best one)
				if (!best || (isReady && !bestReady) || (isReady == bestReady && (d->Power > bestDef->Power || (d->Power == bestDef->Power && d->Speed > bestDef->Speed))))
				{
					best = r;
					bestDef = d;
					bestReady = isReady;
				}
			}
		known = best && (!query.Known || query.Known(*best));
		return best;
	}

	std::string PlanReport(const ProductionPlan& plan)
	{
		std::string s;
		for (const ProductionStep& st : plan.Steps)
			s += std::string(static_cast<size_t>(st.Depth) * 2, ' ') + "make " + st.Output + " x" + std::to_string(st.Batches) + " (" + st.Recipe + (st.Ready ? ", ready" : ", waiting for inputs") + "): " + st.Why + "\n";
		if (!plan.Raw.empty())
		{
			s += "to gather:";
			for (const auto& [item, n] : plan.Raw) s += " " + item + " " + std::to_string(n);
			s += "\n";
		}
		for (const std::string& b : plan.Blocked) s += "blocked: " + b + "\n";
		if (s.empty()) s = "nothing to make\n";
		return s;
	}
}
