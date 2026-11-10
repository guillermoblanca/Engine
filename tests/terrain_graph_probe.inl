// TERR0 (documentation/02_tecnico/motor/TERRENO_V5.md): a measuring probe of one world, not a check. Included by terrain_graph_cache_test.cpp.
//   GBSimTests terrain_graph_cache probe <preset> <type> <seed> <size> <resolution> [out_dir]
// <preset> is a graph of TerrainGraphPresetNames(), or "gen4" for the world generator 4 makes by itself for <type> (what `generate` builds).
// It prints the time of every node, of the whole graph and of the rest of the world (GenerateData), measures the drainage of the final ground
// and how the water sits on it. With [out_dir] it writes <out_dir>/<tag>_{height,water,flow}.f32 (float32, N*N, row major) and <tag>_info.json,
// which tools/terrain_probe_png.py turns into maps.
#include "GB/World/FlowAccumulation.h"
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

	int Probe(int argc, char** argv)
	{
		if (argc < 7) { std::fprintf(stderr, "probe <preset|gen4> <temperate|alpine|desert> <seed> <size> <resolution> [out_dir]\n"); return 2; }
		const std::string preset = argv[2], type = argv[3];
		WorldDesc desc;
		desc.Type = type == "alpine" ? WorldType::Alpine : type == "desert" ? WorldType::Desert : WorldType::Temperate;
		desc.Seed = static_cast<uint32_t>(std::atoi(argv[4]));
		desc.Size = static_cast<float>(std::atof(argv[5]));
		desc.Resolution = static_cast<uint32_t>(std::atoi(argv[6]));
		desc.GeneratorVersion = kWorldGeneratorLatest;
		if (preset != "gen4")
		{
			TerrainGraph g;
			if (!MakeTerrainGraphPreset(preset, g)) { std::fprintf(stderr, "unknown preset %s\n", preset.c_str()); return 2; }
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
		}
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		const std::string outDir = argc > 7 ? argv[7] : "";
		const std::string tag = preset + "_" + type + "_" + std::to_string(desc.Seed) + "_" + std::to_string(desc.Resolution);

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

		// the water raster: 1 lake (inside its outline), 2 river (inside the drawn ribbon, 1.5 half-widths: WaterSurface's widthScale), 0 dry
		std::vector<float> wet(count, 0.0f), riverRel(count, 1e9f), riverLevel(count, -1e9f), riverDist(count, 1e9f);
		for (uint32_t j = 0; j < n; ++j)
			for (uint32_t i = 0; i < n; ++i)
				if (lake && water->Lake().Outline.EffectiveDistance(h.SamplePosition(i, j), water->Lake().Center) <= water->Lake().Radius) wet[static_cast<size_t>(j) * n + i] = 1.0f;
		if (river)
		{
			// the nearest segment of every cell within 3.2 of the widest half-widths (the segments in order, the first of equals: as NearestRiver)
			float widest = river->HalfWidth;
			for (const float w : river->HalfWidths) widest = std::max(widest, w);
			const float reach = 3.2f * widest;
			for (size_t k = 0; k + 1 < river->Points.size(); ++k)
			{
				const glm::vec2 a(river->Points[k].x, river->Points[k].z), b(river->Points[k + 1].x, river->Points[k + 1].z);
				const glm::vec2 lo = (glm::min(a, b) - glm::vec2(reach) - h.Min()) / sp, hi = (glm::max(a, b) + glm::vec2(reach) - h.Min()) / sp;
				const int i0 = std::max(0, static_cast<int>(std::floor(lo.x))), i1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil(hi.x)));
				const int j0 = std::max(0, static_cast<int>(std::floor(lo.y))), j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil(hi.y)));
				for (int j = j0; j <= j1; ++j)
					for (int i = i0; i <= i1; ++i)
					{
						const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
						const glm::vec2 p = h.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
						const RiverSample s = ProbeRiverSegment(*river, k, p.x, p.y);
						if (s.Distance < riverDist[c] && s.HalfWidth > 0.0f) { riverDist[c] = s.Distance; riverRel[c] = s.Distance / s.HalfWidth; riverLevel[c] = s.Level; }
					}
			}
			for (size_t c = 0; c < count; ++c) if (riverRel[c] <= 1.5f && wet[c] == 0.0f) wet[c] = 2.0f;
		}

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
				biggestBasin = std::max(biggestBasin, volume);
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
		std::printf("  hollows: filling the final ground raises %.2f %% of the cells by > 0.05 m and %.3f %% by > 0.5 m; the biggest closed basin holds %.0f m3\n",
			Pct(fill5, count), Pct(fill50, count), biggestBasin);
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
