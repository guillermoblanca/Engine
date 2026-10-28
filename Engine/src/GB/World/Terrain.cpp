#include "Terrain.h"
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		float Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
		}

		float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

		// Value noise in [0,1]. `period` > 0 makes it tile every `period` cells (for seamless textures).
		float ValueNoise(float x, float y, uint32_t seed, int period = 0)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float fx = Smooth(x - static_cast<float>(ix)), fy = Smooth(y - static_cast<float>(iy));
			auto wrap = [&](int v) { return period > 0 ? ((v % period) + period) % period : v; };
			const float a = Hash(wrap(ix), wrap(iy), seed), b = Hash(wrap(ix + 1), wrap(iy), seed);
			const float c = Hash(wrap(ix), wrap(iy + 1), seed), d = Hash(wrap(ix + 1), wrap(iy + 1), seed);
			return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
		}

		float Fbm(float x, float y, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			for (int o = 0; o < octaves; ++o, amp *= 0.5f, x *= 2.0f, y *= 2.0f)
			{
				sum += amp * ValueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u);
				norm += amp;
			}
			return sum / norm;
		}

		// Tileable fractal noise for textures: `cells` base cells per tile, octaves double the frequency.
		float TileFbm(float u, float v, int cells, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			int period = cells;
			for (int o = 0; o < octaves; ++o, amp *= 0.5f, period *= 2)
			{
				sum += amp * ValueNoise(u * static_cast<float>(period), v * static_cast<float>(period), seed + static_cast<uint32_t>(o) * 31u, period);
				norm += amp;
			}
			return sum / norm;
		}

		// Dune cross-section: phase 0..1 along the wind; 0 = trough, `crest` = crest, back to 0 on the steep lee side.
		float DuneProfile(float phase, float crest, float sharpness)
		{
			float cosine, triangle;
			if (phase < crest)
			{
				const float t = phase / crest;
				cosine = 0.5f - 0.5f * std::cos(kPi * t);
				triangle = t;
			}
			else
			{
				const float t = (phase - crest) / (1.0f - crest);
				cosine = 0.5f + 0.5f * std::cos(kPi * t);
				triangle = 1.0f - t;
			}
			return glm::mix(cosine, triangle, sharpness);
		}
	}

	// ------------------------------------------------------------------ Heightmap
	Heightmap::Heightmap(uint32_t resolution, float size, const glm::vec2& center)
		: m_Resolution(std::max(resolution, 2u)), m_Size(size), m_Center(center),
		  m_Heights(static_cast<size_t>(m_Resolution) * m_Resolution, 0.0f)
	{
	}

	bool Heightmap::Contains(float x, float z) const
	{
		const glm::vec2 lo = GetMin(), hi = GetMax();
		return x >= lo.x && x <= hi.x && z >= lo.y && z <= hi.y;
	}

	glm::vec2 Heightmap::SamplePosition(uint32_t i, uint32_t j) const
	{
		return GetMin() + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * GetSpacing();
	}

	float Heightmap::GetHeight(float x, float z) const
	{
		const glm::vec2 lo = GetMin();
		const float maxIndex = static_cast<float>(m_Resolution - 1);
		const float fx = glm::clamp((x - lo.x) / GetSpacing(), 0.0f, maxIndex);
		const float fz = glm::clamp((z - lo.y) / GetSpacing(), 0.0f, maxIndex);
		const uint32_t i0 = std::min(static_cast<uint32_t>(fx), m_Resolution - 2), j0 = std::min(static_cast<uint32_t>(fz), m_Resolution - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		return glm::mix(glm::mix(At(i0, j0), At(i0 + 1, j0), tx), glm::mix(At(i0, j0 + 1), At(i0 + 1, j0 + 1), tx), tz);
	}

	glm::vec3 Heightmap::GetNormal(float x, float z) const
	{
		const float e = GetSpacing();
		const float dx = GetHeight(x + e, z) - GetHeight(x - e, z);
		const float dz = GetHeight(x, z + e) - GetHeight(x, z - e);
		return glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
	}

	float Heightmap::GetMinHeight() const { return *std::min_element(m_Heights.begin(), m_Heights.end()); }
	float Heightmap::GetMaxHeight() const { return *std::max_element(m_Heights.begin(), m_Heights.end()); }

	// ------------------------------------------------------------------ generators
	std::unique_ptr<Terrain> Terrain::CreateDunes(const DuneParams& p, float size, uint32_t resolution, TerrainStyle style, bool gpu)
	{
		std::unique_ptr<Terrain> t(new Terrain(resolution, size));
		t->m_WindAngle = glm::radians(p.WindAngleDegrees);
		const glm::vec2 wind(std::cos(t->m_WindAngle), std::sin(t->m_WindAngle));
		Heightmap& hm = t->m_Heightmap;

		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				// Crest lines meander: shift the along-wind coordinate by low-frequency noise.
				const float warp = (Fbm(pos.x / p.WarpScale, pos.y / p.WarpScale, 3, p.Seed) - 0.5f) * 2.0f * p.Warp;
				const float s = glm::dot(pos, wind) / p.Wavelength + warp;
				const float phase = s - std::floor(s);
				// Neighbouring dune ridges differ in size.
				const float cell = std::floor(s);
				const float across = glm::dot(pos, glm::vec2(-wind.y, wind.x));
				const float size01 = ValueNoise(cell * 3.7f, across / 160.0f, p.Seed + 7);
				const float amplitude = p.Height * (1.0f + p.SizeVariation * (size01 - 0.5f) * 2.0f);
				const float hills = (Fbm(pos.x / 170.0f, pos.y / 170.0f, 3, p.Seed + 3) - 0.5f) * 2.0f * p.BaseHills;
				hm.At(i, j) = amplitude * DuneProfile(phase, p.CrestPosition, p.CrestSharpness) + hills;
			}
		}

		// Crest points: local maxima along the wind direction.
		const float step = std::max(p.Wavelength * 0.08f, hm.GetSpacing() * 2.0f);
		const float minHeight = hm.GetMinHeight() + 0.55f * (hm.GetMaxHeight() - hm.GetMinHeight());
		for (uint32_t j = 2; j + 2 < hm.GetResolution(); j += 3)
		{
			for (uint32_t i = 2; i + 2 < hm.GetResolution(); i += 3)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				const float h = hm.At(i, j);
				if (h < minHeight)
					continue;
				const float ahead = hm.GetHeight(pos.x + wind.x * step, pos.y + wind.y * step);
				const float behind = hm.GetHeight(pos.x - wind.x * step, pos.y - wind.y * step);
				if (h >= ahead && h >= behind)
					t->m_Crests.push_back({ pos.x, h, pos.y });
			}
		}
		t->Finish(style, gpu);
		return t;
	}

	std::unique_ptr<Terrain> Terrain::CreateHills(const HillParams& p, float size, uint32_t resolution, TerrainStyle style, bool gpu)
	{
		std::unique_ptr<Terrain> t(new Terrain(resolution, size));
		t->m_SurfaceMaps = p.SurfaceMaps;
		t->m_ShoreSubdivision = std::clamp(p.ShoreSubdivision, 1, 8);
		t->m_ShoreMargin = std::max(p.ShoreMarginCells, 0);
		Heightmap& hm = t->m_Heightmap;
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				float n = Fbm(pos.x / p.Scale, pos.y / p.Scale, p.Octaves, p.Seed);
				n = glm::clamp((n - 0.25f) / 0.6f, 0.0f, 1.0f);
				n = glm::mix(n, n * n * (3.0f - 2.0f * n), p.Flatten + 0.35f);   // rounded hilltops, flatter valleys
				float h = n * p.Height;
				if (p.LakeRadius > 0.0f)
				{
					const float d = glm::length(pos - p.LakeCenter);
					if (p.LakeBankSlope > 0.0f)
					{
						// Hills above the basin are cut down to a cone that rises at most LakeBankSlope away from the shore (smooth minimum,
						// fading out far from the lake): the water gets a gentle bank, not a cliff.
						const float cone = p.WaterLevel + 0.45f + std::max(d - p.LakeRadius, 0.0f) * p.LakeBankSlope;
						const float k = 2.0f, m = std::max(k - std::abs(h - cone), 0.0f) / k;
						const float cut = std::min(h, cone) - m * m * k * 0.25f;
						h = glm::mix(cut, h, glm::smoothstep(p.LakeRadius * 3.0f, p.LakeRadius * 5.0f, d));
					}
					// Banks: lift the ground near the lake above the water, then dig the basin.
					const float bank = glm::smoothstep(p.LakeRadius * 2.8f, p.LakeRadius * 1.5f, d);
					h = glm::mix(h, glm::max(h, p.WaterLevel + 0.45f), bank);
					const float basin = glm::smoothstep(p.LakeRadius * 1.15f, p.LakeRadius * 0.15f, d);
					h = glm::mix(h, p.WaterLevel - p.LakeDepth, basin);
				}
				hm.At(i, j) = h;
			}
		}
		if (!p.RiverPath.empty() && p.RiverPath.size() >= 2)
			t->CarveRiver(p);
		if (p.LakeRadius > 0.0f)
		{
			t->m_HasLake = true;
			t->m_WaterLevel = p.WaterLevel;
			t->m_LakeCenter = p.LakeCenter;
			t->m_LakeRadius = p.LakeRadius;
		}
		t->Finish(style, gpu);
		return t;
	}

	// ------------------------------------------------------------------ river
	namespace
	{
		constexpr float kMouthLength = 16.0f;   // meters over which a river blends into the lake it flows into

		glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
		{
			const float t2 = t * t, t3 = t2 * t;
			return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
		}
	}

	void Terrain::CarveRiver(const HillParams& p)
	{
		Heightmap& hm = m_Heightmap;
		// Dense polyline, about 1.5 m apart.
		std::vector<glm::vec2> pts;
		const auto& c = p.RiverPath;
		for (size_t i = 0; i + 1 < c.size(); ++i)
		{
			const glm::vec2 p0 = c[i == 0 ? 0 : i - 1], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(i + 2, c.size() - 1)];
			const int n = std::max(2, static_cast<int>(glm::length(p2 - p1) / 1.5f));
			for (int k = 0; k < n; ++k) pts.push_back(CatmullRom(p0, p1, p2, p3, static_cast<float>(k) / static_cast<float>(n)));
		}
		pts.push_back(c.back());
		const int n = static_cast<int>(pts.size());

		// Water level: follows the (smoothed) ground but only ever goes down.
		std::vector<float> base(static_cast<size_t>(n)), level(static_cast<size_t>(n));
		for (int i = 0; i < n; ++i) base[static_cast<size_t>(i)] = hm.GetHeight(pts[static_cast<size_t>(i)].x, pts[static_cast<size_t>(i)].y);
		for (int i = 0; i < n; ++i)
		{
			float sum = 0.0f; int cnt = 0;
			for (int k = std::max(0, i - 12); k <= std::min(n - 1, i + 12); ++k) { sum += base[static_cast<size_t>(k)]; ++cnt; }
			const float smooth = sum / static_cast<float>(cnt) - p.RiverDepth * 0.3f;
			level[static_cast<size_t>(i)] = i == 0 ? smooth : std::min(smooth, level[static_cast<size_t>(i) - 1] - 0.0045f * 1.5f);
		}
		if (p.LakeRadius > 0.0f)
		{
			// Near the lake the river comes down to the lake level (at most ~0.35 m of drop per meter), cutting a channel through the bank.
			for (int i = 0; i < n; ++i)
			{
				const float dist = glm::length(pts[static_cast<size_t>(i)] - p.LakeCenter);
				level[static_cast<size_t>(i)] = std::min(level[static_cast<size_t>(i)], p.WaterLevel + std::max(dist - p.LakeRadius, 0.0f) * 0.35f);
				if (i > 0) level[static_cast<size_t>(i)] = std::min(level[static_cast<size_t>(i)], level[static_cast<size_t>(i) - 1] - 0.0045f * 1.5f);
			}
			// The river meets the lake at exactly the lake level (no step where the two surfaces overlap); never below it before that.
			for (int i = 0; i < n; ++i)
				level[static_cast<size_t>(i)] = glm::length(pts[static_cast<size_t>(i)] - p.LakeCenter) <= p.LakeRadius ? p.WaterLevel : std::max(level[static_cast<size_t>(i)], p.WaterLevel);
			for (int i = n - 2; i >= 0; --i)
				level[static_cast<size_t>(i)] = std::max(level[static_cast<size_t>(i)], level[static_cast<size_t>(i) + 1]);
		}
		m_River.HalfWidth = p.RiverWidth * 0.5f;
		m_River.Depth = p.RiverDepth;
		for (int i = 0; i < n; ++i)
		{
			m_River.Points.push_back({ pts[static_cast<size_t>(i)].x, level[static_cast<size_t>(i)], pts[static_cast<size_t>(i)].y });
			const int a = std::max(0, i - 6), b = std::min(n - 1, i + 6);
			const float run = std::max(glm::length(pts[static_cast<size_t>(b)] - pts[static_cast<size_t>(a)]), 1e-3f);
			const float slope = (level[static_cast<size_t>(a)] - level[static_cast<size_t>(b)]) / run;
			float speed = glm::clamp(0.9f + 18.0f * slope, 0.9f, 4.5f);
			if (p.LakeRadius > 0.0f)   // the current dies down as the river reaches the still water of the lake
				speed *= glm::smoothstep(p.LakeRadius * 0.9f, p.LakeRadius + kMouthLength, glm::length(pts[static_cast<size_t>(i)] - p.LakeCenter));
			m_River.Speed.push_back(speed);
		}
		m_River.MouthLength = p.LakeRadius > 0.0f ? kMouthLength : 0.0f;

		// Carve the valley: V-shaped slopes down to a flat bed, banks kept above the water.
		const float w = m_River.HalfWidth, reach = w * 6.0f;
		glm::vec2 lo(1e9f), hi(-1e9f);
		for (const glm::vec2& q : pts) { lo = glm::min(lo, q); hi = glm::max(hi, q); }
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				if (pos.x < lo.x - reach || pos.x > hi.x + reach || pos.y < lo.y - reach || pos.y > hi.y + reach) continue;
				float bestD = 1e9f, bestLevel = 0.0f;
				for (int k = 0; k + 1 < n; ++k)
				{
					const glm::vec2 a = pts[static_cast<size_t>(k)], ab = pts[static_cast<size_t>(k) + 1] - a;
					const float tt = glm::clamp(glm::dot(pos - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
					const float d = glm::length(pos - (a + ab * tt));
					if (d < bestD) { bestD = d; bestLevel = glm::mix(level[static_cast<size_t>(k)], level[static_cast<size_t>(k) + 1], tt); }
				}
				if (bestD > reach) continue;
				const float bed = bestLevel - p.RiverDepth;
				const float cap = bed + std::max(bestD - w * 0.55f, 0.0f) * 0.6f;                              // valley wall
				const float bank = bestLevel + 0.4f * glm::smoothstep(w * 0.7f, w * 1.4f, bestD);            // never below the water on land
				float h = hm.At(i, j);
				// Hills above the valley wall are cut down to it, with the cut fading out toward `reach` (a hard stop there left a cliff along the whole river).
				h -= std::max(h - cap, 0.0f) * (1.0f - glm::smoothstep(reach * 0.5f, reach, bestD));
				// The raised bank must not fill the lake basin where the river arrives (it would cut the lake with a flat shelf and a cliff).
				const float lakeFree = p.LakeRadius > 0.0f ? glm::smoothstep(p.LakeRadius * 0.85f, p.LakeRadius * 1.2f, glm::length(pos - p.LakeCenter)) : 1.0f;
				if (bestD > w * 0.7f) h = glm::mix(h, std::max(h, bank), lakeFree * (1.0f - glm::smoothstep(reach * 0.62f, reach, bestD)));   // fades out toward `reach` (a hard stop left a dam along the river)
				hm.At(i, j) = h;
			}
		}
	}

	float Terrain::GetRiverLevelAt(float x, float z) const
	{
		if (m_River.Points.empty()) return -1e9f;
		float bestD = 1e9f, bestLevel = -1e9f;
		const auto& P = m_River.Points;
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
			const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
			const float d = glm::length(glm::vec2(x, z) - (a + ab * tt));
			if (d < bestD) { bestD = d; bestLevel = glm::mix(P[k].y, P[k + 1].y, tt); }
		}
		return bestD <= m_River.HalfWidth * 1.2f && m_Heightmap.GetHeight(x, z) < bestLevel ? bestLevel : -1e9f;
	}

	float Terrain::GetRiverSurfaceLevel(float x, float z) const
	{
		float bestD = 1e9f, lvl = -1e9f;
		const auto& P = m_River.Points;
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
			const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
			const float d = glm::length(glm::vec2(x, z) - (a + ab * tt));
			if (d < bestD) { bestD = d; lvl = glm::mix(P[k].y, P[k + 1].y, tt); }
		}
		return lvl;
	}

	float Terrain::DistanceToRiver(float x, float z) const
	{
		float best = 1e9f;
		const auto& P = m_River.Points;
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
			const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
			best = std::min(best, glm::length(glm::vec2(x, z) - (a + ab * tt)));
		}
		return best;
	}

	glm::vec2 Terrain::GetRiverFlow(float x, float z) const
	{
		if (m_River.Points.empty()) return glm::vec2(0.0f);
		float bestD = 1e9f;
		glm::vec2 dir(0.0f);
		float speed = 0.0f;
		const auto& P = m_River.Points;
		for (size_t k = 0; k + 1 < P.size(); ++k)
		{
			const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
			const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
			const float d = glm::length(glm::vec2(x, z) - (a + ab * tt));
			if (d < bestD) { bestD = d; dir = ab; speed = glm::mix(m_River.Speed[k], m_River.Speed[k + 1], tt); }
		}
		if (bestD > m_River.HalfWidth * 1.1f || glm::length(dir) < 1e-6f) return glm::vec2(0.0f);
		const float edge = glm::clamp(bestD / m_River.HalfWidth, 0.0f, 1.0f);
		return glm::normalize(dir) * speed * (1.0f - 0.65f * edge * edge);
	}

	// ------------------------------------------------------------------ shore refinement
	// The grid is uniform, so where the water meets the ground (lake shore, river banks) the waterline follows its
	// ~0.5 m cells and looks stepped. Cells there are split N x N and the new vertices sit on a smooth cubic surface
	// through the heightmap. Vertices on the border of the refined area keep the straight coarse edge, so it stays
	// watertight against the plain grid without any stitching.
	float Terrain::SmoothHeight(float x, float z) const
	{
		const Heightmap& hm = m_Heightmap;
		const int n = static_cast<int>(hm.GetResolution());
		const glm::vec2 lo = hm.GetMin();
		const float s = hm.GetSpacing(), maxIndex = static_cast<float>(n - 1);
		const float fx = glm::clamp((x - lo.x) / s, 0.0f, maxIndex), fz = glm::clamp((z - lo.y) / s, 0.0f, maxIndex);
		const int i0 = std::min(static_cast<int>(fx), n - 2), j0 = std::min(static_cast<int>(fz), n - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		auto H = [&](int i, int j) { return hm.At(static_cast<uint32_t>(std::clamp(i, 0, n - 1)), static_cast<uint32_t>(std::clamp(j, 0, n - 1))); };
		auto cubic = [](float p0, float p1, float p2, float p3, float t)
		{
			return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
		};
		float rows[4];
		for (int k = 0; k < 4; ++k)
			rows[k] = cubic(H(i0 - 1, j0 - 1 + k), H(i0, j0 - 1 + k), H(i0 + 1, j0 - 1 + k), H(i0 + 2, j0 - 1 + k), tx);
		const float h = cubic(rows[0], rows[1], rows[2], rows[3], tz);
		const float a = H(i0, j0), b = H(i0 + 1, j0), c = H(i0, j0 + 1), d = H(i0 + 1, j0 + 1);
		return glm::clamp(h, std::min(std::min(a, b), std::min(c, d)), std::max(std::max(a, b), std::max(c, d)));   // no ringing at sharp banks
	}

	glm::vec3 Terrain::GetSmoothNormal(float x, float z) const
	{
		const float e = m_Heightmap.GetSpacing() * 0.5f;
		const float dx = SmoothHeight(x + e, z) - SmoothHeight(x - e, z);
		const float dz = SmoothHeight(x, z + e) - SmoothHeight(x, z - e);
		return glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
	}

	bool Terrain::IsCellRefined(int ci, int cj) const
	{
		const int cells = static_cast<int>(m_Heightmap.GetResolution()) - 1;
		return ci >= 0 && cj >= 0 && ci < cells && cj < cells && !m_RefinedCells.empty() && m_RefinedCells[static_cast<size_t>(cj) * static_cast<size_t>(cells) + static_cast<size_t>(ci)] != 0;
	}

	float Terrain::FineHeight(int fi, int fj) const
	{
		const int N = m_ShoreSubdivision;
		const int ci = fi / N, cj = fj / N, a = fi % N, b = fj % N;
		const Heightmap& hm = m_Heightmap;
		auto at = [&](int i, int j) { return hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)); };
		if (a == 0 && b != 0 && (!IsCellRefined(ci - 1, cj) || !IsCellRefined(ci, cj)))   // edge shared with the plain grid
			return glm::mix(at(ci, cj), at(ci, cj + 1), static_cast<float>(b) / static_cast<float>(N));
		if (b == 0 && a != 0 && (!IsCellRefined(ci, cj - 1) || !IsCellRefined(ci, cj)))
			return glm::mix(at(ci, cj), at(ci + 1, cj), static_cast<float>(a) / static_cast<float>(N));
		const glm::vec2 pos = hm.GetMin() + glm::vec2(static_cast<float>(fi), static_cast<float>(fj)) * (hm.GetSpacing() / static_cast<float>(N));
		return SmoothHeight(pos.x, pos.y);
	}

	Vertex Terrain::MakeFineVertex(int fi, int fj) const
	{
		const Heightmap& hm = m_Heightmap;
		const float step = hm.GetSpacing() / static_cast<float>(m_ShoreSubdivision);
		const glm::vec2 pos = hm.GetMin() + glm::vec2(static_cast<float>(fi), static_cast<float>(fj)) * step;
		Vertex v;
		v.Position = { pos.x, FineHeight(fi, fj), pos.y };
		v.Normal = GetSmoothNormal(pos.x, pos.y);
		v.UV = { pos.x / 4.0f, pos.y / 4.0f };   // same detail tiling as the grid (tileMeters in Finish)
		v.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
		return v;
	}

	std::vector<uint8_t> Terrain::FlagShoreCells(int marginCells) const
	{
		const Heightmap& hm = m_Heightmap;
		const int cells = static_cast<int>(hm.GetResolution()) - 1;
		const glm::vec2 lo = hm.GetMin();
		const float s = hm.GetSpacing();
		std::vector<uint8_t> flags(static_cast<size_t>(cells) * static_cast<size_t>(cells), 0);
		auto cellAt = [&](int ci, int cj) -> uint8_t& { return flags[static_cast<size_t>(cj) * static_cast<size_t>(cells) + static_cast<size_t>(ci)]; };
		auto center = [&](int ci, int cj) { return lo + (glm::vec2(static_cast<float>(ci), static_cast<float>(cj)) + 0.5f) * s; };

		if (m_HasLake)   // cells whose heights straddle the lake level, near the lake
		{
			const float reach = m_LakeRadius * 1.8f + s;
			const int c0 = std::max(0, static_cast<int>((m_LakeCenter.x - reach - lo.x) / s)), c1 = std::min(cells - 1, static_cast<int>((m_LakeCenter.x + reach - lo.x) / s));
			const int r0 = std::max(0, static_cast<int>((m_LakeCenter.y - reach - lo.y) / s)), r1 = std::min(cells - 1, static_cast<int>((m_LakeCenter.y + reach - lo.y) / s));
			for (int cj = r0; cj <= r1; ++cj)
				for (int ci = c0; ci <= c1; ++ci)
				{
					if (glm::length(center(ci, cj) - m_LakeCenter) > reach) continue;
					const float a = hm.At(ci, cj), b = hm.At(ci + 1, cj), c = hm.At(ci, cj + 1), d = hm.At(ci + 1, cj + 1);
					const float lowest = std::min(std::min(a, b), std::min(c, d)), highest = std::max(std::max(a, b), std::max(c, d));
					if (lowest <= m_WaterLevel + 0.6f && highest >= m_WaterLevel - 0.6f)
						cellAt(ci, cj) = 1;
				}
		}
		if (!m_River.Points.empty())   // the bank zone on both sides of the river axis
		{
			const float inner = m_River.HalfWidth * 0.5f, outer = m_River.HalfWidth * 1.8f + s;
			const auto& P = m_River.Points;
			for (size_t k = 0; k + 1 < P.size(); ++k)
			{
				const glm::vec2 a(P[k].x, P[k].z), b(P[k + 1].x, P[k + 1].z), ab = b - a;
				const glm::vec2 bmin = glm::min(a, b) - outer, bmax = glm::max(a, b) + outer;
				const int c0 = std::max(0, static_cast<int>((bmin.x - lo.x) / s)), c1 = std::min(cells - 1, static_cast<int>((bmax.x - lo.x) / s));
				const int r0 = std::max(0, static_cast<int>((bmin.y - lo.y) / s)), r1 = std::min(cells - 1, static_cast<int>((bmax.y - lo.y) / s));
				for (int cj = r0; cj <= r1; ++cj)
					for (int ci = c0; ci <= c1; ++ci)
					{
						const glm::vec2 q = center(ci, cj);
						const float tt = glm::clamp(glm::dot(q - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
						const float d = glm::length(q - (a + ab * tt));
						if (d >= inner && d <= outer)
							cellAt(ci, cj) = 1;
					}
			}
		}
		// Grow the area so the refinement has a margin to fade out into.
		for (int pass = 0; pass < marginCells; ++pass)
		{
			std::vector<uint8_t> grown = flags;
			for (int cj = 0; cj < cells; ++cj)
				for (int ci = 0; ci < cells; ++ci)
					if (flags[static_cast<size_t>(cj) * static_cast<size_t>(cells) + static_cast<size_t>(ci)])
						for (int dj = -1; dj <= 1; ++dj)
							for (int di = -1; di <= 1; ++di)
							{
								const int x = ci + di, y = cj + dj;
								if (x >= 0 && y >= 0 && x < cells && y < cells) grown[static_cast<size_t>(y) * static_cast<size_t>(cells) + static_cast<size_t>(x)] = 1;
							}
			flags.swap(grown);
		}
		return flags;
	}

	void Terrain::BuildShoreRefinement(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		m_FineIndex.clear();
		m_RefinedCells.clear();
		const int N = m_ShoreSubdivision;
		if (N < 2 || (!m_HasLake && m_River.Points.empty())) return;
		std::vector<uint8_t> flags = FlagShoreCells(m_ShoreMargin);
		size_t flagged = 0;
		for (uint8_t f : flags) flagged += f;
		if (flagged == 0) return;
		m_RefinedCells = std::move(flags);

		const int n = static_cast<int>(m_Heightmap.GetResolution()), cells = n - 1;
		m_FineFirst = static_cast<uint32_t>(vertices.size());
		auto vertexAt = [&](int fi, int fj) -> uint32_t
		{
			if (fi % N == 0 && fj % N == 0)
				return static_cast<uint32_t>((fj / N) * n + fi / N);   // a grid vertex
			const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(fj)) << 32) | static_cast<uint32_t>(fi);
			const auto it = m_FineIndex.find(key);
			if (it != m_FineIndex.end()) return it->second;
			const uint32_t index = static_cast<uint32_t>(vertices.size());
			vertices.push_back(MakeFineVertex(fi, fj));
			m_FineIndex.emplace(key, index);
			return index;
		};

		std::vector<uint32_t> out;
		out.reserve(indices.size() + flagged * static_cast<size_t>(N) * static_cast<size_t>(N) * 6);
		for (int cj = 0; cj < cells; ++cj)
		{
			for (int ci = 0; ci < cells; ++ci)
			{
				const size_t cell = static_cast<size_t>(cj) * static_cast<size_t>(cells) + static_cast<size_t>(ci);
				if (!m_RefinedCells[cell])
				{
					out.insert(out.end(), indices.begin() + static_cast<std::ptrdiff_t>(cell * 6), indices.begin() + static_cast<std::ptrdiff_t>(cell * 6 + 6));
					continue;
				}
				for (int b = 0; b < N; ++b)
					for (int a = 0; a < N; ++a)
					{
						const int fi = ci * N + a, fj = cj * N + b;
						const uint32_t i0 = vertexAt(fi, fj), i1 = vertexAt(fi + 1, fj), i2 = vertexAt(fi, fj + 1), i3 = vertexAt(fi + 1, fj + 1);
						out.insert(out.end(), { i0, i2, i1, i1, i2, i3 });   // same winding and diagonal as the grid cells
					}
			}
		}
		indices.swap(out);
		GB_CORE_INFO("Terrain shore refinement: {} cells split {}x{} (+{} vertices)", flagged, N, N, vertices.size() - m_FineFirst);
	}

	void Terrain::RefreshMesh(int i0, int j0, int i1, int j1)
	{
		const int n = static_cast<int>(m_Heightmap.GetResolution());
		i0 = std::clamp(i0 - 2, 0, n - 1); j0 = std::clamp(j0 - 2, 0, n - 1);
		i1 = std::clamp(i1 + 2, 0, n - 1); j1 = std::clamp(j1 + 2, 0, n - 1);
		if (!m_Mesh || m_Vertices.size() < static_cast<size_t>(n) * static_cast<size_t>(n) || i1 < i0 || j1 < j0) return;   // no mesh yet: BuildGpu reads the heights as they are
		if (!m_FineIndex.empty())   // refined shore vertices that sit over the edited texels
		{
			const int N = m_ShoreSubdivision;
			uint32_t lowest = ~0u, highest = 0;
			for (int cj = std::max(j0 - 1, 0); cj <= std::min(j1, n - 2); ++cj)
				for (int ci = std::max(i0 - 1, 0); ci <= std::min(i1, n - 2); ++ci)
				{
					if (!IsCellRefined(ci, cj)) continue;
					for (int b = 0; b <= N; ++b)
						for (int a = 0; a <= N; ++a)
						{
							const int fi = ci * N + a, fj = cj * N + b;
							const auto it = m_FineIndex.find((static_cast<uint64_t>(static_cast<uint32_t>(fj)) << 32) | static_cast<uint32_t>(fi));
							if (it == m_FineIndex.end()) continue;
							m_Vertices[it->second] = MakeFineVertex(fi, fj);
							lowest = std::min(lowest, it->second); highest = std::max(highest, it->second);
						}
				}
			if (lowest <= highest)
				m_Mesh->UpdateVertices(lowest, &m_Vertices[lowest], highest - lowest + 1);
		}
		if (m_NormalMap)   // the same area of the high-resolution normal map (SmoothHeight reads two cells around a point)
		{
			const glm::vec2 lo = m_Heightmap.GetMin(), a = m_Heightmap.SamplePosition(static_cast<uint32_t>(i0), static_cast<uint32_t>(j0)), b = m_Heightmap.SamplePosition(static_cast<uint32_t>(i1), static_cast<uint32_t>(j1));
			const float inv = static_cast<float>(m_NormalRes) / m_Heightmap.GetSize(), margin = m_Heightmap.GetSpacing() * 2.0f;
			WriteNormalTexels(static_cast<int>(std::floor((a.x - margin - lo.x) * inv)), static_cast<int>(std::floor((a.y - margin - lo.y) * inv)),
				static_cast<int>(std::ceil((b.x + margin - lo.x) * inv)), static_cast<int>(std::ceil((b.y + margin - lo.y) * inv)));
		}
		for (int j = j0; j <= j1; ++j)
		{
			for (int i = i0; i <= i1; ++i)
			{
				const glm::vec2 pos = m_Heightmap.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				Vertex& v = m_Vertices[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)];
				v.Position.y = m_Heightmap.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				v.Normal = GetSmoothNormal(pos.x, pos.y);
			}
			m_Mesh->UpdateVertices(static_cast<uint32_t>(j * n + i0), &m_Vertices[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i0)], static_cast<uint32_t>(i1 - i0 + 1));
		}
	}

	// ------------------------------------------------------------------ mesh + material
	void Terrain::Finish(TerrainStyle style, bool gpu)
	{
		m_Style = style;
		if (gpu) BuildGpu();
	}

	void Terrain::BuildGpu()
	{
		if (m_Mesh) return;
		const TerrainStyle style = m_Style;
		const Heightmap& hm = m_Heightmap;
		const uint32_t n = hm.GetResolution();
		const float tileMeters = 4.0f;   // one detail texture tile

		std::vector<Vertex> vertices(static_cast<size_t>(n) * n);
		for (uint32_t j = 0; j < n; ++j)
		{
			for (uint32_t i = 0; i < n; ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				Vertex& v = vertices[static_cast<size_t>(j) * n + i];
				v.Position = { pos.x, hm.At(i, j), pos.y };
				v.Normal = GetSmoothNormal(pos.x, pos.y);
				v.UV = { pos.x / tileMeters, pos.y / tileMeters };
				v.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
			}
		}
		std::vector<uint32_t> indices;
		indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
		for (uint32_t j = 0; j + 1 < n; ++j)
		{
			for (uint32_t i = 0; i + 1 < n; ++i)
			{
				const uint32_t i0 = j * n + i, i1 = i0 + 1, i2 = i0 + n, i3 = i2 + 1;
				indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });   // counter-clockwise seen from above (+Y)
			}
		}
		BuildShoreRefinement(vertices, indices);
		m_Mesh = std::make_unique<Mesh>(vertices, indices);
		m_Vertices = std::move(vertices);

		// ---- palette
		glm::vec3 sunlit, shade, crest;
		float grainContrast = 0.22f, rippleDepth = 1.0f, roughness = 0.9f;
		switch (style)
		{
		case TerrainStyle::Desert: sunlit = { 0.80f, 0.62f, 0.38f }; shade = { 0.58f, 0.38f, 0.22f }; crest = { 0.88f, 0.72f, 0.48f }; break;
		case TerrainStyle::Snow: sunlit = { 0.78f, 0.82f, 0.88f }; shade = { 0.50f, 0.58f, 0.72f }; crest = { 0.86f, 0.89f, 0.94f }; rippleDepth = 0.35f; roughness = 0.75f; grainContrast = 0.08f; break;
		default: sunlit = { 0.30f, 0.45f, 0.16f }; shade = { 0.16f, 0.26f, 0.09f }; crest = { 0.42f, 0.52f, 0.20f }; rippleDepth = 0.0f; grainContrast = 0.3f; roughness = 0.95f; break;
		}

		// ---- detail grain (grayscale, tileable)
		{
			constexpr uint32_t R = 256;
			Image grain = Image::Solid(R, R, 255, 255, 255);
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					const float u = static_cast<float>(x) / R, v = static_cast<float>(y) / R;
					const float f = 0.65f * TileFbm(u, v, 24, 3, 11) + 0.35f * ValueNoise(u * 48.0f, v * 48.0f, 29, 48);   // finest grain ~8 cm: no texel-sized speckle
					const float g = glm::clamp(1.0f - grainContrast + grainContrast * 2.0f * f, 0.0f, 1.0f);
					uint8_t* p = &grain.Pixels[(static_cast<size_t>(y) * R + x) * 4];
					p[0] = p[1] = p[2] = static_cast<uint8_t>(g * 255.0f + 0.5f);
				}
			m_Grain = std::make_unique<Texture2D>(grain, TextureEncoding::SRGB);
		}

		// ---- wind ripples (tangent-space normal map, tileable). Wave vector along the wind.
		{
			constexpr uint32_t R = 256;
			constexpr int kWaves = 14;                         // ripples per tile
			const int kx = static_cast<int>(std::lround(std::cos(m_WindAngle) * kWaves));
			const int kz = static_cast<int>(std::lround(std::sin(m_WindAngle) * kWaves));
			std::vector<float> h(static_cast<size_t>(R) * R);
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					const float u = static_cast<float>(x) / R, v = static_cast<float>(y) / R;
					const float wobble = (TileFbm(u, v, 6, 3, 41) - 0.5f) * 1.2f;
					const float phase = 2.0f * kPi * (static_cast<float>(kx) * u + static_cast<float>(kz) * v) + wobble * 6.0f;
					// Slightly asymmetric ripple: gentle windward side, steeper lee side.
					const float ripple = std::sin(phase) + 0.25f * std::sin(2.0f * phase + 0.8f);
					h[static_cast<size_t>(y) * R + x] = ripple * 0.35f + (TileFbm(u, v, 40, 2, 53) - 0.5f) * 0.35f;
				}
			Image normal = Image::Solid(R, R, 128, 128, 255);
			const float strength = 2.2f * rippleDepth;
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					auto at = [&](int xx, int yy) { return h[static_cast<size_t>((yy + static_cast<int>(R)) % R) * R + ((xx + static_cast<int>(R)) % R)]; };
					const float dhdu = (at(static_cast<int>(x) + 1, static_cast<int>(y)) - at(static_cast<int>(x) - 1, static_cast<int>(y))) * 0.5f * strength;
					const float dhdv = (at(static_cast<int>(x), static_cast<int>(y) + 1) - at(static_cast<int>(x), static_cast<int>(y) - 1)) * 0.5f * strength;
					// Tangent = +X (u), bitangent = cross(N, T) = -Z, i.e. against +v: green carries +dh/dv.
					glm::vec3 nrm = glm::normalize(glm::vec3(-dhdu, dhdv, 1.0f));
					uint8_t* p = &normal.Pixels[(static_cast<size_t>(y) * R + x) * 4];
					p[0] = static_cast<uint8_t>((nrm.x * 0.5f + 0.5f) * 255.0f + 0.5f);
					p[1] = static_cast<uint8_t>((nrm.y * 0.5f + 0.5f) * 255.0f + 0.5f);
					p[2] = static_cast<uint8_t>((nrm.z * 0.5f + 0.5f) * 255.0f + 0.5f);
				}
			m_Ripples = std::make_unique<Texture2D>(normal, TextureEncoding::Linear);
		}

		if (m_SurfaceMaps)
			BuildSurfaceMaps(style, sunlit, shade, crest);

		m_Material = {};
		m_Material.Albedo = glm::vec3(1.0f);
		m_Material.Roughness = roughness;
		m_Material.AlbedoMap = m_Grain.get();
		m_Material.NormalMap = rippleDepth > 0.0f ? m_Ripples.get() : nullptr;
		m_Material.UVScale = 1.0f;
		m_Material.HeightScale = 0.0f;
		m_Material.CavityStrength = 0.0f;
		// Ground marks: sand holds shallow, compacted prints; snow takes deep, blue-shaded ones.
		switch (style)
		{
		case TerrainStyle::Desert: m_Material.TrailDepth = 0.07f; m_Material.TrailTint = { 0.60f, 0.52f, 0.44f }; m_Material.TrailNormalBoost = 3.5f; break;
		case TerrainStyle::Snow: m_Material.TrailDepth = 0.14f; m_Material.TrailTint = { 0.70f, 0.80f, 0.96f }; m_Material.TrailNormalBoost = 3.0f; break;
		default: m_Material.TrailDepth = 0.05f; m_Material.TrailTint = { 0.62f, 0.70f, 0.50f }; m_Material.TrailNormalBoost = 3.0f; break;
		}
		m_Material.MacroMap = m_Macro.get();
		m_Material.SurfaceNormalMap = m_NormalMap.get();
		m_Material.MacroRect = { hm.GetMin().x, hm.GetMin().y, hm.GetSize(), hm.GetSize() };
	}

	// A normal map and a macro color map over the whole terrain, both at a texel of ~0.2 m (much finer than the 0.5 m heightmap grid):
	// heights come from the smooth Catmull-Rom surface, so slopes shade smoothly instead of showing the grid's facets, and the
	// color carries mid-scale variation that keeps the ground from looking flat and pixelated up close.
	void Terrain::BuildSurfaceMaps(TerrainStyle style, const glm::vec3& sunlit, const glm::vec3& shade, const glm::vec3& crest)
	{
		(void)style;
		const Heightmap& hm = m_Heightmap;
		uint32_t res = 512;
		while (res < 2048 && static_cast<float>(res) * 0.25f < hm.GetSize()) res *= 2;
		m_NormalRes = res;
		const float texel = hm.GetSize() / static_cast<float>(res);
		const glm::vec2 lo = hm.GetMin();
		const int R = static_cast<int>(res);

		std::vector<float> heights(static_cast<size_t>(res) * res);
		for (int y = 0; y < R; ++y)
			for (int x = 0; x < R; ++x)
				heights[static_cast<size_t>(y) * res + static_cast<size_t>(x)] = SmoothHeight(lo.x + (static_cast<float>(x) + 0.5f) * texel, lo.y + (static_cast<float>(y) + 0.5f) * texel);
		auto H = [&](int x, int y) { return heights[static_cast<size_t>(std::clamp(y, 0, R - 1)) * res + static_cast<size_t>(std::clamp(x, 0, R - 1))]; };

		// Large patches of lighter and darker ground (38 m features): evaluated on a coarse grid and interpolated, they are smooth anyway.
		constexpr int kPatch = 257;
		std::vector<float> patches(static_cast<size_t>(kPatch) * kPatch);
		for (int y = 0; y < kPatch; ++y)
			for (int x = 0; x < kPatch; ++x)
				patches[static_cast<size_t>(y) * kPatch + static_cast<size_t>(x)] = Fbm((lo.x + static_cast<float>(x) / (kPatch - 1) * hm.GetSize()) / 38.0f, (lo.y + static_cast<float>(y) / (kPatch - 1) * hm.GetSize()) / 38.0f, 4, 71);
		auto patchAt = [&](float u, float v)
		{
			const float fx = u * (kPatch - 1), fy = v * (kPatch - 1);
			const int ix = std::min(static_cast<int>(fx), kPatch - 2), iy = std::min(static_cast<int>(fy), kPatch - 2);
			const float tx = fx - static_cast<float>(ix), ty = fy - static_cast<float>(iy);
			auto P = [&](int i, int j) { return patches[static_cast<size_t>(j) * kPatch + static_cast<size_t>(i)]; };
			return glm::mix(glm::mix(P(ix, iy), P(ix + 1, iy), tx), glm::mix(P(ix, iy + 1), P(ix + 1, iy + 1), tx), ty);
		};

		m_NormalImage = Image::Solid(res, res, 128, 128, 255);
		Image macro = Image::Solid(res, res, 255, 255, 255);
		const float low = hm.GetMinHeight(), high = std::max(hm.GetMaxHeight(), low + 1e-3f);
		for (int y = 0; y < R; ++y)
			for (int x = 0; x < R; ++x)
			{
				const float dx = H(x + 1, y) - H(x - 1, y), dz = H(x, y + 1) - H(x, y - 1);
				const glm::vec3 nrm = glm::normalize(glm::vec3(-dx, 2.0f * texel, -dz));
				const size_t o = (static_cast<size_t>(y) * res + static_cast<size_t>(x)) * 4;
				for (int k = 0; k < 3; ++k) m_NormalImage.Pixels[o + static_cast<size_t>(k)] = static_cast<uint8_t>(glm::clamp(nrm[k] * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);

				const float wx = lo.x + (static_cast<float>(x) + 0.5f) * texel, wz = lo.y + (static_cast<float>(y) + 0.5f) * texel;
				const float slope = 1.0f - nrm.y;
				const float altitude = (H(x, y) - low) / (high - low);
				const float patch = patchAt(static_cast<float>(x) / static_cast<float>(R - 1), static_cast<float>(y) / static_cast<float>(R - 1));
				const float fine = ValueNoise(wx / 1.9f, wz / 1.9f, 83) * 0.6f + ValueNoise(wx / 0.6f, wz / 0.6f, 97) * 0.4f;   // meter-scale mottling
				glm::vec3 c = glm::mix(sunlit, shade, glm::clamp(slope * 4.5f, 0.0f, 1.0f));
				c = glm::mix(c, crest, glm::clamp((altitude - 0.55f) * 2.4f, 0.0f, 1.0f) * (1.0f - glm::clamp(slope * 6.0f, 0.0f, 1.0f)));
				c *= (0.86f + 0.28f * patch) * (0.9f + 0.2f * fine);
				for (int k = 0; k < 3; ++k) macro.Pixels[o + static_cast<size_t>(k)] = static_cast<uint8_t>(glm::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		m_Macro = std::make_unique<Texture2D>(macro, TextureEncoding::SRGB);
		m_NormalMap = std::make_unique<Texture2D>(m_NormalImage, TextureEncoding::Linear);
	}

	void Terrain::WriteNormalTexels(int x0, int y0, int x1, int y1)
	{
		if (!m_NormalMap || m_NormalRes == 0) return;
		const int R = static_cast<int>(m_NormalRes);
		x0 = std::clamp(x0, 0, R - 1); y0 = std::clamp(y0, 0, R - 1); x1 = std::clamp(x1, 0, R - 1); y1 = std::clamp(y1, 0, R - 1);
		if (x1 < x0 || y1 < y0) return;
		const float texel = m_Heightmap.GetSize() / static_cast<float>(m_NormalRes);
		const glm::vec2 lo = m_Heightmap.GetMin();
		const int w = x1 - x0 + 1, h = y1 - y0 + 1, hw = w + 2;
		std::vector<float> heights(static_cast<size_t>(hw) * static_cast<size_t>(h + 2));
		for (int y = -1; y <= h; ++y)
			for (int x = -1; x <= w; ++x)
				heights[static_cast<size_t>(y + 1) * static_cast<size_t>(hw) + static_cast<size_t>(x + 1)] =
					SmoothHeight(lo.x + (static_cast<float>(x0 + x) + 0.5f) * texel, lo.y + (static_cast<float>(y0 + y) + 0.5f) * texel);
		auto H = [&](int x, int y) { return heights[static_cast<size_t>(y + 1) * static_cast<size_t>(hw) + static_cast<size_t>(x + 1)]; };
		std::vector<uint8_t> patch(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const glm::vec3 nrm = glm::normalize(glm::vec3(-(H(x + 1, y) - H(x - 1, y)), 2.0f * texel, -(H(x, y + 1) - H(x, y - 1))));
				const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
				for (int k = 0; k < 3; ++k) patch[o + static_cast<size_t>(k)] = static_cast<uint8_t>(glm::clamp(nrm[k] * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
				patch[o + 3] = 255;
				std::copy_n(&patch[o], 4, &m_NormalImage.Pixels[(static_cast<size_t>(y0 + y) * m_NormalRes + static_cast<size_t>(x0 + x)) * 4]);
			}
		m_NormalMap->Update(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(w), static_cast<uint32_t>(h), patch.data());
	}

	std::function<float(float, float)> Terrain::MakeGroundFunction(bool waterIsGround) const
	{
		const Heightmap* hm = &m_Heightmap;
		if (waterIsGround && m_HasLake)
		{
			const float level = m_WaterLevel;
			return [hm, level](float x, float z) { return std::max(hm->GetHeight(x, z), level); };
		}
		return [hm](float x, float z) { return hm->GetHeight(x, z); };
	}

	void Terrain::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		if (!m_Mesh) return;   // a CPU-only terrain (BuildGpu not called) draws nothing
		RenderObject obj;
		obj.MeshPtr = m_Mesh.get();
		obj.Mat = m_Material;
		obj.Transform = root;
		scene.Objects.push_back(std::move(obj));
	}
}
