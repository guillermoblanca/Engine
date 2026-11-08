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
		for (const auto& [id, def] : *f.Buildings)
		{
			if (!known(def) || !payable(def)) continue;   // H11 decides what is known
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
			if ((!def.Store.empty() || def.StoreCap > 0) && !f.FullItem.empty() && count[id] < std::max(1, alive / 25))
				want(2.0f, "the store is full of " + f.FullItem + " (" + std::to_string(f.FullCount) + "/" + std::to_string(f.StoreCap) + ")");
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

	std::optional<glm::vec2> PlaceSite(const SitePlacement& s)
	{
		const BuildingDef& def = *s.Def;
		const float start = 0.6f + 1.1f * static_cast<float>(s.Others.size());
		const auto stand = [&](const glm::vec2& p) { return !s.CanStand || s.CanStand(p); };
		auto fits = [&](const glm::vec2& p)
		{
			const float r = def.Radius();
			if (glm::distance(p, s.Camp) < r + 2.5f || glm::distance(p, s.Fire) < r + 2.5f) return false;
			for (const PlannerSite& o : s.Others) if (glm::distance(p, o.Pos) < r + o.Def->Radius() + 1.5f) return false;
			for (const glm::vec2& o : s.Obstacles) if (glm::distance(p, o) < r + 1.2f) return false;
			if (!s.CanStand) return true;
			if (!stand(p)) return false;
			for (int k = 0; k < 4; ++k)   // the corners too: not half in the lake
			{
				const glm::vec2 c = p + glm::vec2((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f) * def.Footprint;
				if (!stand(c)) return false;
			}
			return stand(SiteFront(p, def));
		};
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
