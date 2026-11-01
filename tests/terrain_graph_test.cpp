// The terrain graph (docs/GRAFO_TERRENO.md), headless: validation (duplicate ids, unknown types, cycles),
// compiling to a topological order, evaluating a noise -> erosion -> biome pipeline (reusing GB::HydraulicErosion
// as-is), determinism for a seed, and applying a result to both an in-memory target and a real GB::Heightmap.
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"
#include <cstdio>
#include <set>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

namespace
{
	TgNode Node(const char* id, const char* type, const char* source = "", std::map<std::string, double> params = {})
	{
		TgNode n;
		n.Id = id; n.Type = type; n.Source = source; n.Params = std::move(params);
		return n;
	}

	// noise -> erosion -> biome_classify, with two outputs ("height" off the erosion node, "biome" off the
	// classifier). Small resolution and a light erosion pass: the test cares about wiring and determinism,
	// not about producing a game-ready terrain.
	TerrainGraph MakeGraph()
	{
		TerrainGraph g;
		g.Id = "test";
		g.Nodes = {
			Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 18.0 } }),
			Node("erosion", "erosion_hydraulic", "", { { "Droplets", 3000.0 } }),
			Node("biome", "biome_classify", "", { { "WaterLevel", 0.0 }, { "ForestMoisture", 0.5 } }),
			Node("out_height", "output", "height"),
			Node("out_biome", "output", "biome"),
		};
		g.Edges = {
			{ "noise", "erosion" },
			{ "erosion", "biome" },
			{ "erosion", "out_height" },
			{ "biome", "out_biome" },
		};
		return g;
	}

	TgContext MakeContext(uint32_t seed)
	{
		TgContext ctx;
		ctx.Seed = seed;
		ctx.Resolution = 65;
		ctx.Size = 128.0f;
		return ctx;
	}

	uint64_t FieldFingerprint(const TgField& field)
	{
		Qa::Fingerprint fp;
		for (float v : field.Values) fp.Add(v);
		return fp.Value();
	}
}

