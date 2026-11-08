#include "Terrain.h"
#include "GB/Log.h"
#include "GB/Scene/Image.h"
#include "GB/Water/WaterCarver.h"
#include "GB/World/TerrainNoise.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	using namespace TerrainNoise;   // (GB/World/TerrainNoise.h: shared with the terrain graph's hills and dunes nodes)

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
	std::unique_ptr<Terrain> Terrain::CreateDunes(const DuneParams& p, float size, uint32_t resolution, TerrainStyle style)
	{
		std::unique_ptr<Terrain> t(new Terrain(resolution, size));
		t->m_ShoreSubdivision = std::clamp(p.DetailSubdivision, 1, 8);
		t->m_DetailThreshold = p.DetailThreshold;
		t->m_WindAngle = glm::radians(p.WindAngleDegrees);
		Heightmap& hm = t->m_Heightmap;

		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				hm.At(i, j) = DuneHeight(hm.SamplePosition(i, j), t->m_WindAngle, p.Wavelength, p.Height, p.CrestPosition, p.CrestSharpness, p.Warp, p.WarpScale,
					p.SizeVariation, p.BaseHills, p.Seed);
			}
		}

		t->FindCrests(p.Wavelength);
		t->m_Style = style;
		return t;
	}

	void Terrain::SetWind(float angleRadians, float wavelength)
	{
		m_WindAngle = angleRadians;
		m_Crests.clear();
		FindCrests(wavelength);
	}

	void Terrain::FindCrests(float wavelength)
	{
		const Heightmap& hm = m_Heightmap;
		const glm::vec2 wind(std::cos(m_WindAngle), std::sin(m_WindAngle));
		// Crest points: local maxima along the wind direction.
		const float step = std::max(wavelength * 0.08f, hm.GetSpacing() * 2.0f);
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
					m_Crests.push_back({ pos.x, h, pos.y });
			}
		}
	}

	std::unique_ptr<Terrain> Terrain::CreateHills(const HillParams& p, float size, uint32_t resolution, TerrainStyle style)
	{
		Heightmap ground(resolution, size);
		for (uint32_t j = 0; j < ground.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < ground.GetResolution(); ++i)
			{
				ground.At(i, j) = HillHeight(ground.SamplePosition(i, j), p.Scale, p.Octaves, p.Seed, p.Flatten, p.Height);
			}
		}
		return CreateFromHeightmap(ground, p, style);
	}

	std::unique_ptr<Terrain> Terrain::CreateFromHeightmap(const Heightmap& ground, const WaterField& water, const HillParams& p, TerrainStyle style)
	{
		std::unique_ptr<Terrain> t(new Terrain(ground.GetResolution(), ground.GetSize()));
		t->m_SurfaceMaps = p.SurfaceMaps;
		t->m_ShoreSubdivision = std::clamp(p.ShoreSubdivision, 1, 8);
		t->m_ShoreMargin = std::max(p.ShoreMarginCells, 0);
		t->m_DetailThreshold = p.DetailThreshold;
		t->m_Heightmap = ground;
		t->m_Water = water;
		t->m_Style = style;
		return t;
	}

	std::unique_ptr<Terrain> Terrain::CreateFromHeightmap(const Heightmap& ground, const HillParams& p, TerrainStyle style)
	{
		std::unique_ptr<Terrain> t(new Terrain(ground.GetResolution(), ground.GetSize()));
		t->m_SurfaceMaps = p.SurfaceMaps;
		t->m_ShoreSubdivision = std::clamp(p.ShoreSubdivision, 1, 8);
		t->m_ShoreMargin = std::max(p.ShoreMarginCells, 0);
		t->m_DetailThreshold = p.DetailThreshold;
		t->m_Heightmap = ground;
		Heightmap& hm = t->m_Heightmap;
		LakeShape lake;
		lake.Center = p.LakeCenter; lake.Radius = p.LakeRadius; lake.Depth = p.LakeDepth; lake.Level = p.WaterLevel; lake.BankSlope = p.LakeBankSlope;
		lake.Outline = MakeLakeOutline(p.Seed, p.LakeIrregularity);
		if (lake.Radius > 0.0f)
			for (uint32_t j = 0; j < hm.GetResolution(); ++j)
				for (uint32_t i = 0; i < hm.GetResolution(); ++i)
					hm.At(i, j) = ApplyLakeBasin(hm.At(i, j), hm.SamplePosition(i, j), lake);   // the basin and its banks (GB/Water/WaterCarver.h)
		if (p.RiverPath.size() >= 2)
		{
			RiverShape river;
			river.Path = p.RiverPath; river.Width = p.RiverWidth; river.Depth = p.RiverDepth; river.Discharge = p.RiverDischarge;
			t->m_Water.SetRiver(CarveRiver(hm, river, lake));
		}
		if (p.LakeRadius > 0.0f)
			t->m_Water.SetLake({ true, p.LakeCenter, p.LakeRadius, p.WaterLevel, lake.Outline });
		t->m_Style = style;
		return t;
	}

	// ------------------------------------------------------------------ river
	// The river itself (carving, queries) is GB/Water; only the question that needs the ground stays here.
	float Terrain::GetRiverLevelAt(float x, float z) const
	{
		const RiverSample s = m_Water.NearestRiverInBand(x, z);   // (the same answer as NearestRiver here: only the band is wanted)
		return m_Water.InRiverBand(s) && m_Heightmap.GetHeight(x, z) < s.Level ? s.Level : kNoWaterLevel;
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
		v.UV = { pos.x / 4.0f, pos.y / 4.0f };   // same detail tiling as the grid (tileMeters in PrepareRenderData)
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

		if (m_Water.HasLake())   // cells whose heights straddle the lake level, near the lake
		{
			const float reach = m_Water.LakeRadius() * 1.8f + s;
			const int c0 = std::max(0, static_cast<int>((m_Water.LakeCenter().x - reach - lo.x) / s)), c1 = std::min(cells - 1, static_cast<int>((m_Water.LakeCenter().x + reach - lo.x) / s));
			const int r0 = std::max(0, static_cast<int>((m_Water.LakeCenter().y - reach - lo.y) / s)), r1 = std::min(cells - 1, static_cast<int>((m_Water.LakeCenter().y + reach - lo.y) / s));
			for (int cj = r0; cj <= r1; ++cj)
				for (int ci = c0; ci <= c1; ++ci)
				{
					if (glm::length(center(ci, cj) - m_Water.LakeCenter()) > reach) continue;
					const float a = hm.At(ci, cj), b = hm.At(ci + 1, cj), c = hm.At(ci, cj + 1), d = hm.At(ci + 1, cj + 1);
					const float lowest = std::min(std::min(a, b), std::min(c, d)), highest = std::max(std::max(a, b), std::max(c, d));
					if (lowest <= m_Water.LakeLevel() + 0.6f && highest >= m_Water.LakeLevel() - 0.6f)
						cellAt(ci, cj) = 1;
				}
		}
		if (const RiverData* river = m_Water.River())   // the bank zone on both sides of the river axis
		{
			const auto& P = river->Points;
			for (size_t k = 0; k + 1 < P.size(); ++k)
			{
				// the bank zone of this segment: a river that widens downstream has a wider one there (its widest end decides)
				const float hw = std::max(river->HalfWidthAt(k, 0.0f), river->HalfWidthAt(k, 1.0f));
				const float inner = hw * 0.5f, outer = hw * 1.8f + s;
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

	// Cells where a flat quad would visibly miss the ground: the centre of the bilinear quad (what an unrefined cell actually draws)
	// compared with SmoothHeight's cubic surface through the same four corners (what the refined vertices follow). Ridgelines, gullies
	// and the channels HydraulicErosion carves diverge enough to flag; a gentle slope or a lake bed does not. Unlike FlagShoreCells this
	// has no notion of water -- a second, independent strategy over the same (n-1)^2 grid of flags (SRP/OCP: BuildMeshRefinement only
	// ORs whatever strategies flag a cell, it does not know why any of them did).
	std::vector<uint8_t> Terrain::FlagDetailCells(float threshold) const
	{
		const Heightmap& hm = m_Heightmap;
		const int cells = static_cast<int>(hm.GetResolution()) - 1;
		std::vector<uint8_t> flags(static_cast<size_t>(cells) * static_cast<size_t>(cells), 0);
		if (threshold <= 0.0f) return flags;
		for (int cj = 0; cj < cells; ++cj)
		{
			for (int ci = 0; ci < cells; ++ci)
			{
				const float a = hm.At(ci, cj), b = hm.At(ci + 1, cj), c = hm.At(ci, cj + 1), d = hm.At(ci + 1, cj + 1);
				const float bilinearCenter = (a + b + c + d) * 0.25f;
				const glm::vec2 pos = hm.GetMin() + (glm::vec2(static_cast<float>(ci), static_cast<float>(cj)) + 0.5f) * hm.GetSpacing();
				if (std::fabs(SmoothHeight(pos.x, pos.y) - bilinearCenter) > threshold)
					flags[static_cast<size_t>(cj) * static_cast<size_t>(cells) + static_cast<size_t>(ci)] = 1;
			}
		}
		return flags;
	}

	void Terrain::BuildMeshRefinement(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		m_FineIndex.clear();
		m_RefinedCells.clear();
		const int N = m_ShoreSubdivision;
		if (N < 2) return;
		std::vector<uint8_t> flags = FlagShoreCells(m_ShoreMargin);   // always sized (n-1)^2, water cells set to 1
		if (m_DetailThreshold > 0.0f)
		{
			const std::vector<uint8_t> detail = FlagDetailCells(m_DetailThreshold);
			for (size_t i = 0; i < flags.size(); ++i) flags[i] |= detail[i];
		}
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
		GB_CORE_INFO("Terrain mesh refinement: {} cells split {}x{} (+{} vertices)", flagged, N, N, vertices.size() - m_FineFirst);
	}

	void Terrain::RefreshMesh(int i0, int j0, int i1, int j1)
	{
		const int n = static_cast<int>(m_Heightmap.GetResolution());
		i0 = std::clamp(i0 - 2, 0, n - 1); j0 = std::clamp(j0 - 2, 0, n - 1);
		i1 = std::clamp(i1 + 2, 0, n - 1); j1 = std::clamp(j1 + 2, 0, n - 1);
		if (m_Vertices.size() < static_cast<size_t>(n) * static_cast<size_t>(n) || i1 < i0 || j1 < j0) return;   // no renderer took the mesh yet: it reads the heights as they are
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
			{
				if (m_Edits.FineLast < m_Edits.FineFirst) { m_Edits.FineFirst = lowest; m_Edits.FineLast = highest; }
				else { m_Edits.FineFirst = std::min(m_Edits.FineFirst, lowest); m_Edits.FineLast = std::max(m_Edits.FineLast, highest); }
			}
		}
		if (m_NormalRes > 0 && !m_NormalImage.Pixels.empty())   // the same area of the high-resolution normal map (SmoothHeight reads two cells around a point)
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
		}
		if (m_Edits.RowJ1 < m_Edits.RowJ0) { m_Edits.RowI0 = i0; m_Edits.RowI1 = i1; m_Edits.RowJ0 = j0; m_Edits.RowJ1 = j1; }
		else
		{
			m_Edits.RowI0 = std::min(m_Edits.RowI0, i0); m_Edits.RowI1 = std::max(m_Edits.RowI1, i1);
			m_Edits.RowJ0 = std::min(m_Edits.RowJ0, j0); m_Edits.RowJ1 = std::max(m_Edits.RowJ1, j1);
		}
		++m_EditRevision;
	}

	TerrainEdits Terrain::TakeEdits()
	{
		TerrainEdits edits = m_Edits;
		m_Edits = {};
		return edits;
	}

	// ------------------------------------------------------------------ mesh + material images (the CPU half of the drawing)
	void Terrain::PrepareRenderData()
	{
		if (m_Prepared) return;   // (a terrain already handed to a renderer prepares again from its heights, for another one)
		auto prepared = std::make_unique<TerrainRenderData>();
		TerrainRenderData& d = *prepared;
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
		BuildMeshRefinement(vertices, indices);
		SortIntoTiles(vertices, indices);
		m_PreparedVertices = std::move(vertices);
		d.Indices = std::move(indices);

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
			d.Grain = std::move(grain);
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
			d.Ripples = std::move(normal);
		}

		if (m_SurfaceMaps)
			BuildSurfaceMaps(style, sunlit, shade, crest, d.Macro);
		d.Roughness = roughness;
		d.RippleDepth = rippleDepth;
		m_Prepared = std::move(prepared);
	}

	std::unique_ptr<TerrainRenderData> Terrain::TakeRenderData()
	{
		if (!m_Prepared) return nullptr;
		m_Vertices = std::move(m_PreparedVertices);
		m_PreparedVertices = {};
		return std::move(m_Prepared);
	}

	// A normal map and a macro color map over the whole terrain, both at a texel of ~0.2 m (much finer than the 0.5 m heightmap grid):
	// heights come from the smooth Catmull-Rom surface, so slopes shade smoothly instead of showing the grid's facets, and the
	// color carries mid-scale variation that keeps the ground from looking flat and pixelated up close.
	void Terrain::BuildSurfaceMaps(TerrainStyle style, const glm::vec3& sunlit, const glm::vec3& shade, const glm::vec3& crest, Image& out)
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
		const bool layered = !m_Layers.empty();
		for (int y = 0; y < R; ++y)
			for (int x = 0; x < R; ++x)
			{
				const float wx = lo.x + (static_cast<float>(x) + 0.5f) * texel, wz = lo.y + (static_cast<float>(y) + 0.5f) * texel;
				heights[static_cast<size_t>(y) * res + static_cast<size_t>(x)] = layered ? SmoothHeight(wx, wz) + SurfaceDetail(wx, wz) : SmoothHeight(wx, wz);
			}
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
				if (layered) c = PaintLayers(c, wx, wz, H(x, y), nrm, patch, fine);
				for (int k = 0; k < 3; ++k) macro.Pixels[o + static_cast<size_t>(k)] = static_cast<uint8_t>(glm::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		out = std::move(macro);   // (the renderer makes the textures from it and m_NormalImage: GB/Renderer/TerrainGpu.h)
	}

	// ------------------------------------------------------------------ material layers
	void Terrain::SetSurfaceLayers(std::vector<SurfaceLayer> layers)
	{
		const size_t count = static_cast<size_t>(m_Heightmap.GetResolution()) * m_Heightmap.GetResolution();
		layers.erase(std::remove_if(layers.begin(), layers.end(), [count](const SurfaceLayer& l) { return l.Weights.size() != count; }), layers.end());
		m_Layers = std::move(layers);
		m_Prepared.reset();   // (the surface maps painted without them are made again)
	}

	float Terrain::SurfaceLayerWeight(SurfaceLayerKind kind, float x, float z) const
	{
		const Heightmap& hm = m_Heightmap;
		for (const SurfaceLayer& layer : m_Layers)
		{
			if (layer.Kind != kind) continue;
			const int n = static_cast<int>(hm.GetResolution());
			const float maxIndex = static_cast<float>(n - 1);
			const float fx = glm::clamp((x - hm.GetMin().x) / hm.GetSpacing(), 0.0f, maxIndex), fz = glm::clamp((z - hm.GetMin().y) / hm.GetSpacing(), 0.0f, maxIndex);
			const int i0 = std::min(static_cast<int>(fx), n - 2), j0 = std::min(static_cast<int>(fz), n - 2);
			const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
			auto W = [&](int i, int j) { return layer.Weights[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)]; };
			return glm::clamp(glm::mix(glm::mix(W(i0, j0), W(i0 + 1, j0), tx), glm::mix(W(i0, j0 + 1), W(i0 + 1, j0 + 1), tx), tz), 0.0f, 1.0f);
		}
		return 0.0f;
	}

	// Rock flutes: the grooves that rockfall and meltwater cut down a cliff, which run down its fall line. A noise stretched along the downhill
	// direction (narrow across the slope, long down it), stronger the steeper the face, on the rock layer only.
	float Terrain::SurfaceDetail(float x, float z) const
	{
		const float rock = SurfaceLayerWeight(SurfaceLayerKind::Rock, x, z);
		if (rock <= 0.0f) return 0.0f;
		const float e = m_Heightmap.GetSpacing() * 2.0f;
		const glm::vec2 grad((m_Heightmap.GetHeight(x + e, z) - m_Heightmap.GetHeight(x - e, z)) / (2.0f * e), (m_Heightmap.GetHeight(x, z + e) - m_Heightmap.GetHeight(x, z - e)) / (2.0f * e));
		const float steep = glm::clamp((glm::length(grad) - 0.35f) * 1.5f, 0.0f, 1.0f);
		if (steep <= 0.0f) return 0.0f;
		const glm::vec2 down = grad / std::max(glm::length(grad), 1e-4f);
		const glm::vec2 across(-down.y, down.x);
		const glm::vec2 p(x, z);
		const float u = glm::dot(p, across), v = glm::dot(p, down);
		const float ribs = Fbm(u / 7.0f, v / 40.0f, 4, 401) - 0.5f;            // ribs and gullies ~8 m apart, tens of metres long
		const float flutes = Fbm(u / 1.8f, v / 12.0f, 3, 403) - 0.5f;          // grooves ~2 m apart
		const float blocks = Fbm(u / 4.0f, v / 3.0f, 2, 409) - 0.5f;           // broken blocks between them
		return (ribs * 2.6f + flutes * 0.8f + blocks * 0.5f) * rock * steep;
	}

	glm::vec3 Terrain::PaintLayers(const glm::vec3& base, float x, float z, float height, const glm::vec3& normal, float patch, float fine) const
	{
		glm::vec3 c = base;
		const float slope = 1.0f - normal.y;
		const float jitter = (ValueNoise(x / 2.3f, z / 2.3f, 211) - 0.5f) * 0.35f + (ValueNoise(x / 0.7f, z / 0.7f, 223) - 0.5f) * 0.15f;   // ragged edges
		for (const SurfaceLayer& layer : m_Layers)
		{
			float w = SurfaceLayerWeight(layer.Kind, x, z);
			if (w <= 0.0f) continue;
			w = glm::clamp((w - 0.5f) * 1.6f + 0.5f + jitter, 0.0f, 1.0f);
			glm::vec3 color;
			switch (layer.Kind)
			{
			case SurfaceLayerKind::Rock:
			{
				// warm granite, banded by its bedding and darker in the grooves and on the steepest faces
				const float band = std::sin(height * 1.3f + (Fbm(x / 40.0f, z / 40.0f, 2, 307) - 0.5f) * 6.0f) * 0.5f + 0.5f;
				const float groove = glm::clamp(0.5f - SurfaceDetail(x, z) * 0.9f, 0.0f, 1.0f);
				const float tone = Fbm(x / 14.0f, z / 14.0f, 3, 311);   // lighter and darker slabs
				color = glm::mix(glm::vec3(0.55f, 0.43f, 0.35f), glm::vec3(0.33f, 0.25f, 0.21f), glm::clamp(band * 0.45f + (1.0f - tone) * 0.6f - 0.15f, 0.0f, 1.0f));
				color *= (0.7f + 0.45f * groove) * (0.9f + 0.2f * fine) * (1.0f - 0.15f * glm::clamp(slope * 1.5f, 0.0f, 1.0f));
				break;
			}
			case SurfaceLayerKind::Snow: color = glm::vec3(0.93f, 0.95f, 0.99f) * (0.96f + 0.04f * fine); break;
			case SurfaceLayerKind::Grass: color = glm::vec3(0.30f, 0.45f, 0.16f) * (0.86f + 0.28f * patch) * (0.9f + 0.2f * fine); break;
			case SurfaceLayerKind::Dry:
			{
				// sun-cured grass: olive where it is only drying, straw where it is dry through, in patches
				const float cure = glm::clamp(w * 1.2f + (patch - 0.5f) * 0.8f, 0.0f, 1.0f);
				color = glm::mix(glm::vec3(0.36f, 0.42f, 0.17f), glm::vec3(0.55f, 0.50f, 0.27f), cure) * (0.9f + 0.2f * fine);
				w = glm::clamp(w * 0.85f, 0.0f, 1.0f);
				break;
			}
			case SurfaceLayerKind::Forest:
			{
				// crowns of 4-7 m, lit on top and dark in the gaps between them, broadleaf greens mixed with darker conifers in clumps
				const float crowns = ValueNoise(x / 4.5f, z / 4.5f, 421) * 0.65f + ValueNoise(x / 1.8f, z / 1.8f, 431) * 0.35f;
				const float species = Fbm(x / 60.0f, z / 60.0f, 3, 433);
				const glm::vec3 lit = glm::mix(glm::vec3(0.17f, 0.25f, 0.08f), glm::vec3(0.10f, 0.17f, 0.08f), glm::clamp((species - 0.42f) * 5.0f, 0.0f, 1.0f));
				color = glm::mix(glm::vec3(0.07f, 0.10f, 0.05f), lit * 1.25f, glm::clamp((crowns - 0.25f) * 2.2f, 0.0f, 1.0f));
				break;
			}
			default: color = glm::vec3(0.80f, 0.66f, 0.45f) * (0.9f + 0.2f * fine); break;
			}
			c = glm::mix(c, color, w);
		}
		return c;
	}

	void Terrain::WriteNormalTexels(int x0, int y0, int x1, int y1)
	{
		if (m_NormalRes == 0 || m_NormalImage.Pixels.empty()) return;
		const int R = static_cast<int>(m_NormalRes);
		x0 = std::clamp(x0, 0, R - 1); y0 = std::clamp(y0, 0, R - 1); x1 = std::clamp(x1, 0, R - 1); y1 = std::clamp(y1, 0, R - 1);
		if (x1 < x0 || y1 < y0) return;
		const float texel = m_Heightmap.GetSize() / static_cast<float>(m_NormalRes);
		const glm::vec2 lo = m_Heightmap.GetMin();
		const int w = x1 - x0 + 1, h = y1 - y0 + 1, hw = w + 2;
		std::vector<float> heights(static_cast<size_t>(hw) * static_cast<size_t>(h + 2));
		for (int y = -1; y <= h; ++y)
			for (int x = -1; x <= w; ++x)
			{
				const float wx = lo.x + (static_cast<float>(x0 + x) + 0.5f) * texel, wz = lo.y + (static_cast<float>(y0 + y) + 0.5f) * texel;
				heights[static_cast<size_t>(y + 1) * static_cast<size_t>(hw) + static_cast<size_t>(x + 1)] = m_Layers.empty() ? SmoothHeight(wx, wz) : SmoothHeight(wx, wz) + SurfaceDetail(wx, wz);
			}
		auto H = [&](int x, int y) { return heights[static_cast<size_t>(y + 1) * static_cast<size_t>(hw) + static_cast<size_t>(x + 1)]; };
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const glm::vec3 nrm = glm::normalize(glm::vec3(-(H(x + 1, y) - H(x - 1, y)), 2.0f * texel, -(H(x, y + 1) - H(x, y - 1))));
				uint8_t* p = &m_NormalImage.Pixels[(static_cast<size_t>(y0 + y) * m_NormalRes + static_cast<size_t>(x0 + x)) * 4];
				for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(glm::clamp(nrm[k] * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
				p[3] = 255;
			}
		if (m_Edits.NormalX1 < m_Edits.NormalX0) { m_Edits.NormalX0 = x0; m_Edits.NormalY0 = y0; m_Edits.NormalX1 = x1; m_Edits.NormalY1 = y1; }
		else
		{
			m_Edits.NormalX0 = std::min(m_Edits.NormalX0, x0); m_Edits.NormalY0 = std::min(m_Edits.NormalY0, y0);
			m_Edits.NormalX1 = std::max(m_Edits.NormalX1, x1); m_Edits.NormalY1 = std::max(m_Edits.NormalY1, y1);
		}
	}

	std::function<float(float, float)> Terrain::MakeGroundFunction(bool waterIsGround) const
	{
		const Heightmap* hm = &m_Heightmap;
		if (waterIsGround && m_Water.HasLake())
		{
			const float level = m_Water.LakeLevel();
			return [hm, level](float x, float z) { return std::max(hm->GetHeight(x, z), level); };
		}
		return [hm](float x, float z) { return hm->GetHeight(x, z); };
	}

	// The triangles in tile order (each by the cell its centroid falls in), and each tile's range and bounds. A terrain of one tile or less (the
	// worlds of a few hundred metres) stays one draw, as it was.
	void Terrain::SortIntoTiles(const std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		m_Tiles.clear();
		const Heightmap& hm = m_Heightmap;
		const int cells = static_cast<int>(hm.GetResolution()) - 1;
		const int per = (cells + kTileCells - 1) / kTileCells;
		if (per <= 1) return;
		const float tileSize = hm.GetSize() / static_cast<float>(per);
		const glm::vec2 lo = hm.GetMin();
		std::vector<std::vector<uint32_t>> buckets(static_cast<size_t>(per) * per);
		for (size_t t = 0; t + 2 < indices.size(); t += 3)
		{
			const glm::vec3 c = (vertices[indices[t]].Position + vertices[indices[t + 1]].Position + vertices[indices[t + 2]].Position) / 3.0f;
			const int tx = std::clamp(static_cast<int>((c.x - lo.x) / tileSize), 0, per - 1), tz = std::clamp(static_cast<int>((c.z - lo.y) / tileSize), 0, per - 1);
			auto& b = buckets[static_cast<size_t>(tz) * per + tx];
			b.insert(b.end(), { indices[t], indices[t + 1], indices[t + 2] });
		}
		std::vector<uint32_t> sorted;
		sorted.reserve(indices.size());
		for (const auto& b : buckets)
		{
			if (b.empty()) continue;
			TerrainTile tile;
			tile.First = static_cast<uint32_t>(sorted.size());
			tile.Count = static_cast<uint32_t>(b.size());
			glm::vec3 mn(1e30f), mx(-1e30f);
			for (const uint32_t i : b) { mn = glm::min(mn, vertices[i].Position); mx = glm::max(mx, vertices[i].Position); }
			tile.Center = (mn + mx) * 0.5f;
			tile.Radius = glm::length(mx - mn) * 0.5f + 1.0f;   // (a metre for the displacement and the ground marks)
			sorted.insert(sorted.end(), b.begin(), b.end());
			m_Tiles.push_back(tile);
		}
		indices.swap(sorted);
	}
}
