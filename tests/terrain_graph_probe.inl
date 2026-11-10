// TERR0 (documentation/02_tecnico/motor/TERRENO_V5.md): a measuring probe of one world, not a check. Included by terrain_graph_cache_test.cpp.
//   GBSimTests terrain_graph_cache probe <preset> <type> <seed> <size> <resolution> [out_dir]
// <preset> is a graph of TerrainGraphPresetNames(), "gen4" for the world generator 4 makes by itself for <type> (what `generate` builds), or a
// graph file (*.json). "e1:<preset>" is that graph with the nodes of TERRENO_V5 E1 appended to its final height (AppendE1 below): drainage
// and lake_fill, the scatter kept out of the lakes. "e1x:<preset>" the same with lake_fill making no lake (desert: only the drainage).
//   GBSimTests terrain_graph_cache probe_export <preset> <type> <file.json> <id> [name]
// writes that graph (with its e1: prefix applied) as a terrain graph file.
// It prints the time of every node, of the whole graph and of the rest of the world (GenerateData), measures the drainage of the final ground
// and how the water sits on it. With [out_dir] it writes <out_dir>/<tag>_{height,water,flow}.f32 (float32, N*N, row major) and <tag>_info.json,
// which tools/terrain_probe_png.py turns into maps.
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Water/WaterRaster.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <functional>
#include <queue>
#include <string>
#include <utility>

namespace
{
	struct ProbeJson
	{
		FILE* File = nullptr;
		bool First = true;
		void Num(const char* key, double v) { if (File) std::fprintf(File, "%s\"%s\": %.6g", First ? "" : ",\n  ", key, v); First = false; }
	};

