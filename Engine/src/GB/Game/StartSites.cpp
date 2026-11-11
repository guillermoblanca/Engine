#include "GB/Game/StartSites.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cstdio>

namespace GB
{
	std::vector<CampSite> GraphCampSites(const GeneratedWorld& world)
	{
		std::vector<const SettlementSite*> order;
		for (const SettlementSite& s : world.Settlements) order.push_back(&s);
		std::stable_sort(order.begin(), order.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->Rank < b->Rank; });   // (published best first already)
		std::vector<CampSite> out;
		for (const SettlementSite* s : order)
		{
			CampSite c;
			c.At = s->Position;
			c.Score = s->Score;
			char why[160];
			std::snprintf(why, sizeof why, "site %d of the terrain graph (water %.0f m, a levelled platform of %.0f m at %.1f m)", s->Rank, static_cast<double>(s->WaterDistance),
				static_cast<double>(s->Radius), static_cast<double>(s->PlatformLevel));
			c.Why = why;
			out.push_back(std::move(c));
		}
		return out;
	}

	std::vector<glm::vec2> GraphStartCamps(const std::vector<SettlementSite>& sites, int count, const std::vector<glm::vec2>& taken, float minApart)
	{
		std::vector<const SettlementSite*> order;
		for (const SettlementSite& s : sites) order.push_back(&s);
		std::stable_sort(order.begin(), order.end(), [](const SettlementSite* a, const SettlementSite* b) { return a->Rank < b->Rank; });
		std::vector<glm::vec2> out;
		for (const SettlementSite* s : order)
		{
			if (static_cast<int>(out.size()) >= count) break;
			bool free = true;
			for (const glm::vec2& t : taken) free = free && glm::distance(t, s->Position) >= minApart;
			if (free) out.push_back(s->Position);
		}
		return out;
	}

	const SettlementSite* GraphSiteAt(const std::vector<SettlementSite>& sites, const glm::vec2& at)
	{
		const SettlementSite* best = nullptr;
		float nearest = 1e30f;
		for (const SettlementSite& s : sites)
		{
			const float d = glm::distance(s.Position, at);
			if (d <= s.Radius && d < nearest) { nearest = d; best = &s; }
		}
		return best;
	}

	float CampGround(const GeneratedWorld& world, const glm::vec2& at)
	{
		if (const SettlementSite* s = GraphSiteAt(world.Settlements, at)) return s->PlatformLevel;
		return world.TerrainRef ? world.TerrainRef->GetHeight(at.x, at.y) : 0.0f;
	}

	float TrailUndergrowth(const GeneratedWorld& world, float x, float z, float undergrowth)
	{
		if (world.PathField.N > 1 && world.PathField.Sample(x, z) > 0.5f) return 0.0f;
		return undergrowth;
	}
}
