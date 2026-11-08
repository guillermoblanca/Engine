// The ground of the world generators 1 to 3 (GB/World/WorldGenVersion.h), kept apart and unchanged since generator 4 builds its worlds from a
// terrain graph (docs/GRAFO_TERRENO.md, section 12): a saved world of an older generator rebuilds its baseline from this code, bit for bit.
#include "GB/World/WorldGenInternal.h"
#include "GB/Log.h"
#include "GB/Water/WaterPlanner.h"
#include "GB/Water/WaterPresets.h"
#include "GB/World/Erosion.h"
#include <algorithm>
#include <functional>
#include <queue>

namespace GB::WorldGenDetail
{
	namespace
	{
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

	void LegacyTerrain(const WorldDesc& desc, GeneratedWorld& w)
	{
		const bool desert = desc.Type == WorldType::Desert;
		if (desert)
		{
			DuneParams dp;
			dp.Seed = desc.Seed;
			dp.DetailThreshold = desc.MeshDetail;
			w.TerrainPtr = Terrain::CreateDunes(dp, desc.Size, desc.Resolution, TerrainStyle::Desert);
		}
		else
		{
			HillParams hp;
			hp.Seed = desc.Seed;
			hp.Height = desc.Type == WorldType::Alpine ? 58.0f : 16.0f;
			hp.Scale = desc.Type == WorldType::Alpine ? 190.0f : 140.0f;
			hp.Flatten = desc.Type == WorldType::Alpine ? 0.05f : 0.25f;
			hp.DetailThreshold = desc.MeshDetail;
			const TerrainStyle style = desc.Type == WorldType::Alpine ? TerrainStyle::Snow : TerrainStyle::Meadow;

			// Pass 1: the bare terrain, to find the basin for the lake and the way down for the river.
			hp.SurfaceMaps = false;
			auto bare = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style);
			// Generators 1 and 2 find the water below with code of their own; generator 3 asks a terrain graph (GB/Water/WaterPlanner.h) to analyse the
			// bare ground: where the broadest basin is, where the rain goes, which flow line is the river. If the graph fails (it should not) the world
			// is still built, with the old method.
			const bool organic = desc.GeneratorVersion >= kWorldGeneratorV3;
			const WaterGenParams waterGen = DefaultWaterGen(desc.Type == WorldType::Alpine, organic);
			bool planned = false;
			if (organic && (desc.Lake || desc.River))
			{
				WaterPlanRequest request;
				request.Lake = desc.Lake;
				request.River = desc.River;
				request.Gen = waterGen;
				request.Seed = desc.Seed;
				WaterPlan plan;
				std::string why;
				if (PlanWater(MakeDefaultTerrainNodeRegistry(), bare->GetHeightmap(), request, plan, why))
				{
					ApplyWaterPlan(plan, waterGen, hp);
					planned = true;
				}
				else GB_CORE_WARN("the water graph failed ({}); using the coarse-grid river", why);
			}
			if (!planned)
			{
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
					hp.LakeRadius = waterGen.LakeRadius;
					hp.LakeDepth = waterGen.LakeDepth;
					hp.WaterLevel = lowest + waterGen.LakeLevelAboveBasin;
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
					hp.RiverWidth = waterGen.RiverWidth;
				}
			}
			hp.SurfaceMaps = true;
			w.Hills = hp;
			w.TerrainPtr = Terrain::CreateHills(hp, desc.Size, desc.Resolution, style);
			// Hueco #1 of docs/GRAFO_TERRENO.md: the generator used to never call HydraulicErosion, even though it is implemented and tested
			// (tests/world_test.cpp) -- only Sandbox's interactive rain used it, on a terrain already built. Carved here, right after the lake and
			// river and before SnowLine/biomes/spawn/trees/rocks read the terrain, so every one of them sees the eroded ground, same as a player who
			// lets it rain before building. The terrain above has no mesh nor normal map yet (Terrain::PrepareRenderData), so there is nothing stale
			// to invalidate: PrepareRenderData reads these heights afterwards, whenever it is called.
			if (desc.Erosion)
			{
				Heightmap& hm = w.TerrainPtr->GetEditableHeightmap();
				// The lowest point of the map is, by construction, the lake basin (it is where the basin was dug from in the first place,
				// tests/sweep_test.cpp checks it stays a metre under WaterLevel): with droplets starting anywhere, water keeps running downhill
				// into it and a full-map pass would silt it back up. A copy of the pre-erosion shore and basin, restored after, keeps the lake
				// exactly as CreateHills sculpted it while letting erosion carve freely everywhere else (the river bed could in principle see the
				// same siltation; no test catches it today, so it is not guarded here -- the same restore-after-erosion technique would apply).
				const Heightmap before = hm;
				ErosionParams ep;
				// One droplet per ~6 texels keeps the density about the same regardless of WorldDesc::Resolution (tests use much smaller worlds
				// than the default); the Strength/BrushRadius/etc. defaults of ErosionParams are already tuned for this engine's scale.
				ep.Droplets = std::clamp(static_cast<int>(static_cast<double>(desc.Resolution) * static_cast<double>(desc.Resolution) / 6.0), 1500, 90000);
				ep.Seed = desc.Seed + 97u;
				HydraulicErosion::Run(hm, ep);
				if (hp.LakeRadius > 0.0f)
					for (uint32_t j = 0; j < hm.GetResolution(); ++j)
						for (uint32_t i = 0; i < hm.GetResolution(); ++i)
							if (MakeLakeOutline(hp.Seed, hp.LakeIrregularity).EffectiveDistance(hm.SamplePosition(i, j), hp.LakeCenter) < hp.LakeRadius)
								hm.At(i, j) = before.At(i, j);
				// Generator 3 keeps the river's channel too: the droplets run down the carved valley and silt up a narrow, shallow bed (the headwaters are
				// two metres wide), so the corridor of the river -- its channel and banks, out to six half-widths -- is given back as CreateHills carved it,
				// fading into the eroded ground at its edge. Generators 1 and 2 never did, and their worlds must not change.
				if (organic && w.TerrainPtr->GetRiver())
				{
					const WaterField& water = w.TerrainPtr->Water();
					for (uint32_t j = 0; j < hm.GetResolution(); ++j)
						for (uint32_t i = 0; i < hm.GetResolution(); ++i)
						{
							const glm::vec2 pos = hm.SamplePosition(i, j);
							const RiverSample s = water.NearestRiver(pos.x, pos.y);
							const float reach = s.HalfWidth * 6.0f;
							if (!s.Found || s.Distance >= reach) continue;
							const float keep = 1.0f - glm::smoothstep(reach * 0.5f, reach, s.Distance);
							hm.At(i, j) = glm::mix(hm.At(i, j), before.At(i, j), keep);
						}
				}
			}
			if (desc.Type == WorldType::Alpine)
				w.SnowLine = w.TerrainPtr->GetHeightmap().GetMinHeight() + 0.68f * (w.TerrainPtr->GetHeightmap().GetMaxHeight() - w.TerrainPtr->GetHeightmap().GetMinHeight());
		}
	}
}
