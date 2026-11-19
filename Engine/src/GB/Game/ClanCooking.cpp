#include "GB/Game/ClanCooking.h"
#include "GB/Gameplay/GameplayTag.h"
#include "GB/Gameplay/Spoilage.h"
#include <algorithm>

namespace GB
{
	namespace
	{
		bool Food(const ClanCooking::Query& q, const std::string& item)
		{
			if (q.IsFood) return q.IsFood(item);
			const ItemDef* d = q.Items ? q.Items->Find(item) : nullptr;
			return d && std::any_of(d->Tags.begin(), d->Tags.end(), [](const std::string& t) { return GameplayTag::TagMatches("Item.Food", t); });
		}

		float Sates(const ClanCooking::Query& q, const std::string& item)
		{
			if (!Food(q, item)) return 0.0f;
			return q.Nutrition ? q.Nutrition(item) : 1.0f;
		}

		std::string GroupOf(const ClanCooking::Query& q, const std::string& item)
		{
			std::string g = q.Group ? q.Group(item) : std::string();
			return g.empty() ? item : g;
		}

		bool Usable(const ClanCooking::Query& q, const Recipe& r)
		{
			return (!q.Known || q.Known(r)) && (r.Station.empty() || !q.Station || q.Station(r.Station));
		}

		float LifeOf(const ClanCooking::Query& q, const std::string& item)
		{
			const ItemDef* d = q.Items ? q.Items->Find(item) : nullptr;
			return d ? ClanCooking::Life(*d) : ClanCooking::Life(ItemDef());
		}

		// Adds `batches` of `r` to `out` (one entry a recipe).
		void Add(std::vector<ClanCooking::Cook>& out, const Recipe& r, int batches, bool chain, const std::string& why)
		{
			for (ClanCooking::Cook& c : out)
				if (c.Use == &r) { c.Batches += batches; c.Chain = c.Chain || chain; return; }
			out.push_back({ &r, batches, chain, why });
		}

		int Batches(const std::vector<ClanCooking::Cook>& out, const Recipe& r)
		{
			for (const ClanCooking::Cook& c : out) if (c.Use == &r) return c.Batches;
			return 0;
		}

		// One batch of `r` from `stock`: every input in it, or (only when `r` is what the planner would use for its output, so the planner makes
		// it) made first from it by the recipe the planner would pick for that input. On success `stock` loses what it takes and `chain` says
		// whether an input had to be made.
		bool OneBatch(const ClanCooking::Query& q, const Recipe& r, std::map<std::string, int>& stock, bool& chain)
		{
			std::map<std::string, int> trial = stock;
			bool made = false;
			for (const auto& [item, n] : r.Inputs)
			{
				int& have = trial[item];
				if (have >= n) { have -= n; continue; }
				if (ClanCooking::PlannerPick(q, r.Output) != &r) return false;
				const Recipe* m = ClanCooking::PlannerPick(q, item);
				if (!m || m == &r) return false;
				const int per = std::max(1, m->OutputCount);
				const int runs = (n - have + per - 1) / per;
				for (const auto& [sub, k] : m->Inputs)
				{
					int& left = trial[sub];
					if (sub == item || left < k * runs) return false;
					left -= k * runs;
				}
				have = have + runs * per - n;
				made = true;
			}
			stock = std::move(trial);
			chain = made;
			return true;
		}
	}

	float ClanCooking::Life(const ItemDef& d) { return d.Perishable() ? d.ShelfLife : 1e30f; }

	bool ClanCooking::Keeps(const Query& q, const Recipe& r, const std::string& food, float life)
	{
		const ItemDef* out = q.Items ? q.Items->Find(r.Output) : nullptr;
		const ItemDef* in = q.Items ? q.Items->Find(food) : nullptr;
		if (!out || !in || !r.Inputs.count(food) || !in->Perishable()) return false;
		const float lasts = Life(*out);
		if (lasts <= (life >= 0.0f ? life : in->ShelfLife)) return false;
		for (const auto& [item, n] : r.Inputs)
		{
			if (item == food) continue;
			const ItemDef* d = q.Items->Find(item);
			if (d && d->Perishable() && d->ShelfLife > lasts) return false;   // (it would outlive what it is spent on)
		}
		return true;
	}

	bool ClanCooking::Cooks(const Query& q, const Recipe& r)
	{
		if (!Food(q, r.Output)) return false;
		float in = 0.0f;
		for (const auto& [item, n] : r.Inputs) in += Sates(q, item) * static_cast<float>(n);
		return Sates(q, r.Output) * static_cast<float>(std::max(1, r.OutputCount)) >= in;
	}

	bool ClanCooking::IsMeal(const Query& q, const Recipe& r)
	{
		if (!q.Items || !Cooks(q, r)) return false;
		const float lasts = LifeOf(q, r.Output);
		for (const auto& [item, n] : r.Inputs)
			if (const ItemDef* d = q.Items->Find(item); d && d->Perishable() && d->ShelfLife > lasts) return true;
		return false;
	}

	const Recipe* ClanCooking::PlannerPick(const Query& q, const std::string& item)
	{
		if (!q.Recipes) return nullptr;
		for (const std::string& id : q.Recipes->Ids())
			if (const Recipe* r = q.Recipes->Find(id); r && r->Output == item && Usable(q, *r)) return r;
		return nullptr;
	}

