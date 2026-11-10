// The material layers and the trees of the terrain of TERRENO_V5 (GRAFO_TERRENO.md, "Capas de material y árboles del terreno nuevo"): a
// measuring probe, not a check. Included by terrain_graph_cache_test.cpp after terrain_graph_probe.inl (it reuses its helpers).
//   GBSimTests terrain_graph_cache layers <graph.json|preset> <type> <seed> <size> <resolution> [out_dir]
// Evaluates the graph, builds the world from it (GenerateData) and prints:
//   - the time of the layer and tree nodes (layer_*, biome_water, tree_species);
//   - for the published splat_* layers: the largest |sum - 1| and the lowest weight over all samples, and the cover of each (mean weight, and
//     the share of samples where it is the largest layer);
//   - where the new layers are: gravel and mud measured from the water and the channels, cliff and scree by slope;
//   - the trees of the finished world: how many, by species and style and by height band, and how many stand in the water, on a cliff,
//     scree or a path (at the tree's point and at the samples around it), or on a Water or Shore biome.
// With [out_dir] it writes <tag>_layer_<name>.f32 (float32, N*N, row major), <tag>_height.f32, <tag>_wet.f32 and <tag>_trees.f32 (x, z,
// species index per tree) and <tag>_layers.json, for a script that draws the maps.
#include "GB/World/Graph/Nodes/FieldMath.h"
#include "GB/World/Graph/Nodes/SurfaceLayerNodes.h"
#include "GB/World/Trees.h"
#include <map>

namespace
{
	// 8-neighbour chamfer distance (1, sqrt 2) in metres to the seed samples: within 8 % of the Euclidean one, enough for a probe
	std::vector<float> ChamferDistance(const std::vector<uint8_t>& seeds, uint32_t n, float spacing)
	{
		const float far = 1e9f, d1 = spacing, d2 = spacing * 1.41421356f;
		std::vector<float> d(seeds.size(), far);
		for (size_t c = 0; c < seeds.size(); ++c) if (seeds[c]) d[c] = 0.0f;
		const int N = static_cast<int>(n);
		auto at = [&](int i, int j) -> float& { return d[static_cast<size_t>(j) * n + static_cast<size_t>(i)]; };
		for (int j = 0; j < N; ++j)
			for (int i = 0; i < N; ++i)
			{
				float& v = at(i, j);
				if (i > 0) v = std::min(v, at(i - 1, j) + d1);
				if (j > 0) v = std::min(v, at(i, j - 1) + d1);
				if (i > 0 && j > 0) v = std::min(v, at(i - 1, j - 1) + d2);
				if (i + 1 < N && j > 0) v = std::min(v, at(i + 1, j - 1) + d2);
			}
		for (int j = N - 1; j >= 0; --j)
			for (int i = N - 1; i >= 0; --i)
			{
				float& v = at(i, j);
				if (i + 1 < N) v = std::min(v, at(i + 1, j) + d1);
				if (j + 1 < N) v = std::min(v, at(i, j + 1) + d1);
				if (i + 1 < N && j + 1 < N) v = std::min(v, at(i + 1, j + 1) + d2);
				if (i > 0 && j + 1 < N) v = std::min(v, at(i - 1, j + 1) + d2);
			}
		return d;
	}

	const TgNode* NodeOfType(const TerrainGraph& g, const char* type)
	{
		for (const TgNode& n : g.Nodes) if (n.Type == type) return &n;
		return nullptr;
	}

	// the field that feeds `port` of `to`, from the evaluation's outputs of every node
	const TgField* FieldInto(const TerrainGraph& g, const TgResult& r, const std::string& to, const char* port)
	{
		for (const TgEdge& e : g.Edges)
			if (e.To == to && e.Port == port)
			{
				const auto node = r.Nodes.find(e.From);
				if (node == r.Nodes.end()) return nullptr;
				const auto value = node->second.find(e.FromPort);
				return value != node->second.end() && value->second.Kind == TgPortKind::Field ? &value->second.Field : nullptr;
			}
		return nullptr;
	}

