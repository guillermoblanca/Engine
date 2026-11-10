#include "WorldGen.h"
#include "GB/Log.h"
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Graph/Nodes/ScatterNode.h"
#include "GB/World/WorldGenInternal.h"
#include <algorithm>
#include <functional>
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace GB
{
	namespace
	{
		using WorldGenDetail::Hash2;
		using WorldGenDetail::Noise2;
		using WorldGenDetail::Fbm2;

		constexpr float kBiomeCell = 1.0f;   // meters between the samples of the baked biome map (BiomeMap)

		// The salt of a placement hash mixed with the seed of the world (generator 2): without it, accepting or rejecting a tree at a place and
		// choosing its species came out the same in every seed (generator 1 keeps the bare salts).
		uint32_t SeedSalt(uint32_t seed, uint32_t salt)
		{
			uint32_t h = seed * 0x9E3779B1u ^ (salt + 0x7F4A7C15u);
			h ^= h >> 16; h *= 0x85EBCA6Bu;
			h ^= h >> 13; h *= 0xC2B2AE35u;
			return h ^ (h >> 16);
		}

		// A graph's placements ("trees", "rocks") on the ground: the height of each comes from the terrain (the species and the rock variant are
		// chosen below, as for the built-in scatter).
		std::vector<TreePlacement> PlacementsOf(const TgPlacements& placements, const Heightmap& ground)
		{
			std::vector<TreePlacement> spots;
			spots.reserve(placements.Items.size());
			for (const TgPlacement& p : placements.Items)
				spots.push_back({ std::max(p.Variant, 0), { p.Position.x, ground.GetHeight(p.Position.x, p.Position.y), p.Position.y }, p.Yaw, p.Scale });
			return spots;
		}

		// The material layers a graph published ("splat_dry", "splat_forest", "splat_rock", "splat_snow", "splat_grass", "splat_sand"), in that order
		// of painting: the canopy covers the dry meadow, rock both, snow the rock, grass and sand all of them. A field of another size than the
		// terrain is left out.
		std::vector<SurfaceLayer> SurfaceLayersOf(const TgResult& result, const Heightmap& ground)
		{
			std::vector<SurfaceLayer> layers;
			const std::pair<const char*, SurfaceLayerKind> slots[] = { { "splat_dry", SurfaceLayerKind::Dry }, { "splat_forest", SurfaceLayerKind::Forest },
				{ "splat_rock", SurfaceLayerKind::Rock }, { "splat_snow", SurfaceLayerKind::Snow },
				{ "splat_grass", SurfaceLayerKind::Grass }, { "splat_sand", SurfaceLayerKind::Sand } };
			for (const auto& [name, kind] : slots)
			{
				const auto it = result.Outputs.find(name);
				if (it == result.Outputs.end() || it->second.N != ground.GetResolution()) continue;
				layers.push_back({ kind, it->second.Values });
			}
			return layers;
		}

		// What a world of generator 4 asks of its graph (TerrainGraphPresets.h): the WorldDesc's type, water, erosion, budgets and forest.
		WorldGraphOptions WorldGraphOptionsOf(const WorldDesc& desc)
		{
			WorldGraphOptions o;
			o.Kind = desc.Type == WorldType::Alpine ? WorldGraphOptions::Ground::Mountains : desc.Type == WorldType::Desert ? WorldGraphOptions::Ground::Dunes : WorldGraphOptions::Ground::Hills;
			o.Lake = desc.Lake;
			o.River = desc.River;
			o.Erosion = desc.Erosion;
			const WaterGenParams water = DefaultWaterGen(desc.Type == WorldType::Alpine, true);
			o.LakeRadius = water.LakeRadius;
			o.LakeDepth = water.LakeDepth;
			o.LakeLevelAbove = water.LakeLevelAboveBasin;
			o.LakeIrregularity = water.LakeIrregularity;
			o.RiverWidth = water.RiverWidth;
			o.TreeCount = desc.TreeBudget * 3;
			o.RockCount = desc.RockBudget * 3;
			o.ForestMoisture = 0.8f - 0.5f * desc.ForestAmount;
			return o;
		}

		bool UsesInput(const TerrainGraph& graph)
		{
			return std::any_of(graph.Nodes.begin(), graph.Nodes.end(), [](const TgNode& n) { return n.Type == "height_input"; });
		}

		// The graph of a world (its own, or the one MakeWorldGraph makes for it) evaluated over the WorldDesc's footprint, with the built-in ground
		// as the "height" input when the graph reads one. `graph` is set to the graph that ran (`preset` holds generator 4's).
		bool EvaluateGraphOf(const WorldDesc& desc, TerrainGraph& preset, const TerrainGraph*& graph, TgResult& result, std::string& error)
		{
			graph = desc.TerrainGraphDefinition.get();
			if (!graph) { preset = MakeWorldGraph(WorldGraphOptionsOf(desc)); graph = &preset; }
			const bool desert = desc.Type == WorldType::Desert;
			const TerrainStyle style = desert ? TerrainStyle::Desert : desc.Type == WorldType::Alpine ? TerrainStyle::Snow : TerrainStyle::Meadow;

			TgContext context;
			context.Seed = desc.Seed;
			context.Resolution = desc.Resolution;
			context.Size = desc.Size;
			std::map<std::string, TgField> inputs;
			if (UsesInput(*graph))   // the built-in ground of the world's type, for a height_input node (the dunes, or the hills before any water)
			{
				HillParams hp;
				hp.Seed = desc.Seed;
				hp.Height = desc.Type == WorldType::Alpine ? 58.0f : 16.0f;
				hp.Scale = desc.Type == WorldType::Alpine ? 190.0f : 140.0f;
				hp.Flatten = desc.Type == WorldType::Alpine ? 0.05f : 0.25f;
				hp.SurfaceMaps = false;
				DuneParams dp;
				dp.Seed = desc.Seed;
				const auto base = desert ? Terrain::CreateDunes(dp, desc.Size, desc.Resolution, style) : Terrain::CreateHills(hp, desc.Size, desc.Resolution, style);
				const Heightmap& hm = base->GetHeightmap();
				TgField& f = inputs["height"];
				f.Resize(hm.GetResolution(), hm.GetSize(), (hm.GetMin() + hm.GetMax()) * 0.5f);
				for (uint32_t j = 0; j < f.N; ++j)
					for (uint32_t i = 0; i < f.N; ++i) f.At(i, j) = hm.At(i, j);
				context.Inputs = &inputs;
			}
			return EvaluateTerrainGraph(*graph, MakeDefaultTerrainNodeRegistry(), context, result, error, desc.GraphCache.get());
		}

		// The ground of a world built by a terrain graph: the WorldDesc's own graph, or (generator 4) the one MakeWorldGraph makes for it. Fills
		// w.TerrainPtr (the graph's height, its water as it carved it, its material layers), w.Hills (the recipe it was built with) and the snow line,
		// and leaves what else the graph published in `result`. False, with a warning, when the graph fails or publishes no usable height.
		bool GraphTerrain(const WorldDesc& desc, GeneratedWorld& w, TgResult& result)
		{
			TerrainGraph preset;
			const TerrainGraph* graph = nullptr;
			const bool desert = desc.Type == WorldType::Desert;
			const TerrainStyle style = desert ? TerrainStyle::Desert : desc.Type == WorldType::Alpine ? TerrainStyle::Snow : TerrainStyle::Meadow;
			std::string error;
			if (!EvaluateGraphOf(desc, preset, graph, result, error))
			{
				GB_CORE_WARN("terrain graph '{}' failed ({}); the world is built by generator 3", graph->Id, error);
				return false;
			}
			Heightmap ground(desc.Resolution, desc.Size);
			HeightmapTerrainTarget target(ground);
			if (!ApplyTerrainGraph(result, "height", target, error))
			{
				GB_CORE_WARN("terrain graph '{}' has no usable height ({}); the world is built by generator 3", graph->Id, error);
				return false;
			}

			HillParams recipe;   // only the refinement and the surface maps: the graph dug the water
			recipe.Seed = desc.Seed;
			recipe.DetailThreshold = desc.MeshDetail;
			recipe.SurfaceMaps = true;
			const auto water = result.WaterOutputs.find("water");
			w.TerrainPtr = water != result.WaterOutputs.end() ? Terrain::CreateFromHeightmap(ground, water->second, recipe, style)
				: Terrain::CreateFromHeightmap(ground, recipe, style);
			w.TerrainPtr->SetSurfaceLayers(SurfaceLayersOf(result, ground));
			if (desert)   // the wind of the default dunes (DuneParams): the ripples and the blowing sand follow it
			{
				const DuneParams dp;
				w.TerrainPtr->SetWind(glm::radians(dp.WindAngleDegrees), dp.Wavelength);
			}
			if (water != result.WaterOutputs.end() && water->second.HasLake())   // (the recipe as a record of the world's water)
			{
				const LakeBody& lake = water->second.Lake();
				recipe.LakeCenter = lake.Center; recipe.LakeRadius = lake.Radius; recipe.WaterLevel = lake.Level; recipe.LakeIrregularity = lake.Outline.Irregularity;
			}
			w.Hills = recipe;
			if (desc.Type == WorldType::Alpine)
				w.SnowLine = ground.GetMinHeight() + 0.68f * (ground.GetMaxHeight() - ground.GetMinHeight());
			return true;
		}

		// The biome from what is measured at a point. `riverD` and `riverLevel` are the distance to the river axis and the water level at its
		// nearest point (1e9 and -1e9 without a river, or far from it).
		Biome Classify(const GeneratedWorld& w, float x, float z, float riverD, float riverLevel, float riverHalf)
		{
			const Terrain& t = *w.TerrainRef;
			const float h = t.GetHeight(x, z);
			if (t.HasLake() && h < t.GetWaterLevel() - 0.02f && glm::distance(glm::vec2(x, z), t.GetLakeCenter()) < t.GetLakeRadius() * 1.3f) return Biome::Water;
			const RiverData* river = t.GetRiver();
			if (river && riverD <= riverHalf * 1.2f && h < riverLevel) return Biome::Water;
			float nearWater = 1e9f;
			if (t.HasLake()) nearWater = std::min(nearWater, std::max(h - t.GetWaterLevel(), 0.0f) + (glm::distance(glm::vec2(x, z), t.GetLakeCenter()) > t.GetLakeRadius() * 1.8f ? 1e9f : 0.0f));
			if (river && riverD < riverHalf * 1.8f) nearWater = std::min(nearWater, std::max(h - riverLevel, 0.0f));
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

		constexpr float kGrassBlend = 14.0f;   // metres over which the grass cover changes from one biome's to the next

		float BiomeGrass(Biome b)
		{
			switch (b)
			{
			case Biome::Meadow: return 1.0f;
			case Biome::Forest: return 0.55f;
			case Biome::Shore: return 0.25f;
			case Biome::Rock: return 0.12f;
			default: return 0.0f;
			}
		}

		// The biome a terrain graph's "biome" field holds at a point: its nearest sample (biome ids do not interpolate).
		Biome GraphBiome(const TgField& field, float x, float z)
		{
			const float maxIndex = static_cast<float>(field.N - 1);
			const glm::vec2 lo = field.Min();
			const uint32_t i = static_cast<uint32_t>(glm::clamp(std::floor((x - lo.x) / field.Spacing() + 0.5f), 0.0f, maxIndex));
			const uint32_t j = static_cast<uint32_t>(glm::clamp(std::floor((z - lo.y) / field.Spacing() + 0.5f), 0.0f, maxIndex));
			return static_cast<Biome>(std::clamp(static_cast<int>(std::lround(field.At(i, j))), 0, static_cast<int>(Biome::Dunes)));
		}

		// Bakes the biome of every sample and the distance from it to the water: measured (Classify), or read from a terrain graph's biome field.
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
			const bool fromGraph = w.BiomeField.N > 1;
			if (w.Desc.Type == WorldType::Desert && !fromGraph) return m;

			// The river's distance and level, rasterized segment by segment around the river only: a sample takes the first segment that is strictly
			// nearest, like the loop over all the segments of Terrain, and a sample farther than `reach` from every segment does not need it.
			std::vector<float> riverD(count, 1e9f), riverLevel(count, -1e9f), riverHalf(count, 0.0f);   // (riverHalf: the half-width of the river at the nearest point)
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
							const RiverSample s = ProbeRiverSegment(*r, k, p.x, p.y);   // (the same geometry as Terrain's river queries: GB/World/Water)
							const size_t o = static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i);
							if (s.Distance < riverD[o]) { riverD[o] = s.Distance; riverLevel[o] = s.Level; riverHalf[o] = s.HalfWidth; }
						}
				}
			}
			std::vector<float> f(count);
			for (int j = 0; j < m.N; ++j)
				for (int i = 0; i < m.N; ++i)
				{
					const size_t o = static_cast<size_t>(j) * static_cast<size_t>(m.N) + static_cast<size_t>(i);
					const glm::vec2 p = m.SamplePosition(i, j);
					const Biome b = fromGraph ? GraphBiome(w.BiomeField, p.x, p.y) : Classify(w, p.x, p.y, riverD[o], riverLevel[o], riverHalf[o]);
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
			// The grass cover: the density of each biome, blurred over kGrassBlend metres (two box passes = a soft ramp), so the grass thins out across the
			// border between a meadow and a forest, a shore or bare rock instead of stopping at a line.
			{
				m.Grass.resize(count);
				for (size_t o = 0; o < count; ++o) m.Grass[o] = BiomeGrass(static_cast<Biome>(m.Biomes[o]));
				const int r = std::max(1, static_cast<int>(std::lround(kGrassBlend * 0.5f / m.Cell)));
				std::vector<float> tmp(count);
				for (int pass = 0; pass < 2; ++pass)
				{
					for (int j = 0; j < n; ++j)   // along x
					{
						float sum = 0.0f;
						const size_t row = static_cast<size_t>(j) * static_cast<size_t>(n);
						for (int i = -r; i <= r; ++i) sum += m.Grass[row + static_cast<size_t>(std::clamp(i, 0, n - 1))];
						for (int i = 0; i < n; ++i)
						{
							tmp[row + static_cast<size_t>(i)] = sum / static_cast<float>(2 * r + 1);
							sum += m.Grass[row + static_cast<size_t>(std::clamp(i + r + 1, 0, n - 1))] - m.Grass[row + static_cast<size_t>(std::clamp(i - r, 0, n - 1))];
						}
					}
					for (int i = 0; i < n; ++i)   // along z
					{
						float sum = 0.0f;
						for (int j = -r; j <= r; ++j) sum += tmp[static_cast<size_t>(std::clamp(j, 0, n - 1)) * static_cast<size_t>(n) + static_cast<size_t>(i)];
						for (int j = 0; j < n; ++j)
						{
							m.Grass[static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i)] = sum / static_cast<float>(2 * r + 1);
							sum += tmp[static_cast<size_t>(std::clamp(j + r + 1, 0, n - 1)) * static_cast<size_t>(n) + static_cast<size_t>(i)]
								- tmp[static_cast<size_t>(std::clamp(j - r, 0, n - 1)) * static_cast<size_t>(n) + static_cast<size_t>(i)];
						}
					}
				}
			}
			return m;
		}
	}

	// BiomeName moved to GB/World/Biome.cpp (the enum now lives in GB/World/Biome.h, included by WorldGen.h, so a terrain graph node can use it
	// without WorldGen.h's heavier includes); every existing `#include "GB/World/WorldGen.h"` still gets GB::BiomeName unchanged.

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

	float GeneratedWorld::MoistureAt(float x, float z) const { return MoistureField.N > 1 ? MoistureField.Sample(x, z) : Fbm2(x / 60.0f, z / 60.0f, Desc.Seed * 7u + 3u); }

	Biome GeneratedWorld::ComputeBiome(float x, float z) const
	{
		if (BiomeField.N > 1) return GraphBiome(BiomeField, x, z);
		if (Desc.Type == WorldType::Desert) return Biome::Dunes;
		float riverD = 1e9f, riverLevel = -1e9f, riverHalf = 0.0f;
		if (const RiverData* r = TerrainRef->GetRiver())
		{
			const RiverSample s = NearestRiverPoint(*r, x, z);
			riverD = s.Distance;
			riverLevel = s.Level;
			riverHalf = s.HalfWidth;
		}
		return Classify(*this, x, z, riverD, riverLevel, riverHalf);
	}

	Biome GeneratedWorld::BiomeAt(float x, float z) const
	{
		return Maps.Empty() ? ComputeBiome(x, z) : static_cast<Biome>(Maps.Biomes[Maps.Nearest(x, z)]);
	}

	float GeneratedWorld::WaterDistanceAt(float x, float z) const { return Maps.WaterDistanceAt(x, z); }

	float BiomeMap::GrassAt(float x, float z) const
	{
		const float maxIndex = static_cast<float>(N - 1);
		const float fx = glm::clamp((x - Min.x) / Cell, 0.0f, maxIndex), fz = glm::clamp((z - Min.y) / Cell, 0.0f, maxIndex);
		const int i0 = std::min(static_cast<int>(fx), N - 2), j0 = std::min(static_cast<int>(fz), N - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		auto G = [&](int i, int j) { return Grass[static_cast<size_t>(j) * static_cast<size_t>(N) + static_cast<size_t>(i)]; };
		return glm::mix(glm::mix(G(i0, j0), G(i0 + 1, j0), tx), glm::mix(G(i0, j0 + 1), G(i0 + 1, j0 + 1), tx), tz);
	}

	float GeneratedWorld::GrassDensityAt(float x, float z) const
	{
		// the baked cover is a soft blend of the biomes around the point (water stays bare: the blend of water and meadow fades out toward the bank)
		if (!Maps.Empty() && !Maps.Grass.empty() && BiomeAt(x, z) != Biome::Water) return Maps.GrassAt(x, z);
		return BiomeGrass(BiomeAt(x, z));
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
		PrepareRenderData(w);
		return w;
	}

	std::string WorldGraphJson(const WorldDesc& desc)
	{
		return desc.TerrainGraphDefinition ? TerrainGraphToJsonCompact(*desc.TerrainGraphDefinition) : std::string();
	}

	std::string SetWorldGraphJson(WorldDesc& desc, const std::string& json)
	{
		if (json.empty()) { desc.TerrainGraphDefinition.reset(); return {}; }
		TerrainGraph graph;
		if (std::string e = TerrainGraphFromJson(json, graph); !e.empty()) return "the world's terrain graph does not read: " + e;
		const std::vector<TgIssue> issues = ValidateTerrainGraph(graph, MakeDefaultTerrainNodeRegistry());
		if (HasTgErrors(issues)) return "the world's terrain graph '" + graph.Id + "' has errors: " + TgIssuesText(issues);
		desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(std::move(graph));
		return {};
	}

	bool EvaluateWorldGraph(const WorldDesc& desc, TgResult& result, std::string& error)
	{
		TerrainGraph preset;
		const TerrainGraph* graph = nullptr;
		return EvaluateGraphOf(desc, preset, graph, result, error);
	}

	void WorldGenerator::PrepareRenderData(GeneratedWorld& world)
	{
		if (world.TerrainPtr) world.TerrainPtr->PrepareRenderData();
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
			w.Desc.GraphCache.reset();
			return w;
		}
		GeneratedWorld w;
		w.Desc = desc;
		// T84: the world does not keep the memo it was evaluated with (it never changes the world, and it holds hundreds of MB): its owner, the
		// editor's Terrain Graph panel, decides how long it lives.
		w.Desc.GraphCache.reset();
		const bool desert = desc.Type == WorldType::Desert;
		// Generator 1 is kept exactly as it was (a saved world rebuilds its baseline from the seed): fixed salts, and the trees and rocks placed
		// with the biome measured at each point. Generator 2 mixes the seed into the salts and bakes the biome map before placing anything.
		const bool v1 = desc.GeneratorVersion <= kWorldGeneratorV1;
		auto salt = [&](uint32_t s) { return v1 ? s : SeedSalt(desc.Seed, s); };
		TgResult graph;   // what the world's terrain graph published (WorldDesc::TerrainGraphDefinition), when it has one and it ran

		// Generator 4 (and any world given a graph of its own) is a terrain graph; generators 1-3 keep their code (WorldGenLegacy.cpp). A graph that
		// fails leaves a warning and the world of generator 3.
		if (!(desc.TerrainGraphDefinition || desc.GeneratorVersion >= kWorldGeneratorV4) || !GraphTerrain(desc, w, graph))
		{
			graph = TgResult();
			WorldGenDetail::LegacyTerrain(desc, w);
		}

		w.TerrainRef = w.TerrainPtr.get();
		// a graph's biome and moisture replace the measured ones (the baked map, the grass and the placements below read them)
		if (const auto it = graph.Outputs.find("biome"); it != graph.Outputs.end()) w.BiomeField = it->second;
		if (const auto it = graph.Outputs.find("moisture"); it != graph.Outputs.end()) w.MoistureField = it->second;
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
			const auto graphTrees = graph.PlacementOutputs.find("trees");
			auto spots = graphTrees != graph.PlacementOutputs.end() ? PlacementsOf(graphTrees->second, terrain.GetHeightmap()) :
				ScatterTrees(terrain.GetHeightmap(), desc.TreeBudget * 3, static_cast<int>(w.TreeRecipes.size()), desert ? 18.0f : 4.2f, desc.Seed + 11, -1e9f, w.SnowLine, 38.0f,
				[&](float x, float z) { return Hash2(x, z, acceptSalt) < density(x, z) && (!terrain.GetRiver() || terrain.DistanceToRiver(x, z) > terrain.GetRiver()->HalfWidth * 2.2f); });
			if (graphTrees != graph.PlacementOutputs.end())   // (a density read between its samples may let one through: a tree never stands in the water or on a shore)
				spots.erase(std::remove_if(spots.begin(), spots.end(), [&](const TreePlacement& p) { const Biome b = w.BiomeAt(p.Position.x, p.Position.z); return b == Biome::Water || b == Biome::Shore; }), spots.end());
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

		// ---- rocks: mostly on rocky ground and by the water. The recipes are data (assets/data/rocks.json, ROCAS.md): the kit of the world's type,
		// its variants in order (a spot's variant indexes it, and its Size is what the navigation blocks, so the shipped kits keep the six sizes
		// of before), or the rocks a graph's scatter node names by id ("Rock:<recipe>[/<formation>]", ScatterPalette).
		{
			const std::shared_ptr<const RockLibrary> rockLibrary = ShippedRockLibrary();
			const char* kitName = desert ? "desert" : desc.Type == WorldType::Alpine ? "alpine" : "temperate";
			const RockLibrary* kitLibrary = rockLibrary.get();
			const std::vector<RockVariantDef>* kit = kitLibrary->Kit(kitName);
			if (!kit) { GB_CORE_WARN("rocks: {} has no '{}' kit; the built-in one is used", rockLibrary->Source, kitName); kitLibrary = &BuiltinRockLibrary(); kit = kitLibrary->Kit(kitName); }
			for (uint32_t i = 0; i < static_cast<uint32_t>(kit->size()); ++i)
			{
				RockParams rp;
				kitLibrary->Resolve((*kit)[i], rp);   // (the loader checked its formation)
				rp.Seed = desc.Seed * 31u + i + 1u;
				w.RockRecipes.push_back(rp);
			}
			// a graph that names its rocks: the node that feeds its "rocks" output
			std::vector<std::pair<std::string, double>> palette;
			if (const TerrainGraph* def = desc.TerrainGraphDefinition.get())
				for (const TgNode& out : def->Nodes)
					if (out.Type == "output" && out.Source == "rocks")
						for (const TgEdge& e : def->Edges)
							if (e.To == out.Id)
								if (const TgNode* from = def->FindNode(e.From)) palette = ScatterPalette(*from);
			if (!palette.empty())
			{
				std::vector<RockParams> named;
				for (size_t j = 0; j < palette.size(); ++j)
				{
					const std::string& id = palette[j].first;
					const size_t slash = id.find('/');
					RockParams rp;
					if (!rockLibrary->Resolve(id.substr(0, slash), slash == std::string::npos ? std::string() : id.substr(slash + 1), rp))
					{
						GB_CORE_WARN("rocks: the graph names '{}', which {} does not have; variant {} of the kit takes its place", id,
							rockLibrary->Source.empty() ? std::string("the built-in library") : rockLibrary->Source, j % w.RockRecipes.size());
						rp = w.RockRecipes[j % w.RockRecipes.size()];
					}
					rp.Seed = desc.Seed * 31u + 1000u + static_cast<uint32_t>(j);
					named.push_back(rp);
				}
				w.RockRecipes = std::move(named);
			}
			const GeneratedWorld* self = &w;
			const uint32_t desertRockSalt = salt(41u), rockSalt = salt(43u);
			const auto graphRocks = graph.PlacementOutputs.find("rocks");
			auto spots = graphRocks != graph.PlacementOutputs.end() ? PlacementsOf(graphRocks->second, terrain.GetHeightmap()) :
				ScatterTrees(terrain.GetHeightmap(), desc.RockBudget * 3, 6, 5.0f, desc.Seed + 21, -1e9f, 1e9f, 55.0f,
				[&](float x, float z)
				{
					if (desert) return Hash2(x, z, desertRockSalt) < 0.05f;
					const Biome b = self->BiomeAt(x, z);
					if (b == Biome::Water) return false;
					const float p = b == Biome::Rock ? 0.5f : b == Biome::Shore ? 0.35f : b == Biome::Snow ? 0.25f : 0.05f;
					return Hash2(x, z, rockSalt) < p;
				});
			if (graphRocks != graph.PlacementOutputs.end())
				spots.erase(std::remove_if(spots.begin(), spots.end(), [&](const TreePlacement& p) { return w.BiomeAt(p.Position.x, p.Position.z) == Biome::Water; }), spots.end());
			if (static_cast<int>(spots.size()) > desc.RockBudget) spots.resize(static_cast<size_t>(desc.RockBudget));
			for (const TreePlacement& pl : spots)
				w.Rocks.push_back({ pl.Variant % static_cast<int>(w.RockRecipes.size()), pl.Position, pl.Yaw, pl.Scale });
		}
		if (v1) w.Maps = BakeBiomes(w);   // generator 1 placed with the measured biome; from here on the queries read the map
		return w;
	}
}
