#include "GB/Water/WaterCarver.h"
#include "GB/Water/RiverHydraulics.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
		{
			const float t2 = t * t, t3 = t2 * t;
			return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
		}
	}

	float ApplyLakeBasin(float h, const glm::vec2& pos, const LakeShape& lake)
	{
		const float d = lake.Outline.EffectiveDistance(pos, lake.Center);
		if (lake.BankSlope > 0.0f)
		{
			// Hills above the basin are cut down to a cone that rises at most BankSlope away from the shore (smooth minimum,
			// fading out far from the lake): the water gets a gentle bank, not a cliff.
			const float cone = lake.Level + lake.BankLift + std::max(d - lake.Radius, 0.0f) * lake.BankSlope;
			const float k = 2.0f, m = std::max(k - std::abs(h - cone), 0.0f) / k;
			const float cut = std::min(h, cone) - m * m * k * 0.25f;
			h = glm::mix(cut, h, glm::smoothstep(lake.Radius * 3.0f, lake.Radius * 5.0f, d));
		}
		// Banks: lift the ground near the lake above the water, then dig the basin.
		const float bank = glm::smoothstep(lake.Radius * 2.8f, lake.Radius * 1.5f, d);
		h = glm::mix(h, glm::max(h, lake.Level + lake.BankLift), bank);
		const float basin = glm::smoothstep(lake.Radius * 1.15f, lake.Radius * 0.15f, d);
		h = glm::mix(h, lake.Level - lake.Depth, basin);
		return h;
	}

	RiverData CarveRiver(Heightmap& hm, const RiverShape& shape, const LakeShape& lake)
	{
		RiverData river;
		if (shape.Path.size() < 2) return river;
		const bool hasLake = lake.Radius > 0.0f;

		// Dense polyline, about 1.5 m apart.
		std::vector<glm::vec2> pts;
		std::vector<float> discharge;   // relative discharge of each dense point (only when the path comes with one)
		const bool hydraulic = shape.Discharge.size() == shape.Path.size();
		const auto& c = shape.Path;
		for (size_t i = 0; i + 1 < c.size(); ++i)
		{
			const glm::vec2 p0 = c[i == 0 ? 0 : i - 1], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, c.size() - 1)];
			const int n = std::max(2, static_cast<int>(glm::length(p2 - p1) / 1.5f));
			for (int k = 0; k < n; ++k)
			{
				const float t = static_cast<float>(k) / static_cast<float>(n);
				pts.push_back(CatmullRom(p0, p1, p2, p3, t));
				if (hydraulic) discharge.push_back(glm::mix(shape.Discharge[i], shape.Discharge[i + 1], t));
			}
		}
		pts.push_back(c.back());
		if (hydraulic) discharge.push_back(shape.Discharge.back());
		const int n = static_cast<int>(pts.size());

		// Width and depth at every point. A river with a discharge widens and deepens downstream as the hydraulic geometry says (RiverHydraulics.h);
		// one without keeps the single width and depth of the recipe, exactly as before.
		std::vector<float> halfWidth(static_cast<size_t>(n), shape.Width * 0.5f), depth(static_cast<size_t>(n), shape.Depth);
		if (hydraulic)
			for (int i = 0; i < n; ++i)
			{
				halfWidth[static_cast<size_t>(i)] = shape.Width * 0.5f * HydraulicWidthFactor(discharge[static_cast<size_t>(i)], shape.Hydraulics);
				depth[static_cast<size_t>(i)] = shape.Depth * HydraulicDepthFactor(discharge[static_cast<size_t>(i)], shape.Hydraulics);
			}

		// Water level: follows the (smoothed) ground but only ever goes down.
		std::vector<float> base(static_cast<size_t>(n)), level(static_cast<size_t>(n));
		for (int i = 0; i < n; ++i) base[static_cast<size_t>(i)] = hm.GetHeight(pts[static_cast<size_t>(i)].x, pts[static_cast<size_t>(i)].y);
		for (int i = 0; i < n; ++i)
		{
			float sum = 0.0f; int cnt = 0;
			for (int k = std::max(0, i - 12); k <= std::min(n - 1, i + 12); ++k) { sum += base[static_cast<size_t>(k)]; ++cnt; }
			const float smooth = sum / static_cast<float>(cnt) - shape.Depth * 0.3f;
			level[static_cast<size_t>(i)] = i == 0 ? smooth : std::min(smooth, level[static_cast<size_t>(i) - 1] - 0.0045f * 1.5f);
		}
		if (hasLake)
		{
			// Near the lake the river comes down to the lake level (at most ~0.35 m of drop per meter), cutting a channel through the bank.
			for (int i = 0; i < n; ++i)
			{
				const float dist = lake.Outline.EffectiveDistance(pts[static_cast<size_t>(i)], lake.Center);
				level[static_cast<size_t>(i)] = std::min(level[static_cast<size_t>(i)], lake.Level + std::max(dist - lake.Radius, 0.0f) * 0.35f);
				if (i > 0) level[static_cast<size_t>(i)] = std::min(level[static_cast<size_t>(i)], level[static_cast<size_t>(i) - 1] - 0.0045f * 1.5f);
			}
			// The river meets the lake at exactly the lake level (no step where the two surfaces overlap); never below it before that.
			for (int i = 0; i < n; ++i)
				level[static_cast<size_t>(i)] = lake.Outline.EffectiveDistance(pts[static_cast<size_t>(i)], lake.Center) <= lake.Radius ? lake.Level : std::max(level[static_cast<size_t>(i)], lake.Level);
			for (int i = n - 2; i >= 0; --i)
				level[static_cast<size_t>(i)] = std::max(level[static_cast<size_t>(i)], level[static_cast<size_t>(i) + 1]);
		}
		river.HalfWidth = shape.Width * 0.5f;
		river.Depth = shape.Depth;
		if (hydraulic)
		{
			river.HalfWidth = *std::max_element(halfWidth.begin(), halfWidth.end());
			river.Depth = *std::max_element(depth.begin(), depth.end());
			river.HalfWidths = halfWidth;
		}
		for (int i = 0; i < n; ++i)
		{
			river.Points.push_back({ pts[static_cast<size_t>(i)].x, level[static_cast<size_t>(i)], pts[static_cast<size_t>(i)].y });
			const int a = std::max(0, i - 6), b = std::min(n - 1, i + 6);
			const float run = std::max(glm::length(pts[static_cast<size_t>(b)] - pts[static_cast<size_t>(a)]), 1e-3f);
			const float slope = (level[static_cast<size_t>(a)] - level[static_cast<size_t>(b)]) / run;
			// With a discharge the speed is Manning's for the channel of this point (its width, its depth, the slope of the water);
			// without one, the old rule of thumb: faster the steeper the reach.
			float speed = hydraulic ? ManningSpeed(2.0f * halfWidth[static_cast<size_t>(i)], depth[static_cast<size_t>(i)], slope, shape.Hydraulics)
				: glm::clamp(0.9f + 18.0f * slope, 0.9f, 4.5f);
			if (hasLake)   // the current dies down as the river reaches the still water of the lake
				speed *= glm::smoothstep(lake.Radius * 0.9f, lake.Radius + shape.MouthLength, lake.Outline.EffectiveDistance(pts[static_cast<size_t>(i)], lake.Center));
			river.Speed.push_back(speed);
		}
		river.MouthLength = hasLake ? shape.MouthLength : 0.0f;

		// Carve the valley: V-shaped slopes down to a flat bed, banks kept above the water.
		const float widest = river.HalfWidth, farthest = widest * 6.0f;
		glm::vec2 lo(1e9f), hi(-1e9f);
		for (const glm::vec2& q : pts) { lo = glm::min(lo, q); hi = glm::max(hi, q); }
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				if (pos.x < lo.x - farthest || pos.x > hi.x + farthest || pos.y < lo.y - farthest || pos.y > hi.y + farthest) continue;
				float bestD = 1e9f, bestLevel = 0.0f, w = widest, bestDepth = shape.Depth;
				for (int k = 0; k + 1 < n; ++k)
				{
					const glm::vec2 a = pts[static_cast<size_t>(k)], ab = pts[static_cast<size_t>(k) + 1] - a;
					const float tt = glm::clamp(glm::dot(pos - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
					const float d = glm::length(pos - (a + ab * tt));
					if (d < bestD)
					{
						bestD = d; bestLevel = glm::mix(level[static_cast<size_t>(k)], level[static_cast<size_t>(k) + 1], tt);
						if (hydraulic)   // the channel here: between the widths and depths of the two ends of the segment
						{
							w = glm::mix(halfWidth[static_cast<size_t>(k)], halfWidth[static_cast<size_t>(k) + 1], tt);
							bestDepth = glm::mix(depth[static_cast<size_t>(k)], depth[static_cast<size_t>(k) + 1], tt);
						}
					}
				}
				const float reach = w * 6.0f;
				if (bestD > reach) continue;
				const float bed = bestLevel - bestDepth;
				const float cap = bed + std::max(bestD - w * 0.55f, 0.0f) * 0.6f;                              // valley wall
				const float bank = bestLevel + 0.4f * glm::smoothstep(w * 0.7f, w * 1.4f, bestD);            // never below the water on land
				float h = hm.At(i, j);
				// Hills above the valley wall are cut down to it, with the cut fading out toward `reach` (a hard stop there left a cliff along the whole river).
				h -= std::max(h - cap, 0.0f) * (1.0f - glm::smoothstep(reach * 0.5f, reach, bestD));
				// The raised bank must not fill the lake basin where the river arrives (it would cut the lake with a flat shelf and a cliff).
				const float lakeFree = hasLake ? glm::smoothstep(lake.Radius * 0.85f, lake.Radius * 1.2f, lake.Outline.EffectiveDistance(pos, lake.Center)) : 1.0f;
				if (bestD > w * 0.7f) h = glm::mix(h, std::max(h, bank), lakeFree * (1.0f - glm::smoothstep(reach * 0.62f, reach, bestD)));   // fades out toward `reach` (a hard stop left a dam along the river)
				hm.At(i, j) = h;
			}
		}
		return river;
	}
}
