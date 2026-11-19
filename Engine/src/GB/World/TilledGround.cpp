#include "GB/World/TilledGround.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr float kTau = 6.28318530718f;

		uint32_t Mix(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7feb352dU;
			x ^= x >> 15; x *= 0x846ca68bU;
			x ^= x >> 16;
			return x;
		}
		uint32_t Hash3(uint32_t a, uint32_t b, uint32_t c) { return Mix(a ^ Mix(b + 0x9E3779B9U * (c + 1U))); }
		float Unit(uint32_t h) { return static_cast<float>(h >> 8) / 16777216.0f; }   // 0..1

		// value noise on a lattice of `cell` metres, 0..1
		float ValueNoise(float x, float z, float cell, uint32_t seed)
		{
			const float gx = x / cell, gz = z / cell;
			const float fx0 = std::floor(gx), fz0 = std::floor(gz);
			const int ix = static_cast<int>(fx0), iz = static_cast<int>(fz0);
			float tx = gx - fx0, tz = gz - fz0;
			tx = tx * tx * (3.0f - 2.0f * tx);
			tz = tz * tz * (3.0f - 2.0f * tz);
			auto at = [&](int i, int j) { return Unit(Hash3(static_cast<uint32_t>(i), static_cast<uint32_t>(j), seed)); };
			const float a = at(ix, iz), b = at(ix + 1, iz), c = at(ix, iz + 1), d = at(ix + 1, iz + 1);
			return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
		}

		float Smooth(float e0, float e1, float x)
		{
			const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// The cells of the field's grid that hold a plot (and which plot), Columns x Rows.
		struct Occupancy
		{
			int Cols = 1, Rows = 1;
			std::vector<int> Plot;   // per cell: the plot on it, -1 none
			Occupancy(const TilledShape& s) : Cols(std::max(1, s.Columns)), Rows(s.Rows())
			{
				Plot.assign(static_cast<size_t>(Cols) * static_cast<size_t>(Rows), -1);
				for (int i = 0; i < s.Plots; ++i)
				{
					const int c = s.CellOf(i);
					if (c >= 0 && c < Cols * Rows) Plot[static_cast<size_t>(c)] = i;
				}
			}
			int At(int col, int row) const { return col < 0 || row < 0 || col >= Cols || row >= Rows ? -1 : Plot[static_cast<size_t>(row) * static_cast<size_t>(Cols) + static_cast<size_t>(col)]; }
			float Filled(int col, int row) const { return At(col, row) >= 0 ? 1.0f : 0.0f; }
			// the occupancy filtered bilinearly at grid coordinates g (cell middles at whole numbers): 0.5 on the edge of a straight side
			float Coverage(const glm::vec2& g) const
			{
				const float fx0 = std::floor(g.x), fz0 = std::floor(g.y);
				const int i = static_cast<int>(fx0), j = static_cast<int>(fz0);
				const float tx = g.x - fx0, tz = g.y - fz0;
				const float a = Filled(i, j), b = Filled(i + 1, j), c = Filled(i, j + 1), d = Filled(i + 1, j + 1);
				return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
			}
		};
	}

	int TilledShape::Rows() const
	{
		const int cols = std::max(1, Columns);
		int last = Plots - 1;
		for (int c : Cells) last = std::max(last, c);
		return std::max(1, last / cols + 1);
	}

	bool TilledShape::AlongX() const
	{
		// the extent of the cells that hold plots
		const int cols = std::max(1, Columns);
		int c0 = 1 << 30, c1 = -1, r0 = 1 << 30, r1 = -1;
		for (int i = 0; i < Plots; ++i)
		{
			const int c = CellOf(i);
			c0 = std::min(c0, c % cols); c1 = std::max(c1, c % cols);
			r0 = std::min(r0, c / cols); r1 = std::max(r1, c / cols);
		}
		return Plots == 0 || (c1 - c0) >= (r1 - r0);
	}

	uint64_t TilledShape::Hash() const
	{
		uint64_t h = 1469598103934665603ULL;
		auto add = [&](const void* p, size_t n)
		{
			const auto* b = static_cast<const unsigned char*>(p);
			for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
		};
		add(&Origin, sizeof Origin);
		add(&Columns, sizeof Columns);
		add(&Spacing, sizeof Spacing);
		add(&Plots, sizeof Plots);
		if (!Cells.empty()) add(Cells.data(), Cells.size() * sizeof(int));
		if (!Outline.empty()) add(Outline.data(), Outline.size() * sizeof(glm::vec2));
		return h;
	}

	float OutlineDistance(const std::vector<glm::vec2>& poly, const glm::vec2& p)
	{
		if (poly.size() < 3) return 1e9f;
		float best = 1e30f;
		bool inside = false;
		for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
		{
			const glm::vec2 a = poly[j], b = poly[i], ab = b - a;
			const float t = glm::dot(ab, ab) > 1e-12f ? std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
			best = std::min(best, glm::length(p - (a + t * ab)));
			if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) / (b.y - a.y) * (b.x - a.x)) inside = !inside;
		}
		return inside ? best : -best;
	}

	uint32_t TilledSeed(const std::string& name, const glm::vec2& origin)
	{
		uint32_t h = 2166136261U;
		for (char c : name) { h ^= static_cast<unsigned char>(c); h *= 16777619U; }
		uint32_t x = 0, z = 0;
		std::memcpy(&x, &origin.x, sizeof x);
		std::memcpy(&z, &origin.y, sizeof z);
		return Hash3(h, x, z);
	}

	namespace
	{
		// How far inside the field p is, without the wobble: the union of its cells (their occupancy filtered over a plot, so a slanted run of
		// cells gives a slanted edge and the corners round off) grown by kCellsMargin, cut by a painted zone's perimeter grown by half a plot
		constexpr float kCellsMargin = 0.35f;
		float CellsInside(const Occupancy& occ, const TilledShape& shape, const glm::vec2& p)
		{
			const float sp = shape.Spacing;
			const glm::vec2 g = (p - shape.Origin) / sp;
			const float cov = 0.25f * (occ.Coverage(g + glm::vec2(0.35f, 0.35f)) + occ.Coverage(g + glm::vec2(-0.35f, 0.35f)) + occ.Coverage(g + glm::vec2(0.35f, -0.35f))
				+ occ.Coverage(g + glm::vec2(-0.35f, -0.35f)));
			float s = (cov - 0.5f) * sp + kCellsMargin;
			if (shape.Outline.size() >= 3) s = std::min(s, OutlineDistance(shape.Outline, p) + 0.5f * sp);
			return s;
		}
	}

	float TilledInside(const TilledShape& shape, const TilledLook& look, uint32_t seed, const glm::vec2& p)
	{
		const float sp = shape.Spacing;
		const float wobble = std::clamp(look.EdgeNoise, 0.0f, 0.45f) * (ValueNoise(p.x, p.y, 1.7f, seed) - 0.5f) * 2.0f * sp;
		const Occupancy occ(shape);
		return CellsInside(occ, shape, p) + wobble;
	}

	namespace
	{
		// TilledInside with the occupancy made once (the builders ask it for thousands of points)
		struct InsideFn
		{
			const TilledShape& Shape;
			const TilledLook& Look;
			uint32_t Seed;
			Occupancy Occ;
			InsideFn(const TilledShape& s, const TilledLook& l, uint32_t seed) : Shape(s), Look(l), Seed(seed), Occ(s) {}
			float operator()(const glm::vec2& p) const
			{
				const float sp = Shape.Spacing;
				const float wobble = std::clamp(Look.EdgeNoise, 0.0f, 0.45f) * (ValueNoise(p.x, p.y, 1.7f, Seed) - 0.5f) * 2.0f * sp;
				(void)sp;
				return CellsInside(Occ, Shape, p) + wobble;
			}
		};

		// the furrows' weight: none on the rim, all of it a little inside
		float FurrowWeight(const TilledLook& look, float inside) { return Smooth(look.RimWidth, look.RimWidth + 0.25f, inside); }

		// the box the field's drawing covers: its cells' grid, `margin` m more all round
		void FieldBox(const TilledShape& shape, float margin, glm::vec2& lo, glm::vec2& hi)
		{
			const Occupancy occ(shape);
			const float sp = shape.Spacing;
			lo = shape.Origin - glm::vec2(0.5f * sp + margin);
			hi = shape.Origin + glm::vec2(static_cast<float>(occ.Cols) - 0.5f, static_cast<float>(occ.Rows) - 0.5f) * sp + glm::vec2(margin);
		}
	}

	MeshData BuildTilledGround(const TilledShape& shape, const TilledLook& look, const HeightFn& ground, uint32_t seed, size_t* rimIndices)
	{
		MeshData out;
		if (rimIndices) *rimIndices = 0;
		if (shape.Plots <= 0 || shape.Spacing <= 0.0f) return out;
		const InsideFn insideAt(shape, look, seed);
		const bool alongX = shape.AlongX();
		const glm::vec2 A = alongX ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f);   // along the furrows
		const glm::vec2 C = alongX ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);   // across them
		const float acrossStep = std::max(0.02f, look.FurrowSpacing / static_cast<float>(std::max(2, look.FurrowSamples)));
		const float alongStep = std::max(0.05f, look.Step);

		glm::vec2 lo, hi;
		FieldBox(shape, kCellsMargin + 0.1f, lo, hi);
		const float a0 = glm::dot(lo, A) - alongStep, a1 = glm::dot(hi, A) + alongStep;
		const float c0 = glm::dot(lo, C) - acrossStep, c1 = glm::dot(hi, C) + acrossStep;
		const int na = static_cast<int>(std::ceil((a1 - a0) / alongStep)) + 1;
		const int nc = static_cast<int>(std::ceil((c1 - c0) / acrossStep)) + 1;
		const float cOrigin = glm::dot(shape.Origin, C), aOrigin = glm::dot(shape.Origin, A);
		const float tile = 8.0f * std::max(0.05f, look.FurrowSpacing);   // the texture's furrows run across v, eight to a tile
		auto idx = [&](int i, int j) { return static_cast<size_t>(i) * static_cast<size_t>(nc) + static_cast<size_t>(j); };
		auto at = [&](int i, int j) { return A * (a0 + static_cast<float>(i) * alongStep) + C * (c0 + static_cast<float>(j) * acrossStep); };
		auto vertex = [&](const glm::vec2& p, float y, float s)
		{
			Vertex v;
			v.Position = glm::vec3(p.x, y, p.y);
			v.Normal = glm::vec3(0.0f);
			v.UV = glm::vec2((glm::dot(p, A) - aOrigin) / tile, (glm::dot(p, C) - cOrigin) / tile);
			(void)s;
			v.Tangent = glm::vec4(0.0f);
			return v;
		};
		auto groundAt = [&](const glm::vec2& p) { return ground ? ground(p.x, p.y) : 0.0f; };

		std::vector<float> inside(static_cast<size_t>(na) * static_cast<size_t>(nc));
		for (int i = 0; i < na; ++i)
			for (int j = 0; j < nc; ++j) inside[idx(i, j)] = insideAt(at(i, j));
		out.Vertices.resize(inside.size());
		for (int i = 0; i < na; ++i)
			for (int j = 0; j < nc; ++j)
			{
				const glm::vec2 p = at(i, j);
				const float s = inside[idx(i, j)];
				if (s > 0.0f)
				{
					// the ridges rise from the sheet (the troughs stay at Lift, above the terrain)
					const float furrow = 0.5f * look.FurrowDepth * (1.0f + std::sin(kTau * (glm::dot(p, C) - cOrigin) / std::max(0.05f, look.FurrowSpacing)));
					out.Vertices[idx(i, j)] = vertex(p, groundAt(p) + look.Lift + furrow * FurrowWeight(look, s), s);
					continue;
				}
				// a vertex just outside goes onto the edge line (where the inside crosses zero towards its nearest inside neighbour), at the
				// terrain: the earth meets the grass along the outline, not along the grid. One farther out hides under the terrain.
				float bestT = 2.0f;
				glm::vec2 onEdge = p;
				const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
				for (int k = 0; k < 4; ++k)
				{
					const int ii = i + di[k], jj = j + dj[k];
					if (ii < 0 || jj < 0 || ii >= na || jj >= nc) continue;
					const float sn = inside[idx(ii, jj)];
					if (sn <= 0.0f) continue;
					const float t = s / (s - sn);   // 0 here .. 1 at the neighbour
					if (t < bestT) { bestT = t; onEdge = p + (at(ii, jj) - p) * t; }
				}
				out.Vertices[idx(i, j)] = bestT <= 1.0f ? vertex(onEdge, groundAt(onEdge) - 0.01f, 0.0f) : vertex(p, groundAt(p) - look.Sink, 0.0f);
			}

		// the quads with a corner inside the edge (the rest is under the terrain or away from the field)
		std::vector<uint32_t> used(out.Vertices.size(), 0);
		std::vector<uint32_t> tris, rim;   // the furrowed sheet, the rim
		// what each vertex is inside, where it stands now (a vertex moved onto the edge is on it)
		std::vector<float> placed(inside.size());
		for (size_t k = 0; k < inside.size(); ++k) placed[k] = std::max(inside[k], 0.0f);
		for (int i = 0; i + 1 < na; ++i)
			for (int j = 0; j + 1 < nc; ++j)
			{
				const uint32_t q[4] = { static_cast<uint32_t>(idx(i, j)), static_cast<uint32_t>(idx(i + 1, j)), static_cast<uint32_t>(idx(i + 1, j + 1)), static_cast<uint32_t>(idx(i, j + 1)) };
				float best = -1e9f;
				for (uint32_t k : q) best = std::max(best, inside[k]);
				if (best <= 0.0f) continue;
				// counter-clockwise seen from above (+y up): which way depends on the handedness of the field's along/across axes
				const glm::vec2 e1 = at(i + 1, j) - at(i, j), e2 = at(i + 1, j + 1) - at(i, j);
				const bool up = e1.y * e2.x - e1.x * e2.y > 0.0f;   // the y of cross((e1.x, 0, e1.y), (e2.x, 0, e2.y))
				const uint32_t t[2][3] = { { q[0], up ? q[1] : q[2], up ? q[2] : q[1] }, { q[0], up ? q[2] : q[3], up ? q[3] : q[2] } };
				for (const auto& tri : t)
				{
					const bool onRim = std::max({ placed[tri[0]], placed[tri[1]], placed[tri[2]] }) < look.RimWidth;
					(onRim ? rim : tris).insert((onRim ? rim : tris).end(), { tri[0], tri[1], tri[2] });
				}
				for (uint32_t k : q) used[k] = 1;
			}
		if (rimIndices) *rimIndices = rim.size();
		tris.insert(tris.begin(), rim.begin(), rim.end());
		if (tris.empty()) { out.Vertices.clear(); if (rimIndices) *rimIndices = 0; return out; }
		// face normals summed on the vertices (area-weighted)
		for (size_t t = 0; t < tris.size(); t += 3)
		{
			Vertex& a = out.Vertices[tris[t]];
			Vertex& b = out.Vertices[tris[t + 1]];
			Vertex& c = out.Vertices[tris[t + 2]];
			const glm::vec3 n = glm::cross(b.Position - a.Position, c.Position - a.Position);
			a.Normal += n; b.Normal += n; c.Normal += n;
		}
		// only the vertices the triangles use, renumbered
		std::vector<uint32_t> remap(out.Vertices.size(), 0);
		std::vector<Vertex> kept;
		kept.reserve(out.Vertices.size());
		for (size_t k = 0; k < out.Vertices.size(); ++k)
			if (used[k])
			{
				remap[k] = static_cast<uint32_t>(kept.size());
				Vertex v = out.Vertices[k];
				const float len = glm::length(v.Normal);
				v.Normal = len > 1e-8f && v.Normal.y > 0.05f * len ? v.Normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
				kept.push_back(v);
			}
		out.Vertices = std::move(kept);
		out.Indices.reserve(tris.size());
		for (uint32_t k : tris) out.Indices.push_back(remap[k]);
		out.ComputeTangents();
		return out;
	}

	Image BuildTilledTints(const TilledShape& shape, const std::vector<std::string>& tints, const std::vector<uint8_t>& wet, const TilledLook& look, uint32_t seed,
		glm::vec4& rect)
	{
		Image img;
		const InsideFn insideAt(shape, look, seed);
		const Occupancy& occ = insideAt.Occ;
		const float sp = shape.Spacing;
		const bool alongX = shape.AlongX();
		const glm::vec2 C = alongX ? glm::vec2(0.0f, 1.0f) : glm::vec2(1.0f, 0.0f);
		const float cOrigin = glm::dot(shape.Origin, C);
		// texels: Texels a plot along the furrows, FurrowSamples a furrow across them (a plot of margin all round: the texture wraps)
		const float along = sp / static_cast<float>(std::clamp(look.Texels, 1, 16));
		const float across = std::max(0.02f, look.FurrowSpacing / static_cast<float>(std::max(2, look.FurrowSamples)));
		const glm::vec2 texel = alongX ? glm::vec2(along, across) : glm::vec2(across, along);
		glm::vec2 lo, hi;
		FieldBox(shape, sp, lo, hi);
		img.Width = static_cast<uint32_t>(std::clamp(static_cast<int>(std::ceil((hi.x - lo.x) / texel.x)), 1, 2048));
		img.Height = static_cast<uint32_t>(std::clamp(static_cast<int>(std::ceil((hi.y - lo.y) / texel.y)), 1, 2048));
		rect = glm::vec4(lo.x, lo.y, static_cast<float>(img.Width) * texel.x, static_cast<float>(img.Height) * texel.y);
		img.Pixels.assign(static_cast<size_t>(img.Width) * img.Height * 4u, 255);

		auto tintOf = [&](int plot)
		{
			if (plot < 0 || plot >= static_cast<int>(tints.size())) return glm::vec3(1.0f);
			const auto it = look.Tints.find(tints[static_cast<size_t>(plot)]);
			glm::vec3 c = it == look.Tints.end() ? glm::vec3(1.0f) : it->second;
			if (plot < static_cast<int>(wet.size()) && wet[static_cast<size_t>(plot)]) c *= look.WetTint;
			return c;
		};
		auto shaded = [&](int plot)
		{
			return plot >= 0 && plot < static_cast<int>(tints.size())
				&& std::find(look.ShadeStates.begin(), look.ShadeStates.end(), tints[static_cast<size_t>(plot)]) != look.ShadeStates.end();
		};
		// per cell: its plot, or the nearest plot round it (so the edge does not take the white of no plot)
		std::vector<int> nearest(occ.Plot.size(), -1);
		for (int r = 0; r < occ.Rows; ++r)
			for (int c = 0; c < occ.Cols; ++c)
			{
				int plot = occ.At(c, r);
				for (int ring = 1; plot < 0 && ring <= 3; ++ring)
					for (int dr = -ring; dr <= ring && plot < 0; ++dr)
						for (int dc = -ring; dc <= ring && plot < 0; ++dc) plot = occ.At(c + dc, r + dr);
				nearest[static_cast<size_t>(r) * static_cast<size_t>(occ.Cols) + static_cast<size_t>(c)] = plot;
			}
		// where each plot's plants stand (the shade under them)
		std::vector<glm::vec2> plants(static_cast<size_t>(std::max(0, shape.Plots)));
		for (int i = 0; i < shape.Plots; ++i)
		{
			const int cell = shape.CellOf(i);
			plants[static_cast<size_t>(i)] = shape.Origin + glm::vec2(static_cast<float>(cell % occ.Cols), static_cast<float>(cell / occ.Cols)) * sp + PlantOf(look, sp, seed, i, 1).Offset;
		}
		const float noise = std::clamp(look.TintNoise, 0.0f, 1.0f);
		const float shadeRadius = std::max(0.01f, look.PlantShadeRadius * sp * look.Spread);
		for (uint32_t y = 0; y < img.Height; ++y)
			for (uint32_t x = 0; x < img.Width; ++x)
			{
				const glm::vec2 p = lo + (glm::vec2(static_cast<float>(x), static_cast<float>(y)) + 0.5f) * texel;
				const glm::vec2 g = (p - shape.Origin) / sp;
				const int col = std::clamp(static_cast<int>(std::lround(g.x)), 0, occ.Cols - 1), row = std::clamp(static_cast<int>(std::lround(g.y)), 0, occ.Rows - 1);
				const int plot = nearest[static_cast<size_t>(row) * static_cast<size_t>(occ.Cols) + static_cast<size_t>(col)];
				const float s = insideAt(p);
				const float n = 1.0f + noise * (Unit(Hash3(x, y, seed)) - 0.5f) * 2.0f;
				glm::vec3 c;
				if (s < look.RimWidth) c = look.RimTint * (1.0f + 0.5f * (n - 1.0f));   // the margin (and what lies outside it, unseen)
				else
				{
					c = tintOf(plot) * n;
					// the bottom of the furrow
					const float trough = 0.5f - 0.5f * std::sin(kTau * (glm::dot(p, C) - cOrigin) / std::max(0.05f, look.FurrowSpacing));
					c *= 1.0f - look.FurrowShade * trough * trough * FurrowWeight(look, s);
					// the ground under the plants of the plots round it
					float shade = 0.0f;
					for (int dr = -1; dr <= 1; ++dr)
						for (int dc = -1; dc <= 1; ++dc)
						{
							const int q = occ.At(col + dc, row + dr);
							if (q < 0 || !shaded(q)) continue;
							shade = std::max(shade, 1.0f - Smooth(0.0f, shadeRadius, glm::length(p - plants[static_cast<size_t>(q)])));
						}
					c *= 1.0f - look.PlantShade * shade;
				}
				uint8_t* px = &img.Pixels[(static_cast<size_t>(y) * img.Width + x) * 4u];
				for (int k = 0; k < 3; ++k) px[k] = static_cast<uint8_t>(std::clamp(c[k] * 0.5f, 0.0f, 1.0f) * 255.0f + 0.5f);
				px[3] = 255;
			}
		return img;
	}

	std::vector<glm::vec4> TilledStones(const TilledShape& shape, const TilledLook& look, const HeightFn& ground, uint32_t seed)
	{
		std::vector<glm::vec4> out;
		if (shape.Plots <= 0 || look.StoneChance <= 0.0f || look.StoneSpacing <= 0.0f || look.StoneModel.empty()) return out;
		const InsideFn insideAt(shape, look, seed);
		const float step = 0.15f, mid = 0.5f * look.RimWidth, band = 0.5f * step;
		glm::vec2 lo, hi;
		FieldBox(shape, kCellsMargin + 0.1f, lo, hi);
		// candidates on the middle of the rim, in a fixed order; one kept when no other stands within StoneSpacing, then by chance
		std::map<std::pair<int, int>, std::vector<glm::vec2>> placed;
		auto key = [&](const glm::vec2& p) { return std::make_pair(static_cast<int>(std::floor(p.x / look.StoneSpacing)), static_cast<int>(std::floor(p.y / look.StoneSpacing))); };
		uint32_t n = 0;
		const int nx = static_cast<int>((hi.x - lo.x) / step), nz = static_cast<int>((hi.y - lo.y) / step);
		for (int iz = 0; iz <= nz; ++iz)
			for (int ix = 0; ix <= nx; ++ix)
			{
				const glm::vec2 p = lo + glm::vec2(static_cast<float>(ix), static_cast<float>(iz)) * step;
				if (std::abs(insideAt(p) - mid) > band) continue;
				const auto k = key(p);
				bool free = true;
				for (int dx = -1; dx <= 1 && free; ++dx)
					for (int dz = -1; dz <= 1 && free; ++dz)
						if (const auto it = placed.find({ k.first + dx, k.second + dz }); it != placed.end())
							for (const glm::vec2& q : it->second) free = free && glm::length(q - p) >= look.StoneSpacing;
				if (!free) continue;
				placed[k].push_back(p);
				const uint32_t h = Hash3(seed, n++, 0x570E5U);
				if (Unit(h) >= look.StoneChance) continue;
				out.emplace_back(p.x, (ground ? ground(p.x, p.y) : 0.0f) + look.Lift, p.y, Unit(Mix(h + 7U)));
			}
		return out;
	}

	TilledPlant PlantOf(const TilledLook& look, float spacing, uint32_t seed, int plot, int variants)
	{
		TilledPlant p;
		const uint32_t h = Hash3(seed, static_cast<uint32_t>(plot), 0x51ED27U);
		const float jx = Unit(Mix(h + 1U)) * 2.0f - 1.0f, jz = Unit(Mix(h + 2U)) * 2.0f - 1.0f;
		p.Offset = glm::vec2(jx, jz) * look.Jitter * spacing;
		p.Yaw = Unit(Mix(h + 3U)) * kTau;
		p.Tilt = glm::radians(look.Tilt) * Unit(Mix(h + 6U));
		p.TiltAxis = Unit(Mix(h + 7U)) * kTau;
		p.Scale = look.Scale.x + (look.Scale.y - look.Scale.x) * Unit(Mix(h + 4U));
		p.Variant = variants > 1 ? static_cast<int>(Mix(h + 5U) % static_cast<uint32_t>(variants)) : 0;
		return p;
	}
}
