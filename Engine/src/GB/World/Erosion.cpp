#include "Erosion.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		struct Rng
		{
			uint32_t s;
			float Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return static_cast<float>(s & 0xFFFFFF) / 16777216.0f; }
		};

		// Bilinear height and gradient in texel units.
		void HeightAndGradient(const Heightmap& hm, float fx, float fz, float& h, glm::vec2& g)
		{
			const int n = static_cast<int>(hm.GetResolution());
			const int i = std::clamp(static_cast<int>(fx), 0, n - 2), j = std::clamp(static_cast<int>(fz), 0, n - 2);
			const float u = fx - static_cast<float>(i), v = fz - static_cast<float>(j);
			const float h00 = hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)), h10 = hm.At(static_cast<uint32_t>(i + 1), static_cast<uint32_t>(j));
			const float h01 = hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j + 1)), h11 = hm.At(static_cast<uint32_t>(i + 1), static_cast<uint32_t>(j + 1));
			g = { (h10 - h00) * (1.0f - v) + (h11 - h01) * v, (h01 - h00) * (1.0f - u) + (h11 - h10) * u };
			h = h00 * (1.0f - u) * (1.0f - v) + h10 * u * (1.0f - v) + h01 * (1.0f - u) * v + h11 * u * v;
		}
	}

	ErosionStats HydraulicErosion::Run(Heightmap& hm, const ErosionParams& p)
	{
		ErosionStats stats;
		const int n = static_cast<int>(hm.GetResolution());
		if (n < 8) return stats;
		const float spacing = hm.GetSpacing();
		const glm::vec2 lo = hm.GetMin();
		Rng rng{ p.Seed * 2654435761u + 12345u };
		if (rng.s == 0) rng.s = 1;
		const int R = std::max(p.BrushRadius, 1);
		auto touch = [&](int i, int j)
		{
			stats.MinI = std::min(stats.MinI, i); stats.MaxI = std::max(stats.MaxI, i);
			stats.MinJ = std::min(stats.MinJ, j); stats.MaxJ = std::max(stats.MaxJ, j);
		};

		for (int d = 0; d < p.Droplets; ++d)
		{
			glm::vec2 pos;
			if (p.Radius > 0.0f)
			{
				const float a = rng.Next() * 6.2831853f, r = std::sqrt(rng.Next()) * p.Radius;
				pos = (p.Center + glm::vec2(std::cos(a), std::sin(a)) * r - lo) / spacing;
			}
			else
				pos = glm::vec2(rng.Next(), rng.Next()) * static_cast<float>(n - 1);
			glm::vec2 dir(0.0f);
			float speed = 1.0f, water = 1.0f, sediment = 0.0f;

			for (int life = 0; life < p.Lifetime; ++life)
			{
				if (pos.x < 1.0f || pos.y < 1.0f || pos.x >= static_cast<float>(n - 2) || pos.y >= static_cast<float>(n - 2)) break;
				float h; glm::vec2 g;
				HeightAndGradient(hm, pos.x, pos.y, h, g);
				dir = dir * p.Inertia - g * (1.0f - p.Inertia);
				const float len = glm::length(dir);
				if (len < 1e-6f)
				{
					const float a = rng.Next() * 6.2831853f;   // flat ground: wander
					dir = { std::cos(a), std::sin(a) };
				}
				else
					dir /= len;
				const glm::vec2 next = pos + dir;
				if (next.x < 1.0f || next.y < 1.0f || next.x >= static_cast<float>(n - 2) || next.y >= static_cast<float>(n - 2)) break;
				float nh; glm::vec2 ng;
				HeightAndGradient(hm, next.x, next.y, nh, ng);
				const float dh = nh - h;

				const float capacity = std::max(-dh, p.MinSlope) * speed * water * p.Capacity;
				const int ci = static_cast<int>(pos.x), cj = static_cast<int>(pos.y);
				const float u = pos.x - static_cast<float>(ci), v = pos.y - static_cast<float>(cj);
				if (sediment > capacity || dh > 0.0f)
				{
					// drop sediment on the four texels around the droplet (filling the pit when it is going uphill)
					const float amount = dh > 0.0f ? std::min(dh, sediment) : (sediment - capacity) * p.DepositSpeed;
					const float s = std::min(amount, sediment);
					sediment -= s;
					const float w[4] = { (1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v };
					const int di[4] = { 0, 1, 0, 1 }, dj[4] = { 0, 0, 1, 1 };
					for (int k = 0; k < 4; ++k)
					{
						hm.At(static_cast<uint32_t>(ci + di[k]), static_cast<uint32_t>(cj + dj[k])) += s * w[k];
						touch(ci + di[k], cj + dj[k]);
					}
					stats.Deposited += s;
				}
				else
				{
					const float amount = std::min((capacity - sediment) * p.ErodeSpeed * p.Strength, -dh);   // never digs deeper than the drop of this step
					// take it from a small brush so the gullies are smooth
					float wsum = 0.0f;
					float weights[25];
					int count = 0;
					for (int oj = -R; oj <= R; ++oj)
						for (int oi = -R; oi <= R; ++oi)
						{
							const float dist = std::sqrt(static_cast<float>(oi * oi + oj * oj));
							const float wgt = std::max(0.0f, static_cast<float>(R) + 0.5f - dist);
							weights[count++] = wgt;
							wsum += wgt;
						}
					count = 0;
					float taken = 0.0f;
					for (int oj = -R; oj <= R; ++oj)
						for (int oi = -R; oi <= R; ++oi, ++count)
						{
							const int ti = ci + oi, tj = cj + oj;
							if (ti < 0 || tj < 0 || ti >= n || tj >= n || weights[count] <= 0.0f) continue;
							const float part = amount * weights[count] / wsum;
							float& cell = hm.At(static_cast<uint32_t>(ti), static_cast<uint32_t>(tj));
							cell -= part;
							taken += part;
							touch(ti, tj);
						}
					sediment += taken;
					stats.Eroded += taken;
				}
				speed = std::sqrt(std::max(speed * speed + dh * -p.Gravity, 0.0f));
				water *= 1.0f - p.Evaporation;
				pos = next;
			}
			// the droplet dries up: what it still carries is left where it stopped
			if (sediment > 0.0f && pos.x >= 1.0f && pos.y >= 1.0f && pos.x < static_cast<float>(n - 2) && pos.y < static_cast<float>(n - 2))
			{
				hm.At(static_cast<uint32_t>(pos.x), static_cast<uint32_t>(pos.y)) += sediment * 0.5f;
				hm.At(static_cast<uint32_t>(pos.x) + 1, static_cast<uint32_t>(pos.y)) += sediment * 0.5f;
				stats.Deposited += sediment;
			}
		}
		return stats;
	}
}