	int Layers(int argc, char** argv)
	{
		if (argc < 7) { std::fprintf(stderr, "layers <graph.json|preset> <temperate|alpine|desert> <seed> <size> <resolution> [out_dir]\n"); return 2; }
		const std::string preset = argv[2], type = argv[3];
		WorldDesc desc = ProbeDesc(type, static_cast<uint32_t>(std::atoi(argv[4])), static_cast<float>(std::atof(argv[5])), static_cast<uint32_t>(std::atoi(argv[6])));
		TerrainGraph g;
		{
			bool own = false;
			std::string why;
			if (!ProbeGraph(preset, desc, g, own, why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 2; }
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
		}
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		desc.TreeBudget = 40000;   // (what `terrain_graph preset ... 40000 900` asks for)
		desc.RockBudget = 900;
		const std::string outDir = argc > 7 ? argv[7] : "";
		std::string label = preset;
		for (char& ch : label) if (ch == ':' || ch == '/' || ch == '\\' || ch == '.') ch = '_';
		if (label.size() > 5 && label.substr(label.size() - 5) == "_json") label = label.substr(0, label.size() - 5);
		if (const size_t cut = label.rfind("terrain_graphs_"); cut != std::string::npos) label = label.substr(cut + 15);
		const std::string tag = label + "_" + type + "_" + std::to_string(desc.Seed) + "_" + std::to_string(desc.Resolution);

		TgResult r;
		std::string error;
		auto t0 = std::chrono::steady_clock::now();
		if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "evaluation failed: %s\n", error.c_str()); return 1; }
		const double graphS = Seconds(t0);
		const TerrainGraphCache::Stats stats = desc.GraphCache->LastStats();
		t0 = std::chrono::steady_clock::now();
		const GeneratedWorld world = WorldGenerator::GenerateData(desc);
		const double worldS = Seconds(t0);
		std::printf("layers %s: graph %.2f s, world adds %.2f s\n", tag.c_str(), graphS, worldS);
		std::printf("  new nodes (ms):");
		double newMs = 0.0;
		for (const TgNode& node : g.Nodes)
			if (node.Type.rfind("layer_", 0) == 0 || node.Type == "biome_water" || node.Type == "tree_species")
			{
				const auto it = stats.RunMs.find(node.Id);
				const double ms = it != stats.RunMs.end() ? it->second : 0.0;
				newMs += ms;
				std::printf(" %s (%s) %.0f,", node.Id.c_str(), node.Type.c_str(), ms);
			}
		std::printf(" total %.0f ms\n", newMs);

		const auto hIt = r.Outputs.find("height");
		if (hIt == r.Outputs.end()) { std::fprintf(stderr, "no height output\n"); return 1; }
		const TgField& h = hIt->second;
		const uint32_t n = h.N;
		const size_t count = h.Values.size();
		const float sp = h.Spacing();

		// the published layers
		const char* slots[] = { "splat_snow", "splat_path", "splat_cliff", "splat_scree", "splat_rock", "splat_gravel", "splat_mud", "splat_mud_dry",
			"splat_grass_wet", "splat_forest", "splat_dry", "splat_sand", "splat_grass" };
		std::vector<std::pair<std::string, const TgField*>> layers;
		for (const char* s : slots)
			if (const auto it = r.Outputs.find(s); it != r.Outputs.end() && it->second.N == n) layers.push_back({ s, &it->second });
		double worstSum = 0.0, lowest = 1e9;
		std::vector<double> mean(layers.size(), 0.0);
		std::vector<size_t> top(layers.size(), 0);
		for (size_t c = 0; c < count; ++c)
		{
			double sum = 0.0;
			size_t best = 0;
			for (size_t k = 0; k < layers.size(); ++k)
			{
				const double w = layers[k].second->Values[c];
				sum += w;
				lowest = std::min(lowest, w);
				mean[k] += w;
				if (w > layers[best].second->Values[c]) best = k;
			}
			worstSum = std::max(worstSum, std::abs(sum - 1.0));
			if (!layers.empty()) ++top[best];
		}
		std::printf("  layers: %zu published; largest |sum - 1| %.3g, lowest weight %.3g\n", layers.size(), worstSum, lowest);
		std::printf("  cover (mean weight %% / largest layer %%):");
		for (size_t k = 0; k < layers.size(); ++k) std::printf(" %s %.2f/%.2f,", layers[k].first.c_str() + 6, 100.0 * mean[k] / static_cast<double>(count), Pct(top[k], count));
		std::printf("\n");

