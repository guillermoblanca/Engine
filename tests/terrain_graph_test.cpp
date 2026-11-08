// The terrain graph (docs/GRAFO_TERRENO.md), headless: validation (duplicate ids, unknown types, cycles),
// compiling to a topological order, evaluating a noise -> erosion -> biome pipeline (reusing GB::HydraulicErosion
// as-is), determinism for a seed, and applying a result to both an in-memory target and a real GB::Heightmap.
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include "GB/World/Graph/ITerrainTarget.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Terrain.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
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

	// ---------------------------------------------------------------- the graph is the ground of a generated world
	// WorldDesc::TerrainGraphDefinition used to reach only the throw-away terrain the water is planned on: the final terrain was rebuilt from the
	// hills' own noise (and eroded again). Now the graph's "height" output is the ground, and the world's water is carved into it.
	{
		auto graph = std::make_shared<TerrainGraph>();
		graph->Nodes = { Node("noise", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 12.0 } }), Node("out", "output", "height") };
		graph->Edges = { { "noise", "out" } };
		auto expected = [&](const WorldDesc& desc)
		{
			TgContext ctx;
			ctx.Seed = desc.Seed;
			ctx.Resolution = desc.Resolution;
			ctx.Size = desc.Size;
			TgResult result;
			std::string error;
			const bool ok = EvaluateTerrainGraph(*graph, registry, ctx, result, error);
			Check(ok, ("the world graph evaluates on its own: " + error).c_str());
			return ok ? result.Outputs["height"] : TgField();
		};
		auto share = [](const Heightmap& hm, const TgField& field)   // share of the samples where the world's ground is the graph's
		{
			if (field.N != hm.GetResolution()) return 0.0f;
			size_t same = 0;
			for (uint32_t j = 0; j < field.N; ++j)
				for (uint32_t i = 0; i < field.N; ++i) same += hm.At(i, j) == field.At(i, j) ? 1u : 0u;
			return static_cast<float>(same) / static_cast<float>(field.Values.size());
		};
		for (const WorldType type : { WorldType::Temperate, WorldType::Desert })
		{
			WorldDesc desc;
			desc.Type = type;
			desc.Seed = 9;
			desc.Size = 128.0f;
			desc.Resolution = 65;
			desc.Lake = false;
			desc.River = false;
			desc.Erosion = true;   // the graph owns its erosion: the built-in pass must not run on top of it
			desc.TreeBudget = 20;
			desc.RockBudget = 5;
			desc.TerrainGraphDefinition = graph;
			const GeneratedWorld world = WorldGenerator::GenerateData(desc);
			Check(share(world.TerrainRef->GetHeightmap(), expected(desc)) == 1.0f, type == WorldType::Desert
				? "without water, a desert's ground is exactly the graph's height output"
				: "without water, a world's ground is exactly the graph's height output (no rebuilt hills, no second erosion)");
		}
		{
			WorldDesc desc;
			desc.Seed = 9;
			desc.Size = 128.0f;
			desc.Resolution = 65;
			desc.TreeBudget = 20;
			desc.RockBudget = 5;
			desc.TerrainGraphDefinition = graph;
			const GeneratedWorld world = WorldGenerator::GenerateData(desc);
			// the graph owns the world: it published no water, so there is none (WorldDesc::Lake/River only drive the built-in generator)
			Check(share(world.TerrainRef->GetHeightmap(), expected(desc)) == 1.0f, "a graph that publishes no water: its ground is left exactly as it is");
			Check(!world.TerrainRef->HasLake() && !world.TerrainRef->GetRiver(), "a graph that publishes no water: no lake nor river is dug into it");
		}
	}

	// ---------------------------------------------------------------- ports by name (graph version 2)
	{
		const TgNodeSchema* river = registry.Schema("river_trace");
		Check(river && river->InputIndex("height") == 0 && river->InputIndex("flow") == 1 && river->InputIndex("outlets") == 2, "river_trace describes its inputs in the order it reads them");
		// only the optional input is connected: an error, not a silent shift of the outlet mask into the height slot
		TerrainGraph g;
		g.Nodes = { Node("noise", "heightfield_noise"), Node("site", "lake_site"), Node("river", "river_trace") };
		g.Edges = { { "noise", "site", "height" }, { "site", "river", "outlets", "outlets" } };
		const std::string text = TgIssuesText(ValidateTerrainGraph(g, registry));
		Check(text.find("input 'height' is not connected") != std::string::npos && text.find("input 'flow' is not connected") != std::string::npos,
			"a required input left unconnected is reported by name");
		// a placements output into a field input
		TerrainGraph k;
		k.Nodes = { Node("noise", "heightfield_noise"), Node("spots", "scatter"), Node("erode", "erosion_hydraulic") };
		k.Edges = { { "noise", "spots", "height" }, { "spots", "erode", "height", "placements" } };
		Check(TgIssuesText(ValidateTerrainGraph(k, registry)).find("takes field, but 'spots.placements' gives placements") != std::string::npos, "kinds must agree across an edge");
		// two edges into one input, an output that does not exist
		TerrainGraph d;
		d.Nodes = { Node("a", "heightfield_noise"), Node("b", "heightfield_noise"), Node("e", "erosion_hydraulic") };
		d.Edges = { { "a", "e", "height" }, { "b", "e", "height" }, { "a", "e", "mask", "nope" } };
		const std::string dt = TgIssuesText(ValidateTerrainGraph(d, registry));
		Check(dt.find("two edges feed input 'height'") != std::string::npos && dt.find("no output named 'nope'") != std::string::npos, "duplicate inputs and unknown outputs are rejected");
		// an output named by its port, a node with several outputs: lake_site's site path into lake_carve, its outlet mask into depression_fill
		TerrainGraph m;
		m.Nodes = { Node("noise", "heightfield_noise", "", { { "Amplitude", 10.0 } }), Node("site", "lake_site"), Node("lake", "lake_carve"),
			Node("fill", "depression_fill"), Node("water", "output", "water"), Node("h", "output", "height") };
		m.Edges = { { "noise", "site", "height" }, { "noise", "lake", "height" }, { "site", "lake", "site", "site" }, { "lake", "fill", "height", "height" },
			{ "site", "fill", "outlets", "outlets" }, { "lake", "water", "in", "water" }, { "fill", "h", "in" } };
		TgResult mr;
		std::string me;
		Check(EvaluateTerrainGraph(m, registry, MakeContext(4), mr, me), ("several outputs per node, chosen by name: " + me).c_str());
		Check(mr.WaterOutputs.count("water") == 1 && mr.WaterOutputs.at("water").HasLake(), "an output node given water publishes it as water");
		Check(mr.Nodes.count("site") == 1 && mr.Nodes.at("site").count("outlets") == 1 && mr.Nodes.at("site").count("site") == 1, "every output of every node is kept for the editor");
	}

	// ---------------------------------------------------------------- every node describes itself
	for (const std::string& type : registry.KnownTypes())
	{
		const TgNodeSchema* s = registry.Schema(type);
		bool ok = s != nullptr;
		std::set<std::string> names;
		if (s) for (const TgPortInfo& p : s->Inputs) ok = ok && names.insert("in:" + p.Name).second;
		if (s) for (const TgPortInfo& p : s->Outputs) ok = ok && names.insert("out:" + p.Name).second;
		if (s) for (const TgParamInfo& p : s->Params) ok = ok && p.Min <= p.Max;
		Check(ok, ("node '" + type + "' has a schema with distinct ports and sane ranges").c_str());
	}

	// ---------------------------------------------------------------- "hills" is the built-in ground, bit for bit
	{
		HillParams hp;
		hp.Seed = 12;
		hp.SurfaceMaps = false;
		const auto terrain = Terrain::CreateHills(hp, 128.0f, 65, TerrainStyle::Meadow, false);
		TerrainGraph g;
		g.Nodes = { Node("hills", "hills", "", { { "Height", 14.0 }, { "Scale", 140.0 }, { "Octaves", 5.0 }, { "Flatten", 0.25 } }), Node("out", "output", "height") };
		g.Edges = { { "hills", "out" } };
		TgResult r;
		std::string e;
		bool same = EvaluateTerrainGraph(g, registry, MakeContext(12), r, e);
		const TgField& f = r.Outputs["height"];
		for (uint32_t j = 0; same && j < f.N; ++j)
			for (uint32_t i = 0; same && i < f.N; ++i) same = f.At(i, j) == terrain->GetHeightmap().At(i, j);
		Check(same, "the hills node rebuilds Terrain::CreateHills' ground exactly");
	}

	// ---------------------------------------------------------------- the presets, through the world generator
	{
		Check(TerrainGraphPresetNames().size() >= 2, "the engine ships terrain graph presets");
		for (const std::string& name : TerrainGraphPresetNames())
		{
			TerrainGraph preset;
			Check(MakeTerrainGraphPreset(name, preset), ("preset '" + name + "' exists").c_str());
			Check(!HasTgErrors(ValidateTerrainGraph(preset, registry)), ("preset '" + name + "' is valid: " + TgIssuesText(ValidateTerrainGraph(preset, registry))).c_str());
		}
		auto worldOf = [&](const char* name, WorldType type)
		{
			auto graph = std::make_shared<TerrainGraph>();
			MakeTerrainGraphPreset(name, *graph);
			WorldDesc desc;
			desc.Type = type;
			desc.Seed = 5;
			desc.Size = 180.0f;
			desc.Resolution = 129;
			desc.TerrainGraphDefinition = graph;
			return WorldGenerator::GenerateData(desc);
		};
		{
			const GeneratedWorld valley = worldOf("temperate_valley", WorldType::Temperate);
			const Terrain& t = *valley.TerrainRef;
			Check(t.HasLake() && t.GetRiver() != nullptr, "temperate_valley: the graph's lake and river are the world's water");
			Check(t.GetHeightmap().GetHeight(t.GetLakeCenter().x, t.GetLakeCenter().y) < t.GetWaterLevel() - 0.5f, "temperate_valley: the lake is dug below its surface");
			Check(valley.BiomeField.N == 129 && valley.MoistureField.N == 129, "temperate_valley: the world keeps the graph's biome and moisture");
			Check(valley.BiomeAt(t.GetLakeCenter().x, t.GetLakeCenter().y) == Biome::Water, "temperate_valley: the graph's biome has the lake as water");
			Check(!valley.Trees.empty() && !valley.Rocks.empty(), "temperate_valley: trees and rocks come from the graph's scatter nodes");
			size_t forested = 0;   // (the scatter reads the mask bilinearly, the baked map by nearest sample: a tree on the forest's edge may straddle them)
			for (const TreePlacement& tree : valley.Trees) forested += valley.BiomeAt(tree.Position.x, tree.Position.z) == Biome::Forest ? 1u : 0u;
			Check(forested * 10 >= valley.Trees.size() * 9, "temperate_valley: the trees stand in the forest the graph's biome mask allowed");
			Check(t.GetSurfaceLayers().size() == 1 && t.GetSurfaceLayers()[0].Kind == SurfaceLayerKind::Rock, "temperate_valley: its rock layer reaches the terrain");
		}
		{
			const GeneratedWorld peaks = worldOf("alpine_peaks", WorldType::Alpine);
			const Heightmap& hm = peaks.TerrainRef->GetHeightmap();
			Check(hm.GetMaxHeight() - hm.GetMinHeight() > 60.0f, "alpine_peaks: a relief of mountains, not hills");
			Check(!peaks.TerrainRef->HasLake() && peaks.Trees.empty() && !peaks.Rocks.empty(), "alpine_peaks: no water, no trees, some boulders");
			const auto& layers = peaks.TerrainRef->GetSurfaceLayers();
			Check(layers.size() == 2 && layers[0].Kind == SurfaceLayerKind::Rock && layers[1].Kind == SurfaceLayerKind::Snow, "alpine_peaks: rock and snow layers, snow painted last");
			if (layers.size() == 2)
			{
				// snow lies on gentle ground and leaves the steep faces bare
				double flatSnow = 0.0, steepSnow = 0.0; int flat = 0, steep = 0;
				for (uint32_t j = 1; j + 1 < hm.GetResolution(); ++j)
					for (uint32_t i = 1; i + 1 < hm.GetResolution(); ++i)
					{
						const glm::vec2 p = hm.SamplePosition(i, j);
						const float ny = hm.GetNormal(p.x, p.y).y;
						const float snow = layers[1].Weights[static_cast<size_t>(j) * hm.GetResolution() + i];
						if (ny > 0.95f) { flatSnow += snow; ++flat; }
						else if (ny < 0.6f) { steepSnow += snow; ++steep; }
					}
				Check(flat > 0 && steep > 0 && flatSnow / flat > steepSnow / steep + 0.3, "alpine_peaks: the snow lies on the gentle ground, not on the faces");
			}
		}
	}

	// ---------------------------------------------------------------- valleys along a course (Nodes/ValleyNodes.h)
	{
		// ground rising 40 m from west to east; a course down it along z = 0, from the east to the west
		TerrainGraph g;
		g.Id = "valley";
		g.Nodes = {
			Node("east", "gradient", "", { { "Mode", 0.0 }, { "AngleDegrees", 0.0 } }),
			Node("ground", "remap", "", { { "OutMax", 40.0 } }),
			Node("course", "path_curve", "", { { "Points", 2.0 }, { "P0X", 60.0 }, { "P0Z", 0.0 }, { "P1X", -60.0 }, { "P1Z", 0.0 }, { "Spacing", 4.0 } }),
			Node("valley", "valley_carve", "", { { "FloorWidth", 10.0 }, { "WidthVariation", 0.0 }, { "WallWidth", 30.0 }, { "WallGamma", 1.5 }, { "Incision", 2.0 },
				{ "BedSmoothing", 0.0 }, { "FloorTilt", 0.0 }, { "MaxFill", 4.0 } }),
			Node("strip", "mask_distance_to_path", "", { { "Inner", 5.0 }, { "Outer", 20.0 } }),
			Node("out_height", "output", "height"),
		};
		g.Edges = {
			{ "east", "ground", "in", "mask" },
			{ "ground", "valley", "height", "out" },
			{ "course", "valley", "path", "path" },
			{ "ground", "strip", "height", "out" },
			{ "course", "strip", "path", "path" },
			{ "valley", "out_height", "in", "height" },
		};
		TgContext ctx = MakeContext(3);
		ctx.Resolution = 129;
		TgResult r;
		std::string error;
		const bool ok = EvaluateTerrainGraph(g, registry, ctx, r, error);
		Check(ok, ("valley graph evaluates: " + error).c_str());
		if (ok)
		{
			const TgPath& course = r.Nodes["course"]["path"].Paths.Items.at(0);
			Check(glm::distance(course.Points.front(), glm::vec2(60, 0)) < 0.5f && glm::distance(course.Points.back(), glm::vec2(-60, 0)) < 0.5f, "path_curve: runs from its first control point to its last");
			bool even = true;
			for (size_t k = 1; k + 1 < course.Points.size(); ++k) even = even && std::fabs(glm::distance(course.Points[k - 1], course.Points[k]) - 4.0f) < 0.3f;
			Check(even, "path_curve: its points are Spacing apart");

			const TgField& ground = r.Nodes["ground"]["out"].Field;
			const TgField& carved = r.Nodes["valley"]["height"].Field;
			const TgField& floor = r.Nodes["valley"]["floor"].Field;
			Check(carved.Sample(40, 0) < ground.Sample(40, 0) - 1.5f && carved.Sample(-40, 0) <= ground.Sample(-40, 0), "valley_carve: the floor lies under the ground along the course");
			Check(std::fabs(carved.Sample(0, -8) - carved.Sample(0, 0)) < 0.05f && std::fabs(carved.Sample(0, 8) - carved.Sample(0, 0)) < 0.05f, "valley_carve: the floor is flat across");
			bool downhill = true;
			for (float x = 54.0f; x > -54.0f; x -= 2.0f) downhill = downhill && carved.Sample(x - 2.0f, 0) <= carved.Sample(x, 0) + 1e-3f;
			Check(downhill, "valley_carve: the floor never rises downstream");
			Check(carved.Sample(20, 25) > carved.Sample(20, 0) && carved.Sample(20, 25) < ground.Sample(20, 25), "valley_carve: the walls rise from the floor toward the ground");
			Check(std::fabs(carved.Sample(20, 50) - ground.Sample(20, 50)) < 1e-3f, "valley_carve: past its walls the ground is untouched");
			Check(floor.Sample(0, 0) > 0.99f && floor.Sample(0, 50) < 0.01f, "valley_carve: its floor output is 1 on the floor, 0 past the walls");
			const TgField& strip = r.Nodes["strip"]["mask"].Field;
			Check(strip.Sample(10, 2) > 0.99f && strip.Sample(10, 30) < 0.01f, "mask_distance_to_path: 1 by the path, 0 past Outer");
		}
	}
	{
		// biome_classify's forest input decides alone, also where a blur left it a hair below zero (it once fell back to the moisture there)
		TerrainGraph g;
		g.Id = "forest_input";
		g.Nodes = {
			Node("flat", "constant", "", { { "Value", 5.0 } }),
			Node("wet", "constant", "", { { "Value", 1.0 } }),
			Node("none", "constant", "", { { "Value", -1e-7 } }),
			Node("biomes", "biome_classify", "", { { "WaterLevel", -100.0 }, { "ForestMoisture", 0.5 } }),
			Node("out_biome", "output", "biome"),
		};
		g.Edges = { { "flat", "biomes", "height", "value" }, { "wet", "biomes", "moisture", "value" }, { "none", "biomes", "forest", "value" }, { "biomes", "out_biome", "in", "biome" } };
		TgResult r;
		std::string error;
		Check(EvaluateTerrainGraph(g, registry, MakeContext(1), r, error), ("forest_input graph evaluates: " + error).c_str());
		const TgField& biome = r.Outputs["biome"];
		Check(!biome.Values.empty() && std::all_of(biome.Values.begin(), biome.Values.end(), [](float b) { return static_cast<Biome>(std::lround(b)) == Biome::Meadow; }),
			"biome_classify: with a forest input of ~0, wet ground stays meadow");
	}
	{
		// scatter's Clumping thins the density into groves; StrongHash changes which spots survive but stays deterministic
		auto scatter = [&](double clumping, double strong)
		{
			TerrainGraph g;
			g.Id = "groves";
			g.Nodes = {
				Node("flat", "constant", "", { { "Value", 0.0 } }),
				Node("half", "constant", "", { { "Value", 0.5 } }),
				Node("trees", "scatter", "", { { "Count", 4000.0 }, { "MinSpacing", 1.5 }, { "Probabilistic", 1.0 }, { "Clumping", clumping }, { "ClumpScale", 20.0 }, { "StrongHash", strong } }),
				Node("out_trees", "output", "trees"),
			};
			g.Edges = { { "flat", "trees", "height", "value" }, { "half", "trees", "density", "value" }, { "trees", "out_trees", "in", "placements" } };
			TgResult r;
			std::string error;
			EvaluateTerrainGraph(g, registry, MakeContext(4), r, error);
			return r.PlacementOutputs["trees"].Items;
		};
		const auto even = scatter(0.0, 0.0), groves = scatter(1.0, 0.0), strong = scatter(0.0, 1.0), strongAgain = scatter(0.0, 1.0);
		Check(!even.empty() && groves.size() < even.size(), "scatter: Clumping leaves glades between the groves");
		Check(!strong.empty() && strong.size() == strongAgain.size() && strong[0].Position == strongAgain[0].Position, "scatter: StrongHash is deterministic");
	}

	if (failures == 0) std::printf("terrain_graph_test: OK\n");
	else std::fprintf(stderr, "terrain_graph_test: %d failure(s)\n", failures);
	return failures;
}
