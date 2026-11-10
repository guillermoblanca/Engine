// GB_WorldGen on its own (docs/GRAFO_TERRENO.md, section 6): this test links GB::WorldGen and nothing above it (no GB_Sim, no GL), so it
// building is the proof that the terrain graph, its nodes, the heightmap, the erosions, the water and the scatter need nothing else. It
// evaluates every preset at a low resolution, with and without the per-node cache, and through JSON and back.
#include "GB/World/Graph/TerrainGraphCache.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Heightmap.h"
#include <cstdio>
#include <string>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
}

int GBTest_worldgen_graph(int, char**)
{
	const TerrainGraphLibrary library;
	TerrainGraphCache cache;
	for (const std::string& id : library.Ids())
	{
		const TerrainGraph& graph = *library.Find(id);
		TgContext ctx;
		ctx.Seed = 7;
		ctx.Resolution = 65;
		ctx.Size = id == "highland_valley" ? 3000.0f : 300.0f;
		TgResult plain, cached, read;
		std::string error;
		Check(EvaluateTerrainGraph(graph, library.Registry(), ctx, plain, error), id + " evaluates (" + error + ")");
		Check(EvaluateTerrainGraph(graph, library.Registry(), ctx, cached, error, &cache) && TgHashResult(cached) == TgHashResult(plain), id + ": the same through the cache");
		TerrainGraph back;
		Check(TerrainGraphFromJson(TerrainGraphToJson(graph), back).empty(), id + ": reads back from JSON");
		Check(EvaluateTerrainGraph(back, library.Registry(), ctx, read, error) && TgHashResult(read) == TgHashResult(plain), id + ": the same from its JSON");

		Heightmap ground(ctx.Resolution, ctx.Size);
		HeightmapTerrainTarget target(ground);
		Check(ApplyTerrainGraph(plain, "height", target, error), id + ": its height goes into a Heightmap (" + error + ")");
		Check(ground.GetMaxHeight() > ground.GetMinHeight(), id + ": and it is not flat");
	}
	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("worldgen_graph: all checks passed\n");
	return failures ? 1 : 0;
}
