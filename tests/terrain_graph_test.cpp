// The terrain graph (docs/GRAFO_TERRENO.md), headless: validation (duplicate ids, unknown types, cycles),
// compiling to a topological order, evaluating a noise -> erosion -> biome pipeline (reusing GB::HydraulicErosion
// as-is), determinism for a seed, and applying a result to both an in-memory target and a real GB::Heightmap.
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"
#include <cmath>
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
	const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();   // injected: there is no global registry

	// ---------------------------------------------------------------- validation
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "heightfield_noise"), Node("a", "erosion_hydraulic") };   // duplicate id
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g, registry);
		Check(HasTgErrors(issues), "duplicate node id is rejected");
	}
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "not_a_real_type") };
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g, registry);
		Check(HasTgErrors(issues), "unknown node type is rejected");
	}
	{
		TerrainGraph g;
		g.Nodes = { Node("a", "heightfield_noise"), Node("b", "erosion_hydraulic") };
		g.Edges = { { "a", "b" }, { "b", "a" } };   // a -> b -> a
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g, registry);
		Check(HasTgErrors(issues), "a cycle is rejected");
		Check(TgIssuesText(issues).find("cycle") != std::string::npos, "the cycle message names it as such");
	}
	{
		TerrainGraph g = MakeGraph();
		const std::vector<TgIssue> issues = ValidateTerrainGraph(g, registry);
		Check(!HasTgErrors(issues), "the noise -> erosion -> biome graph has no structural errors");
	}

	// ---------------------------------------------------------------- compile
	{
		TerrainGraph g = MakeGraph();
		CompiledTerrainGraph compiled;
		const bool ok = CompileTerrainGraph(g, registry, compiled);
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
		const bool ok = EvaluateTerrainGraph(MakeGraph(), registry, MakeContext(7), result, error);
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
		Check(EvaluateTerrainGraph(noiseOnly, registry, ctx, rawResult, error), "the noise-only graph evaluates");

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
		Check(EvaluateTerrainGraph(thermalOnly, registry, ctx, thermalResult, error), "the noise -> erosion_thermal graph evaluates");
		const TgField& thermal = thermalResult.Outputs.at("height");
		bool thermalDifferent = false;
		for (size_t i = 0; i < raw.Values.size(); ++i) if (raw.Values[i] != thermal.Values[i]) { thermalDifferent = true; break; }
		Check(thermalDifferent, "erosion_thermal changes the height field it was given");
	}

	// ---------------------------------------------------------------- determinism
	{
		TgResult again;
		std::string error;
		Check(EvaluateTerrainGraph(MakeGraph(), registry, MakeContext(7), again, error), "the graph re-evaluates");
		Check(FieldFingerprint(result.Outputs.at("height")) == FieldFingerprint(again.Outputs.at("height")),
			"the same seed gives the same height field (erosion included)");

		TgResult different;
		Check(EvaluateTerrainGraph(MakeGraph(), registry, MakeContext(8), different, error), "a different seed also evaluates");
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

	// ---------------------------------------------------------------- flow_accumulation
	{
		TerrainGraph g;
		g.Nodes = {
			Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 18.0 } }),
			Node("flow", "flow_accumulation"),
			Node("out", "output", "flow"),
		};
		g.Edges = { { "noise", "flow" }, { "flow", "out" } };
		TgResult flowResult;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, MakeContext(9), flowResult, error), ("noise -> flow_accumulation evaluates: " + error).c_str());
		const TgField& flow = flowResult.Outputs.at("flow");
		bool everyCellCountsItself = true, someCellCollectsMore = false;
		for (float v : flow.Values)
		{
			if (v < 1.0f) everyCellCountsItself = false;
			if (v > 1.0f) someCellCollectsMore = true;
		}
		Check(everyCellCountsItself && someCellCollectsMore, "flow_accumulation's field is at least 1 everywhere and above 1 somewhere");
	}

	// ---------------------------------------------------------------- scatter
	{
		TerrainGraph g;
		g.Nodes = {
			Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 18.0 } }),
			Node("spots", "scatter", "", { { "Count", 40.0 }, { "MinSpacing", 6.0 } }),
		};
		g.Edges = { { "noise", "spots" } };
		TgResult scatterResult;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, MakeContext(11), scatterResult, error), ("noise -> scatter evaluates: " + error).c_str());
		Check(scatterResult.Placements.count("spots") == 1, "scatter's placements are keyed by the node's id");
		const TgPlacements& spots = scatterResult.Placements.at("spots");
		Check(!spots.Items.empty() && spots.Items.size() <= 40, "scatter places at least one spot and never more than Count");
		bool withinBounds = true;
		for (const TgPlacement& p : spots.Items)
			if (std::fabs(p.Position.x) > 64.0f + 1e-3f || std::fabs(p.Position.y) > 64.0f + 1e-3f) withinBounds = false;
		Check(withinBounds, "every spot lands inside the field it was scattered over");
		bool spacingOk = true;
		for (size_t i = 0; i < spots.Items.size() && spacingOk; ++i)
			for (size_t k = i + 1; k < spots.Items.size(); ++k)
				if (glm::distance(spots.Items[i].Position, spots.Items[k].Position) < 6.0f - 1e-3f) { spacingOk = false; break; }
		Check(spacingOk, "no two spots are closer than MinSpacing");
	}

	// ---------------------------------------------------------------- registries are independent values
	{
		TerrainNodeRegistry custom = MakeDefaultTerrainNodeRegistry();
		Check(custom.IsKnown("output") && custom.IsKnown("heightfield_noise"), "the default registry knows the builtin nodes, output included");
		Check(custom.Unregister("scatter") && !custom.IsKnown("scatter"), "a node type can be unregistered at run time");
		Check(!custom.Unregister("scatter"), "unregistering an unknown type reports false");

		TerrainGraph g;
		g.Nodes = { Node("noise", "heightfield_noise"), Node("spots", "scatter") };
		g.Edges = { { "noise", "spots" } };
		Check(HasTgErrors(ValidateTerrainGraph(g, custom)), "a registry without 'scatter' rejects a graph that uses it");
		Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), "another registry is unaffected by that removal");

		struct ConstantNode final : ITerrainNode
		{
			bool Evaluate(const TgNode&, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const override
			{
				out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
				return true;
			}
		};
		TerrainNodeRegistry plugin = MakeDefaultTerrainNodeRegistry();
		plugin.Register("plugin_constant", [] { return std::make_unique<ConstantNode>(); });
		TerrainGraph pg;
		pg.Nodes = { Node("c", "plugin_constant"), Node("o", "output", "height") };
		pg.Edges = { { "c", "o" } };
		TgResult pluginResult;
		std::string pluginError;
		Check(EvaluateTerrainGraph(pg, plugin, MakeContext(1), pluginResult, pluginError), ("a node registered at run time evaluates: " + pluginError).c_str());
		Check(!registry.IsKnown("plugin_constant"), "a node registered into one registry does not leak into another");
	}

	if (failures == 0) std::printf("terrain_graph_test: OK\n");
	else std::fprintf(stderr, "terrain_graph_test: %d failure(s)\n", failures);
	return failures;
}
