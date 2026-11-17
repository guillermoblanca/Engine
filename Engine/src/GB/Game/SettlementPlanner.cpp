#include "GB/Game/SettlementPlanner.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace GB
{
	glm::vec2 SiteFront(const glm::vec2& pos, const BuildingDef& def, float side) { return pos + glm::vec2(side, 0.5f * def.Footprint.y + 0.8f); }

	std::vector<BuildingNeed> PlanBuildingNeeds(const SettlementFacts& f)
	{
		std::vector<BuildingNeed> out;
		if (!f.Buildings) return out;
		int open = 0, beds = 0;
		std::map<std::string, int> count;   // buildings of each kind, done or not
		for (const PlannerSite& p : f.Sites)
		{
			if (!p.Done) ++open;
			beds += p.Def->Shelter;   // a shelter being built already answers the need for beds
			++count[p.Def->Id];
		}
		const int alive = f.Alive;
		if (alive == 0 || open >= f.MaxOpenSites) return out;
		const auto known = [&](const BuildingDef& d) { return !f.Known || f.Known(d); };
		const auto payable = [&](const BuildingDef& d) { return !f.Payable || f.Payable(d); };

		char why[160];
		// a better home the clan can raise takes the place of the ones it supersedes (the hut over the shelter): those are not raised for beds any more
		std::set<std::string> superseded;
		for (const auto& [id, def] : *f.Buildings)
			if (!def.Supersedes.empty() && known(def) && payable(def)) superseded.insert(def.Supersedes.begin(), def.Supersedes.end());
		// T256: the best food store the clan knows and can pay (the lowest keep, then the most room for food, then the id), and the food stores it has
		const BuildingDef* bestFood = nullptr;
		for (const auto& [id, def] : *f.Buildings)
			if (def.FoodCap > 0 && def.Keep < 1.0f && known(def) && payable(def)
				&& (!bestFood || def.Keep < bestFood->Keep - 1e-4f || (std::abs(def.Keep - bestFood->Keep) <= 1e-4f && def.FoodCap > bestFood->FoodCap))) bestFood = &def;
		// D47: the stores that keep food (room for food, or a store that slows its spoiling: a cache pit, a granary, a jar store, a cellar) are one
		// for every FoodStoresPer living at most, whichever rule asks for them (with T256 on: FoodStoreAt > 0). A better one comes by rebuilding an
		// old one (ClanStores::PlanUpgrade), not beside it. Before, the pile's, the full store's and T256's rules each added their own: 11 for 19.
		const auto keepsFood = [](const BuildingDef& d) { return d.FoodCap > 0 || (d.Keep < 1.0f && (d.StoreCap > 0 || !d.Store.empty())); };
		int foodStores = 0, foodStoresDone = 0;
		bool foodSiteOpen = false;
		for (const PlannerSite& p : f.Sites)
			if (keepsFood(*p.Def)) { ++foodStores; foodStoresDone += p.Done ? 1 : 0; foodSiteOpen = foodSiteOpen || !p.Done; }
		// A clan with no food store standing yet may raise the plain one beside the better one it waits for: "payable" only says where the things
		// come from, not that they come (GBSim seed 77: a granary waited 20 days for its stone and the clan of 20 had no food store at all, food days 3)
		const bool noFoodStore = foodStoresDone == 0;
		const bool foodStoresFull = f.FoodStoreAt > 0 && foodStores >= std::max(1, alive / std::max(1, f.FoodStoresPer)) + (noFoodStore && foodSiteOpen ? 1 : 0);
		const auto passedOver = [&](const std::string& id) { return superseded.count(id) > 0 && !noFoodStore; };
		for (const auto& [id, def] : *f.Buildings)
		{
			if (!known(def) || !payable(def)) continue;   // H11 decides what is known
			// D47 (a home that keeps food is still a home); nor a food store the clan knows a better one of (a cache pit pulled down for a granary was
			// raised again at once for the full store)
			if (keepsFood(def) && def.Shelter == 0 && (foodStoresFull || passedOver(id))) continue;
			BuildingNeed need;
			need.Building = id;
			auto want = [&](float urgency, const std::string& text) { if (urgency > need.Urgency) { need.Urgency = urgency; need.Why = text; } };
			if (def.Shelter > 0 && beds < alive && !superseded.count(id))
			{
				need.Urgency = std::max(need.Urgency, 3.0f * static_cast<float>(alive - beds) / static_cast<float>(alive));
				std::snprintf(why, sizeof why, "beds for %d of %d", beds, alive);
				need.Why = why;
			}
			// (one more for every 25 living while the store stays full: a big village keeps more than one granary)
			if ((!def.Store.empty() || def.StoreCap > 0 || def.FoodCap > 0) && !f.FullItem.empty() && count[id] < std::max(1, alive / 25))
				want(2.0f, "the store is full of " + f.FullItem + " (" + std::to_string(f.FullCount) + "/" + std::to_string(f.StoreCap) + ")");
			// T255: a pile by the fire that no store building has room for: one more store for every 120 units of it (at most one for every 6 living)
			// (the food's pile asks for a building that keeps food, the rest's for one that keeps anything)
			if (const int pile = def.FoodCap > 0 ? f.CampFood : (!def.Store.empty() || def.StoreCap > 0) ? f.CampPile - f.CampFood : 0;
				pile >= 120 && count[id] < std::max(1, alive / 25) + std::min(pile / 120, std::max(1, alive / 6)))
				if (!(def.FoodCap > 0 && passedOver(id))) want(1.5f, std::to_string(pile) + " things lie by the fire: the stores are full");   // (T256: not a store the clan knows a better one of)
			// T256: food that went off lately and food by the fire with no room ask for the best food store the clan knows, one at a time (above the
			// pile's rule; under beds for all)
			if (&def == bestFood && f.FoodStoreAt > 0 && !foodSiteOpen)
				if (const int pressure = f.SpoiledLately + f.CampFood; pressure >= f.FoodStoreAt && !foodStoresFull)
				{
					std::snprintf(why, sizeof why, "%d food went off lately and %d lies by the fire with no room", f.SpoiledLately, f.CampFood);
					want(std::min(2.5f, 1.6f + 0.25f * static_cast<float>(pressure) / static_cast<float>(f.FoodStoreAt)), why);
				}
			// the generic provisions (docs/PROGRESION.md), each only for the first of its kind
			if (count[id] == 0)
			{
				if (def.Keep < 1.0f && f.Spoiled > 0) want(1.2f, std::to_string(f.Spoiled) + " food went off in the store");
				if (def.Healing > 0.0f) want(f.Wounded > 0 ? 1.2f : 0.3f, f.Wounded > 0 ? std::to_string(f.Wounded) + " wounded to tend" : std::string("a place to tend the wounded"));
				if (def.Defense > 0.0f) want(f.Raided > 0 ? 2.0f : 0.35f, f.Raided > 0 ? "raided " + std::to_string(f.Raided) + " times" : std::string("walls against raiders"));
				if (def.Culture > 0.0f && def.Aura > 0.0f) want(std::min(0.8f, 0.3f + 0.05f * static_cast<float>(alive)), "a place for the clan to meet");
				if (def.Shelter == 0 && def.Warmth > 0.0f && def.Aura > 0.0f) want(0.4f, "a fire to gather round");
				if (def.Trade > 0.0f) want(0.45f, "a place to trade with the other clans");   // V4
				if (!def.Values.empty()) want(0.5f, "what the clan holds dear");   // V4: a building of its culture (only built where the culture holds it)
				if (!def.Rank.empty()) want(0.6f, "the hall of a " + def.Rank);   // V7: what a settlement of its rank raises
				for (const auto& [provision, amount] : def.Provides)   // V5: the place for a need of the packs (a temple to pray in)
					if (const auto w = f.Wanting.find(provision); w != f.Wanting.end() && w->second > 0.0f) want(0.4f + 1.2f * w->second, "a place for " + provision);
			}
			if (def.Well && f.MembersPerWell > 0)
			{
				int wells = f.Wells;   // (and those being dug)
				for (const PlannerSite& p : f.Sites) if (p.Def->Well && !p.Done) ++wells;
				if (alive > wells * f.MembersPerWell)
				{
					std::snprintf(why, sizeof why, "%d drink from %d wells", alive, wells);
					want(std::min(2.5f, 1.0f + static_cast<float>(alive - wells * f.MembersPerWell) / static_cast<float>(f.MembersPerWell)), why);
				}
			}
			if (!def.Station.empty() && count[id] == 0)
			{
				const bool there = f.StationThere && f.StationThere(def.Station);
				// a recipe needs the station: the tool of a role here, or one whose inputs the store holds
				const std::string recipe = f.RecipeNeeding ? f.RecipeNeeding(def.Station) : std::string();
				if (!there && !recipe.empty() && 1.5f > need.Urgency) { need.Urgency = 1.5f; need.Why = "the recipe " + recipe + " needs a " + def.Station; }
				else if (there && 0.5f > need.Urgency && f.StationIsImprovised && f.StationIsImprovised(def.Station))
				{
					need.Urgency = 0.5f;
					need.Why = "the camp only has an improvised " + def.Station;
				}
			}
			if (need.Urgency > 0.0f) out.push_back(need);
		}
		std::sort(out.begin(), out.end(), [](const BuildingNeed& a, const BuildingNeed& b) { return a.Urgency != b.Urgency ? a.Urgency > b.Urgency : a.Building < b.Building; });
		return out;
	}

	std::string SiteFits(const SitePlacement& s, const glm::vec2& p)
	{
		if (!s.Def) return "no building";
		const BuildingDef& def = *s.Def;
		const float r = def.Radius();
		if (glm::distance(p, s.Camp) < r + 2.5f) return "too near the camp";
		if (glm::distance(p, s.Fire) < r + 2.5f) return "too near the fire";
		for (const PlannerSite& o : s.Others) if (glm::distance(p, o.Pos) < r + o.Def->Radius() + 1.5f) return "too near " + o.Def->Name;
		for (const glm::vec2& o : s.Obstacles) if (glm::distance(p, o) < r + 1.2f) return "something stands there";
		if (std::string on = GroundFree(BuildingGround(def, p), s.Taken, kFieldGap); !on.empty()) return on;   // T260 (a field has no circle in Others)
		if (std::string wet = SiteInWater(def, p, s.WaterDepth); !wet.empty()) return wet;
		if (!s.CanStand) return {};
		if (!s.CanStand(p)) return "nobody can stand there";
		for (int k = 0; k < 4; ++k)   // the corners too: not half in the lake
		{
			const glm::vec2 c = p + glm::vec2((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f) * def.Footprint;
			if (!s.CanStand(c)) return "a corner is on ground nobody can stand on";
		}
		if (!s.CanStand(SiteFront(p, def))) return "its front is on ground nobody can stand on";
		return {};
	}

	std::string SiteInWater(const BuildingDef& def, const glm::vec2& at, const std::function<float(const glm::vec2&)>& waterDepth)
	{
		if (def.AllowWater || !waterDepth) return {};
		// a grid over the whole footprint (edges and corners included), then the spot in front where its builders stand
		const int nx = std::max(1, static_cast<int>(std::ceil(def.Footprint.x / kWetStep)));
		const int nz = std::max(1, static_cast<int>(std::ceil(def.Footprint.y / kWetStep)));
		float deepest = 0.0f;
		for (int i = 0; i <= nx; ++i)
			for (int j = 0; j <= nz; ++j)
			{
				const glm::vec2 q = at + glm::vec2((static_cast<float>(i) / static_cast<float>(nx) - 0.5f) * def.Footprint.x, (static_cast<float>(j) / static_cast<float>(nz) - 0.5f) * def.Footprint.y);
				deepest = std::max(deepest, waterDepth(q));
			}
		const float front = waterDepth(SiteFront(at, def));
		if (deepest <= kWetDepth && front <= kWetDepth) return {};
		char why[96];
		if (deepest > kWetDepth) std::snprintf(why, sizeof why, "there is water under it (%.2f m deep)", static_cast<double>(deepest));
		else std::snprintf(why, sizeof why, "its front is in the water (%.2f m deep)", static_cast<double>(front));
		return why;
	}

	std::optional<glm::vec2> PlaceField(const glm::vec2& wanted, int plots, int columns, float spacing, const std::function<bool(const glm::vec2&)>& free, int rings,
		const std::function<bool(const glm::vec2&)>& room)
	{
		if (!free && !room) return wanted;
		const int c = std::max(1, columns);
		auto fits = [&](const glm::vec2& origin)
		{
			if (room && !room(origin)) return false;
			if (!free) return true;
			if (!free(origin + glm::vec2(-spacing, 0.0f))) return false;   // (its store, beside the first plot: Tribe::MarkField)
			for (int i = 0; i < std::max(0, plots); ++i)
				if (!free(origin + glm::vec2(static_cast<float>(i % c), static_cast<float>(i / c)) * spacing)) return false;
			return true;
		};
		if (fits(wanted)) return wanted;
		for (int ring = 1; ring <= rings; ++ring)
			for (int k = 0; k < 12; ++k)
			{
				const float a = static_cast<float>(k) * 0.5236f;
				const glm::vec2 p = wanted + glm::vec2(std::cos(a), std::sin(a)) * (spacing * static_cast<float>(ring));
				if (fits(p)) return p;
			}
		return std::nullopt;
	}

	std::optional<glm::vec2> PlaceSite(const SitePlacement& s)
	{
		const float start = 0.6f + 1.1f * static_cast<float>(s.Others.size());
		auto fits = [&](const glm::vec2& p) { return SiteFits(s, p).empty(); };
		if (s.Prefer)
		{
			std::optional<glm::vec2> best;
			float top = -1e30f;
			for (int ring = 0; ring < std::max(1, s.Rings); ++ring)
				for (int k = 0; k < 24; ++k)
				{
					const float a = start + static_cast<float>(k) * 0.2618f;
					const glm::vec2 p = s.Camp + glm::vec2(std::cos(a), std::sin(a)) * (s.SiteRadius + 3.0f * static_cast<float>(ring));
					if (!fits(p)) continue;
					if (const float v = s.Prefer(p); v > top) { top = v; best = p; }
				}
			return best;
		}
		for (int ring = 0; ring < 5; ++ring)
			for (int k = 0; k < 12; ++k)
			{
				const float a = start + static_cast<float>(k) * 0.5236f;
				const glm::vec2 p = s.Camp + glm::vec2(std::cos(a), std::sin(a)) * (s.SiteRadius + 3.0f * static_cast<float>(ring));
				if (fits(p)) return p;
			}
		return std::nullopt;
	}
}
