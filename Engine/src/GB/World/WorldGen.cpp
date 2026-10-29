#include "WorldGen.h"
#include <algorithm>
#include <functional>
#include <queue>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace GB
{
	namespace
	{
		constexpr float kBiomeCell = 1.0f;   // meters between the samples of the baked biome map (BiomeMap)

		float Hash2(float x, float z, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(static_cast<int>(std::floor(x * 8.0f))) * 374761393u + static_cast<uint32_t>(static_cast<int>(std::floor(z * 8.0f))) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
		}
		float Noise2(float x, float z, uint32_t seed)
		{
			const float fx = std::floor(x), fz = std::floor(z);
			float u = x - fx, v = z - fz;
			u = u * u * (3.0f - 2.0f * u); v = v * v * (3.0f - 2.0f * v);
			auto h = [&](float dx, float dz) { return Hash2((fx + dx) * 0.125f, (fz + dz) * 0.125f, seed); };
			return glm::mix(glm::mix(h(0, 0), h(1, 0), u), glm::mix(h(0, 1), h(1, 1), u), v);
		}
		float Fbm2(float x, float z, uint32_t seed)
		{
			float v = 0.0f, a = 0.5f;
			for (int i = 0; i < 4; ++i) { v += a * Noise2(x, z, seed + static_cast<uint32_t>(i)); x = x * 2.0f + 11.0f; z = z * 2.0f + 5.0f; a *= 0.5f; }
			return v / 0.9375f;
		}
		// The salt of a placement hash mixed with the seed of the world (generator 2): without it, accepting or rejecting a tree at a place and
		// choosing its species came out the same in every seed (generator 1 keeps the bare salts).
		uint32_t SeedSalt(uint32_t seed, uint32_t salt)
		{
			uint32_t h = seed * 0x9E3779B1u ^ (salt + 0x7F4A7C15u);
			h ^= h >> 16; h *= 0x85EBCA6Bu;
			h ^= h >> 13; h *= 0xC2B2AE35u;
			return h ^ (h >> 16);
		}

		// A smoothed copy of the heightmap's heights for path finding (box blur on a coarse grid).
		struct Coarse
		{
			int N = 0; float Step = 0.0f; glm::vec2 Lo{ 0.0f }; std::vector<float> H;
			float At(int i, int j) const { return H[static_cast<size_t>(std::clamp(j, 0, N - 1)) * static_cast<size_t>(N) + static_cast<size_t>(std::clamp(i, 0, N - 1))]; }
			glm::vec2 Pos(int i, int j) const { return Lo + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * Step; }
		};
		Coarse MakeCoarse(const Heightmap& hm, int n)
		{
			Coarse c;
			c.N = n; c.Step = hm.GetSize() / static_cast<float>(n - 1); c.Lo = hm.GetMin();
			c.H.resize(static_cast<size_t>(n) * static_cast<size_t>(n));
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					float sum = 0.0f;
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
						{
							const glm::vec2 p = c.Pos(std::clamp(i + di, 0, n - 1), std::clamp(j + dj, 0, n - 1));
							sum += hm.GetHeight(p.x, p.y);
						}
					c.H[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)] = sum / 9.0f;
				}
			return c;
		}

		// Distance from (x, z) to segment k of the river, and the water level at the nearest point of it: the very operations of
		// Terrain::DistanceToRiver / GetRiverLevelAt / GetRiverSurfaceLevel, so the results are the same to the bit.
		void RiverSegment(const RiverData& r, size_t k, float x, float z, float& d, float& level)
		{
			const auto& P = r.Points;
			const glm::vec2 a(P[k].x, P[k].z), ab = glm::vec2(P[k + 1].x, P[k + 1].z) - a;
			const float tt = glm::clamp(glm::dot(glm::vec2(x, z) - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
			d = glm::length(glm::vec2(x, z) - (a + ab * tt));
			level = glm::mix(P[k].y, P[k + 1].y, tt);
		}

		// The biome from what is measured at a point. `riverD` and `riverLevel` are the distance to the river axis and the water level at its
		// nearest point (1e9 and -1e9 without a river, or far from it).
		Biome Classify(const GeneratedWorld& w, float x, float z, float riverD, float riverLevel)
		{
			const Terrain& t = *w.TerrainRef;
			const float h = t.GetHeight(x, z);
			if (t.HasLake() && h < t.GetWaterLevel() - 0.02f && glm::distance(glm::vec2(x, z), t.GetLakeCenter()) < t.GetLakeRadius() * 1.3f) return Biome::Water;
			const RiverData* river = t.GetRiver();
			if (river && riverD <= river->HalfWidth * 1.2f && h < riverLevel) return Biome::Water;
			float nearWater = 1e9f;
			if (t.HasLake()) nearWater = std::min(nearWater, std::max(h - t.GetWaterLevel(), 0.0f) + (glm::distance(glm::vec2(x, z), t.GetLakeCenter()) > t.GetLakeRadius() * 1.8f ? 1e9f : 0.0f));
			if (river && riverD < river->HalfWidth * 1.8f) nearWater = std::min(nearWater, std::max(h - riverLevel, 0.0f));
			if (nearWater < 0.7f) return Biome::Shore;
			if (h > w.SnowLine) return Biome::Snow;
			const glm::vec3 n = t.GetHeightmap().GetNormal(x, z);
			const float slopeDeg = glm::degrees(std::acos(glm::clamp(n.y, -1.0f, 1.0f)));
			const float rockStart = w.Desc.Type == WorldType::Alpine ? 26.0f : 33.0f;
			if (slopeDeg > rockStart) return Biome::Rock;
			const float m = w.MoistureAt(x, z);
			return m > 0.8f - 0.5f * w.Desc.ForestAmount ? Biome::Forest : Biome::Meadow;
		}

		// Exact Euclidean distance transform in one dimension (Felzenszwalb and Huttenlocher): `f` holds squared distances, replaced by
		// the lower envelope of the parabolas rooted at each sample.
		void DistanceTransform1D(std::vector<float>& f, int n, std::vector<float>& d, std::vector<int>& v, std::vector<float>& zs)
		{
			constexpr float kInf = 1e20f;   // no water on this line (yet): such samples root no parabola
			auto F = [&](int i) { return f[static_cast<size_t>(i)]; };
			int k = -1;
			for (int q = 0; q < n; ++q)
			{
				if (F(q) >= kInf) continue;
				if (k < 0) { k = 0; v[0] = q; zs[0] = -kInf; zs[1] = kInf; continue; }
				float s;
				for (;;)
				{
					const int p = v[static_cast<size_t>(k)];
					s = ((F(q) + static_cast<float>(q) * static_cast<float>(q)) - (F(p) + static_cast<float>(p) * static_cast<float>(p))) / (2.0f * static_cast<float>(q - p));
					if (s <= zs[static_cast<size_t>(k)]) { --k; continue; }   // zs[0] is -infinity: k never drops below 0
					break;
				}
				++k;
				v[static_cast<size_t>(k)] = q; zs[static_cast<size_t>(k)] = s; zs[static_cast<size_t>(k) + 1] = kInf;
			}
			if (k < 0) return;   // nothing finite: the line stays at infinity
			k = 0;
			for (int q = 0; q < n; ++q)
			{
				while (zs[static_cast<size_t>(k) + 1] < static_cast<float>(q)) ++k;
				const int p = v[static_cast<size_t>(k)];
				const float dq = static_cast<float>(q - p);
				d[static_cast<size_t>(q)] = dq * dq + F(p);
			}
			for (int q = 0; q < n; ++q) f[static_cast<size_t>(q)] = d[static_cast<size_t>(q)];
		}

		// Bakes the biome of every sample and the distance from it to the water.
		BiomeMap BakeBiomes(const GeneratedWorld& w)
		{
			BiomeMap m;
			const Heightmap& hm = w.TerrainRef->GetHeightmap();
			m.N = std::max(2, static_cast<int>(std::ceil(hm.GetSize() / kBiomeCell)) + 1);
			m.Cell = hm.GetSize() / static_cast<float>(m.N - 1);
			m.Min = hm.GetMin();
			const size_t count = static_cast<size_t>(m.N) * static_cast<size_t>(m.N);
			m.Biomes.assign(count, static_cast<uint8_t>(Biome::Dunes));
			m.WaterDistance.assign(count, 1e9f);
			if (w.Desc.Type == WorldType::Desert) return m;

			// The river's distance and level, rasterized segment by segment around the river only: a sample takes the first segment that is strictly
			// nearest, like the loop over all the segments of Terrain, and a sample farther than `reach` from every segment does not need it.
			std::vector<float> riverD(count, 1e9f), riverLevel(count, -1e9f);
			if (const RiverData* r = w.TerrainRef->GetRiver())
			{
				const float reach = r->HalfWidth * 1.8f + m.Cell;
				for (size_t k = 0; k + 1 < r->Points.size(); ++k)
				{
					const glm::vec2 a(r->Points[k].x, r->Points[k].z), b(r->Points[k + 1].x, r->Points[k + 1].z);
					const glm::vec2 lo = glm::min(a, b) - reach, hi = glm::max(a, b) + reach;
					const int i0 = std::max(0, static_cast<int>(std::floor((lo.x - m.Min.x) / m.Cell))), i1 = std::min(m.N - 1, static_cast<int>(std::ceil((hi.x - m.Min.x) / m.Cell)));
					const int j0 = std::max(0, static_cast<int>(std::floor((lo.y - m.Min.y) / m.Cell))), j1 = std::min(m.N - 1, static_cast<int>(std::ceil((hi.y - m.Min.y) / m.Cell)));
					for (int j = j0; j <= j1; ++j)
						for (int i = i0; i <= i1; ++i)
						{
							const glm::vec2 p = m.SamplePosition(i, j);
							float d, level;
							RiverSegment(*r, k, p.x, p.y, d, level);
							const size_t o = static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i);
							if (d < riverD[o]) { riverD[o] = d; riverLevel[o] = level; }
						}
				}
			}
			std::vector<float> f(count);
			for (int j = 0; j < m.N; ++j)
				for (int i = 0; i < m.N; ++i)
				{
					const size_t o = static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i);
					const glm::vec2 p = m.SamplePosition(i, j);
					const Biome b = Classify(w, p.x, p.y, riverD[o], riverLevel[o]);
					m.Biomes[o] = static_cast<uint8_t>(b);
					f[o] = b == Biome::Water ? 0.0f : 1e20f;
				}

			// Squared distance in samples: columns, then rows.
			const int n = m.N;
			std::vector<float> line(static_cast<size_t>(n)), d(static_cast<size_t>(n)), zs(static_cast<size_t>(n) + 1);
			std::vector<int> v(static_cast<size_t>(n));
			for (int i = 0; i < n; ++i)
			{
				for (int j = 0; j < n; ++j) line[static_cast<size_t>(j)] = f[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)];
				DistanceTransform1D(line, n, d, v, zs);
				for (int j = 0; j < n; ++j) f[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)] = line[static_cast<size_t>(j)];
			}
			for (int j = 0; j < n; ++j)
			{
				for (int i = 0; i < n; ++i) line[static_cast<size_t>(i)] = f[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)];
				DistanceTransform1D(line, n, d, v, zs);
				for (int i = 0; i < n; ++i)
				{
					const float sq = line[static_cast<size_t>(i)];
					m.WaterDistance[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)] = sq >= 1e19f ? 1e9f : std::sqrt(sq) * m.Cell;
				}
			}
			return m;
		}
	}

	const char* BiomeName(Biome b)
	{
		switch (b)
		{
		case Biome::Water: return "Water";
		case Biome::Shore: return "Shore";
		case Biome::Meadow: return "Meadow";
		case Biome::Forest: return "Forest";
		case Biome::Rock: return "Rock";
		case Biome::Snow: return "Snow";
		case Biome::Dunes: return "Dunes";
		}
		return "Water";
	}

	size_t BiomeMap::Nearest(float x, float z) const
	{
		const int i = std::clamp(static_cast<int>(std::floor((x - Min.x) / Cell + 0.5f)), 0, N - 1);
		const int j = std::clamp(static_cast<int>(std::floor((z - Min.y) / Cell + 0.5f)), 0, N - 1);
		return static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i);
	}

	float BiomeMap::WaterDistanceAt(float x, float z) const
	{
		if (Empty()) return 1e9f;
		const float maxIndex = static_cast<float>(N - 1);
		const float fx = glm::clamp((x - Min.x) / Cell, 0.0f, maxIndex), fz = glm::clamp((z - Min.y) / Cell, 0.0f, maxIndex);
		const int i0 = std::min(static_cast<int>(fx), N - 2), j0 = std::min(static_cast<int>(fz), N - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		auto D = [&](int i, int j) { return WaterDistance[static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i)]; };
		return glm::mix(glm::mix(D(i0, j0), D(i0 + 1, j0), tx), glm::mix(D(i0, j0 + 1), D(i0 + 1, j0 + 1), tx), tz);
	}

	float GeneratedWorld::MoistureAt(float x, float z) const { return Fbm2(x / 60.0f, z / 60.0f, Desc.Seed * 7u + 3u); }

	Biome GeneratedWorld::ComputeBiome(float x, float z) const
	{
		if (Desc.Type == WorldType::Desert) return Biome::Dunes;
		float riverD = 1e9f, riverLevel = -1e9f;
		if (const RiverData* r = TerrainRef->GetRiver())
			for (size_t k = 0; k + 1 < r->Points.size(); ++k)
			{
				float d, level;
				RiverSegment(*r, k, x, z, d, level);
				if (d < riverD) { riverD = d; riverLevel = level; }
			}
		return Classify(*this, x, z, riverD, riverLevel);
	}

	Biome GeneratedWorld::BiomeAt(float x, float z) const
	{
		return Maps.Empty() ? ComputeBiome(x, z) : static_cast<Biome>(Maps.Biomes[Maps.Nearest(x, z)]);
	}

	float GeneratedWorld::WaterDistanceAt(float x, float z) const { return Maps.WaterDistanceAt(x, z); }

	float GeneratedWorld::GrassDensityAt(float x, float z) const
	{
		switch (BiomeAt(x, z))
		{
		case Biome::Meadow: return 1.0f;
		case Biome::Forest: return 0.55f;
		case Biome::Shore: return 0.25f;
		case Biome::Rock: return 0.12f;
		default: return 0.0f;
		}
	}

	std::vector<float> GeneratedWorld::BiomeShares(int samples) const
	{
		std::vector<float> shares(7, 0.0f);
		const Heightmap& hm = TerrainRef->GetHeightmap();
		for (int j = 0; j < samples; ++j)
			for (int i = 0; i < samples; ++i)
			{
				const float x = hm.GetMin().x + (static_cast<float>(i) + 0.5f) / static_cast<float>(samples) * hm.GetSize();
				const float z = hm.GetMin().y + (static_cast<float>(j) + 0.5f) / static_cast<float>(samples) * hm.GetSize();
				shares[static_cast<size_t>(BiomeAt(x, z))] += 1.0f / static_cast<float>(samples * samples);
			}
		return shares;
	}

	GeneratedWorld WorldGenerator::Generate(const WorldDesc& desc)
	{
		GeneratedWorld w = GenerateData(desc);
		BuildGpu(w);
		return w;
	}

	void WorldGenerator::BuildGpu(GeneratedWorld& world)
	{
		if (world.TerrainPtr) world.TerrainPtr->BuildGpu();
	}

	GeneratedWorld WorldGenerator::GenerateData(const WorldDesc& desc)
	{
		if (desc.Tiles > 1)
		{
			// H16: several tiles are one bigger map of the same generator (seamless because it is one heightmap). The world keeps the desc it was
			// asked for (per-tile Size, the Tiles), so a save writes it back as it came.
			const int t = std::min(desc.Tiles, 4);
			WorldDesc big = desc;
			big.Tiles = 1;
			big.Size = desc.Size * static_cast<float>(t);
			big.Resolution = (desc.Resolution - 1u) * static_cast<uint32_t>(t) + 1u;
			big.TreeBudget = desc.TreeBudget * t * t;
			big.RockBudget = desc.RockBudget * t * t;
			GeneratedWorld w = GenerateData(big);
			w.Desc = desc;
			w.Desc.Tiles = t;
			return w;
		}
		GeneratedWorld w;
		w.Desc = desc;
		const bool desert = desc.Type == WorldType::Desert;
		// Generator 1 is kept exactly as it was (a saved world rebuilds its baseline from the seed): fixed salts, and the trees and rocks placed
		// with the biome measured at each point. Generator 2 mixes the seed into the salts and bakes the biome map before placing anything.
		const bool v1 = desc.GeneratorVersion <= kWorldGeneratorV1;
		auto salt = [&](uint32_t s) { return v1 ? s : SeedSalt(desc.Seed, s); };

		if (desert)
		{
			DuneParams dp;
			dp.Seed = desc.Seed;
			w.TerrainPtr = Terrain::CreateDunes(dp, desc.Size, desc.Resolution, TerrainStyle::Desert, false);
		}
		else
		{
			HillParams hp;
			hp.Seed = desc.Seed;
			hp.Height = desc.Type == WorldType::Alpine ? 58.0f : 16.0f;
			hp.Scale = desc.Type == WorldType::Alpine ? 190.0f : 140.0f;
			hp.Flatten = desc.Type == WorldType::Alpine ? 0.05f : 0.25f;
			const TerrainStyle style = desc.Type == WorldType::Alpine ? TerrainStyle::Snow : TerrainStyle::Meadow;

			// Pass 1: the bare terrain, to find the basin for the lake and the way down for the river.
			hp.SurfaceMaps = false;
			auto bare = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style, false);
			const Coarse coarse = MakeCoarse(bare->GetHeightmap(), 65);
			// lowest point away from the borders
			int bi = 0, bj = 0; float lowest = 1e9f;
			for (int j = 12; j < coarse.N - 12; ++j)
				for (int i = 12; i < coarse.N - 12; ++i)
					if (coarse.At(i, j) < lowest) { lowest = coarse.At(i, j); bi = i; bj = j; }
			const glm::vec2 basin = coarse.Pos(bi, bj);
			if (desc.Lake)
			{
				hp.LakeCenter = basin;
				hp.LakeRadius = desc.Type == WorldType::Alpine ? 26.0f : 22.0f;
				hp.LakeDepth = 3.0f;
				hp.WaterLevel = lowest + 1.0f;
			}
			if (desc.River)
			{
				// A cheapest path (Dijkstra) on the coarse grid from the highest border point to the lake basin (or, without
				// a lake, to the lowest border point): going uphill is expensive, so the river follows the valleys and always arrives.
				const int N = coarse.N;
				glm::ivec2 startCell(4, 4); float top = -1e9f;
				glm::ivec2 goalCell(bi, bj); float bottom = 1e9f;
				for (int k = 4; k < N - 4; ++k)
					for (const glm::ivec2 e : { glm::ivec2(k, 3), glm::ivec2(k, N - 4), glm::ivec2(3, k), glm::ivec2(N - 4, k) })
					{
						if (coarse.At(e.x, e.y) > top && (!desc.Lake || glm::distance(coarse.Pos(e.x, e.y), basin) > desc.Size * 0.3f)) { top = coarse.At(e.x, e.y); startCell = e; }
						if (!desc.Lake && coarse.At(e.x, e.y) < bottom && glm::distance(coarse.Pos(e.x, e.y), coarse.Pos(startCell.x, startCell.y)) > desc.Size * 0.4f) { bottom = coarse.At(e.x, e.y); goalCell = e; }
					}
				std::vector<float> cost(static_cast<size_t>(N) * static_cast<size_t>(N), 1e30f);
				std::vector<int> from(cost.size(), -1);
				using Item = std::pair<float, int>;
				std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
				auto idx = [&](int i, int j) { return j * N + i; };
				cost[static_cast<size_t>(idx(startCell.x, startCell.y))] = 0.0f;
				open.push({ 0.0f, idx(startCell.x, startCell.y) });
				while (!open.empty())
				{
					const auto [c, cur] = open.top();
					open.pop();
					if (c > cost[static_cast<size_t>(cur)]) continue;
					const int ci = cur % N, cj = cur / N;
					if (ci == goalCell.x && cj == goalCell.y) break;
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
						{
							const int ni = ci + di, nj = cj + dj;
							if ((di == 0 && dj == 0) || ni < 2 || nj < 2 || ni >= N - 2 || nj >= N - 2) continue;
							const float len = (di != 0 && dj != 0 ? 1.4142f : 1.0f) * coarse.Step;
							const float rise = coarse.At(ni, nj) - coarse.At(ci, cj);
							const glm::vec2 q = coarse.Pos(ni, nj);
							const float meander = 1.0f + 0.6f * Noise2(q.x / 30.0f, q.y / 30.0f, desc.Seed + 99u);
							// Going down costs nothing extra (a term "2 * max(-rise, 0) * 0" used to stand here: adding +0 never changed a cost, so dropping it
							// keeps generator 1 exact).
							const float next = c + len * meander + 14.0f * std::max(rise, 0.0f);
							if (next < cost[static_cast<size_t>(idx(ni, nj))]) { cost[static_cast<size_t>(idx(ni, nj))] = next; from[static_cast<size_t>(idx(ni, nj))] = cur; open.push({ next, idx(ni, nj) }); }
						}
				}
				std::vector<glm::vec2> path;
				for (int cur = idx(goalCell.x, goalCell.y); cur >= 0; cur = from[static_cast<size_t>(cur)])
					path.push_back(coarse.Pos(cur % N, cur / N));
				std::reverse(path.begin(), path.end());
				// every third node is plenty for the spline; the first and last stay
				std::vector<glm::vec2> thinned;
				for (size_t i = 0; i < path.size(); ++i)
					if (i % 3 == 0 || i + 1 == path.size()) thinned.push_back(path[i]);
				path.swap(thinned);
				if (desc.Lake)   // the river ends at the bank of the lake, not in the middle of it
				{
					while (path.size() > 2 && glm::distance(path.back(), basin) < hp.LakeRadius * 1.7f) path.pop_back();
					const glm::vec2 toLake = basin - path.back();
					const float len = glm::length(toLake);
					path.push_back(basin - (len > 1e-3f ? toLake / len : glm::vec2(1.0f, 0.0f)) * hp.LakeRadius * 0.95f);
				}
				if (path.size() >= 3) hp.RiverPath = path;
				hp.RiverWidth = desc.Type == WorldType::Alpine ? 5.0f : 6.5f;
			}
			hp.SurfaceMaps = true;
			w.Hills = hp;
			w.TerrainPtr = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style, false);
			if (desc.Type == WorldType::Alpine)
				w.SnowLine = w.TerrainPtr->GetHeightmap().GetMinHeight() + 0.68f * (w.TerrainPtr->GetHeightmap().GetMaxHeight() - w.TerrainPtr->GetHeightmap().GetMinHeight());
		}

		w.TerrainRef = w.TerrainPtr.get();
		Terrain& terrain = *w.TerrainPtr;
		if (!v1) w.Maps = BakeBiomes(w);   // generator 2 places everything below on the baked map

		// Spawn: the flattest dry point near the middle.
		{
			float best = 1e9f;
			for (int k = 0; k < 200; ++k)
			{
				const float x = (Hash2(static_cast<float>(k), 1.0f, desc.Seed) - 0.5f) * desc.Size * 0.4f, z = (Hash2(static_cast<float>(k), 2.0f, desc.Seed) - 0.5f) * desc.Size * 0.4f;
				const glm::vec3 n = terrain.GetHeightmap().GetNormal(x, z);
				const float wet = w.BiomeAt(x, z) == Biome::Water ? 10.0f : 0.0f;
				const float score = (1.0f - n.y) * 10.0f + wet;
				if (score < best) { best = score; w.Spawn = { x, z }; }
			}
		}

		// ---- trees: species recipes and placement by biome
		if (desert)
		{
			for (uint32_t i = 0; i < 3; ++i) { TreeParams tp; tp.Species = TreeSpecies::Palm; tp.Height = 6.0f + 1.5f * static_cast<float>(i); tp.Seed = i + 1; w.TreeRecipes.push_back(tp); }
		}
		else
		{
			auto add = [&](TreeSpecies sp, float h, uint32_t sd, glm::vec3 tint = glm::vec3(1.0f)) { TreeParams tp; tp.Species = sp; tp.Height = h; tp.Seed = sd; tp.LeafTint = tint; w.TreeRecipes.push_back(tp); };
			add(TreeSpecies::Broadleaf, 8.0f, 1); add(TreeSpecies::Broadleaf, 10.5f, 2); add(TreeSpecies::Broadleaf, 7.0f, 3, { 1.15f, 1.0f, 0.7f });   // 0-2 broadleaf
			add(TreeSpecies::Conifer, 11.0f, 4); add(TreeSpecies::Conifer, 14.0f, 5); add(TreeSpecies::Conifer, 8.5f, 6);                               // 3-5 conifers
			add(TreeSpecies::Bare, 8.0f, 7);                                                                                                            // 6 dead tree
		}
		{
			const float minH = terrain.GetHeightmap().GetMinHeight(), maxH = terrain.GetHeightmap().GetMaxHeight();
			const GeneratedWorld* self = &w;
			const uint32_t palmSalt = salt(5u), loneSalt = salt(9u), rockTreeSalt = salt(11u);
			auto density = [self, desert, palmSalt, loneSalt, rockTreeSalt](float x, float z)
			{
				if (desert) return Hash2(x, z, palmSalt) < 0.04f ? 1.0f : 0.0f;   // sparse palms (the scatter's spacing does the rest)
				switch (self->BiomeAt(x, z))
				{
				case Biome::Forest: return 1.0f;
				case Biome::Meadow: return Hash2(x, z, loneSalt) < 0.06f ? 1.0f : 0.0f;   // lone trees
				case Biome::Rock: return Hash2(x, z, rockTreeSalt) < 0.12f ? 1.0f : 0.0f;
				default: return 0.0f;
				}
			};
			const uint32_t acceptSalt = salt(31u);
			auto spots = ScatterTrees(terrain, desc.TreeBudget * 3, static_cast<int>(w.TreeRecipes.size()), desert ? 18.0f : 4.2f, desc.Seed + 11, -1e9f, w.SnowLine, 38.0f,
				[&](float x, float z) { return Hash2(x, z, acceptSalt) < density(x, z) && (!terrain.GetRiver() || terrain.DistanceToRiver(x, z) > terrain.GetRiver()->HalfWidth * 2.2f); });
			if (static_cast<int>(spots.size()) > desc.TreeBudget) spots.resize(static_cast<size_t>(desc.TreeBudget));
			const uint32_t palmVariantSalt = salt(3u), speciesSalt = salt(13u), coniferSalt = salt(17u), broadleafSalt = salt(19u);
			for (TreePlacement& pl : spots)
			{
				if (desert) { pl.Variant = static_cast<int>(Hash2(pl.Position.x, pl.Position.z, palmVariantSalt) * 2.99f); w.Trees.push_back(pl); continue; }
				const float alt = glm::clamp((pl.Position.y - minH) / std::max(maxH - minH, 1.0f), 0.0f, 1.0f);
				const float r = Hash2(pl.Position.x, pl.Position.z, speciesSalt);
				const float conifer = glm::clamp(0.15f + 1.6f * alt + (desc.Type == WorldType::Alpine ? 0.5f : 0.0f), 0.0f, 0.97f);
				const Biome b = w.BiomeAt(pl.Position.x, pl.Position.z);
				if (b == Biome::Rock && r < 0.15f) pl.Variant = 6;
				else if (r < conifer) pl.Variant = 3 + static_cast<int>(Hash2(pl.Position.x, pl.Position.z, coniferSalt) * 2.99f);
				else pl.Variant = static_cast<int>(Hash2(pl.Position.x, pl.Position.z, broadleafSalt) * 2.99f);
				w.Trees.push_back(pl);
			}
		}

		// ---- rocks: mostly on rocky ground and by the water
		{
			const float sizes[] = { 0.55f, 0.9f, 1.3f, 1.8f, 2.5f, 0.4f };
			for (uint32_t i = 0; i < 6; ++i)
			{
				RockParams rp;
				rp.Seed = desc.Seed * 31u + i + 1u;
				rp.Size = sizes[i];
				rp.Stretch = { 1.0f + 0.15f * static_cast<float>(i % 3), 0.65f + 0.1f * static_cast<float>(i % 4), 1.0f + 0.1f * static_cast<float>((i + 1) % 3) };
				rp.Roughness = 0.3f + 0.03f * static_cast<float>(i);
				rp.Facets = 0.5f + 0.08f * static_cast<float>(i);
				if (desert) { rp.Sand = 0.8f; rp.Color = { 0.55f, 0.40f, 0.28f }; }
				else if (desc.Type == WorldType::Alpine) { rp.Moss = 0.12f; rp.Color = { 0.36f, 0.37f, 0.40f }; }
				else { rp.Moss = 0.75f; rp.Color = { 0.40f, 0.39f, 0.36f }; }
				w.RockRecipes.push_back(rp);
			}
			const GeneratedWorld* self = &w;
			const uint32_t desertRockSalt = salt(41u), rockSalt = salt(43u);
			auto spots = ScatterTrees(terrain, desc.RockBudget * 3, 6, 5.0f, desc.Seed + 21, -1e9f, 1e9f, 55.0f,
				[&](float x, float z)
				{
					if (desert) return Hash2(x, z, desertRockSalt) < 0.05f;
					const Biome b = self->BiomeAt(x, z);
					if (b == Biome::Water) return false;
					const float p = b == Biome::Rock ? 0.5f : b == Biome::Shore ? 0.35f : b == Biome::Snow ? 0.25f : 0.05f;
					return Hash2(x, z, rockSalt) < p;
				});
			if (static_cast<int>(spots.size()) > desc.RockBudget) spots.resize(static_cast<size_t>(desc.RockBudget));
			for (const TreePlacement& pl : spots)
				w.Rocks.push_back({ pl.Variant, pl.Position, pl.Yaw, pl.Scale });
		}
		if (v1) w.Maps = BakeBiomes(w);   // generator 1 placed with the measured biome; from here on the queries read the map
		return w;
	}
}
