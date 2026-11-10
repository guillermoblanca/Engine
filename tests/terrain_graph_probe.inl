// TERR0 (documentation/02_tecnico/motor/TERRENO_V5.md): a measuring probe of one world, not a check. Included by terrain_graph_cache_test.cpp.
//   GBSimTests terrain_graph_cache probe <preset> <type> <seed> <size> <resolution> [out_dir]
// <preset> is a graph of TerrainGraphPresetNames(), "gen4" for the world generator 4 makes by itself for <type> (what `generate` builds), or a
// graph file (*.json). "e1:<preset>" is that graph with the nodes of TERRENO_V5 E1 appended to its final height (AppendE1 below): drainage
// and lake_fill, the scatter kept out of the lakes. "e1x:<preset>" the same with lake_fill making no lake (desert: only the drainage).
// "settle:<graph>" appends the settlement nodes (AppendSettle; GRAFO_TERRENO, "Asentamientos y sendas") and measures them (ProbeSettle).
// "e2:<preset>" adds TERRENO_V5 E2 too (AppendE2): fluvial_erosion right after the relief, the droplet erosion and the drawn valley out, and
// E1 at the end; "e2v:" keeps the drawn valley, "e2x:" makes no lake, "e2w:" puts it after the water, in place of the droplets. GB_E2_PARAMS="Name=value ..." sets fluvial_erosion's parameters.
// "e4:<preset>" adds TERRENO_V5 E4 (AppendE4): talus right after fluvial_erosion (E1 and E2 appended first unless the graph has an
// "e2_fluvial" already, as the *_e2.json graphs do). GB_E4_PARAMS="Name=value ..." sets talus's parameters.
//   GBSimTests terrain_graph_cache hausdorff <a_river.f32> <b_river.f32>   (the probe writes <tag>_river.f32 with an out_dir)
//   GBSimTests terrain_graph_cache dem <height.f32> <n> <size> [lakes.f32|-] [out_dir] [tag]
// the same measures on a bare height field, a real DEM crop or one of ours resampled (tools/dem_ref.py; see Dem below).
//   GBSimTests terrain_graph_cache probe_export <preset> <type> <file.json> <id> [name]
//   GBSimTests terrain_graph_cache settle_tune <graph> <type> <seed> <size> <resolution> <set> [<set> ...]   (SettleTune)
// writes that graph (with its e1: prefix applied) as a terrain graph file.
// It prints the time of every node, of the whole graph and of the rest of the world (GenerateData), measures the drainage of the final ground
// and how the water sits on it. With [out_dir] it writes <out_dir>/<tag>_{height,water,flow}.f32 (float32, N*N, row major) and <tag>_info.json,
// which tools/terrain_probe_png.py turns into maps.
#include "GB/World/FlowAccumulation.h"
#include "GB/World/StreamPower.h"
#include "GB/World/Talus.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Water/LakeMesh.h"
#include "GB/World/Water/WaterRaster.h"
#include "GB/World/Settle.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <functional>
#include <queue>
#include <set>
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

	// TERRENO_V5 E2 on any graph: "e2_fluvial" (fluvial_erosion) right after the relief, before any water is placed -- the node "ground" when
	// the graph has one (highland_valley, generator 4), else whatever feeds the droplet erosion "rain" (alpine_peaks) -- so the lake site, the
	// river trace and everything after read the incised ground; the droplet erosion "rain" and the thermal "rockfall" are taken out (their consumers read their input), and so
	// is the drawn valley "valley" (valley_carve: its floor mask stays) unless `keepValley`. E1 is appended after it (AppendE1). The parameters of e2_fluvial come
	// from the environment variable GB_E2_PARAMS ("Name=value Name=value ..."), for tuning; the defaults otherwise.
	// (`keep`: the node stays for its other outputs -- valley_carve's floor mask -- and only `outPort`'s consumers are rewired)
	void Bypass(TerrainGraph& g, const std::string& id, const char* inPort, const char* outPort, bool keep = false)
	{
		std::string from, fromPort;
		for (const TgEdge& e : g.Edges) if (e.To == id && e.Port == inPort) { from = e.From; fromPort = e.FromPort; }
		if (from.empty()) return;
		for (TgEdge& e : g.Edges) if (e.From == id && e.FromPort == outPort) { e.From = from; e.FromPort = fromPort; }
		if (keep) return;
		g.Edges.erase(std::remove_if(g.Edges.begin(), g.Edges.end(), [&](const TgEdge& e) { return e.To == id || e.From == id; }), g.Edges.end());
		g.Nodes.erase(std::remove_if(g.Nodes.begin(), g.Nodes.end(), [&](const TgNode& n) { return n.Id == id; }), g.Nodes.end());
	}

	std::map<std::string, double> EnvParams(const char* var)
	{
		std::map<std::string, double> params;
		if (const char* env = std::getenv(var))
		{
			std::istringstream in(env);
			std::string kv;
			while (in >> kv)
			{
				const size_t eq = kv.find('=');
				if (eq != std::string::npos) params[kv.substr(0, eq)] = std::atof(kv.c_str() + eq + 1);
			}
		}
		return params;
	}

	// `afterWater` ("e2w:"): fluvial_erosion goes where the droplet erosion was instead, after the lake and the river are carved, with that water
	// as its base level (what the 360 m worlds of generator 4 want: their lake site and river trace are frozen nodes that pick the lowest point
	// and the longest flow line, and on an incised relief they pick a puddle and a few metres of river).
	bool AppendE2(TerrainGraph& g, bool keepValley, bool afterWater, std::string& error)
	{
		std::string anchor, port;
		if (afterWater)
		{
			for (const TgEdge& e : g.Edges) if (e.To == "rain" && e.Port == "height") { anchor = e.From; port = e.FromPort; }
			if (anchor.empty()) { error = "e2w: no droplet erosion 'rain' to put fluvial_erosion in place of"; return false; }
		}
		else if (g.FindNode("ground"))
		{
			anchor = "ground";
			for (const TgEdge& e : g.Edges) if (e.From == anchor) { port = e.FromPort; break; }
		}
		else
			for (const TgEdge& e : g.Edges) if (e.To == "rain" && e.Port == "height") { anchor = e.From; port = e.FromPort; }
		if (anchor.empty() || port.empty()) { error = "no relief to put fluvial_erosion after (a node 'ground', or what feeds 'rain')"; return false; }
		for (TgEdge& e : g.Edges) if (e.From == anchor && e.FromPort == port) { e.From = "e2_fluvial"; e.FromPort = "height"; }
		TgNode n;
		n.Id = "e2_fluvial"; n.Type = "fluvial_erosion"; n.Label = "e2_fluvial"; n.Params = EnvParams("GB_E2_PARAMS");
		if (const TgNode* a = g.FindNode(anchor)) { n.X = a->X + 120.0f; n.Y = a->Y + 120.0f; }
		g.Nodes.push_back(std::move(n));
		g.Edges.push_back({ anchor, "e2_fluvial", "height", port });
		if (afterWater)   // the water the droplets were kept out of (mask_water "dry" reads it) is base level
			for (const TgEdge& e : g.Edges)
				if (e.To == "dry" && e.Port == "water") { g.Edges.push_back({ e.From, "e2_fluvial", "water", e.FromPort }); break; }
		// the uplift where the graph has a mask of its range: highland_valley's "range_mask" (the sierra), alpine_peaks' "massif"; the rest of the
		// map (the valley floor, the plain) only erodes
		for (const char* mask : { "range_mask", "massif" })
			if (const TgNode* m = g.FindNode(mask))
			{
				g.Edges.push_back({ mask, "e2_fluvial", "uplift", m->Type == "gradient" ? "mask" : "out" });
				break;
			}
		if (g.FindNode("rain")) Bypass(g, "rain", "height", "height");
		// the thermal erosion too: after the incision it pours the steep walls into the channels, damming them into pits (E4 brings a talus that
		// runs with the drainage)
		if (g.FindNode("rockfall")) Bypass(g, "rockfall", "height", "height");
		// and highland_valley's smoothing of the gentle ground ("settled", a blur of 4 cells x 3 passes blended in where the slope is under 20
		// degrees): it smoothed the droplets' pitting, and on the sharp ridges the incision leaves it pulls a ridge-top cell down to the average of
		// 12 cells around (35 m at 1025, 140 m at 257), a pit lake_fill then fills by tens of metres
		if (const TgNode* s = g.FindNode("settled"); s && s->Type == "blend") Bypass(g, "settled", "a", "out");
		if (!keepValley && g.FindNode("valley")) Bypass(g, "valley", "height", "height", true);
		return true;
	}

	// TERRENO_V5 E4 on a graph with E2: "e4_talus" (talus) right after "e2_fluvial", so everything that read the incised ground reads it after
	// the talus. Its parameters come from GB_E4_PARAMS ("Name=value ..."); the defaults otherwise.
	bool AppendE4(TerrainGraph& g, std::string& error)
	{
		const TgNode* e2 = g.FindNode("e2_fluvial");
		if (!e2) { error = "e4: no e2_fluvial to put talus after"; return false; }
		for (TgEdge& e : g.Edges) if (e.From == "e2_fluvial" && e.FromPort == "height") { e.From = "e4_talus"; }
		TgNode n;
		n.Id = "e4_talus"; n.Type = "talus"; n.Label = "e4_talus"; n.Params = EnvParams("GB_E4_PARAMS");
		n.X = e2->X + 120.0f; n.Y = e2->Y + 120.0f;
		g.Nodes.push_back(std::move(n));
		g.Edges.push_back({ "e2_fluvial", "e4_talus", "height", "height" });
		return true;
	}

	//   GBSimTests terrain_graph_cache e4bench <height.f32> <n> <size> [key=value ...] [out=<height.f32>]
	// TERRENO_V5 E4 on a height field (the probe's <tag>_e2height.f32, fluvial_erosion's ground): TalusErode with its own hardness, timed by
	// phase, its volumes, the hollows it makes (the level of the water over the ground grows) and the slopes in and out of the hard rock.
	// keys: soft, hard, scree (degrees), area (ChannelArea, m2), deposit (0/1), cover, scale, seed, edge, strata, strataHard, warp, strataCover
	int E4Bench(int argc, char** argv)
	{
		if (argc < 5) { std::fprintf(stderr, "e4bench <height.f32> <n> <size> [key=value ...]\n"); return 2; }
		const uint32_t n = static_cast<uint32_t>(std::atoi(argv[3]));
		const float size = static_cast<float>(std::atof(argv[4])), sp = size / static_cast<float>(n - 1);
		std::vector<float> h(static_cast<size_t>(n) * n);
		if (FILE* f = std::fopen(argv[2], "rb")) { const size_t got = std::fread(h.data(), sizeof(float), h.size(), f); std::fclose(f); if (got != h.size()) { std::fprintf(stderr, "short file\n"); return 2; } }
		else { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }
		std::map<std::string, std::string> kv;
		for (int a = 5; a < argc; ++a) { const std::string t = argv[a]; const size_t eq = t.find('='); if (eq != std::string::npos) kv[t.substr(0, eq)] = t.substr(eq + 1); }
		auto num = [&](const char* k, double d) { const auto it = kv.find(k); return it == kv.end() ? d : std::atof(it->second.c_str()); };
		TalusHardnessParams hp;
		hp.Cover = static_cast<float>(num("cover", 0.1)); hp.NoiseScale = static_cast<float>(num("scale", 250)); hp.Seed = static_cast<uint32_t>(num("seed", 0));
		hp.Edge = static_cast<float>(num("edge", 0.08)); hp.StrataSpacing = static_cast<float>(num("strata", 0)); hp.StrataHard = static_cast<float>(num("strataHard", 0.35));
		hp.StrataWarp = static_cast<float>(num("warp", 0)); hp.StrataCover = static_cast<float>(num("strataCover", 1)); hp.Steep = static_cast<float>(num("steep", 0));
		auto t0 = std::chrono::steady_clock::now();
		const std::vector<float> hard = TalusHardness(h.data(), n, sp, -0.5f * size, -0.5f * size, hp);
		const double hardMs = Seconds(t0) * 1000.0;
		TalusOptions o;
		o.SoftAngle = static_cast<float>(num("soft", 36)); o.HardAngle = static_cast<float>(num("hard", 70)); o.ScreeAngle = static_cast<float>(num("scree", 33));
		o.ChannelArea = static_cast<float>(num("area", 2000)); o.Deposit = num("deposit", 1) >= 0.5; o.Hardness = hard.data();
		t0 = std::chrono::steady_clock::now();
		const TalusResult r = TalusErode(h.data(), n, sp, o);
		const double ms = Seconds(t0) * 1000.0;
		const TalusStats& st = r.Stats;
		std::printf("e4bench %u^2 (%.2f m): %.0f ms (hardness %.0f; flood %.0f, cut %.0f, fall %.0f, scree %.0f in %d passes, apex factor %.2f)\n", n, sp, ms, hardMs,
			st.FloodMs, st.CutMs, st.FallMs, st.ScreeMs, st.FitPasses, st.ApexFactor);
		std::printf("  cut %.3g m3 on %zu cells; to channels %.3g, lakes %.3g, border %.3g; %zu feet with %.3g m3, scree %.3g m3 on %zu cells; %zu channel cells, %zu lake cells\n",
			st.CutVolume, st.CutCells, st.ChannelVolume, st.LakeVolume, st.BorderVolume, st.Feet, st.FootVolume, st.ScreeVolume, st.ScreeCells, st.ChannelCells, st.LakeCells);
		const DrainageResult before = ComputeDrainage(h.data(), n, sp), after = ComputeDrainage(r.Heights.data(), n, sp);
		size_t grew = 0, grew50 = 0, minima = 0, minimaScree = 0;
		float deepest = 0.0f; size_t at = 0;
		for (size_t c = 0; c < h.size(); ++c)
		{
			const float g = (after.Level[c] - r.Heights[c]) - (before.Level[c] - h[c]);
			grew += g > 0.05f; grew50 += g > 0.5f;
			if (g > deepest) { deepest = g; at = c; }
			const uint32_t i = static_cast<uint32_t>(c % n), j = static_cast<uint32_t>(c / n);
			if (i == 0 || j == 0 || i + 1 == n || j + 1 == n || r.Lake[c]) continue;
			bool lower = false;
			for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) if ((di || dj) && r.Heights[c + static_cast<size_t>(static_cast<int>(n) * dj + di)] < r.Heights[c]) lower = true;
			if (!lower) { ++minima; minimaScree += r.Scree[c] > 0.0f; }
		}
		std::printf("  new hollows > 0.05 m on %.3f %% of the cells, > 0.5 m on %.3f %%, deepest %.2f m at cell (%zu, %zu): cut %.2f, scree %.2f, lake %d; %zu local minima outside the lakes (%zu on scree)\n",
			Pct(grew, h.size()), Pct(grew50, h.size()), deepest, at % n, at / n, r.Cut[at], r.Scree[at], r.Lake[at], minima, minimaScree);
		size_t hc = 0, sc = 0, h50 = 0, h3040 = 0, s40 = 0, s50 = 0, s3040 = 0, all40 = 0, all50 = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (r.Lake[c]) continue;
				const float gx = (r.Heights[c + 1] - r.Heights[c - 1]) / (2.0f * sp), gz = (r.Heights[c + n] - r.Heights[c - n]) / (2.0f * sp);
				const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
				all40 += deg > 40.0f; all50 += deg > 50.0f;
				if (hard[c] >= 0.5f) { ++hc; h50 += deg > 50.0f; h3040 += deg > 30.0f && deg <= 40.0f; }
				else { ++sc; s40 += deg > 40.0f; s50 += deg > 50.0f; s3040 += deg > 30.0f && deg <= 40.0f; }
			}
		std::printf("  slopes: all > 40 %.1f %%, > 50 %.2f %%; hard (%.1f %%) > 50 %.1f %%, 30-40 %.1f %%; soft > 40 %.2f %%, > 50 %.2f %%, 30-40 %.1f %%\n",
			Pct(all40, hc + sc), Pct(all50, hc + sc), Pct(hc, hc + sc), Pct(h50, hc), Pct(h3040, hc), Pct(s40, sc), Pct(s50, sc), Pct(s3040, sc));
		if (kv.count("out")) WriteF32(kv["out"], r.Heights);
		if (kv.count("outHard")) WriteF32(kv["outHard"], hard);
		if (kv.count("outScree")) WriteF32(kv["outScree"], r.Scree);
		return 0;
	}

	// Settlement sites, trails and their levelling (GRAFO_TERRENO, "Asentamientos y sendas") on any graph: "settle_sites" (settlement_sites),
	// "settle_routes" (route_network) and "settle_grade" (path_grade) on the height its "height" output publishes, with its water and, when the
	// graph has one, "forest_ok" as the forest; the height output, the biomes, the scatters, the snow and every field that feeds an output
	// directly read the levelled ground; the scatters are kept off the treads and the platforms; and the sites, trails, fords, cols and the
	// path and platform masks are published ("settlements", "trails", "trail_info", "fords", "cols", "path", "platform").
	bool AppendSettle(TerrainGraph& g, std::string& error)
	{
		auto outputNamed = [&](const char* source) -> std::string { for (const TgNode& n : g.Nodes) if (n.Type == "output" && n.Source == source) return n.Id; return ""; };
		const std::string outHeight = outputNamed("height"), outWater = outputNamed("water");
		if (outHeight.empty() || outWater.empty()) { error = "settle: the graph has no height or no water output"; return false; }
		std::string from, fromPort, water, waterPort;
		for (const TgEdge& e : g.Edges) if (e.To == outHeight) { from = e.From; fromPort = e.FromPort; }
		for (const TgEdge& e : g.Edges) if (e.To == outWater) { water = e.From; waterPort = e.FromPort; }
		if (from.empty() || water.empty()) { error = "settle: nothing feeds the height or the water output"; return false; }
		auto node = [&](const std::string& id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
			n.X = 5200.0f + 250.0f * static_cast<float>(g.Nodes.size() % 3); n.Y = 40.0f + 160.0f * static_cast<float>(g.Nodes.size() % 12);
			g.Nodes.push_back(std::move(n));
		};
		auto typeOf = [&](const std::string& id) -> std::string { const TgNode* n = g.FindNode(id); return n ? n->Type : std::string(); };
		auto feedsOutput = [&](const std::string& id) { for (const TgEdge& e : g.Edges) if (e.From == id && typeOf(e.To) == "output") return true; return false; };
		// (not what the sites read: the forest they weigh is worked out on the ground before the levelling)
		std::set<std::string> before;
		if (g.FindNode("forest_ok")) for (const TgNode& u : TerrainGraphUpstreamOf(g, { "forest_ok" }).Nodes) before.insert(u.Id);
		for (TgEdge& e : g.Edges)   // what shows the ground reads the levelled one
		{
			if (e.From != from || e.FromPort != fromPort) continue;
			if (before.count(e.To)) continue;
			const std::string t = typeOf(e.To);
			if (e.To == outHeight || t == "biome_classify" || t == "scatter" || t == "snow_accumulate" || feedsOutput(e.To)) { e.From = "settle_grade"; e.FromPort = "height"; }
		}
		node("settle_sites", "settlement_sites");
		node("settle_routes", "route_network");
		node("settle_grade", "path_grade");
		g.Edges.push_back({ from, "settle_sites", "height", fromPort });
		g.Edges.push_back({ water, "settle_sites", "water", waterPort });
		if (g.FindNode("forest_ok")) g.Edges.push_back({ "forest_ok", "settle_sites", "forest", "out" });
		g.Edges.push_back({ from, "settle_routes", "height", fromPort });
		g.Edges.push_back({ water, "settle_routes", "water", waterPort });
		g.Edges.push_back({ "settle_sites", "settle_routes", "sites", "sites" });
		g.Edges.push_back({ from, "settle_grade", "height", fromPort });
		g.Edges.push_back({ water, "settle_grade", "water", waterPort });
		g.Edges.push_back({ "settle_sites", "settle_grade", "sites", "sites" });
		g.Edges.push_back({ "settle_routes", "settle_grade", "trails", "trails" });
		g.Edges.push_back({ "settle_routes", "settle_grade", "trail_info", "trail_info" });
		g.Edges.push_back({ "settle_routes", "settle_grade", "fords", "fords" });
		// the scatters off the treads and the platforms (and a cell around them)
		node("settle_worked", "combine", { { "Mode", 4 } });
		g.Edges.push_back({ "settle_grade", "settle_worked", "a", "path" });
		g.Edges.push_back({ "settle_grade", "settle_worked", "b", "platform" });
		node("settle_near", "blur", { { "Radius", 1 }, { "Passes", 1 } });
		g.Edges.push_back({ "settle_worked", "settle_near", "in", "out" });
		node("settle_clear", "remap", { { "InMin", 0 }, { "InMax", 0.05 }, { "OutMin", 1 }, { "OutMax", 0 } });
		g.Edges.push_back({ "settle_near", "settle_clear", "in", "out" });
		std::vector<TgEdge> densities;
		for (const TgEdge& e : g.Edges) if (e.Port == "density" && typeOf(e.To) == "scatter") densities.push_back(e);
		for (const TgEdge& e : densities)
		{
			const std::string id = "settle_keep_" + e.To;
			node(id, "combine", { { "Mode", 2 } });
			for (TgEdge& f : g.Edges) if (f.To == e.To && f.Port == "density") { f.From = id; f.FromPort = "out"; }
			g.Edges.push_back({ e.From, id, "a", e.FromPort });
			g.Edges.push_back({ "settle_clear", id, "b", "out" });
		}
		const std::pair<const char*, const char*> outs[] = { { "settlements", "sites" }, { "trails", "trails" }, { "trail_info", "trail_info" },
			{ "fords", "fords" }, { "path", "path" }, { "platform", "platform" } };
		for (const auto& [source, port] : outs)
		{
			node(std::string("settle_out_") + source, "output", {}, source);
			g.Edges.push_back({ "settle_grade", std::string("settle_out_") + source, "in", port });
		}
		node("settle_out_cols", "output", {}, "cols");
		g.Edges.push_back({ "settle_routes", "settle_out_cols", "in", "cols" });
		return true;
	}

	// The river's course as float32 pairs (x, z), every point: what `hausdorff` compares between two probes.
	void WriteRiver(const std::string& path, const RiverData& river)
	{
		std::vector<float> xz;
		for (const glm::vec3& p : river.Points) { xz.push_back(p.x); xz.push_back(p.z); }
		WriteF32(path, xz);
	}

	std::vector<glm::vec2> ReadRiver(const std::string& path)
	{
		std::vector<glm::vec2> out;
		if (FILE* f = std::fopen(path.c_str(), "rb"))
		{
			float xz[2];
			while (std::fread(xz, sizeof(float), 2, f) == 2) out.push_back({ xz[0], xz[1] });
			std::fclose(f);
		}
		return out;
	}

	// The distance from p to the polyline (segment by segment).
	float DistanceToPolyline(const glm::vec2& p, const std::vector<glm::vec2>& line)
	{
		float best = 1e30f;
		for (size_t k = 0; k + 1 < line.size(); ++k)
		{
			const glm::vec2 a = line[k], ab = line[k + 1] - a;
			const float len2 = glm::dot(ab, ab);
			const float t = len2 > 0.0f ? glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
			best = std::min(best, glm::distance(p, a + ab * t));
		}
		if (line.size() == 1) best = glm::distance(p, line[0]);
		return best;
	}

	//   GBSimTests terrain_graph_cache hausdorff <a_river.f32> <b_river.f32>
	// The Hausdorff distance between two river courses the probe wrote (the 257 preview and the 1025 world): the farthest any point of one is
	// from the other, both ways, and where. Also the 95th percentile and the mean, both ways.
	int Hausdorff(int argc, char** argv)
	{
		if (argc < 4) { std::fprintf(stderr, "hausdorff <a_river.f32> <b_river.f32>\n"); return 2; }
		const std::vector<glm::vec2> a = ReadRiver(argv[2]), b = ReadRiver(argv[3]);
		if (a.size() < 2 || b.size() < 2) { std::fprintf(stderr, "a course is empty\n"); return 1; }
		auto oneWay = [](const std::vector<glm::vec2>& from, const std::vector<glm::vec2>& to, const char* name)
		{
			std::vector<float> d;
			float worst = 0.0f;
			glm::vec2 at(0.0f);
			for (const glm::vec2& p : from)
			{
				const float v = DistanceToPolyline(p, to);
				d.push_back(v);
				if (v > worst) { worst = v; at = p; }
			}
			std::vector<float> sorted = d;
			std::sort(sorted.begin(), sorted.end());
			double mean = 0.0;
			for (const float v : d) mean += v;
			mean /= static_cast<double>(d.size());
			std::printf("  %s: max %.1f m at (%.0f, %.0f), p95 %.1f m, mean %.1f m over %zu points\n", name, worst, at.x, at.y,
				sorted[static_cast<size_t>(0.95 * static_cast<double>(sorted.size() - 1))], mean, d.size());
			return worst;
		};
		const float ab = oneWay(a, b, "a to b"), ba = oneWay(b, a, "b to a");
		std::printf("hausdorff %.1f m\n", std::max(ab, ba));
		return 0;
	}

	// The graph a probe name stands for (see the top of this file); false (and why) when there is none. `own`: the world must be built from it
	// (anything but plain gen4, which the world builds by itself).
	bool ProbeGraph(std::string name, const WorldDesc& desc, TerrainGraph& g, bool& own, std::string& error)
	{
		bool e1 = false, lakes = true, e2 = false, keepValley = false, afterWater = false, e4 = false;
		if (name.rfind("e4:", 0) == 0) { e4 = true; name = name.substr(3); }
		bool e1 = false, lakes = true, e2 = false, keepValley = false, afterWater = false;
		bool settle = false;
		if (name.rfind("settle:", 0) == 0) { settle = true; name = name.substr(7); }
		if (name.rfind("e1:", 0) == 0) { e1 = true; name = name.substr(3); }
		else if (name.rfind("e1x:", 0) == 0) { e1 = true; lakes = false; name = name.substr(4); }
		else if (name.rfind("e2:", 0) == 0) { e1 = e2 = true; name = name.substr(3); }
		else if (name.rfind("e2v:", 0) == 0) { e1 = e2 = keepValley = true; name = name.substr(4); }
		else if (name.rfind("e2x:", 0) == 0) { e1 = e2 = true; lakes = false; name = name.substr(4); }
		else if (name.rfind("e2w:", 0) == 0) { e1 = e2 = afterWater = true; name = name.substr(4); }
		own = e1 || settle || name != "gen4";
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
		if (e4 && !g.FindNode("e2_fluvial")) e1 = e2 = true;
		own = own || e4;
		if (e2 && !AppendE2(g, keepValley, afterWater, error)) return false;
		if (e1 && !AppendE1(g, lakes, error)) return false;
		return !e4 || AppendE4(g, error);
		return !settle || AppendSettle(g, error);
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

	// TERRENO_V5 E2, the cost first: the stream-power erosion run straight on one node's height (the relief before any water: "ground" in
	// highland_valley and generator 4), at the graph's resolution. Prints, every few iterations, the time so far (drainage, solve, diffusion),
	// how deep the channels are cut and the Strahler order of the network of the result.
	//   GBSimTests terrain_graph_cache e2bench <graph> <type> <seed> <size> <resolution> <node> [key=value ...]
	// keys: K (1e-5/yr), U (mm/yr), Kd (m2/yr), Sc (critical slope, degrees; 0 linear), dt (yr), it (iterations), every (drainage every), report (iterations between lines),
	// cascade=1 (StreamPowerErodeCascade with ci/mi/fi iterations instead; coarse=257)
	struct NetworkStats { int Strahler = 0; size_t Confluences = 0, Cells = 0; };
	NetworkStats NetworkOf(const DrainageResult& d)
	{
		NetworkStats s;
		const size_t count = static_cast<size_t>(d.N) * d.N;
		const float net = 0.002f * static_cast<float>(count) * d.Spacing * d.Spacing;
		std::vector<uint8_t> order(count, 0), topIn(count, 0), topCount(count, 0), ins(count, 0);
		for (size_t k = count; k-- > 0;)
		{
			const uint32_t c = d.Order[k];
			if (d.Area[c] < net) continue;
			++s.Cells;
			order[c] = topIn[c] == 0 ? 1 : static_cast<uint8_t>(topIn[c] + (topCount[c] >= 2 ? 1 : 0));
			if (ins[c] >= 2) ++s.Confluences;
			s.Strahler = std::max(s.Strahler, static_cast<int>(order[c]));
			const int32_t r = d.Receiver[c];
			if (r < 0 || d.Area[static_cast<size_t>(r)] < net) continue;
			const size_t rc = static_cast<size_t>(r);
			ins[rc] = static_cast<uint8_t>(std::min(255, ins[rc] + 1));
			if (order[c] > topIn[rc]) { topIn[rc] = order[c]; topCount[rc] = 1; }
			else if (order[c] == topIn[rc]) topCount[rc] = static_cast<uint8_t>(std::min(255, topCount[rc] + 1));
		}
		return s;
	}

	int E2Bench(int argc, char** argv)
	{
		if (argc < 8) { std::fprintf(stderr, "e2bench <graph> <type> <seed> <size> <resolution> <node> [key=value ...]\n"); return 2; }
		WorldDesc desc = ProbeDesc(argv[3], static_cast<uint32_t>(std::atoi(argv[4])), static_cast<float>(std::atof(argv[5])), static_cast<uint32_t>(std::atoi(argv[6])));
		std::map<std::string, double> kv = { { "K", 1.0 }, { "U", 0.0 }, { "Kd", 0.0 }, { "dt", 500.0 }, { "it", 40 }, { "every", 1 }, { "report", 5 },
			{ "cascade", 0 }, { "ci", 60 }, { "mi", 12 }, { "fi", 4 }, { "coarse", 257 }, { "Sc", 0 } };
		for (int a = 8; a < argc; ++a)
		{
			const std::string arg = argv[a];
			const size_t eq = arg.find('=');
			if (eq != std::string::npos) kv[arg.substr(0, eq)] = std::atof(arg.c_str() + eq + 1);
		}
		TerrainGraph g;
		bool own = false;
		std::string error;
		if (!ProbeGraph(argv[2], desc, g, own, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
		desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
		TgResult r;
		if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "evaluation failed: %s\n", error.c_str()); return 1; }
		const auto node = r.Nodes.find(argv[7]);
		if (node == r.Nodes.end() || node->second.empty()) { std::fprintf(stderr, "no node %s\n", argv[7]); return 2; }
		const TgField* f = nullptr;
		for (const auto& [port, value] : node->second) if (value.Kind == TgPortKind::Field && !f) f = &value.Field;
		if (!f) { std::fprintf(stderr, "node %s has no field\n", argv[7]); return 2; }
		const uint32_t n = f->N;
		const float sp = f->Spacing();
		StreamPowerOptions o;
		o.Erodibility = static_cast<float>(kv["K"] * 1e-5);
		o.Uplift = static_cast<float>(kv["U"] * 1e-3);
		o.Diffusion = static_cast<float>(kv["Kd"]);
		o.TimeStep = static_cast<float>(kv["dt"]);
		if (kv["Sc"] > 0.0) o.CriticalSlope = static_cast<float>(std::tan(kv["Sc"] * 3.14159265358979323846 / 180.0));
		std::printf("e2bench %s %s seed %u, %u^2 (%.2f m cells), node %s: K %.2g /yr, U %.2g m/yr, Kd %.3g m2/yr, dt %.0f yr\n", argv[2], argv[3], desc.Seed, n, sp, argv[7],
			o.Erodibility, o.Uplift, o.Diffusion, o.TimeStep);
		const DrainageResult d0 = ComputeDrainage(f->Values.data(), n, sp);
		const NetworkStats s0 = NetworkOf(d0);
		std::printf("  before: Strahler %d, %zu confluences, %zu network cells\n", s0.Strahler, s0.Confluences, s0.Cells);
		auto report = [&](const char* what, const std::vector<float>& h, const StreamPowerStats& st, const DrainageResult& d)
		{
			const NetworkStats s = NetworkOf(d);
			const float net = 0.002f * static_cast<float>(h.size()) * sp * sp;
			double cut = 0.0, maxCut = 0.0;
			size_t channels = 0, cut2 = 0;
			for (size_t c = 0; c < h.size(); ++c)
			{
				const double dz = static_cast<double>(f->Values[c]) - h[c];
				maxCut = std::max(maxCut, dz);
				cut2 += dz > 2.0;
				if (d.Area[c] >= net) { cut += dz; ++channels; }
			}
			std::printf("  %s: %d it, %.0f ms (drainage %.0f in %d, solve %.0f, diffusion %.0f in %d steps); channels cut %.1f m on average, at most %.1f m; %.1f %% of cells cut > 2 m; Strahler %d, %zu confluences; eroded %.3g m3, uplift %.3g m3\n",
				what, st.Iterations, st.DrainageMs + st.SolveMs + st.DiffusionMs, st.DrainageMs, st.Drainages, st.SolveMs, st.DiffusionMs, st.DiffusionSteps,
				channels ? cut / static_cast<double>(channels) : 0.0, maxCut, Pct(cut2, h.size()), s.Strahler, s.Confluences, st.IncisionVolume, st.UpliftVolume);
		};
		if (kv["cascade"] > 0.5)
		{
			StreamPowerCascade cascade;
			cascade.CoarseN = static_cast<uint32_t>(kv["coarse"]);
			cascade.CoarseIterations = static_cast<int>(kv["ci"]);
			cascade.MidIterations = static_cast<int>(kv["mi"]);
			cascade.FineIterations = static_cast<int>(kv["fi"]);
			cascade.FineDrainageEvery = static_cast<int>(kv["every"]);
			const auto t0 = std::chrono::steady_clock::now();
			const StreamPowerCascadeResult res = StreamPowerErodeCascade(f->Values.data(), n, sp, o, cascade);
			const double ms = 1000.0 * Seconds(t0);
			std::printf("  cascade stages:");
			for (uint32_t s : res.Stages) std::printf(" %u", s);
			std::printf(", wall %.0f ms\n", ms);
			const DrainageResult dd = ComputeDrainage(res.Heights.data(), n, sp);
			report("cascade", res.Heights, res.Stats, dd);
			return 0;
		}
		std::vector<float> h = f->Values;
		StreamPowerStats total;
		const int iterations = static_cast<int>(kv["it"]), step = std::max(1, static_cast<int>(kv["report"]));
		o.DrainageEvery = static_cast<int>(kv["every"]);
		for (int done = 0; done < iterations; done += step)
		{
			o.Iterations = std::min(step, iterations - done);
			StreamPowerStats st;
			StreamPowerErode(h, n, sp, o, &st);
			total.Iterations += st.Iterations; total.Drainages += st.Drainages; total.DiffusionSteps += st.DiffusionSteps;
			total.DrainageMs += st.DrainageMs; total.SolveMs += st.SolveMs; total.DiffusionMs += st.DiffusionMs;
			total.IncisionVolume += st.IncisionVolume; total.UpliftVolume += st.UpliftVolume;
			const DrainageResult dd = ComputeDrainage(h.data(), n, sp);
			report("after", h, total, dd);
		}
		return 0;
	}

	// The closed hollows of a ground: Priority-Flood (as depression_fill, the map's border the only outlet, the water too: `wet` non-zero) and
	// how deep it fills each cell; the pitting of the droplets fills by millimetres, a real basin by metres.
	std::vector<float> FillHollows(const std::vector<float>& heights, const std::vector<float>& wet, uint32_t n)
	{
		const size_t count = heights.size();
		auto border = [n](size_t c) { const size_t i = c % n, j = c / n; return i == 0 || j == 0 || i == n - 1 || j == n - 1; };
		std::vector<float> filled = heights;
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
		return filled;
	}

	// The biggest closed basin: the connected cells filled over 0.05 m, by volume (m3); `at` its first cell in the map's order.
	double BiggestBasin(const TgField& h, const std::vector<float>& filled, glm::vec2& at)
	{
		const uint32_t n = h.N;
		const size_t count = h.Values.size();
		const float sp = h.Spacing();
		double biggest = 0.0;
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
			if (volume > biggest) { biggest = volume; at = h.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n)); }
		}
		return biggest;
	}

	// The channel network of a filled ground, the probe's (TERR0): D8 (FlowAccumulation) on it, the cells draining over `netCells` cells, their
	// Strahler order and confluences (a channel cell with two or more channel cells draining into it).
	struct FilledNetworkStats { int Strahler = 0; size_t Confluences = 0, Cells = 0; FlowAccumulationResult Acc; };
	FilledNetworkStats FilledNetwork(const std::vector<float>& filled, uint32_t n, float netCells)
	{
		FilledNetworkStats s;
		const size_t count = filled.size();
		const std::vector<int> fdown = FlowAccumulation::Directions(filled.data(), n);
		s.Acc = FlowAccumulation::Run(filled.data(), n);
		std::vector<uint32_t> byHeight(count);
		for (size_t c = 0; c < count; ++c) byHeight[c] = static_cast<uint32_t>(c);
		std::sort(byHeight.begin(), byHeight.end(), [&](uint32_t a, uint32_t b) { return filled[a] != filled[b] ? filled[a] > filled[b] : a < b; });
		std::vector<uint8_t> order(count, 0), topIn(count, 0), topCount(count, 0), ins(count, 0);
		for (uint32_t c : byHeight)
		{
			if (s.Acc.Accumulation[c] < netCells) continue;
			++s.Cells;
			order[c] = topIn[c] == 0 ? 1 : static_cast<uint8_t>(topIn[c] + (topCount[c] >= 2 ? 1 : 0));
			if (ins[c] >= 2) ++s.Confluences;
			s.Strahler = std::max(s.Strahler, static_cast<int>(order[c]));
			const int d = fdown[c];
			if (d < 0) continue;
			const size_t dc = static_cast<size_t>(d);
			if (s.Acc.Accumulation[dc] < netCells) continue;
			ins[dc] = static_cast<uint8_t>(std::min(255, ins[dc] + 1));
			if (order[c] > topIn[dc]) { topIn[dc] = order[c]; topCount[dc] = 1; }
			else if (order[c] == topIn[dc]) topCount[dc] = static_cast<uint8_t>(std::min(255, topCount[dc] + 1));
		}
		return s;
	}

	double Percentile(std::vector<float> v, double q)
	{
		if (v.empty()) return 0.0;
		std::sort(v.begin(), v.end());
		return v[static_cast<size_t>(q * static_cast<double>(v.size() - 1))];
	}

	//   GBSimTests terrain_graph_cache dem <height.f32> <n> <size> [lakes.f32|-] [out_dir] [tag]
	// TERRENO_V5 "Referencias reales": the same measures as the probe on a bare height field -- a real DEM crop that tools/dem_ref.py made
	// metric, or one of our worlds (the probe's <tag>_height.f32) resampled by it to the DEM's spacing. No graph is evaluated. Lakes: the cells
	// of `lakes.f32` equal to 1 (the probe's water kinds or dem_ref's resample), else the flat surfaces of the DEM itself (a reservoir or a tarn
	// has no bathymetry in a DEM: connected cells whose 4 neighbours are within 5 cm, 2000 m2 or more, as lake_fill's MinArea). Everything is
	// measured on the dry cells. Besides the probe's measures (slopes, pits, fill, the D8 network of the filled ground), on the drainage of E1
	// (ComputeDrainage, the receivers E2 incises along):
	//   - its own network (NetworkOf, what e2bench prints), on the same area threshold, 0.2 % of the map;
	//   - the height above the channel each cell drains into (HAND: Level minus the Level of the first network cell down its flow path) and
	//     how far it is from it in a straight line: "the ground N m from the channel" is the median HAND of the cells N m away (0.75..1.25 N),
	//     for every channel and for the trunks (over 0.25 km2);
	//   - the width of the valley floors: the dry cells within 2 m (and 5 m) of their trunk's level, over the length of the trunks;
	//   - the drainage density: the length of the network per km2.
	// With out_dir it writes <tag>_flow.f32 (the drainage area in m2), <tag>_water.f32 (1 lake) and <tag>_relief.json.
	int Dem(int argc, char** argv)
	{
		if (argc < 5) { std::fprintf(stderr, "dem <height.f32> <n> <size> [lakes.f32|-] [out_dir] [tag]\n"); return 2; }
		const uint32_t n = static_cast<uint32_t>(std::atoi(argv[3]));
		const float size = static_cast<float>(std::atof(argv[4]));
		const size_t count = static_cast<size_t>(n) * n;
		const std::string lakesPath = argc > 5 ? argv[5] : "-", outDir = argc > 6 ? argv[6] : "", tag = argc > 7 ? argv[7] : "dem";
		TgField h;
		h.N = n; h.Size = size; h.Center = glm::vec2(0.0f);
		h.Values.assign(count, 0.0f);
		{
			FILE* f = std::fopen(argv[2], "rb");
			if (!f || std::fread(h.Values.data(), sizeof(float), count, f) != count) { std::fprintf(stderr, "cannot read %u^2 floats from %s\n", n, argv[2]); if (f) std::fclose(f); return 1; }
			std::fclose(f);
		}
		const float sp = h.Spacing(), cellArea = sp * sp;
		auto border = [n](size_t c) { const size_t i = c % n, j = c / n; return i == 0 || j == 0 || i == n - 1 || j == n - 1; };
		std::vector<float> wet(count, 0.0f);
		bool flats = false;
		if (lakesPath != "-")
		{
			FILE* f = std::fopen(lakesPath.c_str(), "rb");
			std::vector<float> w(count, 0.0f);
			if (!f || std::fread(w.data(), sizeof(float), count, f) != count) { std::fprintf(stderr, "cannot read %u^2 floats from %s\n", n, lakesPath.c_str()); if (f) std::fclose(f); return 1; }
			std::fclose(f);
			for (size_t c = 0; c < count; ++c) wet[c] = w[c] == 1.0f ? 1.0f : 0.0f;
		}
		else
		{
			flats = true;
			std::vector<uint8_t> flat(count, 0), seen(count, 0);
			for (uint32_t j = 1; j + 1 < n; ++j)
				for (uint32_t i = 1; i + 1 < n; ++i)
				{
					const float z = h.At(i, j);
					flat[static_cast<size_t>(j) * n + i] = std::abs(h.At(i + 1, j) - z) < 0.05f && std::abs(h.At(i - 1, j) - z) < 0.05f &&
						std::abs(h.At(i, j + 1) - z) < 0.05f && std::abs(h.At(i, j - 1) - z) < 0.05f;
				}
			std::vector<size_t> stack, comp;
			for (size_t c = 0; c < count; ++c)
			{
				if (!flat[c] || seen[c]) continue;
				comp.clear();
				stack.assign(1, c);
				seen[c] = 1;
				while (!stack.empty())
				{
					const size_t k = stack.back();
					stack.pop_back();
					comp.push_back(k);
					const size_t ki = k % n, kj = k / n;
					const size_t nb[4] = { ki > 0 ? k - 1 : k, ki + 1 < n ? k + 1 : k, kj > 0 ? k - n : k, kj + 1 < n ? k + n : k };
					for (const size_t m : nb) if (flat[m] && !seen[m]) { seen[m] = 1; stack.push_back(m); }
				}
				if (comp.size() >= 4 && static_cast<float>(comp.size()) * cellArea >= 2000.0f) for (const size_t k : comp) wet[k] = 1.0f;
			}
		}
		size_t lakeCells = 0;
		for (const float w : wet) lakeCells += w != 0.0f;

		// slopes of the dry interior, the probe's central differences
		const float bins[] = { 3.0f, 14.0f, 30.0f, 40.0f, 1e9f };
		size_t slopeBin[5] = {}, s50 = 0, dryInterior = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				if (wet[static_cast<size_t>(j) * n + i] != 0.0f) continue;
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
				++dryInterior;
				int b = 0;
				while (deg >= bins[b]) ++b;
				++slopeBin[b];
				s50 += deg > 50.0f;
			}
		// the same slopes on the mountain part only: the dry interior cells with over 100 m of relief (max - min) in the square of +-300 m around
		// them -- the same rule for a real crop and ours, so a valley that is half plain is compared by its sierra
		std::vector<float> lo = h.Values, hi = h.Values;
		const int reliefCells = std::max(1, static_cast<int>(std::lround(300.0f / sp)));
		for (int pass = 0; pass < 2; ++pass)   // the min and max along the rows, then along the columns of that
		{
			std::vector<float> tlo(count), thi(count);
			for (uint32_t a = 0; a < n; ++a)
				for (uint32_t b = 0; b < n; ++b)
				{
					float mn = 1e30f, mx = -1e30f;
					const int k0 = std::max(0, static_cast<int>(b) - reliefCells), k1 = std::min(static_cast<int>(n) - 1, static_cast<int>(b) + reliefCells);
					for (int k = k0; k <= k1; ++k)
					{
						const size_t m = pass == 0 ? static_cast<size_t>(a) * n + static_cast<size_t>(k) : static_cast<size_t>(k) * n + a;
						mn = std::min(mn, lo[m]); mx = std::max(mx, hi[m]);
					}
					const size_t c = pass == 0 ? static_cast<size_t>(a) * n + b : static_cast<size_t>(b) * n + a;
					tlo[c] = mn; thi[c] = mx;
				}
			lo.swap(tlo); hi.swap(thi);
		}
		size_t mountBin[5] = {}, mount50 = 0, mountCells = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (wet[c] != 0.0f || hi[c] - lo[c] < 100.0f) continue;
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
				++mountCells;
				int b = 0;
				while (deg >= bins[b]) ++b;
				++mountBin[b];
				mount50 += deg > 50.0f;
			}
		// pits and where the dry land drains (D8 on the bare heights), the fill and the biggest basin, the probe's network
		const std::vector<int> down = FlowAccumulation::Directions(h.Values.data(), n);
		size_t pits = 0, pitLand = 0, land = 0;
		for (size_t c = 0; c < count; ++c)
		{
			if (wet[c] != 0.0f) continue;
			if (down[c] < 0 && !border(c)) ++pits;
			++land;
			size_t k = c, steps = 0;
			while (wet[k] == 0.0f && down[k] >= 0 && steps++ < count) k = static_cast<size_t>(down[k]);
			if (wet[k] == 0.0f && !border(k)) ++pitLand;
		}
		const std::vector<float> filled = FillHollows(h.Values, wet, n);
		size_t fill50 = 0;
		for (size_t c = 0; c < count; ++c) fill50 += filled[c] - h.Values[c] > 0.5f;
		glm::vec2 basinAt(0.0f);
		const double basin = BiggestBasin(h, filled, basinAt);
		const FilledNetworkStats fnet = FilledNetwork(filled, n, 0.002f * static_cast<float>(count));
		size_t floorCells = 0, floorFlat = 0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				if (fnet.Acc.Accumulation[c] < 0.0005f * static_cast<float>(count) || wet[c] != 0.0f) continue;
				++floorCells;
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				floorFlat += glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz))) < 3.0f;
			}

		// the drainage of E1, the lakes as outlets
		std::vector<uint8_t> outlets(count, 0);
		for (size_t c = 0; c < count; ++c) outlets[c] = wet[c] != 0.0f;
		DrainageOptions dopt;
		dopt.Outlets = outlets.data();
		const DrainageResult d = ComputeDrainage(h.Values.data(), n, sp, dopt);
		const NetworkStats dnet = NetworkOf(d);
		const float netArea = 0.002f * static_cast<float>(count) * cellArea, trunkArea = 250000.0f;
		std::vector<int32_t> entry(count, -1);
		double netLength = 0.0, trunkLength = 0.0;
		for (const uint32_t c : d.Order)
		{
			const int32_t r = d.Receiver[c];
			if (d.Area[c] >= netArea && wet[c] == 0.0f)
			{
				entry[c] = static_cast<int32_t>(c);
				if (r >= 0)
				{
					const bool diag = (c % n) != (static_cast<uint32_t>(r) % n) && (c / n) != (static_cast<uint32_t>(r) / n);
					const double step = (diag ? 1.41421356 : 1.0) * sp;
					netLength += step;
					if (d.Area[c] >= trunkArea) trunkLength += step;
				}
			}
			else if (r >= 0) entry[c] = entry[static_cast<size_t>(r)];
		}
		const float dists[] = { 30.0f, 60.0f, 120.0f, 240.0f };
		std::vector<float> handAt[4], handTrunkAt[4], handAll;
		size_t floor2 = 0, floor5 = 0;
		for (size_t c = 0; c < count; ++c)
		{
			if (wet[c] != 0.0f || entry[c] < 0) continue;
			const size_t e = static_cast<size_t>(entry[c]);
			const float hand = d.Level[c] - d.Level[e];
			handAll.push_back(hand);
			const bool trunk = d.Area[e] >= trunkArea;
			if (trunk) { floor2 += hand < 2.0f; floor5 += hand < 5.0f; }
			if (e == c) continue;
			const float dist = glm::distance(h.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n)), h.SamplePosition(static_cast<uint32_t>(e % n), static_cast<uint32_t>(e / n)));
			for (int k = 0; k < 4; ++k)
				if (dist >= 0.75f * dists[k] && dist <= 1.25f * dists[k]) { handAt[k].push_back(hand); if (trunk) handTrunkAt[k].push_back(hand); }
		}
		const double mapKm2 = static_cast<double>(size) * size / 1e6;
		const double width2 = trunkLength > 0.0 ? static_cast<double>(floor2) * cellArea / trunkLength : 0.0;
		const double width5 = trunkLength > 0.0 ? static_cast<double>(floor5) * cellArea / trunkLength : 0.0;

		std::printf("dem %s: %u^2, %.2f m cells, %.0f m; %.0f..%.0f m; lakes %.2f %% of the map (%s)\n", tag.c_str(), n, sp, size,
			*std::min_element(h.Values.begin(), h.Values.end()), *std::max_element(h.Values.begin(), h.Values.end()), Pct(lakeCells, count), flats ? "flat surfaces of the DEM" : "given");
		std::printf("  slopes (dry): < 3 deg %.1f %%, 3-14 %.1f %%, 14-30 %.1f %%, 30-40 %.1f %%, > 40 %.1f %%, > 50 %.2f %%\n",
			Pct(slopeBin[0], dryInterior), Pct(slopeBin[1], dryInterior), Pct(slopeBin[2], dryInterior), Pct(slopeBin[3], dryInterior), Pct(slopeBin[4], dryInterior), Pct(s50, dryInterior));
		std::printf("  mountain (> 100 m of relief within 300 m: %.1f %% of the dry land): < 3 deg %.1f %%, 3-14 %.1f %%, 14-30 %.1f %%, 30-40 %.1f %%, > 40 %.1f %%, > 50 %.2f %%\n",
			Pct(mountCells, dryInterior), Pct(mountBin[0], mountCells), Pct(mountBin[1], mountCells), Pct(mountBin[2], mountCells), Pct(mountBin[3], mountCells), Pct(mountBin[4], mountCells), Pct(mount50, mountCells));
		std::printf("  pits %.2f per 1000 cells, dry land into a pit %.1f %%, filling raises %.3f %% by > 0.5 m, biggest basin %.0f m3 at (%.0f, %.0f)\n",
			10.0 * Pct(pits, land), Pct(pitLand, land), Pct(fill50, count), basin, basinAt.x, basinAt.y);
		std::printf("  network > %.0f m2: probe (D8 on the filled ground) Strahler %d, %zu confluences, %zu cells; drainage (E1 receivers) Strahler %d, %zu confluences, %zu cells; %.2f km/km2; channel floors under 3 deg %.1f %%\n",
			netArea, fnet.Strahler, fnet.Confluences, fnet.Cells, dnet.Strahler, dnet.Confluences, dnet.Cells, netLength / 1000.0 / mapKm2, Pct(floorFlat, floorCells));
		std::printf("  height over the channel (median HAND) at 30 / 60 / 120 / 240 m: every channel %.1f / %.1f / %.1f / %.1f m; trunks (> 0.25 km2, %.0f m long) %.1f / %.1f / %.1f / %.1f m; all dry cells p50 %.1f m, p90 %.1f m\n",
			Percentile(handAt[0], 0.5), Percentile(handAt[1], 0.5), Percentile(handAt[2], 0.5), Percentile(handAt[3], 0.5), trunkLength,
			Percentile(handTrunkAt[0], 0.5), Percentile(handTrunkAt[1], 0.5), Percentile(handTrunkAt[2], 0.5), Percentile(handTrunkAt[3], 0.5), Percentile(handAll, 0.5), Percentile(handAll, 0.9));
		std::printf("  valley floor of the trunks: %.0f m wide within 2 m of their level, %.0f m within 5 m\n", width2, width5);
		if (!outDir.empty())
		{
			ProbeJson js;
			js.File = std::fopen((outDir + "/" + tag + "_relief.json").c_str(), "w");
			if (js.File) std::fprintf(js.File, "{\n  ");
			js.Num("n", n); js.Num("size", size); js.Num("spacing", sp); js.Num("lakes", Pct(lakeCells, count));
			js.Num("slope_lt3", Pct(slopeBin[0], dryInterior)); js.Num("slope_3_14", Pct(slopeBin[1], dryInterior)); js.Num("slope_14_30", Pct(slopeBin[2], dryInterior));
			js.Num("slope_30_40", Pct(slopeBin[3], dryInterior)); js.Num("slope_gt40", Pct(slopeBin[4], dryInterior)); js.Num("slope_gt50", Pct(s50, dryInterior));
			js.Num("mountain", Pct(mountCells, dryInterior)); js.Num("m_slope_lt3", Pct(mountBin[0], mountCells)); js.Num("m_slope_3_14", Pct(mountBin[1], mountCells));
			js.Num("m_slope_14_30", Pct(mountBin[2], mountCells)); js.Num("m_slope_30_40", Pct(mountBin[3], mountCells)); js.Num("m_slope_gt40", Pct(mountBin[4], mountCells));
			js.Num("m_slope_gt50", Pct(mount50, mountCells));
			js.Num("pits_per_1000", 10.0 * Pct(pits, land)); js.Num("land_to_pit", Pct(pitLand, land)); js.Num("fill_05", Pct(fill50, count)); js.Num("biggest_basin_m3", basin);
			js.Num("strahler", fnet.Strahler); js.Num("confluences", static_cast<double>(fnet.Confluences)); js.Num("strahler_e1", dnet.Strahler); js.Num("confluences_e1", static_cast<double>(dnet.Confluences));
			js.Num("drainage_density", netLength / 1000.0 / mapKm2); js.Num("floor_flat", Pct(floorFlat, floorCells));
			for (int k = 0; k < 4; ++k)
			{
				const std::string a = "hand_" + std::to_string(static_cast<int>(dists[k])), b = "hand_trunk_" + std::to_string(static_cast<int>(dists[k]));
				js.Num(a.c_str(), Percentile(handAt[k], 0.5)); js.Num(b.c_str(), Percentile(handTrunkAt[k], 0.5));
			}
			js.Num("hand_p50", Percentile(handAll, 0.5)); js.Num("hand_p90", Percentile(handAll, 0.9)); js.Num("trunk_length", trunkLength);
			js.Num("floor_width_2", width2); js.Num("floor_width_5", width5);
			if (js.File) { std::fprintf(js.File, "\n}\n"); std::fclose(js.File); }
			WriteF32(outDir + "/" + tag + "_flow.f32", d.Area);
			WriteF32(outDir + "/" + tag + "_water.f32", wet);
		}
		return 0;
	}

	// The villages the game would found on a big world (EditorApp::PickVillageSites, Editor/src/Gameplay/EditorClans.cpp): the same filters and
	// score over the world as built (its terrain, baked biomes and water distance), so the graph's sites can be compared with them headless.
	std::vector<glm::vec2> GameVillageSites(const GeneratedWorld& world, int count)
	{
		const Terrain& ground = *world.TerrainRef;
		const Heightmap& hm = ground.GetHeightmap();
		const float lowest = hm.GetMinHeight(), span = std::max(hm.GetMaxHeight() - hm.GetMinHeight(), 1.0f);
		const float extent = world.Desc.Extent(), margin = std::min(150.0f, extent * 0.1f), apart = std::max(250.0f, extent / 8.0f);
		struct Candidate { glm::vec2 At; float Score; };
		std::vector<Candidate> candidates;
		for (float z = hm.GetMin().y + margin; z <= hm.GetMax().y - margin; z += 25.0f)
			for (float x = hm.GetMin().x + margin; x <= hm.GetMax().x - margin; x += 25.0f)
			{
				const Biome b = world.BiomeAt(x, z);
				if (b != Biome::Meadow && b != Biome::Forest) continue;
				const float hh = ground.GetHeight(x, z);
				float rough = 0.0f;
				for (int k = 0; k < 8; ++k)
				{
					const float angle = static_cast<float>(k) * 0.785398f;
					rough = std::max(rough, std::fabs(ground.GetHeight(x + 22.0f * std::cos(angle), z + 22.0f * std::sin(angle)) - hh));
				}
				if (rough > 2.5f) continue;
				const float wd = world.WaterDistanceAt(x, z);
				if (wd < 35.0f) continue;
				const float score = (1.0f - rough / 2.5f) + (1.0f - glm::clamp((wd - 60.0f) / 400.0f, 0.0f, 1.0f)) - 0.8f * (hh - lowest) / span + (b == Biome::Meadow ? 0.2f : 0.0f);
				candidates.push_back({ { x, z }, score });
			}
		std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& p, const Candidate& q) { return p.Score > q.Score; });
		std::vector<glm::vec2> sites;
		for (const Candidate& c : candidates)
		{
			if (static_cast<int>(sites.size()) >= count) break;
			if (std::all_of(sites.begin(), sites.end(), [&](const glm::vec2& s) { return glm::distance(s, c.At) >= apart; })) sites.push_back(c.At);
		}
		return sites;
	}

	// What the settle nodes made (AppendSettle): the sites against ESCENARIOS S1, S2, X3 and X4 and their separation (R2); the trails (C1, the
	// grade after levelling, the detour against the straight line C2); the earth moved by trail and platform; the fords and the cols (C6); the
	// talus left; the final fill; and where the game's own picker would found its villages on this world.
	void ProbeSettle(const TgResult& r, const TgField& h, const GeneratedWorld* world, const std::vector<float>& wetKinds, const TerrainGraphCache::Stats& stats, ProbeJson& js,
		const std::string& outDir, const std::string& tag)
	{
		const std::vector<glm::vec2> game = world ? GameVillageSites(*world, 3) : std::vector<glm::vec2>();
		if (world)
		{
			std::printf("  game picker (PickVillageSites, 3):");
			for (const glm::vec2& p : game) std::printf(" (%.0f, %.0f) water %.0f m;", p.x, p.y, world->WaterDistanceAt(p.x, p.y));
			std::printf("\n");
		}
		const auto gn = r.Nodes.find("settle_grade");
		if (gn == r.Nodes.end()) return;
		const auto& G = gn->second;
		std::vector<SettlementSite> sites;
		std::vector<Trail> trails;
		std::vector<Ford> fords;
		std::vector<Col> cols;
		DecodeSites(G.at("sites").Paths, sites);
		DecodeTrails(G.at("trails").Paths, G.at("trail_info").Paths, trails);
		DecodeFords(G.at("fords").Paths, fords);
		if (const auto rn = r.Nodes.find("settle_routes"); rn != r.Nodes.end()) DecodeCols(rn->second.at("cols").Paths, cols);
		const TgField& path = G.at("path").Field;
		const TgField& platform = G.at("platform").Field;
		const TgField& earth = G.at("earthwork").Field;
		const TgField& drainFill = G.at("drain_fill").Field;
		const uint32_t n = h.N;
		const float sp = h.Spacing();
		double ms = 0.0;
		std::printf("  settle nodes (ms):");
		for (const char* id : { "settle_sites", "settle_routes", "settle_grade" })
		{
			const auto t = stats.RunMs.find(id);
			const double v = t != stats.RunMs.end() ? t->second : 0.0;
			ms += v;
			std::printf(" %s %.0f", id, v);
			js.Num(id, v);
		}
		std::printf(", together %.0f\n", ms);
		js.Num("settle_ms", ms);

		// sites
		size_t s1 = 0, s2 = 0, x3 = 0, x4 = 0;
		float apart = 1e9f;
		for (size_t i = 0; i < sites.size(); ++i)
		{
			const SettlementSite& s = sites[i];
			s1 += s.WaterDistance >= 35.0f && s.WaterDistance <= 150.0f;
			s2 += s.CampSlopeMax <= 14.0f && s.CampSlopeMean <= 7.0f;
			x3 += s.SteepShare <= 0.2f;
			x4 += s.AboveWater >= 1.5f;
			for (size_t j = i + 1; j < sites.size(); ++j) apart = std::min(apart, glm::distance(s.Position, sites[j].Position));
			float gameNear = 1e9f;
			for (const glm::vec2& p : game) gameNear = std::min(gameNear, glm::distance(p, s.Position));
			std::printf("    site %d (%.0f, %.0f): score %.2f = water %.2f flat %.2f area %.2f dry %.2f low %.2f forest %.2f stone %.2f | water %.0f m (baked %.0f), %.1f m above it, camp %.1f/%.1f deg, village %.1f deg, flat %.0f %%, steep %.0f %%, rough %.1f m | platform %.1f m: cut %.0f m3, fill %.0f m3 | nearest game village %.0f m\n",
				s.Rank, s.Position.x, s.Position.y, s.Score, s.Water, s.Flat, s.Area, s.Dry, s.Low, s.Forest, s.Stone, s.WaterDistance, world ? world->WaterDistanceAt(s.Position.x, s.Position.y) : -1.0f,
				s.AboveWater, s.CampSlopeMax, s.CampSlopeMean, s.VillageSlopeMean, 100.0f * s.FlatShare, 100.0f * s.SteepShare, s.Roughness, s.PlatformLevel, s.Cut, s.Fill, gameNear);
		}
		std::printf("  sites: %zu; S1 (water 35-150 m) %zu, S2 %zu, X3 %zu, X4 (>= 1.5 m above the water) %zu; closest two %.0f m apart\n", sites.size(), s1, s2, x3, x4, sites.size() > 1 ? apart : 0.0f);
		js.Num("settle_sites", static_cast<double>(sites.size())); js.Num("settle_apart", sites.size() > 1 ? apart : 0.0f);
		const TgField& score = r.Nodes.at("settle_sites").at("score").Field;
		for (const glm::vec2& p : game)
		{
			const SettlementSite* best = nullptr;
			for (const SettlementSite& s : sites) if (!best || glm::distance(s.Position, p) < glm::distance(best->Position, p)) best = &s;
			std::printf("    game village (%.0f, %.0f): the graph's score there %.2f (0 = refused), its nearest site %d at %.0f m\n", p.x, p.y, score.Sample(p.x, p.y),
				best ? best->Rank : -1, best ? glm::distance(best->Position, p) : -1.0f);
		}

		// trails
		std::vector<int> root(sites.size());
		for (size_t s = 0; s < root.size(); ++s) root[s] = static_cast<int>(s);
		auto find = [&](int x) { while (root[static_cast<size_t>(x)] != x) x = root[static_cast<size_t>(x)]; return x; };
		double linkLen = 0.0, linkGood = 0.0, ratioSum = 0.0, trailCut = 0.0, trailFill = 0.0, allLen = 0.0, allGood = 0.0;
		float ratioMax = 0.0f;
		int links = 0;
		for (const Trail& t : trails)
		{
			float within = 0.0f, steepest = 0.0f;
			MeasureTrailGrade(h, t, 0.14f, 9.0f, within, steepest);
			allLen += t.Length; allGood += within * t.Length;
			trailCut += t.Cut; trailFill += t.Fill;
			const bool link = t.Kind == TrailKind::Link || t.Kind == TrailKind::Extra;
			if (link && t.From >= 0 && t.To >= 0)
			{
				root[static_cast<size_t>(find(t.From))] = find(t.To);
				const float ratio = t.Length / std::max(t.Straight, 1.0f);
				ratioSum += ratio; ratioMax = std::max(ratioMax, ratio); ++links;
				linkLen += t.Length; linkGood += within * t.Length;
			}
			std::printf("    trail %s %d-%d: %.0f m (straight %.0f, x%.2f), within 14 %% on %.1f %% (record %.1f %%), steepest 9 m %.0f %%, cut %.0f m3, fill %.0f m3, %d fords\n",
				TrailKindName(t.Kind), t.From, t.To, t.Length, t.Straight, t.Length / std::max(t.Straight, 1.0f), 100.0f * within, 100.0f * t.WithinGrade, 100.0f * steepest, t.Cut, t.Fill, t.Fords);
		}
		std::vector<int> roots;
		for (size_t s = 0; s < sites.size(); ++s) roots.push_back(find(static_cast<int>(s)));
		std::sort(roots.begin(), roots.end());
		const size_t groups = static_cast<size_t>(std::unique(roots.begin(), roots.end()) - roots.begin());
		double platCut = 0.0, platFill = 0.0;
		for (const SettlementSite& s : sites) { platCut += s.Cut; platFill += s.Fill; }
		std::printf("  trails: %zu, %.0f m; the sites in %zu group(s) (C1: 1); links %d, detour x%.2f mean, x%.2f worst; within 14 %%: links %.1f %%, all %.1f %%; earth: trails %.0f cut + %.0f fill m3, platforms %.0f + %.0f m3\n",
			trails.size(), allLen, groups, links, links ? ratioSum / links : 0.0, ratioMax, linkLen > 0 ? 100.0 * linkGood / linkLen : 0.0, allLen > 0 ? 100.0 * allGood / allLen : 0.0,
			trailCut, trailFill, platCut, platFill);
		js.Num("settle_trails", static_cast<double>(trails.size())); js.Num("settle_groups", static_cast<double>(groups)); js.Num("settle_detour_mean", links ? ratioSum / links : 0.0);
		js.Num("settle_detour_max", ratioMax); js.Num("settle_within_links", linkLen > 0 ? 100.0 * linkGood / linkLen : 0.0); js.Num("settle_within_all", allLen > 0 ? 100.0 * allGood / allLen : 0.0);
		js.Num("settle_trail_cut", trailCut); js.Num("settle_trail_fill", trailFill); js.Num("settle_platform_cut", platCut); js.Num("settle_platform_fill", platFill);
		for (const Ford& f : fords)
			std::printf("    ford (%.0f, %.0f): water %.2f m, %.2f m deep, half-width %.1f m, %d trails%s\n", f.Position.x, f.Position.y, f.Level, f.Depth, f.HalfWidth, f.Used, f.Built ? ", built (bed raised)" : "");
		for (const Col& c : cols)
		{
			float grade = -1.0f;
			for (const Trail& t : trails)
				if (!t.Points.empty() && glm::distance(glm::vec2(t.Points.back().x, t.Points.back().z), c.Position) < 10.0f) { float w = 0.0f; MeasureTrailGrade(h, t, 0.14f, 9.0f, w, grade); }
			if (grade < 0.0f) std::printf("    col (%.0f, %.0f) at %.0f m, from site %d: reached by another trail\n", c.Position.x, c.Position.y, c.Height, c.Site);
			else std::printf("    col (%.0f, %.0f) at %.0f m, from site %d: its trail's steepest 9 m %.0f %%\n", c.Position.x, c.Position.y, c.Height, c.Site, 100.0f * grade);
		}

		// the ground the levelling changed, outside the treads and platforms: how much of it is steeper than 30 degrees; the final fill
		size_t changed = 0, steep = 0, raised = 0, steepBefore = 0, steepWorse = 0, steepNew = 0;
		float raise = 0.0f;
		double raiseVol = 0.0;
		for (uint32_t j = 1; j + 1 < n; ++j)
			for (uint32_t i = 1; i + 1 < n; ++i)
			{
				const size_t c = static_cast<size_t>(j) * n + i;
				raise = std::max(raise, drainFill.Values[c]);
				raised += drainFill.Values[c] > 0.05f;
				raiseVol += drainFill.Values[c] * sp * sp;
				if (wetKinds[c] != 0.0f || std::fabs(earth.Values[c]) < 0.05f || path.Values[c] > 0.5f || platform.Values[c] > 0.5f) continue;
				++changed;
				const float gx = (h.At(i + 1, j) - h.At(i - 1, j)) / (2.0f * sp), gz = (h.At(i, j + 1) - h.At(i, j - 1)) / (2.0f * sp);
				if (std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2958f <= 30.0f) continue;
				++steep;
				auto pre = [&](uint32_t a, uint32_t b) { return h.At(a, b) - earth.At(a, b); };
				const float px = (pre(i + 1, j) - pre(i - 1, j)) / (2.0f * sp), pz = (pre(i, j + 1) - pre(i, j - 1)) / (2.0f * sp);
				const float before = std::atan(std::sqrt(px * px + pz * pz)) * 57.2958f, after = std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2958f;
				steepBefore += before > 30.0f;
				steepWorse += after > before + 1.0f;
				steepNew += before <= 30.0f;
			}
		std::printf("  talus: %zu cells changed outside the treads and platforms, %.2f %% of them over 30 deg (of those, %.0f %% were over 30 deg before, %.0f %% made steeper than they were: %.2f %% of the changed cells; over 30 deg where the ground was not: %.2f %%); the final fill raised %zu cells by > 5 cm, at most %.2f m, %.0f m3\n",
			changed, Pct(steep, changed), Pct(steepBefore, steep), Pct(steepWorse, steep), Pct(steepWorse, changed), Pct(steepNew, changed), raised, raise, raiseVol);
		js.Num("settle_talus_steep_before", Pct(steepBefore, steep)); js.Num("settle_talus_made_steep", Pct(steepWorse, changed)); js.Num("settle_talus_new_steep", Pct(steepNew, changed));
		js.Num("settle_talus_over30", Pct(steep, changed)); js.Num("settle_fill_max", raise); js.Num("settle_fill_cells", static_cast<double>(raised));
		if (!outDir.empty())
		{
			// the map for the png tools: 1 tread, 2 platform, 3 site centre (within 8 m)
			std::vector<float> map(h.Values.size(), 0.0f);
			for (size_t c = 0; c < map.size(); ++c) map[c] = platform.Values[c] > 0.5f ? 2.0f : path.Values[c] > 0.5f ? 1.0f : 0.0f;
			for (const SettlementSite& s : sites)
				for (size_t c = 0; c < map.size(); ++c)
					if (glm::distance(h.SamplePosition(static_cast<uint32_t>(c % n), static_cast<uint32_t>(c / n)), s.Position) < 8.0f) map[c] = 3.0f;
			WriteF32(outDir + "/" + tag + "_settle.f32", map);
		}
	}

	// The settle nodes again and again on one world, the rest of the graph from the cache: each argument after the first five is a set of
	// parameters ("settle_routes.SteepWeight=60,settle_grade.MaxCut=3"; "-" for none) and gets ProbeSettle's report (without the game picker).
	//   GBSimTests terrain_graph_cache settle_tune <graph> <type> <seed> <size> <resolution> <set> [<set> ...]
	int SettleTune(int argc, char** argv)
	{
		if (argc < 8) { std::fprintf(stderr, "settle_tune <graph> <type> <seed> <size> <resolution> <set> [<set> ...]\n"); return 2; }
		WorldDesc desc = ProbeDesc(argv[3], static_cast<uint32_t>(std::atoi(argv[4])), static_cast<float>(std::atof(argv[5])), static_cast<uint32_t>(std::atoi(argv[6])));
		TerrainGraph base;
		bool own = false;
		std::string error;
		if (!ProbeGraph(std::string("settle:") + argv[2], desc, base, own, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		for (int a = 7; a < argc; ++a)
		{
			TerrainGraph g = base;
			std::string set = argv[a];
			if (set != "-")
			{
				std::stringstream items(set);
				std::string item;
				while (std::getline(items, item, ','))
				{
					const size_t dot = item.find('.'), eq = item.find('=');
					if (dot == std::string::npos || eq == std::string::npos || eq < dot) { std::fprintf(stderr, "bad item %s\n", item.c_str()); return 2; }
					TgNode* nd = g.FindNode(item.substr(0, dot));
					if (!nd) { std::fprintf(stderr, "no node %s\n", item.substr(0, dot).c_str()); return 2; }
					nd->Params[item.substr(dot + 1, eq - dot - 1)] = std::atof(item.substr(eq + 1).c_str());
				}
			}
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
			TgResult r;
			const auto t0 = std::chrono::steady_clock::now();
			if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "evaluation failed: %s\n", error.c_str()); return 1; }
			std::printf("set %s: %.2f s\n", set.c_str(), Seconds(t0));
			const TgField& h = r.Outputs.at("height");
			std::vector<float> wet(h.Values.size(), 0.0f);
			if (const auto w = r.WaterOutputs.find("water"); w != r.WaterOutputs.end())
			{
				const WaterRaster raster = RasterizeWater(w->second, h.N, h.Min(), h.Spacing());
				for (size_t c = 0; c < wet.size(); ++c) wet[c] = static_cast<float>(raster.Kinds[c]);
			}
			ProbeJson js;
			ProbeSettle(r, h, nullptr, wet, desc.GraphCache->LastStats(), js, "", "");
		}
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

		// the closed hollows of the final ground (FillHollows), the biggest basin and the channel network of the filled ground (FilledNetwork)
		const std::vector<float> filled = FillHollows(h.Values, wet, n);
		size_t fill5 = 0, fill50 = 0;
		for (size_t c = 0; c < count; ++c) { const float d = filled[c] - h.Values[c]; fill5 += d > 0.05f; fill50 += d > 0.5f; }
		glm::vec2 biggestAt(0.0f);
		const double biggestBasin = BiggestBasin(h, filled, biggestAt);
		const float floorArea = 0.0005f * static_cast<float>(count);
		const FilledNetworkStats fnet = FilledNetwork(filled, n, 0.002f * static_cast<float>(count));
		const FlowAccumulationResult& facc = fnet.Acc;
		const int maxOrder = fnet.Strahler;
		const size_t confluences = fnet.Confluences, netCells = fnet.Cells;
		size_t floorCells = 0, floorFlat = 0;
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
		// walkable (TERRENO_V5 E4, ESCENARIOS_JUGABLES C3): a dry cell is walkable when its own slope (central differences) is under a limit; the
		// share of all the dry land in the largest 8-connected piece of walkable cells. At 30 degrees (the order's measure) and at 40.4 (rise over
		// run 0.85: NavProfile::MaxSlope, where the navigation makes a cell a wall)
		double walk30 = 0.0, walk40 = 0.0;
		{
			std::vector<float> cellSlope(count, 0.0f);
			for (uint32_t j = 0; j < n; ++j)
				for (uint32_t i = 0; i < n; ++i)
				{
					const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < n ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j + 1 < n ? j + 1 : j;
					const float gx = (h.At(i1, j) - h.At(i0, j)) / (static_cast<float>(i1 - i0) * sp), gz = (h.At(i, j1) - h.At(i, j0)) / (static_cast<float>(j1 - j0) * sp);
					cellSlope[static_cast<size_t>(j) * n + i] = std::sqrt(gx * gx + gz * gz);
				}
			size_t dryAll = 0;
			for (size_t c = 0; c < count; ++c) dryAll += wetKinds[c] == 0.0f;
			auto largest = [&](float limit, size_t& pieces)
			{
				std::vector<uint8_t> seen(count, 0);
				std::vector<uint32_t> stack;
				size_t best = 0;
				pieces = 0;
				for (size_t c0 = 0; c0 < count; ++c0)
				{
					if (seen[c0] || wetKinds[c0] != 0.0f || cellSlope[c0] > limit) continue;
					++pieces;
					size_t size = 0;
					seen[c0] = 1;
					stack.assign(1, static_cast<uint32_t>(c0));
					while (!stack.empty())
					{
						const uint32_t c = stack.back();
						stack.pop_back();
						++size;
						const int ci = static_cast<int>(c % n), cj = static_cast<int>(c / n);
						for (int dj = -1; dj <= 1; ++dj)
							for (int di = -1; di <= 1; ++di)
							{
								const int ni = ci + di, nj = cj + dj;
								if ((!di && !dj) || ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
								const size_t m = static_cast<size_t>(nj) * n + static_cast<size_t>(ni);
								if (seen[m] || wetKinds[m] != 0.0f || cellSlope[m] > limit) continue;
								seen[m] = 1;
								stack.push_back(static_cast<uint32_t>(m));
							}
					}
					best = std::max(best, size);
				}
				return Pct(best, dryAll);
			};
			size_t p30 = 0, p40 = 0;
			walk30 = largest(std::tan(glm::radians(30.0f)), p30);
			walk40 = largest(0.85f, p40);
			std::printf("  walkable: the largest piece of dry cells under 30 deg holds %.1f %% of the dry land (%zu pieces); under 40.4 deg (NavProfile::MaxSlope 0.85) %.1f %% (%zu pieces)\n",
				walk30, p30, walk40, p40);
		}
		ProbeJson js;
		std::string jsonPath;
		if (!outDir.empty()) { jsonPath = outDir + "/" + tag + "_info.json"; js.File = std::fopen(jsonPath.c_str(), "w"); if (js.File) std::fprintf(js.File, "{\n  "); }
		js.Num("n", n); js.Num("size", h.Size); js.Num("cx", h.Center.x); js.Num("cz", h.Center.y);
		js.Num("min_h", *std::min_element(h.Values.begin(), h.Values.end())); js.Num("max_h", *std::max_element(h.Values.begin(), h.Values.end()));
		js.Num("graph_s", graphS); js.Num("world_extra_s", worldExtraS);
		js.Num("slope30", Pct(s30, interior)); js.Num("slope40", Pct(s40, interior)); js.Num("slope50", Pct(s50, interior));
		js.Num("pits_per_1000", 10.0 * Pct(pits, interior)); js.Num("land_to_pit", Pct(pitLand, land)); js.Num("channel_wet", Pct(channelWet, channel));
		js.Num("fill_005", Pct(fill5, count)); js.Num("fill_05", Pct(fill50, count)); js.Num("biggest_basin_m3", biggestBasin);
		js.Num("walk30", walk30); js.Num("walk40", walk40);
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
			double maxDrop10 = 0.0, sumIncision = 0.0, sumUpland = 0.0;
			size_t b = 0, trench = 0, raised = 0, upland = 0, upland5 = 0;
			// the upland stretch (E2): the points whose water stands above the mouth by more than a quarter of the course's fall
			const float uplandLevel = P.back().y + 0.25f * (P.front().y - P.back().y);
			std::vector<float> fars;
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
				fars.push_back(far);
				if (P[k].y > uplandLevel) { ++upland; sumUpland += far; upland5 += far >= 5.0f; }
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
			std::sort(fars.begin(), fars.end());
			std::printf("        (E2) the median %.1f m; the upland stretch (water over %.0f m, %.0f %% of the course): %.1f m on average, >= 5 m on %.0f %% of it\n",
				fars[fars.size() / 2], uplandLevel, Pct(upland, P.size()), upland ? sumUpland / static_cast<double>(upland) : 0.0, Pct(upland5, upland));
			js.Num("river_incision_median", fars[fars.size() / 2]); js.Num("river_incision_upland", upland ? sumUpland / static_cast<double>(upland) : 0.0); js.Num("river_upland5", Pct(upland5, upland));
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
		std::vector<float> lakeMeshMap;   // T105: <tag>_lakemesh.f32, written only when there is a lake (the other files stay as they were)
		if (water && !water->Lakes().empty())
		{
			// T105: every lake as one mesh on the grid (GB/World/Water/LakeMesh.h), what the renderer will draw instead of the discs. Every
			// sample the mesh uses outside its own lake's cells, against the ground: below the level minus 5 cm there it would show water over
			// dry ground (wetKinds 0), over another, lower, lake (1) or over the river (2). The map: 1 a lake cell the mesh draws, 2 the skirt
			// (a vertex past the shore, under the ground), 3 a lake cell no triangle touches, 4 the old disc over dry ground (now not drawn),
			// 5 the mesh over ground below the level outside its lake (a defect).
			const LakeMeshGround ground{ h.Values.data(), n, h.Min(), sp };
			auto tm = std::chrono::steady_clock::now();
			const LakeMesh lm = BuildLakeMesh(*water, ground);
			const double meshMs = 1000.0 * Seconds(tm);
			const std::vector<const LakeBody*> lakes = water->Lakes();
			lakeMeshMap.assign(count, 0.0f);
			for (size_t c = 0; c < count; ++c) if (wet[c] == 5.0f) lakeMeshMap[c] = 4.0f;
			std::vector<uint8_t> used(count, 0);
			size_t skirtVerts = 0, overDry = 0, overLake = 0, overRiver = 0, cellVerts = 0;
			for (const LakeVertex& v : lm.Vertices)
			{
				const int i = static_cast<int>(std::lround((v.Position.x - h.Min().x) / sp)), j = static_cast<int>(std::lround((v.Position.z - h.Min().y) / sp));
				const size_t c = static_cast<size_t>(j) * n + static_cast<size_t>(i);
				const glm::vec2 p = h.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				int owner = -1;
				for (size_t k = 0; k < lakes.size() && owner < 0; ++k) if (lakes[k]->Covers(p)) owner = static_cast<int>(k);
				used[c] = 1;
				if (owner == static_cast<int>(v.Lake)) { ++cellVerts; lakeMeshMap[c] = 1.0f; continue; }
				++skirtVerts;
				if (lakeMeshMap[c] != 1.0f) lakeMeshMap[c] = 2.0f;
				if (h.Values[c] >= v.Position.y - 0.05f) continue;
				lakeMeshMap[c] = 5.0f;
				if (wetKinds[c] == 0.0f) ++overDry; else if (wetKinds[c] == 1.0f) ++overLake; else ++overRiver;
			}
			size_t gap = 0;
			for (size_t c = 0; c < count; ++c) if (wetKinds[c] == 1.0f && !used[c]) { ++gap; lakeMeshMap[c] = 3.0f; }
			std::printf("  lake mesh (T105): %zu lakes, %zu vertices, %zu triangles, built in %.1f ms; %zu lake cells, %zu of them untouched (%.2f %%); %zu shore triangles left out\n",
				lm.Bodies.size(), lm.Vertices.size(), lm.TriangleCount(), meshMs, lm.LakeCells, gap, Pct(gap, lm.LakeCells), lm.DroppedShoreTriangles);
			std::printf("    the mesh outside its lake: %zu skirt vertices; over dry ground below the level %zu (%.2f %%), over a lower lake %zu, over the river %zu\n",
				skirtVerts, overDry, Pct(overDry, skirtVerts), overLake, overRiver);
			js.Num("lakemesh_vertices", static_cast<double>(lm.Vertices.size())); js.Num("lakemesh_triangles", static_cast<double>(lm.TriangleCount()));
			js.Num("lakemesh_ms", meshMs); js.Num("lakemesh_skirt", static_cast<double>(skirtVerts)); js.Num("lakemesh_over_dry", static_cast<double>(overDry));
			js.Num("lakemesh_over_lake", static_cast<double>(overLake)); js.Num("lakemesh_over_river", static_cast<double>(overRiver));
			js.Num("lakemesh_gap", static_cast<double>(gap)); js.Num("lakemesh_dropped", static_cast<double>(lm.DroppedShoreTriangles));
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
		if (const auto e2 = r.Nodes.find("e2_fluvial"); e2 != r.Nodes.end() && e2->second.count("height"))
		{
			// the hollows fluvial_erosion leaves on its own ground (before the rest of the graph and lake_fill): how deep its pits are, and how
			// much it cut
			const TgField& eh = e2->second.at("height").Field;
			const DrainageResult ed = ComputeDrainage(eh.Values.data(), eh.N, eh.Spacing());
			size_t p5 = 0, p50 = 0, p200 = 0;
			float deepest = 0.0f;
			size_t at = 0;
			for (size_t c = 0; c < eh.Values.size(); ++c)
			{
				const float dz = ed.Level[c] - eh.Values[c];
				p5 += dz > 0.05f; p50 += dz > 0.5f; p200 += dz > 2.0f;
				if (dz > deepest) { deepest = dz; at = c; }
			}
			const TgField& eroded = e2->second.at("eroded").Field;
			double cut = 0.0;
			float most = 0.0f;
			for (const float v : eroded.Values) { cut += v; most = std::max(most, v); }
			const glm::vec2 p = eh.SamplePosition(static_cast<uint32_t>(at % eh.N), static_cast<uint32_t>(at / eh.N));
			std::printf("  E2 its own ground: hollows > 0.05 m on %.2f %% of the cells, > 0.5 m on %.3f %%, > 2 m on %.3f %%, deepest %.1f m at (%.0f, %.0f); it cut %.1f m on average, %.0f m at most\n",
				Pct(p5, eh.Values.size()), Pct(p50, eh.Values.size()), Pct(p200, eh.Values.size()), deepest, p.x, p.y, cut / static_cast<double>(eroded.Values.size()), most);
			js.Num("e2_hollow_05", Pct(p50, eh.Values.size())); js.Num("e2_hollow_max", deepest); js.Num("e2_cut_mean", cut / static_cast<double>(eroded.Values.size())); js.Num("e2_cut_max", most);
			if (!outDir.empty())
			{
				WriteF32(outDir + "/" + tag + "_e2height.f32", eh.Values);
				if (const auto in = r.Nodes.find("ground"); in != r.Nodes.end() && !in->second.empty()) WriteF32(outDir + "/" + tag + "_e2input.f32", in->second.begin()->second.Field.Values);
			}
		}
		if (const auto e4 = r.Nodes.find("e4_talus"); e4 != r.Nodes.end() && e4->second.count("height"))
		{
			// the talus against its input (fluvial_erosion's ground): the hollows it makes that were not there (the level of the water over the
			// ground grows by > 0.05 m), how much it cut and laid down as scree, and the steepest ground in and out of the hard rock
			const TgField& th = e4->second.at("height").Field;
			const TgField& tin = r.Nodes.at("e2_fluvial").at("height").Field;
			const DrainageResult before = ComputeDrainage(tin.Values.data(), tin.N, tin.Spacing());
			const DrainageResult after = ComputeDrainage(th.Values.data(), th.N, th.Spacing());
			const size_t tc = th.Values.size();
			size_t newHollow = 0, newHollow50 = 0;
			float deepestNew = 0.0f;
			size_t at = 0;
			for (size_t c = 0; c < tc; ++c)
			{
				const float grew = (after.Level[c] - th.Values[c]) - (before.Level[c] - tin.Values[c]);
				newHollow += grew > 0.05f; newHollow50 += grew > 0.5f;
				if (grew > deepestNew) { deepestNew = grew; at = c; }
			}
			const std::vector<float>& cutV = e4->second.at("cut").Field.Values;
			const std::vector<float>& screeV = e4->second.at("scree").Field.Values;
			const std::vector<float>& hardV = e4->second.at("hardness").Field.Values;
			const float tsp = th.Spacing(), cellA = tsp * tsp;
			double cutVol = 0.0, screeVol = 0.0;
			size_t screeCells = 0, hardCells = 0, softCells = 0, hard50 = 0, soft50 = 0, hard3040 = 0, soft40 = 0;
			for (size_t c = 0; c < tc; ++c) { cutVol += cutV[c] * cellA; screeVol += screeV[c] * cellA; screeCells += screeV[c] > 0.1f; }
			for (uint32_t j = 1; j + 1 < th.N; ++j)
				for (uint32_t i = 1; i + 1 < th.N; ++i)
				{
					const size_t c = static_cast<size_t>(j) * th.N + i;
					const float gx = (th.At(i + 1, j) - th.At(i - 1, j)) / (2.0f * tsp), gz = (th.At(i, j + 1) - th.At(i, j - 1)) / (2.0f * tsp);
					const float deg = glm::degrees(std::atan(std::sqrt(gx * gx + gz * gz)));
					if (hardV[c] >= 0.5f) { ++hardCells; hard50 += deg > 50.0f; hard3040 += deg > 30.0f && deg <= 40.0f; }
					else { ++softCells; soft50 += deg > 50.0f; soft40 += deg > 40.0f; }
				}
			const glm::vec2 p = th.SamplePosition(static_cast<uint32_t>(at % th.N), static_cast<uint32_t>(at / th.N));
			std::printf("  E4 talus: cut %.3g m3, scree %.3g m3 on %.2f %% of the cells (%.0f %% of the cut); new hollows > 0.05 m on %.3f %% of the cells, > 0.5 m on %.3f %%, deepest %.2f m at (%.0f, %.0f)\n",
				cutVol, screeVol, Pct(screeCells, tc), cutVol > 0.0 ? 100.0 * screeVol / cutVol : 0.0, Pct(newHollow, tc), Pct(newHollow50, tc), deepestNew, p.x, p.y);
			std::printf("  E4 slopes at %.2f m: hard rock (%.1f %% of the map) > 50 deg %.1f %%, 30-40 %.1f %%; the rest > 40 deg %.2f %%, > 50 %.2f %%\n",
				tsp, Pct(hardCells, hardCells + softCells), Pct(hard50, hardCells), Pct(hard3040, hardCells), Pct(soft40, softCells), Pct(soft50, softCells));
			js.Num("e4_cut_m3", cutVol); js.Num("e4_scree_m3", screeVol); js.Num("e4_new_hollow_005", Pct(newHollow, tc)); js.Num("e4_new_hollow_05", Pct(newHollow50, tc));
			js.Num("e4_new_hollow_max", deepestNew); js.Num("e4_hard_frac", Pct(hardCells, hardCells + softCells)); js.Num("e4_hard_gt50", Pct(hard50, hardCells));
			js.Num("e4_hard_30_40", Pct(hard3040, hardCells)); js.Num("e4_soft_gt40", Pct(soft40, softCells)); js.Num("e4_soft_gt50", Pct(soft50, softCells));
			if (!outDir.empty())
			{
				WriteF32(outDir + "/" + tag + "_e4hardness.f32", hardV);
				WriteF32(outDir + "/" + tag + "_e4scree.f32", screeV);
				WriteF32(outDir + "/" + tag + "_e4cut.f32", cutV);
			}
		}
		if (world.Desc.Extent() > 1000.0f || r.Nodes.count("settle_grade")) ProbeSettle(r, h, &world, wetKinds, stats, js, outDir, tag);
		if (const char* dump = std::getenv("GB_PROBE_DUMP"); dump && !outDir.empty())   // "node.port node.port ...": those fields as <tag>_node_<node>.f32
		{
			std::istringstream in(dump);
			std::string item;
			while (in >> item)
			{
				const size_t dot = item.find('.');
				const auto nd = r.Nodes.find(item.substr(0, dot));
				if (nd == r.Nodes.end() || dot == std::string::npos || !nd->second.count(item.substr(dot + 1))) continue;
				WriteF32(outDir + "/" + tag + "_node_" + item.substr(0, dot) + ".f32", nd->second.at(item.substr(dot + 1)).Field.Values);
			}
		}
		std::printf("  world: %zu trees, %zu rocks\n", world.Trees.size(), world.Rocks.size());
		if (js.File) { std::fprintf(js.File, "\n}\n"); std::fclose(js.File); }
		if (!outDir.empty())
		{
			WriteF32(outDir + "/" + tag + "_height.f32", h.Values);
			WriteF32(outDir + "/" + tag + "_water.f32", wet);
			WriteF32(outDir + "/" + tag + "_flow.f32", acc.Accumulation);
			if (!lakeMeshMap.empty()) WriteF32(outDir + "/" + tag + "_lakemesh.f32", lakeMeshMap);
			if (river) WriteRiver(outDir + "/" + tag + "_river.f32", *river);
			std::printf("  wrote %s and its fields\n", jsonPath.c_str());
		}
		return 0;
	}
}