	void WriteF32(const std::string& path, const std::vector<float>& v)
	{
		if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(v.data(), sizeof(float), v.size(), f); std::fclose(f); }
	}

	double Pct(size_t a, size_t b) { return 100.0 * static_cast<double>(a) / static_cast<double>(std::max<size_t>(b, 1)); }

	// TERRENO_V5 E1 on any graph: "e1_drain" (drainage) and "e1_lakes" (lake_fill) on the height its "height" output publishes, with the
	// water it publishes as outlets; everything that read that height reads the filled one, the "water" output is lake_fill's, and the density
	// of every scatter is multiplied by "e1_dry" (0 in a lake and a cell or two around it). The frozen nodes (biome_classify, mask_water) are
	// left alone: they know only the main lake, so the trees and rocks are kept out of the new lakes here.
	bool AppendE1(TerrainGraph& g, bool lakes, std::string& error)
	{
		auto outputNamed = [&](const char* source) -> std::string { for (const TgNode& n : g.Nodes) if (n.Type == "output" && n.Source == source) return n.Id; return ""; };
		const std::string outHeight = outputNamed("height"), outWater = outputNamed("water");
		if (outHeight.empty()) { error = "the graph has no height output"; return false; }
		std::string from, fromPort, water, waterPort;
		for (const TgEdge& e : g.Edges) if (e.To == outHeight) { from = e.From; fromPort = e.FromPort; }
		if (!outWater.empty()) for (const TgEdge& e : g.Edges) if (e.To == outWater) { water = e.From; waterPort = e.FromPort; }
		if (from.empty()) { error = "nothing feeds the height output"; return false; }
		auto node = [&](const std::string& id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
			n.X = 4200.0f + 250.0f * static_cast<float>(g.Nodes.size() % 3); n.Y = 40.0f + 160.0f * static_cast<float>(g.Nodes.size() % 12);
			g.Nodes.push_back(std::move(n));
		};
		for (TgEdge& e : g.Edges)   // everything that read the final height reads the filled one
			if (e.From == from && e.FromPort == fromPort) { e.From = "e1_lakes"; e.FromPort = "height"; }
		for (TgEdge& e : g.Edges)
			if (!outWater.empty() && e.To == outWater) { e.From = "e1_lakes"; e.FromPort = "water"; }
		node("e1_drain", "drainage");
		node("e1_lakes", "lake_fill", lakes ? std::map<std::string, double>{} : std::map<std::string, double>{ { "MaxLakes", 0 } });
		g.Edges.push_back({ from, "e1_drain", "height", fromPort });
		g.Edges.push_back({ from, "e1_lakes", "height", fromPort });
		g.Edges.push_back({ "e1_drain", "e1_lakes", "level", "level" });
		g.Edges.push_back({ "e1_drain", "e1_lakes", "receiver", "receiver" });
		g.Edges.push_back({ "e1_drain", "e1_lakes", "order", "order" });
		if (!water.empty())
		{
			g.Edges.push_back({ water, "e1_drain", "water", waterPort });
			g.Edges.push_back({ water, "e1_lakes", "water", waterPort });
		}
		else
		{
			node("e1_out_water", "output", {}, "water");
			g.Edges.push_back({ "e1_lakes", "e1_out_water", "in", "water" });
		}
		// the scatter out of the lakes: 0 in a lake and within about two cells of it
		node("e1_near", "blur", { { "Radius", 2 }, { "Passes", 1 } });
		g.Edges.push_back({ "e1_lakes", "e1_near", "in", "lakes" });
		node("e1_dry", "remap", { { "InMin", 0 }, { "InMax", 0.02 }, { "OutMin", 1 }, { "OutMax", 0 } });
		g.Edges.push_back({ "e1_near", "e1_dry", "in", "out" });
		std::vector<TgEdge> densities;
		for (const TgEdge& e : g.Edges)
		{
			if (e.Port != "density") continue;
			const auto target = std::find_if(g.Nodes.begin(), g.Nodes.end(), [&](const TgNode& n) { return n.Id == e.To; });
			if (target != g.Nodes.end() && target->Type == "scatter") densities.push_back(e);
		}
		for (const TgEdge& e : densities)
		{
			const std::string id = "e1_keep_" + e.To;
			node(id, "combine", { { "Mode", 2 } });
			for (TgEdge& f : g.Edges) if (f.To == e.To && f.Port == "density") { f.From = id; f.FromPort = "out"; }
			g.Edges.push_back({ e.From, id, "a", e.FromPort });
			g.Edges.push_back({ "e1_dry", id, "b", "out" });
		}
		return true;
	}

	// The graph a probe name stands for (see the top of this file); false (and why) when there is none. `own`: the world must be built from it
	// (anything but plain gen4, which the world builds by itself).
	bool ProbeGraph(std::string name, const WorldDesc& desc, TerrainGraph& g, bool& own, std::string& error)
	{
		bool e1 = false, lakes = true;
		if (name.rfind("e1:", 0) == 0) { e1 = true; name = name.substr(3); }
		else if (name.rfind("e1x:", 0) == 0) { e1 = true; lakes = false; name = name.substr(4); }
		own = e1 || name != "gen4";
		if (name == "gen4") g = WorldGraphFor(desc);
		else if (name.size() > 5 && name.substr(name.size() - 5) == ".json")
		{
			std::ifstream file(name, std::ios::binary);
			std::stringstream text;
			text << file.rdbuf();
			error = TerrainGraphFromJson(text.str(), g);
			if (!error.empty()) return false;
		}
		else if (!MakeTerrainGraphPreset(name, g)) { error = "unknown preset " + name; return false; }
		return !e1 || AppendE1(g, lakes, error);
	}

	WorldDesc ProbeDesc(const std::string& type, uint32_t seed, float size, uint32_t resolution)
	{
		WorldDesc desc;
		desc.Type = type == "alpine" ? WorldType::Alpine : type == "desert" ? WorldType::Desert : WorldType::Temperate;
		desc.Seed = seed;
		desc.Size = size;
		desc.Resolution = resolution;
		desc.GeneratorVersion = kWorldGeneratorLatest;
		return desc;
	}

	int ProbeExport(int argc, char** argv)
	{
		if (argc < 6) { std::fprintf(stderr, "probe_export <preset> <type> <file.json> <id> [name]\n"); return 2; }
		TerrainGraph g;
		bool own = false;
		std::string error;
		if (!ProbeGraph(argv[2], ProbeDesc(argv[3], 5, 360.0f, 641), g, own, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
		g.Id = argv[5];
		if (argc > 6) g.Name = argv[6];
		std::ofstream(argv[4], std::ios::binary) << TerrainGraphToJson(g);
		std::printf("wrote %s (%zu nodes)\n", argv[4], g.Nodes.size());
		return 0;
	}

	int Probe(int argc, char** argv)
	{
		if (argc < 7) { std::fprintf(stderr, "probe <preset|gen4|file.json|e1:...> <temperate|alpine|desert> <seed> <size> <resolution> [out_dir]\n"); return 2; }
		const std::string preset = argv[2], type = argv[3];
		WorldDesc desc = ProbeDesc(type, static_cast<uint32_t>(std::atoi(argv[4])), static_cast<float>(std::atof(argv[5])), static_cast<uint32_t>(std::atoi(argv[6])));
		{
			TerrainGraph g;
			bool own = false;
			std::string why;
			if (!ProbeGraph(preset, desc, g, own, why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 2; }
			if (own) desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
		}
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		const std::string outDir = argc > 7 ? argv[7] : "";
		std::string label = preset;
		for (char& ch : label) if (ch == ':' || ch == '/' || ch == '\\' || ch == '.') ch = '_';
		const std::string tag = label + "_" + type + "_" + std::to_string(desc.Seed) + "_" + std::to_string(desc.Resolution);

		TgResult r;
		std::string error;
		auto t0 = std::chrono::steady_clock::now();
		if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "evaluation failed: %s\n", error.c_str()); return 1; }
		const double graphS = Seconds(t0);
		const TerrainGraphCache::Stats stats = desc.GraphCache->LastStats();
		t0 = std::chrono::steady_clock::now();
		const GeneratedWorld world = WorldGenerator::GenerateData(desc);   // the graph is in the cache: this is what the world adds to it
		const double worldExtraS = Seconds(t0);
		std::printf("probe %s: %zu nodes, graph %.2f s, world adds %.2f s (biome maps, placement)\n", tag.c_str(), stats.RunMs.size(), graphS, worldExtraS);
		std::vector<std::pair<double, std::string>> slow;
		for (const auto& [id, ms] : stats.RunMs) slow.push_back({ ms, id });
		std::sort(slow.rbegin(), slow.rend());
		std::printf("  nodes (ms):");
		for (const auto& [ms, id] : slow) if (ms >= 1.0) std::printf(" %s %.0f,", id.c_str(), ms);
		std::printf(" the rest < 1 ms\n");

		const auto hIt = r.Outputs.find("height");
		if (hIt == r.Outputs.end()) { std::fprintf(stderr, "no height output\n"); return 1; }
		const TgField& h = hIt->second;
		const uint32_t n = h.N;
		const size_t count = h.Values.size();
		const float sp = h.Spacing();
		const auto wIt = r.WaterOutputs.find("water");
		const WaterField* water = wIt != r.WaterOutputs.end() ? &wIt->second : nullptr;
		const RiverData* river = water ? water->River() : nullptr;
		const bool lake = water && water->HasLake();

		// the water raster: 1 lake (inside its outline, or its mask: every lake), 2 river (inside the drawn ribbon, 1.5 half-widths: WaterSurface's
		// widthScale), 0 dry. GB/World/Water/WaterRaster.h: the same rule the drainage node takes its outlets by.
		std::vector<float> wet(count, 0.0f), riverRel(count, 1e9f), riverLevel(count, -1e9f), riverDist(count, 1e9f);
		if (water)
		{
			const WaterRaster raster = RasterizeWater(*water, n, h.Min(), sp);
			for (size_t c = 0; c < count; ++c) wet[c] = static_cast<float>(raster.Kinds[c]);
			riverRel = raster.RiverRelative; riverLevel = raster.RiverLevel; riverDist = raster.RiverDistance;
		}
		const std::vector<float> wetKinds = wet;   // (the map marks the defects in `wet` further down)

		// the drainage of the final ground as a player sees it: D8 on the heights (no fill), each dry cell followed to where its water stops
		const std::vector<int> down = FlowAccumulation::Directions(h.Values.data(), n);
		const FlowAccumulationResult acc = FlowAccumulation::Run(h.Values.data(), n);
		auto border = [n](size_t c) { const size_t i = c % n, j = c / n; return i == 0 || j == 0 || i == n - 1 || j == n - 1; };
		size_t pits = 0, pitLand = 0, land = 0, toBorder = 0, toWater = 0;
		for (size_t c = 0; c < count; ++c)
			if (down[c] < 0 && !border(c) && wet[c] == 0.0f) ++pits;
		for (size_t c = 0; c < count; ++c)
		{
			if (wet[c] != 0.0f) continue;
			++land;
			size_t k = c;
			size_t steps = 0;
			while (wet[k] == 0.0f && down[k] >= 0 && steps++ < count) k = static_cast<size_t>(down[k]);
			if (wet[k] != 0.0f) ++toWater;
			else if (border(k)) ++toBorder;
			else ++pitLand;
		}
		// the channels the ground itself has (a drainage area over 0.5 % of the map) and how many of their cells carry water (or are within 3
		// half-widths of the river)
		const float channelArea = 0.005f * static_cast<float>(count);
		size_t channel = 0, channelWet = 0;
		for (size_t c = 0; c < count; ++c)
			if (acc.Accumulation[c] >= channelArea) { ++channel; if (wet[c] != 0.0f || riverRel[c] <= 3.0f) ++channelWet; }
		size_t s30 = 0, s40 = 0, s50 = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
				s30 += deg > 30.0f; s40 += deg > 40.0f; s50 += deg > 50.0f;
			}
		const size_t interior = static_cast<size_t>(n - 2) * static_cast<size_t>(n - 2);

		// the closed hollows of the final ground: Priority-Flood (as depression_fill, the map's border the only outlet, the water too) and how
		// deep it fills each cell; the pitting of the droplets fills by millimetres, a real basin by metres. The biggest basin: the connected
		// cells filled over 0.05 m, by volume.
		std::vector<float> filled = h.Values;
		{
			std::vector<uint8_t> closed(count, 0);
			using Item = std::pair<float, uint32_t>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
			for (size_t c = 0; c < count; ++c)
				if (border(c) || wet[c] != 0.0f) { closed[c] = 1; open.push({ filled[c], static_cast<uint32_t>(c) }); }
			while (!open.empty())
			{
				const auto [z, cell] = open.top();
				open.pop();
				const int ci = static_cast<int>(cell % n), cj = static_cast<int>(cell / n);
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = ci + di, nj = cj + dj;
						if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
						const size_t m = static_cast<size_t>(nj) * n + static_cast<size_t>(ni);
						if (closed[m]) continue;
						closed[m] = 1;
						if (filled[m] <= z) filled[m] = z + 0.0001f;
						open.push({ filled[m], static_cast<uint32_t>(m) });
					}
			}
		}
		size_t fill5 = 0, fill50 = 0;
		for (size_t c = 0; c < count; ++c) { const float d = filled[c] - h.Values[c]; fill5 += d > 0.05f; fill50 += d > 0.5f; }
		double biggestBasin = 0.0;
		glm::vec2 biggestAt(0.0f);   // (its first cell in the map's order)
		{
			std::vector<uint8_t> seen(count, 0);
			std::vector<size_t> stack;
			for (size_t c = 0; c < count; ++c)
			{
				if (seen[c] || filled[c] - h.Values[c] <= 0.05f) continue;
				double volume = 0.0;
				stack.assign(1, c);
				seen[c] = 1;
				while (!stack.empty())
				{
					const size_t k = stack.back();
					stack.pop_back();
					volume += static_cast<double>(filled[k] - h.Values[k]) * sp * sp;
					const int ki = static_cast<int>(k % n), kj = static_cast<int>(k / n);
					for (int dj = -1; dj <= 1; ++dj)
						for (int di = -1; di <= 1; ++di)
						{
							const int ni = ki + di, nj = kj + dj;
							if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
							const size_t m = static_cast<size_t>(nj) * n + static_cast<size_t>(ni);
							if (!seen[m] && filled[m] - h.Values[m] > 0.05f) { seen[m] = 1; stack.push_back(m); }
						}
				}
				if (volume > biggestBasin) { biggestBasin = volume; biggestAt = h.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n)); }
			}
		}
		// the channel network of the filled ground: cells draining over 0.2 % of the map, their Strahler order and confluences (a channel cell
		// with two or more channel cells draining into it), and the flat valley floors (a channel or its 0.05 % neighbour under 3 degrees)
		const std::vector<int> fdown = FlowAccumulation::Directions(filled.data(), n);
		const FlowAccumulationResult facc = FlowAccumulation::Run(filled.data(), n);
		const float netArea = 0.002f * static_cast<float>(count), floorArea = 0.0005f * static_cast<float>(count);
		std::vector<uint32_t> byHeight(count);
		for (size_t c = 0; c < count; ++c) byHeight[c] = static_cast<uint32_t>(c);
		std::sort(byHeight.begin(), byHeight.end(), [&](uint32_t a, uint32_t b) { return filled[a] != filled[b] ? filled[a] > filled[b] : a < b; });
		std::vector<uint8_t> order(count, 0), topIn(count, 0), topCount(count, 0), ins(count, 0);
		int maxOrder = 0;
		size_t confluences = 0, netCells = 0, floorCells = 0, floorFlat = 0;
		for (uint32_t c : byHeight)
		{
			if (facc.Accumulation[c] < netArea) continue;
			++netCells;
			order[c] = topIn[c] == 0 ? 1 : static_cast<uint8_t>(topIn[c] + (topCount[c] >= 2 ? 1 : 0));
			if (ins[c] >= 2) ++confluences;
			maxOrder = std::max(maxOrder, static_cast<int>(order[c]));
			const int d = fdown[c];
			if (d < 0) continue;
			const size_t dc = static_cast<size_t>(d);
			if (facc.Accumulation[dc] < netArea) continue;
			ins[dc] = static_cast<uint8_t>(std::min(255, ins[dc] + 1));
			if (order[c] > topIn[dc]) { topIn[dc] = order[c]; topCount[dc] = 1; }
			else if (order[c] == topIn[dc]) topCount[dc] = static_cast<uint8_t>(std::min(255, topCount[dc] + 1));
		}
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (facc.Accumulation[c] < floorArea || wet[c] != 0.0f) continue;
				++floorCells;
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				floorFlat += glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz))) < 3.0f;
			}
		size_t s3040 = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
				s3040 += deg > 30.0f && deg <= 40.0f;
			}
		std::printf("  hollows: filling the final ground raises %.2f %% of the cells by > 0.05 m and %.3f %% by > 0.5 m; the biggest closed basin holds %.0f m3 (from %.0f, %.0f)\n",
			Pct(fill5, count), Pct(fill50, count), biggestBasin, biggestAt.x, biggestAt.y);
		std::printf("  network (filled, > 0.2 %% of the map): %zu cells, Strahler order %d, %zu confluences; channel floors (> 0.05 %%) under 3 deg: %.1f %% of %zu; slope 30-40 deg %.1f %%\n",
			netCells, maxOrder, confluences, Pct(floorFlat, floorCells), floorCells, Pct(s3040, interior));

		// the water against the ground: mid-channel ground above the water (the surface is cut), the ribbon's edge over lower ground (it floats),
		// the lake's drawn disc over ground below its level outside its shore (water where no lake was dug)
		// (the map marks them: 3 cut, 4 floating, 5 water outside the dug lake)
		size_t mid = 0, midCut = 0, edge = 0, edgeFloat = 0, disc = 0, discSpill = 0;
		std::vector<float> defect(count, 0.0f);
		for (size_t c = 0; c < count; ++c)
		{
			const float g = h.Values[c];
			if (wet[c] == 2.0f && riverRel[c] <= 0.5f) { ++mid; if (g > riverLevel[c] + 0.05f) { ++midCut; defect[c] = 3.0f; } }
			if (wet[c] == 2.0f && riverRel[c] > 1.35f) { ++edge; if (g < riverLevel[c] - 0.1f) { ++edgeFloat; defect[c] = 4.0f; } }
			if (lake && wet[c] != 1.0f)
			{
				const LakeBody& L = water->Lake();
				const glm::vec2 p = h.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n));
				if (glm::distance(p, L.Center) <= L.Radius * (1.0f + L.Outline.Irregularity) * 1.2f) { ++disc; if (g < L.Level - 0.05f && wet[c] == 0.0f) { ++discSpill; defect[c] = 5.0f; } }
			}
		}
		for (size_t c = 0; c < count; ++c) if (defect[c] > 0.0f) wet[c] = defect[c];   // (for the map; the counts above are done)

		std::printf("  ground: %.0f..%.0f m; slope > 30 deg %.1f %%, > 40 %.1f %%, > 50 %.2f %%; interior pits %.2f per 1000 cells\n",
			*std::min_element(h.Values.begin(), h.Values.end()), *std::max_element(h.Values.begin(), h.Values.end()), Pct(s30, interior), Pct(s40, interior),
			Pct(s50, interior), 10.0 * Pct(pits, interior));
		std::printf("  drainage of the dry land: to the water %.1f %%, off the map %.1f %%, into a pit %.1f %%; channel cells %zu, %.1f %% with water\n",
			Pct(toWater, land), Pct(toBorder, land), Pct(pitLand, land), channel, Pct(channelWet, channel));
		ProbeJson js;
		std::string jsonPath;
		if (!outDir.empty()) { jsonPath = outDir + "/" + tag + "_info.json"; js.File = std::fopen(jsonPath.c_str(), "w"); if (js.File) std::fprintf(js.File, "{\n  "); }
		js.Num("n", n); js.Num("size", h.Size); js.Num("cx", h.Center.x); js.Num("cz", h.Center.y);
		js.Num("min_h", *std::min_element(h.Values.begin(), h.Values.end())); js.Num("max_h", *std::max_element(h.Values.begin(), h.Values.end()));
		js.Num("graph_s", graphS); js.Num("world_extra_s", worldExtraS);
		js.Num("slope30", Pct(s30, interior)); js.Num("slope40", Pct(s40, interior)); js.Num("slope50", Pct(s50, interior));
		js.Num("pits_per_1000", 10.0 * Pct(pits, interior)); js.Num("land_to_pit", Pct(pitLand, land)); js.Num("channel_wet", Pct(channelWet, channel));
		js.Num("fill_005", Pct(fill5, count)); js.Num("fill_05", Pct(fill50, count)); js.Num("biggest_basin_m3", biggestBasin);
		js.Num("strahler", maxOrder); js.Num("confluences", static_cast<double>(confluences)); js.Num("floor_flat", Pct(floorFlat, floorCells)); js.Num("slope3040", Pct(s3040, interior));
		if (lake)
		{
			const LakeBody& L = water->Lake();
			std::printf("  lake: centre (%.0f, %.0f), radius %.0f m, level %.2f; the drawn disc over dry ground below that level: %.1f %% of the disc\n",
				L.Center.x, L.Center.y, L.Radius, L.Level, Pct(discSpill, disc));
			js.Num("lake_x", L.Center.x); js.Num("lake_z", L.Center.y); js.Num("lake_r", L.Radius); js.Num("lake_level", L.Level); js.Num("lake_spill", Pct(discSpill, disc));
		}
		if (river)
		{
			const auto& P = river->Points;
			std::vector<double> arc(P.size(), 0.0);
			for (size_t k = 1; k < P.size(); ++k) arc[k] = arc[k - 1] + glm::distance(glm::vec2(P[k].x, P[k].z), glm::vec2(P[k - 1].x, P[k - 1].z));
			float wMin = 1e9f, wMax = 0.0f;
			double maxDrop10 = 0.0, sumIncision = 0.0;
			size_t b = 0, trench = 0, raised = 0;
			for (size_t k = 0; k < P.size(); ++k)
			{
				const size_t s = std::min(k, P.size() - 2);
				const float hw = river->HalfWidthAt(s, k + 1 < P.size() ? 0.0f : 1.0f);
				wMin = std::min(wMin, hw); wMax = std::max(wMax, hw);
				while (b + 1 < P.size() && arc[b] < arc[k] + 10.0) ++b;
				if (arc[b] - arc[k] >= 9.0) maxDrop10 = std::max(maxDrop10, static_cast<double>(P[k].y - P[b].y));
				// beside the water: the ground 8 half-widths away (how far the channel is sunk: a trench) and at the ribbon's edge (under the water:
				// the channel is raised and the water floats)
				glm::vec2 dir(P[s + 1].x - P[s].x, P[s + 1].z - P[s].z);
				dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
				const glm::vec2 side(-dir.y, dir.x), at(P[k].x, P[k].z);
				const glm::vec2 f1 = at + side * hw * 8.0f, f2 = at - side * hw * 8.0f, r1 = at + side * hw * 1.5f, r2 = at - side * hw * 1.5f;
				const float far = 0.5f * (h.Sample(f1.x, f1.y) + h.Sample(f2.x, f2.y)) - P[k].y;
				const float rim = std::min(h.Sample(r1.x, r1.y), h.Sample(r2.x, r2.y)) - P[k].y;
				sumIncision += far;
				trench += far > 6.0f;
				raised += rim < -0.1f;
			}
			double steepest = 0.0;
			for (size_t k = 1; k < P.size(); ++k) steepest = std::max(steepest, static_cast<double>(P[k - 1].y - P[k].y) / std::max(1e-3, arc[k] - arc[k - 1]));
			const double length = arc.back();
			const double chord = glm::distance(glm::vec2(P.front().x, P.front().z), glm::vec2(P.back().x, P.back().z));
			std::printf("  river: %zu points, %.0f m (chord %.0f, sinuosity %.2f), half-width %.1f..%.1f m, level %.1f -> %.1f m, steepest %.2f m/m, most drop in 10 m %.1f m\n",
				P.size(), length, chord, length / std::max(chord, 1.0), wMin, wMax, P.front().y, P.back().y, steepest, maxDrop10);
			std::printf("        ground 8 half-widths aside: %.1f m over the water on average, > 6 m (a trench) on %.0f %% of the course; ribbon edge over ground below the water on %.1f %%\n",
				sumIncision / static_cast<double>(P.size()), Pct(trench, P.size()), Pct(raised, P.size()));
			std::printf("        cells: mid-channel ground over the water %.2f %% of %zu; ribbon edge over ground 0.1 m below the water %.2f %% of %zu\n",
				Pct(midCut, mid), mid, Pct(edgeFloat, edge), edge);
			js.Num("river_points", static_cast<double>(P.size())); js.Num("river_length", length); js.Num("river_sinuosity", length / std::max(chord, 1.0));
			js.Num("river_hw_min", wMin); js.Num("river_hw_max", wMax); js.Num("river_steepest", steepest); js.Num("river_drop10", maxDrop10);
			js.Num("river_incision", sumIncision / static_cast<double>(P.size())); js.Num("river_trench", Pct(trench, P.size())); js.Num("river_raised", Pct(raised, P.size()));
			js.Num("river_midcut", Pct(midCut, mid)); js.Num("river_edgefloat", Pct(edgeFloat, edge));
			if (js.File)   // every 10th point of the course, for the cameras
			{
				std::fprintf(js.File, ",\n  \"river\": [");
				for (size_t k = 0; k < P.size(); k += 10) std::fprintf(js.File, "%s[%.1f, %.2f, %.1f]", k ? ", " : "", P[k].x, P[k].y, P[k].z);
				std::fprintf(js.File, "]");
			}
		}
		if (water && !water->MoreLakes().empty())
		{
			// the lakes lake_fill added (E1). Each one's surface is drawn as today, a disc of its Radius at its level (MakeLakeSurface): the dry
			// ground below that level inside the disc and outside its mask would show water (the renderer does not clip by the mask yet), and
			// so would another lake lower than it
			const auto& more = water->MoreLakes();
			double area = 0.0, volume = 0.0;
			size_t discAll = 0, spillAll = 0, overAll = 0;
			for (const LakeBody& L : more) { area += L.Area; volume += L.Volume; }
			std::printf("  E1 lakes: %zu, %.0f m2 of water (%.2f %% of the map), %.0f m3\n", more.size(), area, 100.0 * area / (static_cast<double>(h.Size) * h.Size), volume);
			js.Num("e1_lakes", static_cast<double>(more.size())); js.Num("e1_lake_area", area); js.Num("e1_lake_volume", volume);
			for (size_t k = 0; k < more.size(); ++k)
			{
				const LakeBody& L = more[k];
				size_t disc = 0, spill = 0, over = 0;
				const int i0 = std::max(0, static_cast<int>(std::floor((L.Center.x - L.Radius - h.Min().x) / sp))), i1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((L.Center.x + L.Radius - h.Min().x) / sp)));
				const int j0 = std::max(0, static_cast<int>(std::floor((L.Center.y - L.Radius - h.Min().y) / sp))), j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil((L.Center.y + L.Radius - h.Min().y) / sp)));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const glm::vec2 p = h.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
						if (glm::distance(p, L.Center) > L.Radius || L.Covers(p)) continue;
						const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
						++disc;
						if (h.Values[c] >= L.Level - 0.05f) continue;
						if (wetKinds[c] == 0.0f) { ++spill; wet[c] = 5.0f; }   // (orange on the map, as the main lake's)
						else if (wetKinds[c] == 1.0f) ++over;
					}
				discAll += disc; spillAll += spill; overAll += over;
				if (k < 12)
					std::printf("    lake %zu: centre (%.0f, %.0f), level %.2f m, %.0f m2, %.1f m deep, %.0f m3, spills at (%.0f, %.0f); its disc (r %.0f m) over dry ground below the level %.1f %%, over a lower lake %.1f %%\n",
						k, L.Center.x, L.Center.y, L.Level, L.Area, L.MaxDepth, L.Volume, L.Outlet.x, L.Outlet.y, L.Radius, Pct(spill, disc), Pct(over, disc));
			}
			std::printf("    all discs: %zu cells outside the lakes, %.1f %% of them over dry ground below their level, %.1f %% over a lower lake\n", discAll, Pct(spillAll, discAll), Pct(overAll, discAll));
			js.Num("e1_disc_spill", Pct(spillAll, discAll)); js.Num("e1_disc_over", Pct(overAll, discAll));
			if (js.File)
			{
				std::fprintf(js.File, ",\n  \"lakes\": [");
				for (size_t k = 0; k < more.size(); ++k)
					std::fprintf(js.File, "%s[%.1f, %.2f, %.1f, %.1f, %.0f]", k ? ", " : "", more[k].Center.x, more[k].Level, more[k].Center.y, more[k].Radius, more[k].Area);
				std::fprintf(js.File, "]");
			}
		}
		if (const auto e1 = r.Nodes.find("e1_lakes"); e1 != r.Nodes.end() && e1->second.count("raised"))
		{
			// what lake_fill added to the ground outside the lakes (the silt of the filled hollows): the other half of "filling raises 0 %"
			const TgField& lift = e1->second.at("raised").Field;
			size_t l5 = 0, l50 = 0, l200 = 0;
			float most = 0.0f;
			size_t at = 0;
			for (size_t c = 0; c < lift.Values.size(); ++c)
			{
				const float v = lift.Values[c];
				l5 += v > 0.05f; l50 += v > 0.5f; l200 += v > 2.0f;
				if (v > most) { most = v; at = c; }
			}
			// where the most was added, and how far that is from the river's axis (in half-widths) and the main lake's centre
			const glm::vec2 p = h.SamplePosition(static_cast<uint32_t>(at % n), static_cast<uint32_t>(at / n));
			std::printf("  E1 the most raised cell: (%.0f, %.0f), %.1f m from the river's axis (%.1f half-widths), %.0f m from the main lake's centre\n",
				p.x, p.y, riverDist[at], riverRel[at], lake ? static_cast<double>(glm::distance(p, water->Lake().Center)) : -1.0);
			std::printf("  E1 lake_fill raised the ground (outside the lakes) by > 0.05 m on %.2f %% of the cells, > 0.5 m on %.3f %%, > 2 m on %.3f %%; at most %.2f m\n",
				Pct(l5, count), Pct(l50, count), Pct(l200, count), most);
			js.Num("e1_raised_005", Pct(l5, count)); js.Num("e1_raised_05", Pct(l50, count)); js.Num("e1_raised_2", Pct(l200, count)); js.Num("e1_raised_max", most);
		}
		std::printf("  world: %zu trees, %zu rocks\n", world.Trees.size(), world.Rocks.size());
		if (js.File) { std::fprintf(js.File, "\n}\n"); std::fclose(js.File); }
		if (!outDir.empty())
		{
			WriteF32(outDir + "/" + tag + "_height.f32", h.Values);
			WriteF32(outDir + "/" + tag + "_water.f32", wet);
			WriteF32(outDir + "/" + tag + "_flow.f32", acc.Accumulation);
			std::printf("  wrote %s and its fields\n", jsonPath.c_str());
		}
		return 0;
	}
}
