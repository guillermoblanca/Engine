#include "GB/World/Water/LakeMesh.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GB
{
	LakeMeshGround GroundOf(const Heightmap& heightmap)
	{
		LakeMeshGround g;
		g.Heights = heightmap.Data();
		g.N = heightmap.GetResolution();
		g.Min = heightmap.GetMin();
		g.Spacing = heightmap.GetSpacing();
		return g;
	}

	namespace
	{
		// The world box that holds every point a lake covers: its mask's samples, or (a disc lake) the reach of its lobed outline.
		void LakeBounds(const LakeBody& lake, glm::vec2& lo, glm::vec2& hi)
		{
			if (!lake.Mask.Empty())
			{
				lo = lake.Mask.Origin;
				hi = lake.Mask.Origin + glm::vec2(static_cast<float>(lake.Mask.W - 1), static_cast<float>(lake.Mask.H - 1)) * lake.Mask.Spacing;
				return;
			}
			const float reach = lake.Radius * (1.0f + std::max(0.0f, lake.Outline.Irregularity));
			lo = lake.Center - glm::vec2(reach);
			hi = lake.Center + glm::vec2(reach);
		}

		// One line of the exact squared Euclidean distance transform (Felzenszwalb & Huttenlocher 2012, "Distance Transforms of Sampled
		// Functions"): d[q] = min over p of (q - p)^2 + f[p], the lower envelope of the parabolas rooted at every p. v and z are scratch of
		// n and n + 1. Every f is finite (a squared distance along the column), so in doubles every value is exact.
		void SquaredDistance1D(const double* f, int n, double* d, int* v, double* z)
		{
			constexpr double kInf = std::numeric_limits<double>::infinity();
			auto meet = [f](int q, int p)   // where the parabolas of q and p cross
			{
				const double dq = static_cast<double>(q), dp = static_cast<double>(p);
				return ((f[q] + dq * dq) - (f[p] + dp * dp)) / (2.0 * (dq - dp));
			};
			int k = 0;
			v[0] = 0;
			z[0] = -kInf;
			z[1] = kInf;
			for (int q = 1; q < n; ++q)
			{
				double s = meet(q, v[k]);
				while (s <= z[k]) { --k; s = meet(q, v[k]); }   // z[0] is -inf: stops at k = 0
				++k;
				v[k] = q;
				z[k] = s;
				z[k + 1] = kInf;
			}
			k = 0;
			for (int q = 0; q < n; ++q)
			{
				while (z[k + 1] < static_cast<double>(q)) ++k;
				const double dq = static_cast<double>(q - v[k]);
				d[q] = dq * dq + f[v[k]];
			}
		}

		// The distance in samples from every sample of a w x h box to the nearest sample that is not a cell (isCell[c] == 0), exact
		// Euclidean, with a ring of non-cells around the box: a lake that reaches the grid's edge has its shore there, and every column has
		// a non-cell at both ends. First the distance along each column (two sweeps), then the transform along each row. out: w * h.
		void ShoreDistanceTransform(const std::vector<uint8_t>& isCell, int w, int h, std::vector<double>& grid, std::vector<double>& row,
			std::vector<int>& v, std::vector<double>& z, std::vector<float>& out)
		{
			const int pw = w + 2, ph = h + 2;
			auto cell = [&](int pi, int pj)   // padded coordinates; the ring is not a cell
			{
				return pi >= 1 && pj >= 1 && pi <= w && pj <= h && isCell[static_cast<size_t>(pj - 1) * static_cast<size_t>(w) + static_cast<size_t>(pi - 1)] != 0;
			};
			grid.assign(static_cast<size_t>(pw) * static_cast<size_t>(ph), 0.0);
			auto g = [&](int pi, int pj) -> double& { return grid[static_cast<size_t>(pj) * static_cast<size_t>(pw) + static_cast<size_t>(pi)]; };
			for (int pi = 0; pi < pw; ++pi)
			{
				int last = 0;   // the padding row 0 is a non-cell
				for (int pj = 0; pj < ph; ++pj)
				{
					if (!cell(pi, pj)) last = pj;
					g(pi, pj) = static_cast<double>(pj - last);
				}
				last = ph - 1;
				for (int pj = ph - 1; pj >= 0; --pj)
				{
					if (!cell(pi, pj)) last = pj;
					const double down = static_cast<double>(last - pj);
					double& d = g(pi, pj);
					d = std::min(d, down);
					d *= d;   // squared, for the row pass
				}
			}
			row.resize(static_cast<size_t>(pw));
			v.resize(static_cast<size_t>(pw));
			z.resize(static_cast<size_t>(pw) + 1);
			out.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
			for (int pj = 1; pj <= h; ++pj)
			{
				SquaredDistance1D(&g(0, pj), pw, row.data(), v.data(), z.data());
				for (int pi = 1; pi <= w; ++pi)
					out[static_cast<size_t>(pj - 1) * static_cast<size_t>(w) + static_cast<size_t>(pi - 1)] = static_cast<float>(std::sqrt(row[static_cast<size_t>(pi)]));
			}
		}
	}

	LakeMesh BuildLakeMesh(const WaterField& water, const LakeMeshGround& ground, const WaterLook& look, const LakeMeshOptions& options)
	{
		LakeMesh mesh;
		const std::vector<const LakeBody*> lakes = water.Lakes();
		mesh.Bodies.reserve(lakes.size());
		mesh.Ranges.resize(lakes.size());
		for (const LakeBody* lake : lakes)
		{
			WaterBodyParams body;
			body.Level = lake->Level;
			body.Look = look.Lake;
			body.Center = lake->Center;
			body.Radius = lake->Radius;
			mesh.Bodies.push_back(body);
		}
		if (!ground.Heights || ground.N < 2 || ground.Spacing <= 0.0f) return mesh;

		const int skirt = std::clamp(options.SkirtCells, 1, 2);
		const int n = static_cast<int>(ground.N);
		constexpr uint8_t kOut = 0, kCell = 1, kSkirt = 2;
		constexpr uint8_t kFar = 255;
		constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
		std::vector<uint8_t> kind, dist, next, isCell;
		std::vector<uint32_t> remap;
		std::vector<double> edtGrid, edtRow, edtZ;
		std::vector<int> edtV;
		std::vector<float> shore;   // per local sample: samples to the nearest non-cell (0 off the lake)

		for (size_t k = 0; k < lakes.size(); ++k)
		{
			const LakeBody& lake = *lakes[k];
			LakeMesh::Range& range = mesh.Ranges[k];
			range.FirstVertex = static_cast<uint32_t>(mesh.Vertices.size());
			range.FirstIndex = static_cast<uint32_t>(mesh.Indices.size());

			// the samples of the box around the lake, grown by the skirt and one more, clamped to the grid
			glm::vec2 lo, hi;
			LakeBounds(lake, lo, hi);
			const int margin = skirt + 1;
			const int i0 = std::max(0, static_cast<int>(std::floor((lo.x - ground.Min.x) / ground.Spacing)) - margin);
			const int j0 = std::max(0, static_cast<int>(std::floor((lo.y - ground.Min.y) / ground.Spacing)) - margin);
			const int i1 = std::min(n - 1, static_cast<int>(std::ceil((hi.x - ground.Min.x) / ground.Spacing)) + margin);
			const int j1 = std::min(n - 1, static_cast<int>(std::ceil((hi.y - ground.Min.y) / ground.Spacing)) + margin);
			if (i1 <= i0 || j1 <= j0) continue;
			const int w = i1 - i0 + 1, h = j1 - j0 + 1;
			const size_t local = static_cast<size_t>(w) * static_cast<size_t>(h);
			auto at = [w](int li, int lj) { return static_cast<size_t>(lj) * static_cast<size_t>(w) + static_cast<size_t>(li); };

			// the lake's cells: the samples it covers that no lake before it in Lakes() covers (as WaterRaster: one owner per sample)
			kind.assign(local, kOut);
			dist.assign(local, kFar);
			size_t cells = 0;
			for (int lj = 0; lj < h; ++lj)
				for (int li = 0; li < w; ++li)
				{
					const glm::vec2 p = ground.SamplePosition(static_cast<uint32_t>(i0 + li), static_cast<uint32_t>(j0 + lj));
					if (!lake.Covers(p)) continue;
					bool earlier = false;
					for (size_t e = 0; e < k && !earlier; ++e) earlier = lakes[e]->Covers(p);
					if (earlier) continue;
					kind[at(li, lj)] = kCell;
					dist[at(li, lj)] = 0;
					++cells;
				}
			mesh.LakeCells += cells;
			if (cells == 0) continue;

			// how far every cell is from the shore (T139): the exact distance transform over the box, the non-cells its features
			isCell.resize(local);
			for (size_t c = 0; c < local; ++c) isCell[c] = kind[c] == kCell ? 1 : 0;
			ShoreDistanceTransform(isCell, w, h, edtGrid, edtRow, edtV, edtZ, shore);

			// the Chebyshev distance to the nearest cell, up to the skirt (one 3x3 grow per ring)
			for (int ring = 1; ring <= skirt; ++ring)
			{
				next = dist;
				for (int lj = 0; lj < h; ++lj)
					for (int li = 0; li < w; ++li)
					{
						if (dist[at(li, lj)] != kFar) continue;
						bool touches = false;
						for (int dj = -1; dj <= 1 && !touches; ++dj)
							for (int di = -1; di <= 1 && !touches; ++di)
							{
								const int ni = li + di, nj = lj + dj;
								touches = ni >= 0 && nj >= 0 && ni < w && nj < h && dist[at(ni, nj)] == static_cast<uint8_t>(ring - 1);
							}
						if (touches) next[at(li, lj)] = static_cast<uint8_t>(ring);
					}
				dist.swap(next);
			}
			// a skirt sample: near the lake, not of it, with the ground at or above its level (under the terrain)
			for (int lj = 0; lj < h; ++lj)
				for (int li = 0; li < w; ++li)
				{
					const size_t c = at(li, lj);
					if (kind[c] == kOut && dist[c] != kFar && ground.At(static_cast<uint32_t>(i0 + li), static_cast<uint32_t>(j0 + lj)) >= lake.Level) kind[c] = kSkirt;
				}

			// the triangles of the terrain's split (Terrain.cpp: {a, c, b} and {b, c, d}, a = (i, j), b = (i + 1, j), c = (i, j + 1)), each drawn
			// when its three corners are cells or skirt and one of them is within skirt - 1 of a cell; the vertices numbered as first used
			remap.assign(local, kNone);
			auto vertex = [&](int li, int lj) -> uint32_t
			{
				uint32_t& v = remap[at(li, lj)];
				if (v == kNone)
				{
					v = static_cast<uint32_t>(mesh.Vertices.size());
					LakeVertex lv;
					const glm::vec2 p = ground.SamplePosition(static_cast<uint32_t>(i0 + li), static_cast<uint32_t>(j0 + lj));
					lv.Position = { p.x, lake.Level, p.y };
					lv.Lake = static_cast<uint32_t>(k);
					lv.Depth = lake.Level - ground.At(static_cast<uint32_t>(i0 + li), static_cast<uint32_t>(j0 + lj));
					lv.ShoreDistance = kind[at(li, lj)] == kCell ? shore[at(li, lj)] * ground.Spacing : 0.0f;
					mesh.Vertices.push_back(lv);
				}
				return v;
			};
			auto triangle = [&](int ai, int aj, int bi, int bj, int ci, int cj)
			{
				const size_t a = at(ai, aj), b = at(bi, bj), c = at(ci, cj);
				const bool lakeCorner = kind[a] == kCell || kind[b] == kCell || kind[c] == kCell;
				const bool inside = kind[a] != kOut && kind[b] != kOut && kind[c] != kOut;
				const int nearest = std::min({ static_cast<int>(dist[a]), static_cast<int>(dist[b]), static_cast<int>(dist[c]) });
				if (!inside || nearest > skirt - 1)
				{
					if (lakeCorner) ++mesh.DroppedShoreTriangles;
					return;
				}
				const uint32_t va = vertex(ai, aj), vb = vertex(bi, bj), vc = vertex(ci, cj);
				mesh.Indices.insert(mesh.Indices.end(), { va, vb, vc });
			};
			for (int lj = 0; lj + 1 < h; ++lj)
				for (int li = 0; li + 1 < w; ++li)
				{
					triangle(li, lj, li, lj + 1, li + 1, lj);           // a, c, b
					triangle(li + 1, lj, li, lj + 1, li + 1, lj + 1);   // b, c, d
				}

			range.VertexCount = static_cast<uint32_t>(mesh.Vertices.size()) - range.FirstVertex;
			range.IndexCount = static_cast<uint32_t>(mesh.Indices.size()) - range.FirstIndex;
		}
		return mesh;
	}
}
