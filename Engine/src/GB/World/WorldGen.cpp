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
	}

	float GeneratedWorld::MoistureAt(float x, float z) const { return Fbm2(x / 60.0f, z / 60.0f, Desc.Seed * 7u + 3u); }

	Biome GeneratedWorld::BiomeAt(float x, float z) const
	{
		const Terrain& t = *TerrainRef;
		if (Desc.Type == WorldType::Desert) return Biome::Dunes;
		const float h = t.GetHeight(x, z);
		if (t.HasLake() && h < t.GetWaterLevel() - 0.02f && glm::distance(glm::vec2(x, z), t.GetLakeCenter()) < t.GetLakeRadius() * 1.3f) return Biome::Water;
		if (t.GetRiver() && t.GetRiverLevelAt(x, z) > -1e8f) return Biome::Water;
		float nearWater = 1e9f;
		if (t.HasLake()) nearWater = std::min(nearWater, std::max(h - t.GetWaterLevel(), 0.0f) + (glm::distance(glm::vec2(x, z), t.GetLakeCenter()) > t.GetLakeRadius() * 1.8f ? 1e9f : 0.0f));
		if (t.GetRiver() && t.DistanceToRiver(x, z) < t.GetRiver()->HalfWidth * 1.8f) nearWater = std::min(nearWater, std::max(h - t.GetRiverSurfaceLevel(x, z), 0.0f));
		if (nearWater < 0.7f) return Biome::Shore;
		if (h > SnowLine) return Biome::Snow;
		const glm::vec3 n = t.GetHeightmap().GetNormal(x, z);
		const float slopeDeg = glm::degrees(std::acos(glm::clamp(n.y, -1.0f, 1.0f)));
		const float rockStart = Desc.Type == WorldType::Alpine ? 26.0f : 33.0f;
		if (slopeDeg > rockStart) return Biome::Rock;
		const float m = MoistureAt(x, z);
		return m > 0.8f - 0.5f * Desc.ForestAmount ? Biome::Forest : Biome::Meadow;
	}

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
		GeneratedWorld w;
		w.Desc = desc;
		const bool desert = desc.Type == WorldType::Desert;

		if (desert)
		{
			DuneParams dp;
			dp.Seed = desc.Seed;
			w.TerrainPtr = Terrain::CreateDunes(dp, desc.Size, desc.Resolution, TerrainStyle::Desert);
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
			auto bare = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style);
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
							const float next = c + len * meander + 14.0f * std::max(rise, 0.0f) + 2.0f * std::max(-rise, 0.0f) * 0.0f;
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
			w.TerrainPtr = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style);
			if (desc.Type == WorldType::Alpine)
				w.SnowLine = w.TerrainPtr->GetHeightmap().GetMinHeight() + 0.68f * (w.TerrainPtr->GetHeightmap().GetMaxHeight() - w.TerrainPtr->GetHeightmap().GetMinHeight());
		}

		w.TerrainRef = w.TerrainPtr.get();
		Terrain& terrain = *w.TerrainPtr;

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
			auto density = [self, desert](float x, float z)
			{
				if (desert) return Hash2(x, z, 5u) < 0.04f ? 1.0f : 0.0f;   // sparse palms (the scatter's spacing does the rest)
				switch (self->BiomeAt(x, z))
				{
				case Biome::Forest: return 1.0f;
				case Biome::Meadow: return Hash2(x, z, 9u) < 0.06f ? 1.0f : 0.0f;   // lone trees
				case Biome::Rock: return Hash2(x, z, 11u) < 0.12f ? 1.0f : 0.0f;
				default: return 0.0f;
				}
			};
			auto spots = ScatterTrees(terrain, desc.TreeBudget * 3, static_cast<int>(w.TreeRecipes.size()), desert ? 18.0f : 4.2f, desc.Seed + 11, -1e9f, w.SnowLine, 38.0f,
				[&](float x, float z) { return Hash2(x, z, 31u) < density(x, z) && (!terrain.GetRiver() || terrain.DistanceToRiver(x, z) > terrain.GetRiver()->HalfWidth * 2.2f); });
			if (static_cast<int>(spots.size()) > desc.TreeBudget) spots.resize(static_cast<size_t>(desc.TreeBudget));
			for (TreePlacement& pl : spots)
			{
				if (desert) { pl.Variant = static_cast<int>(Hash2(pl.Position.x, pl.Position.z, 3u) * 2.99f); w.Trees.push_back(pl); continue; }
				const float alt = glm::clamp((pl.Position.y - minH) / std::max(maxH - minH, 1.0f), 0.0f, 1.0f);
				const float r = Hash2(pl.Position.x, pl.Position.z, 13u);
				const float conifer = glm::clamp(0.15f + 1.6f * alt + (desc.Type == WorldType::Alpine ? 0.5f : 0.0f), 0.0f, 0.97f);
				const Biome b = w.BiomeAt(pl.Position.x, pl.Position.z);
				if (b == Biome::Rock && r < 0.15f) pl.Variant = 6;
				else if (r < conifer) pl.Variant = 3 + static_cast<int>(Hash2(pl.Position.x, pl.Position.z, 17u) * 2.99f);
				else pl.Variant = static_cast<int>(Hash2(pl.Position.x, pl.Position.z, 19u) * 2.99f);
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
			auto spots = ScatterTrees(terrain, desc.RockBudget * 3, 6, 5.0f, desc.Seed + 21, -1e9f, 1e9f, 55.0f,
				[&](float x, float z)
				{
					if (desert) return Hash2(x, z, 41u) < 0.05f;
					const Biome b = self->BiomeAt(x, z);
					if (b == Biome::Water) return false;
					const float p = b == Biome::Rock ? 0.5f : b == Biome::Shore ? 0.35f : b == Biome::Snow ? 0.25f : 0.05f;
					return Hash2(x, z, 43u) < p;
				});
			if (static_cast<int>(spots.size()) > desc.RockBudget) spots.resize(static_cast<size_t>(desc.RockBudget));
			for (const TreePlacement& pl : spots)
				w.Rocks.push_back({ pl.Variant, pl.Position, pl.Yaw, pl.Scale });
		}
		return w;
	}
}
