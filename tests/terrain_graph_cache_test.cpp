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
	}
}

int GBTest_terrain_graph_cache(int, char**)
{
	if (const char* bench = std::getenv("GB_TGRAPH_BENCH"); bench && bench[0] == '1') { Bench(); return 0; }

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

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("terrain_graph_cache: all checks passed\n");
	return failures ? 1 : 0;
}