		// the water and the channels
		const auto wIt = r.WaterOutputs.find("water");
		WaterRaster raster;
		if (wIt != r.WaterOutputs.end()) raster = RasterizeWater(wIt->second, n, h.Min(), sp);
		auto wetKind = [&](size_t c) { return raster.Kinds.empty() ? 0 : static_cast<int>(raster.Kinds[c]); };
		std::vector<uint8_t> wetSeeds(count, 0), riverSeeds(count, 0), channelSeeds(count, 0);
		const TgNode* wetNode = NodeOfType(g, "layer_wet");
		const TgField* area = wetNode ? FieldInto(g, r, wetNode->Id, "area") : nullptr;
		const float channelArea = wetNode ? static_cast<float>(TgParam(*wetNode, "ChannelArea", 200000.0)) : 2e5f;
		const float gravelReach = wetNode ? static_cast<float>(TgParam(*wetNode, "GravelReach", 10.0)) : 10.0f;
		const float mudReach = wetNode ? static_cast<float>(TgParam(*wetNode, "MudDryReach", 20.0)) : 20.0f;
		for (size_t c = 0; c < count; ++c)
		{
			wetSeeds[c] = wetKind(c) != 0;
			riverSeeds[c] = wetKind(c) == WaterRaster::River;
			channelSeeds[c] = riverSeeds[c] || (area && area->N == n && area->Values[c] >= channelArea);
		}
		const std::vector<float> toWater = ChamferDistance(wetSeeds, n, sp), toChannel = ChamferDistance(channelSeeds, n, sp), toRiver = ChamferDistance(riverSeeds, n, sp);
		size_t wetCount = 0;
		for (size_t c = 0; c < count; ++c) wetCount += wetSeeds[c];
		auto layerOf = [&](const char* name) -> const TgField* { const auto it = r.Outputs.find(name); return it != r.Outputs.end() && it->second.N == n ? &it->second : nullptr; };
		const TgField* gravel = layerOf("splat_gravel");
		const TgField* mud = layerOf("splat_mud");
		const TgField* mudDry = layerOf("splat_mud_dry");
		const TgField* cliff = layerOf("splat_cliff");
		const TgField* scree = layerOf("splat_scree");
		const TgField* path = layerOf("splat_path");
		std::printf("  water: %.2f %% of the map wet (lakes and river)\n", Pct(wetCount, count));
		if (gravel)
		{
			size_t dry = 0, inWater = 0, nearCh = 0, far = 0;
			for (size_t c = 0; c < count; ++c)
			{
				if (gravel->Values[c] <= 0.05f) continue;
				if (wetSeeds[c]) { ++inWater; continue; }
				++dry;
				if (toChannel[c] <= gravelReach * 1.1f + sp) ++nearCh; else ++far;
			}
			std::printf("  gravel > 0.05: %zu samples in the river bed, %zu dry (%.2f %% of the map): %zu within %.0f m of the river or a channel (>= %.0f m2), %zu farther\n",
				inWater, dry, Pct(dry, count), nearCh, gravelReach, channelArea, far);
		}
		if (mud && mudDry)
		{
			size_t bed = 0, shore = 0, far = 0, shoreDry = 0, farDry = 0;
			for (size_t c = 0; c < count; ++c)
			{
				if (mud->Values[c] > 0.05f) { if (wetSeeds[c]) ++bed; else if (toWater[c] <= mudReach * 1.1f + sp) ++shore; else ++far; }
				if (mudDry->Values[c] > 0.05f) { if (toWater[c] <= mudReach * 1.1f + sp) ++shoreDry; else ++farDry; }
			}
			std::printf("  mud > 0.05: %zu samples on lake beds, %zu on the shore (%.2f %% of the map, within %.0f m of the water), %zu farther; mud_dry > 0.05: %zu on the shore (%.2f %%), %zu farther\n",
				bed, shore, Pct(shore, count), mudReach, far, shoreDry, Pct(shoreDry, count), farDry);
		}
		auto slopeAt = [&](uint32_t i, uint32_t j) { return FieldMath::SlopeDegrees(h, i, j); };
		if (cliff && scree)
		{
			size_t c50 = 0, cliffN = 0, cliffUnder40 = 0, screeN = 0, slope50 = 0;
			double cliffSlope = 0.0, screeSlope = 0.0;
			std::vector<uint8_t> cliffSeeds(count, 0);
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const size_t c = static_cast<size_t>(j) * n + i;
					const float s = slopeAt(i, j);
					slope50 += s > 50.0f;
					if (cliff->Values[c] > 0.5f) { ++cliffN; cliffSlope += s; cliffUnder40 += s < 40.0f; cliffSeeds[c] = 1; }
					if (cliff->Values[c] > 0.5f && s > 50.0f) ++c50;
				}
			const std::vector<float> toCliff = ChamferDistance(cliffSeeds, n, sp);
			size_t screeNear = 0;
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const size_t c = static_cast<size_t>(j) * n + i;
					if (scree->Values[c] <= 0.5f) continue;
					++screeN; screeSlope += slopeAt(i, j);
					screeNear += toCliff[c] <= 70.0f;
				}
			std::printf("  cliff > 0.5: %.2f %% of the map (slope > 50 deg: %.2f %%), mean slope %.1f deg, %zu under 40 deg; scree > 0.5: %.2f %%, mean slope %.1f deg, %.1f %% within 70 m of a cliff\n",
				Pct(cliffN, count), Pct(slope50, count), cliffN ? cliffSlope / static_cast<double>(cliffN) : 0.0, cliffUnder40, Pct(screeN, count),
				screeN ? screeSlope / static_cast<double>(screeN) : 0.0, Pct(screeNear, screeN));
		}

		// the trees of the finished world
		const TgNode* species = NodeOfType(g, "tree_species");
		const float exclude = species ? static_cast<float>(TgParam(*species, "ExcludeAbove", 0.25)) : 0.25f;
		const float lowTop = species ? static_cast<float>(TgParam(*species, "LowTop", 0.22)) : 0.22f, midTop = species ? static_cast<float>(TgParam(*species, "MidTop", 0.34)) : 0.34f;
		const float highTop = species ? static_cast<float>(TgParam(*species, "HighTop", 0.44)) : 0.44f;
		const auto [minIt, maxIt] = std::minmax_element(h.Values.begin(), h.Values.end());
		const float hMin = *minIt, hRange = std::max(*maxIt - *minIt, 1e-3f);
		size_t inWet = 0, wetAround = 0, onCliff = 0, onScree = 0, onPath = 0, onWaterBiome = 0;
		std::map<std::string, size_t> bySpecies, byRiver;
		size_t nearRiver = 0;
		std::map<std::string, std::map<std::string, size_t>> byBand;
		std::vector<float> treeOut;
		for (const TreePlacement& t : world.Trees)
		{
			const float x = t.Position.x, z = t.Position.z;
			const int i0 = static_cast<int>(std::floor((x - h.Min().x) / sp)), j0 = static_cast<int>(std::floor((z - h.Min().y) / sp));
			const int ni = std::clamp(static_cast<int>(std::lround((x - h.Min().x) / sp)), 0, static_cast<int>(n) - 1), nj = std::clamp(static_cast<int>(std::lround((z - h.Min().y) / sp)), 0, static_cast<int>(n) - 1);
			if (!raster.Kinds.empty() && raster.Wet(static_cast<size_t>(nj) * n + static_cast<size_t>(ni))) ++inWet;
			bool around = false;
			for (int j = j0; j <= j0 + 1; ++j)
				for (int i = i0; i <= i0 + 1; ++i)
				{
					const size_t c = static_cast<size_t>(std::clamp(j, 0, static_cast<int>(n) - 1)) * n + static_cast<size_t>(std::clamp(i, 0, static_cast<int>(n) - 1));
					around = around || (!raster.Kinds.empty() && raster.Wet(c));
				}
			wetAround += around;
			onCliff += cliff && cliff->Sample(x, z) > exclude;
			onScree += scree && scree->Sample(x, z) > exclude;
			onPath += path && path->Sample(x, z) > exclude;
			const Biome b = world.BiomeAt(x, z);
			onWaterBiome += b == Biome::Water || b == Biome::Shore;
			const TreeParams& tp = world.TreeRecipes[static_cast<size_t>(t.Variant)];
			const char* sp_ = tp.Species == TreeSpecies::Broadleaf ? "Broadleaf" : tp.Species == TreeSpecies::Conifer ? "Conifer" : tp.Species == TreeSpecies::Palm ? "Palm" : "Bare";
			const std::string name = tp.Species == TreeSpecies::Bare ? std::string(sp_) : std::string(sp_) + "/" + std::to_string(tp.Style);
			++bySpecies[name];
			if (toRiver[static_cast<size_t>(nj) * n + static_cast<size_t>(ni)] <= 70.0f) { ++nearRiver; ++byRiver[name]; }
			const float rel = (h.Sample(x, z) - hMin) / hRange;
			++byBand[rel < lowTop ? "1 low" : rel < midTop ? "2 mid" : rel < highTop ? "3 high" : "4 top"][name];
			treeOut.push_back(x); treeOut.push_back(z); treeOut.push_back(static_cast<float>(t.Variant));
		}
		const size_t placed = r.PlacementOutputs.count("trees") ? r.PlacementOutputs.at("trees").Items.size() : 0;
		std::printf("  trees: %zu in the world (%zu published by the graph, budget %d); in the water %zu (a wet sample around: %zu), on cliff %zu, on scree %zu, on a path %zu, on a Water/Shore biome %zu\n",
			world.Trees.size(), placed, desc.TreeBudget, inWet, wetAround, onCliff, onScree, onPath, onWaterBiome);
		std::printf("  trees by species:");
		for (const auto& [name, k] : bySpecies) std::printf(" %s %zu (%.1f %%),", name.c_str(), k, Pct(k, world.Trees.size()));
		std::printf("\n");
		std::printf("  within 70 m of the river: %zu trees:", nearRiver);
		for (const auto& [name, k] : byRiver) std::printf(" %s %.0f %%,", name.c_str(), Pct(k, nearRiver));
		std::printf("\n");
		for (const auto& [band, kinds] : byBand)
		{
			size_t all = 0;
			for (const auto& entry : kinds) all += entry.second;
			std::printf("  band %s (%zu, %.1f %%):", band.c_str() + 2, all, Pct(all, world.Trees.size()));
			for (const auto& [name, k] : kinds) std::printf(" %s %.0f %%,", name.c_str(), Pct(k, all));
			std::printf("\n");
		}
		std::printf("  band tops (relative %.2f / %.2f / %.2f): %.0f / %.0f / %.0f m\n", lowTop, midTop, highTop, hMin + lowTop * hRange, hMin + midTop * hRange, hMin + highTop * hRange);

		if (!outDir.empty())
		{
			const std::string base = outDir + "/" + tag;
			WriteF32(base + "_height.f32", h.Values);
			std::vector<float> wet(count, 0.0f);
			for (size_t c = 0; c < count; ++c) wet[c] = static_cast<float>(wetKind(c));
			WriteF32(base + "_wet.f32", wet);
			for (const auto& [name, field] : layers) WriteF32(base + "_layer_" + name.substr(6) + ".f32", field->Values);
			WriteF32(base + "_trees.f32", treeOut);
			if (FILE* f = std::fopen((base + "_layers.json").c_str(), "w"))
			{
				std::fprintf(f, "{\"n\": %u, \"size\": %.3f, \"min\": %.3f, \"max\": %.3f, \"trees\": %zu, \"layers\": [", n, h.Size, hMin, hMin + hRange, world.Trees.size());
				for (size_t k = 0; k < layers.size(); ++k) std::fprintf(f, "%s\"%s\"", k ? ", " : "", layers[k].first.c_str() + 6);
				std::fprintf(f, "], \"recipes\": [");
				for (size_t k = 0; k < world.TreeRecipes.size(); ++k)
				{
					const TreeParams& tp = world.TreeRecipes[k];
					std::fprintf(f, "%s[%d, %d]", k ? ", " : "", static_cast<int>(tp.Species), tp.Style);
				}
				std::fprintf(f, "]}\n");
				std::fclose(f);
			}
			std::printf("  wrote %s_*\n", base.c_str());
		}
		return 0;
	}
}