int main()
{
	Log::Init();

	// ---------------------------------------------------------------- validation
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "heightfield_noise"), Node("a", "erosion_hydraulic") };   // duplicate id
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g);
		Check(HasTgErrors(issues), "duplicate node id is rejected");
	}
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "not_a_real_type") };
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g);
		Check(HasTgErrors(issues), "unknown node type is rejected");
	}
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "heightfield_noise"), Node("b", "erosion_hydraulic") };
		g.Edges = { { "a", "b" }, { "b", "a" } };   // a -> b -> a
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g);
		Check(HasTgErrors(issues), "a cycle is rejected");
		Check(TgIssuesText(issues).find("cycle") != std::string::npos, "the cycle message names it as such");
	}
	{
		TerrainGraph g = MakeGraph();
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g);
		Check(!HasTgErrors(issues), "the noise -> erosion -> biome graph has no structural errors");
	}

	// ---------------------------------------------------------------- compile
	{
		TerrainGraph g = MakeGraph();
		CompiledTerrainGraph compiled;
		const bool ok = CompileTerrainGraph(g, compiled);
		Check(ok, "the graph compiles");
		Check(compiled.Order.size() == g.Nodes.size(), "every node gets an order position");
		// "noise" must come before "erosion", which must come before "biome" and "out_height"; "biome"
		// before "out_biome".
		auto position = [&](const char* id)
		{
			for (size_t i = 0; i < compiled.Order.size(); ++i) if (compiled.Nodes[compiled.Order[i]].Id == id) return i;
			return compiled.Order.size();
		};
		Check(position("noise") < position("erosion"), "noise runs before erosion");
		Check(position("erosion") < position("biome"), "erosion runs before biome_classify");
		Check(position("erosion") < position("out_height"), "erosion runs before its output");
		Check(position("biome") < position("out_biome"), "biome_classify runs before its output");
	}

	// ---------------------------------------------------------------- evaluate
	TgResult result;
	{
		std::string error;
		const bool ok = EvaluateTerrainGraph(MakeGraph(), MakeContext(7), result, error);
		Check(ok, ("the graph evaluates: " + error).c_str());
		Check(result.Outputs.count("height") == 1, "the height output is produced");
		Check(result.Outputs.count("biome") == 1, "the biome output is produced");
		const TgField& height = result.Outputs.at("height");
		const TgField& biome = result.Outputs.at("biome");
		Check(height.N == 65 && biome.N == 65, "both outputs have the context's resolution");

		std::set<int> distinctBiomes;
		for (float v : biome.Values) distinctBiomes.insert(static_cast<int>(v));
		Check(distinctBiomes.size() >= 2, "biome_classify's fallback moisture produces more than one biome");
	}

	// ---------------------------------------------------------------- erosion actually changed the field
	{
		TgContext ctx = MakeContext(7);
		TgResult rawResult;
		std::string error;
		TerrainGraph noiseOnly;
		noiseOnly.Nodes = { Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 18.0 } }), Node("out", "output", "height") };
		noiseOnly.Edges = { { "noise", "out" } };
		Check(EvaluateTerrainGraph(noiseOnly, ctx, rawResult, error), "the noise-only graph evaluates");

		const TgField& raw = rawResult.Outputs.at("height");
		const TgField& eroded = result.Outputs.at("height");
		bool anyDifferent = false;
		for (size_t i = 0; i < raw.Values.size(); ++i) if (raw.Values[i] != eroded.Values[i]) { anyDifferent = true; break; }
		Check(anyDifferent, "erosion_hydraulic changes the height field it was given");

		// erosion_thermal, wired straight after the same noise (not through erosion_hydraulic, to test it alone)
		TerrainGraph thermalOnly;
		thermalOnly.Nodes = {
			Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 18.0 } }),
			Node("thermal", "erosion_thermal", "", { { "Iterations", 10.0 }, { "TalusAngleDegrees", 15.0 } }),
			Node("out", "output", "height"),
		};
		thermalOnly.Edges = { { "noise", "thermal" }, { "thermal", "out" } };
		TgResult thermalResult;
		Check(EvaluateTerrainGraph(thermalOnly, ctx, thermalResult, error), "the noise -> erosion_thermal graph evaluates");
		const TgField& thermal = thermalResult.Outputs.at("height");
		bool thermalDifferent = false;
		for (size_t i = 0; i < raw.Values.size(); ++i) if (raw.Values[i] != thermal.Values[i]) { thermalDifferent = true; break; }
		Check(thermalDifferent, "erosion_thermal changes the height field it was given");
	}

	// ---------------------------------------------------------------- determinism
	{
		TgResult again;
		std::string error;
		Check(EvaluateTerrainGraph(MakeGraph(), MakeContext(7), again, error), "the graph re-evaluates");
		Check(FieldFingerprint(result.Outputs.at("height")) == FieldFingerprint(again.Outputs.at("height")),
			"the same seed gives the same height field (erosion included)");

		TgResult different;
		Check(EvaluateTerrainGraph(MakeGraph(), MakeContext(8), different, error), "a different seed also evaluates");
		Check(FieldFingerprint(result.Outputs.at("height")) != FieldFingerprint(different.Outputs.at("height")),
			"a different seed gives a different height field");
	}

	// ---------------------------------------------------------------- apply: in-memory target
	{
		InMemoryTerrainTarget target;
		target.Resize(65, 128.0f, { 0.0f, 0.0f });
		std::string error;
		const bool ok = ApplyTerrainGraph(result, "height", target, error);
		Check(ok, ("applying to an in-memory target succeeds: " + error).c_str());
		const TgField& height = result.Outputs.at("height");
		bool matches = true;
		for (uint32_t j = 0; j < height.N; ++j)
			for (uint32_t i = 0; i < height.N; ++i)
				if (target.GetHeight(i, j) != height.At(i, j)) matches = false;
		Check(matches, "the in-memory target holds exactly the graph's height output");

		std::string missing;
		Check(!ApplyTerrainGraph(result, "no_such_slot", target, missing), "applying an unknown output slot fails");
	}

	// ---------------------------------------------------------------- apply: a real Heightmap
	{
		Heightmap heightmap(65, 128.0f);
		HeightmapTerrainTarget target(heightmap);
		std::string error;
		const bool ok = ApplyTerrainGraph(result, "height", target, error);
		Check(ok, ("applying to a real Heightmap succeeds: " + error).c_str());
		const TgField& height = result.Outputs.at("height");
		bool matches = true;
		for (uint32_t j = 0; j < height.N; ++j)
			for (uint32_t i = 0; i < height.N; ++i)
				if (heightmap.At(i, j) != height.At(i, j)) matches = false;
		Check(matches, "the wrapped Heightmap holds exactly the graph's height output");
	}

	if (failures == 0) std::printf("terrain_graph_test: OK\n");
	else std::fprintf(stderr, "terrain_graph_test: %d failure(s)\n", failures);
	return failures;
}
