#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	WaterRaster RasterizeWater(const WaterField& water, uint32_t n, const glm::vec2& min, float spacing)
	{
		WaterRaster r;
		r.N = n;
		const size_t count = static_cast<size_t>(n) * n;
		r.Kinds.assign(count, WaterRaster::Dry);
		r.Level.assign(count, kNoWaterLevel);
		r.RiverDistance.assign(count, 1e9f);
		r.RiverRelative.assign(count, 1e9f);
		r.RiverLevel.assign(count, -1e9f);
		if (n == 0) return r;
		auto at = [&](uint32_t i, uint32_t j) { return min + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * spacing; };

		for (const LakeBody* lake : water.Lakes())
		{
			// only the box the lake can reach: its mask, or the circle of its radius grown by the outline's lobes
			glm::vec2 lo, hi;
			if (!lake->Mask.Empty())
			{
				lo = lake->Mask.Origin - glm::vec2(lake->Mask.Spacing);
				hi = lake->Mask.Origin + glm::vec2(static_cast<float>(lake->Mask.W), static_cast<float>(lake->Mask.H)) * lake->Mask.Spacing;
			}
			else
			{
				const float reach = lake->Radius * (1.0f + std::fabs(lake->Outline.Irregularity)) + spacing;
				lo = lake->Center - glm::vec2(reach);
				hi = lake->Center + glm::vec2(reach);
			}
			const int i0 = std::max(0, static_cast<int>(std::floor((lo.x - min.x) / spacing))), i1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((hi.x - min.x) / spacing)));
			const int j0 = std::max(0, static_cast<int>(std::floor((lo.y - min.y) / spacing))), j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((hi.y - min.y) / spacing)));
			for (int j = j0; j <= j1; ++j)
				for (int i = i0; i <= i1; ++i)
				{
					const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
					if (r.Kinds[c] != WaterRaster::Dry) continue;   // (the main lake, then the others in order)
					if (lake->Covers(at(static_cast<uint32_t>(i), static_cast<uint32_t>(j)))) { r.Kinds[c] = WaterRaster::Lake; r.Level[c] = lake->Level; }
				}
		}

		if (const RiverData* river = water.River())
		{
			// the nearest segment of every sample within 3.2 of the widest half-widths (the segments in order, the first of equals: as NearestRiver)
			float widest = river->HalfWidth;
			for (const float w : river->HalfWidths) widest = std::max(widest, w);
			const float reach = 3.2f * widest;
			for (size_t k = 0; k + 1 < river->Points.size(); ++k)
			{
				const glm::vec2 a(river->Points[k].x, river->Points[k].z), b(river->Points[k + 1].x, river->Points[k + 1].z);
				const glm::vec2 lo = (glm::min(a, b) - glm::vec2(reach) - min) / spacing, hi = (glm::max(a, b) + glm::vec2(reach) - min) / spacing;
				const int i0 = std::max(0, static_cast<int>(std::floor(lo.x))), i1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil(hi.x)));
				const int j0 = std::max(0, static_cast<int>(std::floor(lo.y))), j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil(hi.y)));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
						const glm::vec2 p = at(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
						const RiverSample s = ProbeRiverSegment(*river, k, p.x, p.y);
						if (s.Distance < r.RiverDistance[c] && s.HalfWidth > 0.0f) { r.RiverDistance[c] = s.Distance; r.RiverRelative[c] = s.Distance / s.HalfWidth; r.RiverLevel[c] = s.Level; }
					}
			}
			for (size_t c = 0; c < count; ++c)
				if (r.RiverRelative[c] <= 1.5f && r.Kinds[c] == WaterRaster::Dry) { r.Kinds[c] = WaterRaster::River; r.Level[c] = r.RiverLevel[c]; }
		}
		return r;
	}
}
