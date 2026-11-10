// The per-node cache of a terrain graph's evaluation (GB/World/Graph/TerrainGraphCache.h, docs/GRAFO_TERRENO.md 10.5): with a cache, only
// the node that changed and what depends on it run again, and the result is bit for bit the result without a cache; a world built through
// WorldDesc::GraphCache is the world built without it.
//
// GB_TGRAPH_BENCH=1 GBSimTests terrain_graph_cache measures, on the 3 km highland_valley (1025 x 1025), how long regenerating takes without and
// with the cache after changing a final node (the boulders' scatter) and an initial one (the first noise): the graph alone and the whole world.
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	TgNode& NodeOf(TerrainGraph& g, const char* id)
	{
		TgNode* n = g.FindNode(id);
		if (!n) { std::fprintf(stderr, "FAIL: no node %s\n", id); std::exit(1); }
		return *n;
	}

	uint64_t Fresh(const TerrainGraph& g, const TerrainNodeRegistry& registry, const TgContext& ctx)
	{
		TgResult r;
		std::string error;
		if (!EvaluateTerrainGraph(g, registry, ctx, r, error)) { std::fprintf(stderr, "FAIL: evaluation: %s\n", error.c_str()); ++failures; return 0; }
		return TgHashResult(r);
	}

	uint64_t Cached(const TerrainGraph& g, const TerrainNodeRegistry& registry, const TgContext& ctx, TerrainGraphCache& cache)
	{
		TgResult r;
		std::string error;
		if (!EvaluateTerrainGraph(g, registry, ctx, r, error, &cache)) { std::fprintf(stderr, "FAIL: cached evaluation: %s\n", error.c_str()); ++failures; return 0; }
		return TgHashResult(r);
	}

	uint64_t WorldHash(const GeneratedWorld& w)
	{
		uint64_t h = kTgHashSeed;
		const Heightmap& hm = w.TerrainRef->GetHeightmap();
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
			for (uint32_t i = 0; i < hm.GetResolution(); ++i) { const float v = hm.At(i, j); h = TgHashBytes(&v, sizeof v, h); }
		if (!w.Maps.Biomes.empty()) h = TgHashBytes(w.Maps.Biomes.data(), w.Maps.Biomes.size(), h);
		for (const TreePlacement& t : w.Trees) h = TgHashBytes(&t.Position, sizeof t.Position, h);
		for (const RockPlacement& r : w.Rocks) h = TgHashBytes(&r.Position, sizeof r.Position, h);
		return h;
	}

	double Seconds(const std::chrono::steady_clock::time_point& since)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
	}

	// T84: the quick preview of the editor's panel, the graph alone at a low resolution (EvaluateWorldGraph), with a cache of its own: the whole
	// graph, and only what the published height needs (TerrainGraphUpstreamOf, what the panel evaluates when it previews the height).
	void BenchPreview(TerrainGraph g)
	{
		std::string heightOutput;
		for (const TgNode& n : g.Nodes) if (n.Type == "output" && n.Source == "height") heightOutput = n.Id;
		std::printf("quick preview (EvaluateWorldGraph, a cache per resolution):\n");
		for (const bool upstream : { false, true })
			for (const uint32_t resolution : { 129u, 257u, 513u })
			{
				WorldDesc desc;
				desc.Seed = 5;
				desc.Size = 3000.0f;
				desc.Resolution = resolution;
				desc.TreeBudget = 3000;
				desc.GraphCache = std::make_shared<TerrainGraphCache>();
				TerrainGraph edited = g;
				auto run = [&]()
				{
					desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(upstream ? TerrainGraphUpstreamOf(edited, { heightOutput }) : edited);
					TgResult r;
					std::string error;
					const auto t0 = std::chrono::steady_clock::now();
					if (!EvaluateWorldGraph(desc, r, error)) { std::fprintf(stderr, "FAIL: preview: %s\n", error.c_str()); ++failures; }
					return Seconds(t0);
				};
				const double first = run();
				NodeOf(edited, "rolling").Params["Amplitude"] += 5.0;
				const double initial = run();
				const TerrainGraphCache::Stats s = desc.GraphCache->LastStats();
				NodeOf(edited, "stones").Params["Count"] += 100.0;
				const double final = run();
				std::printf("  %-14s %4u x %-4u %2zu nodes: first %6.2f s | initial node changed %6.2f s (%d ran) | final node changed %5.2f s | cache %.1f MB\n",
					upstream ? "height only" : "whole graph", resolution, resolution, desc.TerrainGraphDefinition->Nodes.size(), first, initial, s.Ran, final,
					static_cast<double>(desc.GraphCache->Bytes()) / 1048576.0);
				std::vector<std::pair<double, std::string>> slow;
				for (const auto& [id, ms] : s.RunMs) slow.push_back({ ms, id });
				std::sort(slow.rbegin(), slow.rend());
				std::printf("        slowest:");
				for (size_t i = 0; i < slow.size() && i < 6; ++i) std::printf(" %s %.0f ms%s", slow[i].second.c_str(), slow[i].first, i + 1 < 6 ? "," : "");
				std::printf("\n");
			}
	}

	// T84: the full cache at 1025 x 1025 with a byte budget (what the editor's panel sets): its memory, and what a change costs then.
	void BenchBudget(TerrainGraph g, size_t budgetMb)
	{
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		TgContext ctx;
		ctx.Seed = 5;
		ctx.Size = 3000.0f;
		ctx.Resolution = 1025;
		TerrainGraphCache cache;
		cache.SetByteBudget(budgetMb << 20);
		auto evaluate = [&]()
		{
			TgResult r;
			std::string error;
			const auto t0 = std::chrono::steady_clock::now();
			EvaluateTerrainGraph(g, registry, ctx, r, error, &cache);
			return Seconds(t0);
		};
		std::printf("full cache at 1025 x 1025 with a budget of %zu MB:\n", budgetMb);
		auto line = [&](const char* what, double t)
		{
			const TerrainGraphCache::Stats s = cache.LastStats();
			std::printf("  %-42s %6.2f s (%d ran, %d reused, %d dropped) -> %.1f MB in %zu nodes\n", what, t, s.Ran, s.Reused, s.Evicted,
				static_cast<double>(cache.Bytes()) / 1048576.0, cache.Size());
		};
		line("first run", evaluate());
		line("nothing changed", evaluate());
		NodeOf(g, "stones").Params["Count"] += 100.0;
		line("final node changed (stones)", evaluate());
		NodeOf(g, "trees").Params["Count"] += 100.0;
		line("final node changed (trees)", evaluate());
		cache.Clear();
		std::printf("  after Clear (the panel closed): %.1f MB in %zu nodes\n", static_cast<double>(cache.Bytes()) / 1048576.0, cache.Size());
	}

	void Bench()
	{
		const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
		TerrainGraph g;
		MakeTerrainGraphPreset("highland_valley", g);
		TgContext ctx;
		ctx.Seed = 5;
		ctx.Size = 3000.0f;
		ctx.Resolution = 1025;
		auto evaluate = [&](TerrainGraphCache* cache)
		{
			TgResult r;
			std::string error;
			const auto t0 = std::chrono::steady_clock::now();
			EvaluateTerrainGraph(g, registry, ctx, r, error, cache);
			return Seconds(t0);
		};
		auto world = [&](const std::shared_ptr<TerrainGraphCache>& cache)
		{
			WorldDesc desc;
			desc.Seed = 5;
			desc.Size = 3000.0f;
			desc.Resolution = 1025;
			desc.TreeBudget = 3000;
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
			desc.GraphCache = cache;
			const auto t0 = std::chrono::steady_clock::now();
			const GeneratedWorld w = WorldGenerator::GenerateData(desc);
			return Seconds(t0);
		};
		TgNode& stones = NodeOf(g, "stones");
		TgNode& rolling = NodeOf(g, "rolling");

		std::printf("highland_valley 3000 m, 1025 x 1025, %zu nodes\n", g.Nodes.size());
		auto line = [](const char* what, double seconds, const TerrainGraphCache* cache)
		{
			if (cache) std::printf("  %-42s %6.2f s (%d ran, %d reused)\n", what, seconds, cache->LastStats().Ran, cache->LastStats().Reused);
			else std::printf("  %-42s %6.2f s\n", what, seconds);
		};
		std::printf("graph only (EvaluateTerrainGraph):\n");
		double t = evaluate(nullptr);
		line("no cache (any change: every node runs)", t, nullptr);
		TerrainGraphCache cache;
		t = evaluate(&cache);
		line("cache, first run (fills it)", t, &cache);
		t = evaluate(&cache);
		line("cache, nothing changed", t, &cache);
		stones.Params["Count"] += 100.0;
		t = evaluate(&cache);
		line("cache, final node changed (stones)", t, &cache);
		rolling.Params["Amplitude"] += 5.0;
		t = evaluate(&cache);
		line("cache, initial node changed (rolling)", t, &cache);

		std::printf("whole world (WorldGenerator::GenerateData):\n");
		t = world(nullptr);
		line("no cache (any change)", t, nullptr);
		const auto shared = std::make_shared<TerrainGraphCache>();
		t = world(shared);
		line("cache, first run", t, shared.get());
		stones.Params["Count"] += 100.0;
		t = world(shared);
		line("cache, final node changed (stones)", t, shared.get());
		rolling.Params["Amplitude"] += 5.0;
		t = world(shared);
		line("cache, initial node changed (rolling)", t, shared.get());
		std::printf("  cache memory after the world: %.1f MB in %zu nodes\n", static_cast<double>(shared->Bytes()) / 1048576.0, shared->Size());
		{
			std::vector<std::pair<double, std::string>> slow;
			for (const auto& [id, ms] : cache.LastStats().RunMs) slow.push_back({ ms, id });
			std::sort(slow.rbegin(), slow.rend());
			std::printf("  slowest nodes at 1025 (initial node changed):");
			for (size_t i = 0; i < slow.size() && i < 10; ++i) std::printf(" %s %.0f ms%s", slow[i].second.c_str(), slow[i].first, i + 1 < 10 ? "," : "");
			std::printf("\n");
		}
		BenchPreview(g);
	}
}