	std::vector<std::string> ClanCooking::MealFoods(const Query& q)
	{
		std::vector<std::string> out;
		if (!q.Recipes) return out;
		for (const std::string& id : q.Recipes->Ids())
			if (const Recipe* r = q.Recipes->Find(id); r && IsMeal(q, *r) && std::find(out.begin(), out.end(), r->Output) == out.end()) out.push_back(r->Output);
		std::sort(out.begin(), out.end());
		return out;
	}

	std::vector<ClanCooking::Cook> ClanCooking::Meals(const Query& q, std::map<std::string, int>& stock, std::map<std::string, int>& held, int wanted)
	{
		std::vector<Cook> out;
		if (!q.Items || !q.Recipes || wanted <= 0) return out;
		std::vector<const Recipe*> meals;
		for (const std::string& id : q.Recipes->Ids())
			if (const Recipe* r = q.Recipes->Find(id); r && Usable(q, *r) && IsMeal(q, *r)) meals.push_back(r);
		if (meals.empty()) return out;
		int total = 0;
		std::map<std::string, int> groups;
		for (const auto& [item, n] : held) { total += n; groups[GroupOf(q, item)] += n; }
		const int most = std::max(1, q.MaxBatches);
		while (total < wanted)
		{
			const Recipe* best = nullptr;
			int bestHeld = 0;
			float bestLife = 0.0f, bestSates = 0.0f;
			for (const Recipe* r : meals)
			{
				if (Batches(out, *r) >= most) continue;
				std::map<std::string, int> trial = stock;
				bool chain = false;
				if (!OneBatch(q, *r, trial, chain)) continue;
				const int g = groups[GroupOf(q, r->Output)];
				const float life = LifeOf(q, r->Output), sates = Sates(q, r->Output);
				if (!best || g < bestHeld || (g == bestHeld && (life > bestLife || (life == bestLife && sates > bestSates))))   // (ids in order: a tie keeps the first)
				{
					best = r;
					bestHeld = g;
					bestLife = life;
					bestSates = sates;
				}
			}
			if (!best) break;
			bool chain = false;
			OneBatch(q, *best, stock, chain);
			const int units = std::max(1, best->OutputCount);
			Add(out, *best, 1, chain, "the clan keeps " + std::to_string(wanted) + " cooked meals");
			total += units;
			groups[GroupOf(q, best->Output)] += units;
			held[best->Output] += units;
		}
		return out;
	}

	std::vector<ClanCooking::Cook> ClanCooking::RawSoon(const Query& q, std::map<std::string, int>& stock, std::map<std::string, int>& held, const std::map<std::string, Soon>& soon)
	{
		std::vector<Cook> out;
		if (!q.Items || !q.Recipes) return out;
		const std::vector<std::string> ids = q.Recipes->Ids();
		const int most = std::max(1, q.MaxBatches);
		for (const auto& [food, s] : soon)
		{
			if (s.Units <= 0) continue;
			const Recipe* best = nullptr;
			float bestLife = 0.0f, bestSates = 0.0f;
			for (const std::string& id : ids)
			{
				const Recipe* r = q.Recipes->Find(id);
				if (!r || !Usable(q, *r) || !Cooks(q, *r) || !Keeps(q, *r, food, s.Left) || Batches(out, *r) >= most) continue;
				if (!std::all_of(r->Inputs.begin(), r->Inputs.end(), [&](const auto& in) { const auto it = stock.find(in.first); return it != stock.end() && it->second >= in.second; })) continue;
				const float life = LifeOf(q, r->Output), sates = Sates(q, r->Output) * static_cast<float>(std::max(1, r->OutputCount));
				if (!best || life > bestLife || (life == bestLife && sates > bestSates)) { best = r; bestLife = life; bestSates = sates; }
			}
			if (!best) continue;
			const int per = std::max(1, best->Inputs.at(food));
			int batches = std::min((s.Units + per - 1) / per, most - Batches(out, *best));
			for (const auto& [item, n] : best->Inputs) batches = std::min(batches, stock[item] / std::max(1, n));
			if (batches <= 0) continue;
			for (const auto& [item, n] : best->Inputs) stock[item] -= n * batches;
			Add(out, *best, batches, false, "cooks " + std::to_string(s.Units) + " " + food + " before it goes off");
			if (IsMeal(q, *best)) held[best->Output] += batches * std::max(1, best->OutputCount);
		}
		return out;
	}

	std::string ClanCooking::EatFirst(const Query& q, const Inventory& inv, float rawSoon)
	{
		if (!q.Items || !q.Recipes) return {};
		std::vector<std::string> meals;
		std::string best;
		float bestLeft = 0.0f;
		bool listed = false;
		for (const ItemSlot& s : inv.Stacks())
		{
			if (s.Count <= 0 || !Food(q, s.Id)) continue;
			const float left = LifeLeft(*q.Items, s);
			if (left < rawSoon) return {};   // (about to go off: eaten first, as before)
			if (!listed) { meals = MealFoods(q); listed = true; }
			if (!std::binary_search(meals.begin(), meals.end(), s.Id)) continue;
			if (best.empty() || left < bestLeft || (left == bestLeft && s.Id < best)) { best = s.Id; bestLeft = left; }
		}
		return best;
	}
}
