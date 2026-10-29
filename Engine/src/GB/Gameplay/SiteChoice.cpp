#include "GB/Gameplay/SiteChoice.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	std::string SiteRating::Text() const
	{
		char b[256];
		if (!Ok)
		{
			std::snprintf(b, sizeof b, "(%.0f, %.0f) refused: %s", static_cast<double>(At.x), static_cast<double>(At.y), Why.c_str());
			return b;
		}
		std::snprintf(b, sizeof b, "score %.2f at (%.0f, %.0f): water %.2f, resources %.2f, flat %.2f, biome %.2f, shelter %.2f", static_cast<double>(Total),
			static_cast<double>(At.x), static_cast<double>(At.y), static_cast<double>(Water), static_cast<double>(Resources), static_cast<double>(Flat),
			static_cast<double>(Biome), static_cast<double>(Shelter));
		std::string s = b;
		if (!Near.empty())
		{
			s += " (near:";
			for (const auto& [kind, n] : Near) s += " " + kind + " " + std::to_string(n);
			s += ")";
		}
		return s;
	}

	SiteRating ScoreSite(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& w, const glm::vec2& at)
	{
		SiteRating s;
		s.At = at;
		char b[96];
		auto refuse = [&](const std::string& why) { s.Ok = false; s.Why = why; s.Total = 0.0f; return s; };

		if (at.x < ground.Min.x + w.Edge || at.y < ground.Min.y + w.Edge || at.x > ground.Max.x - w.Edge || at.y > ground.Max.y - w.Edge) return refuse("too near the edge of the map");

		// water: near enough to fetch, not on the bank
		const float water = ground.WaterDistance ? ground.WaterDistance(at.x, at.y) : 1e9f;
		if (water <= 0.0f) return refuse("in the water");
		if (water < w.WaterNear) { std::snprintf(b, sizeof b, "on the bank (%.1f m from the water)", static_cast<double>(water)); return refuse(b); }
		if (water <= w.WaterBest) s.Water = 1.0f - 0.3f * (w.WaterBest - water) / std::max(w.WaterBest - w.WaterNear, 1e-3f);
		else s.Water = std::max(0.0f, 1.0f - (water - w.WaterBest) / std::max(w.WaterFar - w.WaterBest, 1e-3f));

		// biome
		const std::string biome = ground.Biome ? ground.Biome(at.x, at.y) : std::string("Meadow");
		const auto value = w.BiomeValue.find(biome);
		if (value == w.BiomeValue.end()) return refuse("on " + biome);
		s.Biome = value->second;

		// flat: the slope over the camp's ground (the centre, a ring at half the radius and one at the radius)
		if (ground.SlopeDegrees)
		{
			float sum = 0.0f, worst = 0.0f;
			int n = 0;
			auto sample = [&](const glm::vec2& p)
			{
				const float d = ground.SlopeDegrees(p.x, p.y);
				sum += d;
				worst = std::max(worst, d);
				++n;
			};
			sample(at);
			for (int k = 0; k < 8; ++k)
			{
				const float a = static_cast<float>(k) * 0.785398f;
				const glm::vec2 dir(std::cos(a), std::sin(a));
				sample(at + dir * (0.5f * w.FlatRadius));
				sample(at + dir * w.FlatRadius);
				if (ground.WaterDistance && ground.WaterDistance(at.x + dir.x * w.FlatRadius, at.y + dir.y * w.FlatRadius) <= 0.0f) return refuse("water inside the camp");
			}
			if (worst > w.MaxSlope) { std::snprintf(b, sizeof b, "too steep: %.0f degrees", static_cast<double>(worst)); return refuse(b); }
			s.Flat = std::clamp(1.0f - (sum / static_cast<float>(n)) / std::max(w.MaxSlope, 1e-3f), 0.0f, 1.0f);
		}
		else s.Flat = 1.0f;

		// the world's trees and rocks: none inside the camp, some around it
		int around = 0;
		for (const ResourceGround::Blocker& k : ground.Blockers)
		{
			const float d = glm::distance(k.At, at);
			if (d < w.ClearRadius + k.Radius) return refuse("a tree or a rock of the world is in the way");
			if (d < w.ShelterRadius) ++around;
		}
		s.Shelter = std::min(1.0f, static_cast<float>(around) / std::max(w.ShelterEnough, 1e-3f));

		// resources: what the clan wants, near
		std::map<std::string, float> found;
		for (const SiteResource& r : resources)
		{
			const float d = glm::distance(r.At, at);
			if (d > w.ResourceRadius) continue;
			if (d < w.ClearRadius) return refuse("a resource node is in the way");
			++s.Near[r.Kind];
			found[r.Kind] += 1.0f - 0.5f * d / w.ResourceRadius;
		}
		float wanted = 0.0f, got = 0.0f;
		for (const auto& [kind, want] : w.Want)
		{
			if (want <= 0.0f) continue;
			wanted += want;
			const auto f = found.find(kind);
			if (f != found.end()) got += want * std::min(1.0f, f->second / std::max(w.Enough, 1e-3f));
		}
		s.Resources = wanted > 0.0f ? got / wanted : 0.0f;

		s.Ok = true;
		s.Total = w.Water * s.Water + w.Resources * s.Resources + w.Flat * s.Flat + w.Biome * s.Biome + w.Shelter * s.Shelter;
		return s;
	}

	std::vector<SiteRating> RankSites(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& w, int top)
	{
		std::vector<SiteRating> out;
		const float step = std::max(w.Spacing, 1.0f);
		const glm::vec2 from = ground.Min + glm::vec2(w.Edge), to = ground.Max - glm::vec2(w.Edge);
		for (float z = from.y; z <= to.y + 1e-3f; z += step)
			for (float x = from.x; x <= to.x + 1e-3f; x += step)
			{
				SiteRating s = ScoreSite(ground, resources, w, { x, z });
				if (s.Ok) out.push_back(std::move(s));
			}
		std::sort(out.begin(), out.end(), [](const SiteRating& a, const SiteRating& b)
		{
			if (a.Total != b.Total) return a.Total > b.Total;
			if (a.At.x != b.At.x) return a.At.x < b.At.x;
			return a.At.y < b.At.y;
		});
		if (top > 0 && static_cast<int>(out.size()) > top) out.resize(static_cast<size_t>(top));
		return out;
	}

	SiteRating BestSite(const ResourceGround& ground, const std::vector<SiteResource>& resources, const SiteWeights& w)
	{
		std::vector<SiteRating> ranked = RankSites(ground, resources, w, 1);
		if (!ranked.empty()) return ranked.front();
		SiteRating none;
		none.At = 0.5f * (ground.Min + ground.Max);
		none.Why = "no place of the map fits a camp";
		return none;
	}

	std::vector<SiteResource> SceneResources(const EntityWorld& world, const GameplayWorld& gameplay, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		std::vector<SiteResource> out;
		for (const std::string& name : gameplay.Names())
		{
			const AbilitySystemComponent* a = gameplay.Find(name);
			if (!a || !a->HasTag("Resource.Node")) continue;
			std::string kind;
			for (const auto& [id, def] : nodes)
				if (a->Tags().HasExact("Resource.Node." + id)) { kind = id; break; }
			if (kind.empty()) continue;
			const EntityId id = world.FindByName(name);
			if (id == NullEntity) continue;
			const glm::vec4 p = world.WorldMatrix(id)[3];
			out.push_back({ kind, { p.x, p.z } });
		}
		return out;
	}

	std::vector<SiteResource> SpotResources(const std::vector<ResourceSpot>& spots)
	{
		std::vector<SiteResource> out;
		out.reserve(spots.size());
		for (const ResourceSpot& s : spots) out.push_back({ s.Node, { s.Position.x, s.Position.z } });
		return out;
	}
}