#include "terrain_graph_probe.inl"

int GBTest_terrain_graph_cache(int argc, char** argv)
{
	if (argc > 1 && std::string(argv[1]) == "probe") return Probe(argc, argv);
	if (argc > 1 && std::string(argv[1]) == "probe_export") return ProbeExport(argc, argv);
	if (argc > 1 && std::string(argv[1]) == "e2bench") return E2Bench(argc, argv);
	if (argc > 1 && std::string(argv[1]) == "hausdorff") return Hausdorff(argc, argv);
	if (const char* bench = std::getenv("GB_TGRAPH_BENCH"); bench && bench[0] != 0 && bench[0] != '0')
	{
		// 1: everything; preview: only the quick preview; budget[=MB]: only the full cache with a byte budget
		const std::string what = bench;
		TerrainGraph g;
		MakeTerrainGraphPreset("highland_valley", g);
		if (what == "preview") BenchPreview(g);
		else if (what.rfind("budget", 0) == 0) BenchBudget(g, what.size() > 7 ? static_cast<size_t>(std::atoi(what.c_str() + 7)) : 128);
		else Bench();
		return 0;
	}

	const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
	TgContext ctx;
	ctx.Seed = 5;
	ctx.Resolution = 65;
	ctx.Size = 3000.0f;

	TerrainGraph g;
	MakeTerrainGraphPreset("highland_valley", g);
	const int nodes = static_cast<int>(g.Nodes.size());
	TerrainGraphCache cache;

	Check(Cached(g, registry, ctx, cache) == Fresh(g, registry, ctx), "a first evaluation with a cache is the evaluation without one");
	Check(cache.LastStats().Ran == nodes && cache.LastStats().Reused == 0 && static_cast<int>(cache.Size()) == nodes, "it ran every node and keeps them all");
	const uint64_t generation = cache.Generation();

	Check(Cached(g, registry, ctx, cache) == Fresh(g, registry, ctx), "nothing changed: the same result");
	Check(cache.LastStats().Ran == 0 && cache.LastStats().Reused == nodes, "and no node ran again");
	Check(cache.Generation() > generation, "every evaluation moves the generation (a preview knows to refresh)");

	// a final node: only it (and the output that publishes it) run again
	NodeOf(g, "stones").Params["Count"] += 100.0;
	Check(Cached(g, registry, ctx, cache) == Fresh(g, registry, ctx), "a final node changed: the cached result is the fresh one");
	Check(cache.LastStats().Ran >= 1 && cache.LastStats().Ran <= 3, "only the changed node and what reads it ran (" + std::to_string(cache.LastStats().Ran) + ")");

	// an initial node: everything downstream of it runs again, and only that
	NodeOf(g, "rolling").Params["Amplitude"] += 5.0;
	Check(Cached(g, registry, ctx, cache) == Fresh(g, registry, ctx), "an initial node changed: the cached result is the fresh one");
	const int ranInitial = cache.LastStats().Ran;
	Check(ranInitial > nodes / 2 && cache.LastStats().Reused > 0, "the nodes downstream ran again, the ones beside it did not (" + std::to_string(ranInitial) + " of " + std::to_string(nodes) + ")");

	// what the editor changes without changing the result: label and position
	NodeOf(g, "rolling").Label = "renamed";
	NodeOf(g, "rolling").X += 50.0f;
	Cached(g, registry, ctx, cache);
	Check(cache.LastStats().Ran == 0, "a node's label or position does not invalidate it");

	// the context: another seed is another world
	TgContext other = ctx;
	other.Seed = 6;
	Check(Cached(g, registry, other, cache) == Fresh(g, registry, other), "another seed: the fresh result");
	Check(cache.LastStats().Ran == nodes, "and every node ran");

	// a node removed: its entry goes; a preview reads a node's output
	TgValue value;
	Check(cache.TryCopyOutput("rolling", "height", value) && value.Kind == TgPortKind::Field && value.Field.N == 65, "an output can be copied out for a preview");
	Check(!cache.OutputPorts("trees").empty(), "the ports a node produced are listed");
	TerrainGraph smaller;
	MakeTerrainGraphPreset("temperate_valley", smaller);
	Check(Cached(smaller, registry, ctx, cache) == Fresh(smaller, registry, ctx), "another graph through the same cache: its fresh result");
	Check(!cache.TryCopyOutput("rolling", "height", value), "the nodes of the previous graph are dropped");

	// a world built with the cache is the world built without it (WorldDesc::GraphCache), and a second build reuses the graph
	{
		WorldDesc desc;
		desc.Seed = 5;
		desc.Size = 180.0f;
		desc.Resolution = 129;
		desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(smaller);
		const uint64_t plain = WorldHash(WorldGenerator::GenerateData(desc));
		desc.GraphCache = std::make_shared<TerrainGraphCache>();
		Check(WorldHash(WorldGenerator::GenerateData(desc)) == plain, "a world built through a cache is the world built without one");
		Check(WorldHash(WorldGenerator::GenerateData(desc)) == plain && desc.GraphCache->LastStats().Ran == 0, "built again, it is the same and no node ran");
	}

	// T84: only what a port needs (TerrainGraphUpstreamOf, the editor's quick preview): the same outputs for that node, fewer nodes run
	{
		TerrainGraph h;
		MakeTerrainGraphPreset("highland_valley", h);
		std::string heightOutput;
		for (const TgNode& n : h.Nodes) if (n.Type == "output" && n.Source == "height") heightOutput = n.Id;
		const TerrainGraph up = TerrainGraphUpstreamOf(h, { heightOutput, "no_such_node" });
		Check(!up.Nodes.empty() && up.Nodes.size() < h.Nodes.size() && up.FindNode(heightOutput) && up.FindNode("rolling") && !up.FindNode("stones"),
			"the height's upstream keeps its ancestors and leaves the scatters out (" + std::to_string(up.Nodes.size()) + " of " + std::to_string(h.Nodes.size()) + ")");
		TgResult whole, part;
		std::string error;
		Check(EvaluateTerrainGraph(h, registry, ctx, whole, error) && EvaluateTerrainGraph(up, registry, ctx, part, error), "both evaluate (" + error + ")");
		Check(TgHashField(whole.Outputs["height"]) == TgHashField(part.Outputs["height"]), "the published height is the same bit for bit");
		Check(TerrainGraphUpstreamOf(h, {}).Nodes.empty(), "nothing asked: an empty graph");
	}

	// T84: the memory. Bytes() counts what is stored; a byte budget drops entries after each evaluation but never changes a result; the pinned
	// node (the editor's preview) stays; Clear frees everything.
	{
		TerrainGraph h;
		MakeTerrainGraphPreset("highland_valley", h);
		TerrainGraphCache all, capped;
		Cached(h, registry, ctx, all);
		const size_t full = all.Bytes();
		Check(full > static_cast<size_t>(nodes) * 65 * 65 * sizeof(float) / 2, "the cache counts the samples it keeps (" + std::to_string(full) + " bytes)");
		const size_t budget = full / 4;
		capped.SetByteBudget(budget);
		capped.SetPinned("rolling");
		for (int round = 0; round < 3; ++round)
		{
			if (round == 1) NodeOf(h, "stones").Params["Count"] += 50.0;
			if (round == 2) NodeOf(h, "rolling").Params["Amplitude"] += 3.0;
			Check(Cached(h, registry, ctx, capped) == Fresh(h, registry, ctx), "with a budget a quarter of its size the result is the fresh one (round " + std::to_string(round) + ")");
			Check(capped.Bytes() <= budget + all.Bytes() / static_cast<size_t>(nodes) * 4 && capped.LastStats().Evicted > 0, "and it keeps to the budget, dropping entries");
			Check(capped.TryCopyOutput("rolling", "height", value), "the pinned node is kept");
		}
		Check(capped.LastStats().Ms > 0.0 && !capped.LastStats().RunMs.empty(), "the evaluation is timed, node by node");
		capped.Clear();
		Check(capped.Bytes() == 0 && capped.Size() == 0, "Clear frees every entry");
		all.SetByteBudget(1);
		Check(all.Bytes() <= TerrainGraphCache::BytesOf(value) * 8 || all.Size() <= 1, "a budget set later applies at once");
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("terrain_graph_cache: all checks passed\n");
	return failures ? 1 : 0;
}
