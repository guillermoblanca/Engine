// The material layers and the trees of the terrain of TERRENO_V5 (Nodes/SurfaceLayerNodes.h, GRAFO_TERRENO.md "Capas de material y árboles
// del terreno nuevo"), headless. On a synthetic map (a flat with a lake, a 70-degree wall up to a plateau, a channel):
//   - layer_cliff_scree: the wall is cliff, the flat is not; scree only at the wall's foot; E4's masks are used as they come;
//   - layer_wet: the lake bed is mud, mud only next to the water, gravel only along the channel;
//   - layer_normalize: the layers sum to 1 at every sample and none is negative, also from inputs out of 0..1; snow lies on top;
//   - biome_water: the lake's samples are Water;
//   - tree_species: no tree in the lake, on the cliff or on a path; the species of each band; the same twice;
//   - every node refuses a Revision it does not have.
// Then a whole world from a graph (GenerateData): the new layers reach the terrain after the six of before, which keep their ids, the tree
// recipes are the node's species and every tree keeps the species the node gave it.
#include "GB/World/Biome.h"
#include "GB/World/Graph/Nodes/SurfaceLayerNodes.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/Water/WaterRaster.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace GB;

static_assert(static_cast<int>(SurfaceLayerKind::Rock) == 0 && static_cast<int>(SurfaceLayerKind::Snow) == 1 && static_cast<int>(SurfaceLayerKind::Grass) == 2 &&
	static_cast<int>(SurfaceLayerKind::Sand) == 3 && static_cast<int>(SurfaceLayerKind::Dry) == 4 && static_cast<int>(SurfaceLayerKind::Forest) == 5,
	"the six layer kinds of before keep their ids");
static_assert(static_cast<int>(SurfaceLayerKind::Cliff) == 6 && static_cast<int>(SurfaceLayerKind::Scree) == 7 && static_cast<int>(SurfaceLayerKind::Gravel) == 8 &&
	static_cast<int>(SurfaceLayerKind::Mud) == 9 && static_cast<int>(SurfaceLayerKind::MudDry) == 10 && static_cast<int>(SurfaceLayerKind::GrassWet) == 11 &&
	static_cast<int>(SurfaceLayerKind::Path) == 12, "the new kinds come after them, in the order of the contract");

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	constexpr uint32_t kN = 129;   // 1 m per cell: Size 128, centred on the origin (sample (i, j) at (i - 64, j - 64))
	constexpr float kLakeLevel = 1.0f;

	// A flat at 2 m. A bowl down to -1 m around (-30, -30), radius 14 m, holding a lake at 1 m. A wall from x = 20 m to 30 m rising 2.75 m per
	// metre (70 degrees) to a plateau at 29.5 m.
	TgField SyntheticMap()
	{
		TgField f;
		f.Resize(kN, 128.0f);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const float x = static_cast<float>(i) - 64.0f, z = static_cast<float>(j) - 64.0f;
				float h = 2.0f;
				const float r = std::sqrt((x + 30.0f) * (x + 30.0f) + (z + 30.0f) * (z + 30.0f));
				if (r < 14.0f) h = -1.0f + 3.0f * (r / 14.0f) * (r / 14.0f);
				if (x > 20.0f) h = 2.0f + 2.75f * std::min(x - 20.0f, 10.0f);
				f.At(i, j) = h;
			}
		return f;
	}

	WaterField SyntheticWater()
	{
		WaterField w;
		LakeBody lake;
		lake.Present = true;
		lake.Center = { -30.0f, -30.0f };
		lake.Radius = 11.5f;   // (where the bowl reaches 1 m: r = 14 sqrt(2/3))
		lake.Level = kLakeLevel;
		w.SetLake(lake);
		return w;
	}

	TgValue FieldValue(TgField f) { TgValue v; v.Kind = TgPortKind::Field; v.Field = std::move(f); return v; }
	TgValue Constant(float value) { TgField f; f.Resize(kN, 128.0f); std::fill(f.Values.begin(), f.Values.end(), value); return FieldValue(std::move(f)); }

	// Runs one node of `type` with the given inputs by port name; `ok` says whether it ran.
	std::map<std::string, TgValue> RunNode(const TerrainNodeRegistry& registry, const std::string& type, const std::map<std::string, const TgValue*>& in,
		const std::map<std::string, double>& params, bool& ok, std::string& error)
	{
		const ITerrainNode* impl = registry.Find(type);
		const TgNodeSchema* schema = registry.Schema(type);
		std::map<std::string, TgValue> out;
		ok = false;
		if (!impl || !schema) { error = "unknown node " + type; return out; }
		TgNode node;
		node.Id = type;
		node.Type = type;
		node.Params = params;
		std::vector<const TgValue*> inputs(schema->Inputs.size(), nullptr);
		for (const auto& [port, value] : in)
		{
			const int at = schema->InputIndex(port);
			if (at < 0) { error = type + " has no input " + port; return out; }
			inputs[static_cast<size_t>(at)] = value;
		}
		TgContext ctx;
		ctx.Seed = 7;
		ctx.Resolution = kN;
		ctx.Size = 128.0f;
		ok = impl->Run(node, ctx, inputs, out, error);
		return out;
	}

	size_t At(float x, float z) { return static_cast<size_t>(std::lround(z + 64.0f)) * kN + static_cast<size_t>(std::lround(x + 64.0f)); }
}

int GBTest_surface_layers(int, char**)
{
	const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();
	const TgValue height = FieldValue(SyntheticMap());
	TgValue water;
	water.Kind = TgPortKind::Water;
	water.Water = SyntheticWater();
	std::string error;
	bool ok = false;

	// ---------------------------------------------------------------- layer_cliff_scree
	TgValue cliff, scree;
	{
		auto out = RunNode(registry, "layer_cliff_scree", { { "height", &height } }, {}, ok, error);
		Check(ok, "layer_cliff_scree runs: " + error);
		cliff = out["cliff"]; scree = out["scree"];
		Check(cliff.Field.Values[At(25.0f, 0.0f)] > 0.99f && cliff.Field.Values[At(0.0f, 0.0f)] == 0.0f && cliff.Field.Values[At(40.0f, 0.0f)] == 0.0f,
			"cliff: the 70-degree wall is cliff, the flat and the plateau are not");
		bool screeOk = true;
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const float x = static_cast<float>(i) - 64.0f, s = scree.Field.Values[static_cast<size_t>(j) * kN + i];
				if (s < 0.0f || s > 1.0f || (s > 0.0f && (x > 30.5f || x < 20.0f - 60.0f))) screeOk = false;   // never above the wall, never past its reach
				if (s + cliff.Field.Values[static_cast<size_t>(j) * kN + i] > 1.0001f) screeOk = false;
			}
		Check(screeOk, "scree: only below the wall and within its reach, and never on top of the cliff");
		// the flat at the foot is under ScreeMinDegrees: a 35-degree apron at the foot is scree
		TgField apron = height.Field;
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const float x = static_cast<float>(i) - 64.0f;
				if (x > 8.0f && x <= 20.0f) apron.At(i, j) = 2.0f - 0.7f * (20.0f - x);   // 35 degrees down from the wall's foot
				else if (x <= 8.0f && x > -40.0f && apron.At(i, j) == 2.0f) apron.At(i, j) = 2.0f - 0.7f * 12.0f;
			}
		const TgValue apronH = FieldValue(apron);
		auto withApron = RunNode(registry, "layer_cliff_scree", { { "height", &apronH } }, {}, ok, error);
		Check(ok && withApron["scree"].Field.Values[At(14.0f, 0.0f)] > 0.5f, "scree: a 35-degree apron at the foot of the cliff is scree");

		const TgValue e4Cliff = Constant(0.3f), e4Scree = Constant(1.7f);
		auto e4 = RunNode(registry, "layer_cliff_scree", { { "height", &height }, { "cliff", &e4Cliff }, { "scree", &e4Scree } }, {}, ok, error);
		Check(ok && e4["cliff"].Field.Values[At(25.0f, 0.0f)] == 0.3f && e4["scree"].Field.Values[At(0.0f, 0.0f)] == 0.7f,
			"cliff/scree: E4's masks are used as they come (clamped, scree under what the cliff covers)");
	}

	// ---------------------------------------------------------------- layer_wet
	TgValue gravel, mud, mudDry, grassWet;
	{
		TgField area;   // a channel along z = 30 m (x < 10 m): 1e6 m2 on it, 100 m2 elsewhere
		area.Resize(kN, 128.0f);
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i) area.At(i, j) = j == 94 && i < 74 ? 1e6f : 100.0f;
		const TgValue areaV = FieldValue(area), moist = Constant(0.9f);
		auto out = RunNode(registry, "layer_wet", { { "height", &height }, { "water", &water }, { "area", &areaV }, { "moisture", &moist } }, {}, ok, error);
		Check(ok, "layer_wet runs: " + error);
		gravel = out["gravel"]; mud = out["mud"]; mudDry = out["mud_dry"]; grassWet = out["grass_wet"];
		Check(mud.Field.Values[At(-30.0f, -30.0f)] == 1.0f, "mud: the lake bed is mud");
		const WaterRaster raster = RasterizeWater(water.Water, kN, height.Field.Min(), height.Field.Spacing());
		bool mudNear = true, gravelNear = true, ranges = true;
		for (uint32_t j = 0; j < kN; ++j)
			for (uint32_t i = 0; i < kN; ++i)
			{
				const size_t c = static_cast<size_t>(j) * kN + i;
				const float x = static_cast<float>(i) - 64.0f, z = static_cast<float>(j) - 64.0f;
				const float toLake = std::sqrt((x + 30.0f) * (x + 30.0f) + (z + 30.0f) * (z + 30.0f)) - 11.5f;
				if ((mud.Field.Values[c] > 0.0f || mudDry.Field.Values[c] > 0.0f) && !raster.Wet(c) && toLake > 20.0f + 2.0f) mudNear = false;
				if (gravel.Field.Values[c] > 0.0f && (std::abs(z - 30.0f) > 10.0f + 1.0f || x > 20.0f)) gravelNear = false;
				for (const TgValue* v : { &gravel, &mud, &mudDry, &grassWet })
					if (v->Field.Values[c] < 0.0f || v->Field.Values[c] > 1.0f) ranges = false;
			}
		Check(mudNear, "mud and dried mud only next to the water");
		Check(gravelNear && gravel.Field.Values[At(0.0f, 30.0f)] > 0.99f && gravel.Field.Values[At(0.0f, 0.0f)] == 0.0f, "gravel only along the channel");
		Check(ranges, "the wet layers are 0..1");
		Check(grassWet.Field.Values[At(0.0f, 0.0f)] > 0.99f && grassWet.Field.Values[At(25.0f, 0.0f)] == 0.0f, "wet grass where it is damp and gentle, not on the wall");
	}

	// ---------------------------------------------------------------- layer_normalize
	{
		TgField wild;   // inputs out of 0..1 on purpose
		wild.Resize(kN, 128.0f);
		for (size_t c = 0; c < wild.Values.size(); ++c) wild.Values[c] = std::sin(static_cast<float>(c) * 0.37f) * 1.3f + 0.2f;
		const TgValue wildV = FieldValue(wild), snow = Constant(0.0f), rock = Constant(0.4f);
		TgField snowCap = snow.Field;
		for (uint32_t i = 0; i < kN; ++i) snowCap.At(i, 0) = 1.0f;
		const TgValue snowV = FieldValue(snowCap);
		auto out = RunNode(registry, "layer_normalize", { { "height", &height }, { "snow", &snowV }, { "path", &wildV }, { "cliff", &cliff }, { "scree", &scree },
			{ "rock", &rock }, { "gravel", &gravel }, { "mud", &mud }, { "mud_dry", &mudDry }, { "grass_wet", &grassWet }, { "forest", &wildV }, { "dry", &wildV } }, {}, ok, error);
		Check(ok, "layer_normalize runs: " + error);
		double worst = 0.0, lowest = 1.0;
		for (size_t c = 0; c < height.Field.Values.size(); ++c)
		{
			double sum = 0.0;
			for (const std::string& name : NormalizedLayerNames())
			{
				const float w = out[name].Field.Values[c];
				sum += w;
				lowest = std::min(lowest, static_cast<double>(w));
			}
			worst = std::max(worst, std::abs(sum - 1.0));
		}
		Check(worst <= 1e-5 && lowest >= 0.0, "layer_normalize: the layers sum to 1 (" + std::to_string(worst) + ") and none is negative (" + std::to_string(lowest) + ")");
		Check(out["snow"].Field.Values[3] == 1.0f && out["cliff"].Field.Values[3] == 0.0f && out["grass"].Field.Values[3] == 0.0f, "layer_normalize: snow lies on top");
		Check(out["sand"].Field.Values[At(0.0f, 0.0f)] == 0.0f, "layer_normalize: an unconnected layer is 0");
	}

	// ---------------------------------------------------------------- biome_water
	TgValue biome;
	{
		const TgValue meadow = Constant(static_cast<float>(Biome::Forest));
		auto out = RunNode(registry, "biome_water", { { "biome", &meadow }, { "water", &water }, { "height", &height } }, {}, ok, error);
		Check(ok, "biome_water runs: " + error);
		biome = out["biome"];
		Check(biome.Field.Values[At(-30.0f, -30.0f)] == static_cast<float>(Biome::Water) && biome.Field.Values[At(0.0f, 0.0f)] == static_cast<float>(Biome::Forest),
			"biome_water: the lake is Water, the rest keeps its biome");
	}

	// ---------------------------------------------------------------- tree_species
	{
		TgValue grid;   // a tree every 2 m
		grid.Kind = TgPortKind::Placements;
		for (float z = -63.0f; z < 63.0f; z += 2.0f)
			for (float x = -63.0f; x < 63.0f; x += 2.0f) grid.Placements.Items.push_back({ { x + 0.3f, z + 0.7f }, 0.0f, 1.0f, 0 });
		TgField pathF;
		pathF.Resize(kN, 128.0f);
		for (uint32_t j = 0; j < kN; ++j) pathF.At(64, j) = 1.0f;   // a path along x = 0
		const TgValue pathV = FieldValue(pathF);
		const std::map<std::string, double> params = { { "Tree:low:Broadleaf/0", 1.0 }, { "Tree:top:Conifer/0", 1.0 }, { "LowTop", 10.0 }, { "MidTop", 10.0 },
			{ "HighTop", 10.0 }, { "AbsoluteHeights", 1.0 }, { "BandBlend", 0.0 } };
		auto out = RunNode(registry, "tree_species", { { "placements", &grid }, { "height", &height }, { "water", &water }, { "cliff", &cliff }, { "path", &pathV } }, params, ok, error);
		Check(ok, "tree_species runs: " + error);
		const auto& trees = out["placements"].Placements.Items;
		TgNode node;
		node.Params = params;
		const std::vector<std::string> palette = TreeSpeciesPalette(node);
		Check(palette.size() == 2 && palette[0] == "Broadleaf/0" && palette[1] == "Conifer/0", "tree_species: the palette is the species it names, sorted");
		const WaterRaster raster = RasterizeWater(water.Water, kN, height.Field.Min(), height.Field.Spacing());
		size_t wet = 0, onCliff = 0, onPath = 0, wrongBand = 0;
		for (const TgPlacement& t : trees)
		{
			const float x = t.Position.x, z = t.Position.y;
			if (raster.Wet(At(x, z))) ++wet;
			if (cliff.Field.Sample(x, z) > 0.25f) ++onCliff;
			if (pathV.Field.Sample(x, z) > 0.25f) ++onPath;
			const int kind = t.Variant / TreeSpeciesVariety(node);
			if ((height.Field.Sample(x, z) < 10.0f) != (kind == 0)) ++wrongBand;
		}
		Check(!trees.empty() && trees.size() < grid.Placements.Items.size(), "tree_species: some trees are taken out, most stay (" + std::to_string(trees.size()) + ")");
		Check(wet == 0 && onCliff == 0 && onPath == 0, "tree_species: no tree in the lake (" + std::to_string(wet) + "), on the cliff (" + std::to_string(onCliff) + ") or on the path (" + std::to_string(onPath) + ")");
		Check(wrongBand == 0, "tree_species: broadleaf below 10 m, conifers above (" + std::to_string(wrongBand) + " in the wrong band)");
		auto again = RunNode(registry, "tree_species", { { "placements", &grid }, { "height", &height }, { "water", &water }, { "cliff", &cliff }, { "path", &pathV } }, params, ok, error);
		bool same = again["placements"].Placements.Items.size() == trees.size();
		for (size_t k = 0; same && k < trees.size(); ++k) same = again["placements"].Placements.Items[k].Variant == trees[k].Variant && again["placements"].Placements.Items[k].Position == trees[k].Position;
		Check(same, "tree_species: the same twice");
		TgNode defaults;
		Check(TreeSpeciesPalette(defaults).size() == 6, "tree_species: without Tree: parameters, the default palette (broadleaf 0-2, conifer 0-1, bare)");
	}

	// ---------------------------------------------------------------- Revision
	for (const char* type : { "layer_cliff_scree", "layer_wet", "layer_normalize", "biome_water", "tree_species" })
	{
		RunNode(registry, type, { { std::string(type) == "biome_water" ? "biome" : "height", &height } }, { { "Revision", 2.0 } }, ok, error);
		Check(!ok && error.find("Revision") != std::string::npos, std::string(type) + ": Revision 2 is an error");
	}

	// ---------------------------------------------------------------- a world from a graph
	{
		auto node = [](const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "")
		{
			TgNode n; n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params); return n;
		};
		auto graph = std::make_shared<TerrainGraph>();
		graph->Id = "layers_world";
		graph->Nodes = {
			node("ground", "heightfield_noise", { { "Amplitude", 60.0 }, { "Frequency", 0.01 }, { "Octaves", 5.0 }, { "Mode", 1.0 } }),
			node("out_height", "output", {}, "height"),
			node("slopes", "layer_cliff_scree"),
			node("faces", "mask_slope", { { "MinDegrees", 40.0 } }),
			node("wet", "layer_wet"),
			node("layers", "layer_normalize"),
			node("trees", "scatter", { { "Count", 3000.0 }, { "MinSpacing", 2.0 }, { "MaxSlopeDegrees", 60.0 } }),
			node("species", "tree_species", { { "Tree:low:Broadleaf/0", 1.0 }, { "Tree:mid:Broadleaf/2", 1.0 }, { "Tree:high:Conifer/1", 1.0 }, { "Tree:top:Bare", 1.0 } }),
			node("out_trees", "output", {}, "trees"),
		};
		graph->Edges = { { "ground", "out_height", "in", "height" }, { "ground", "slopes", "height", "height" }, { "ground", "faces", "height", "height" },
			{ "ground", "wet", "height", "height" }, { "ground", "layers", "height", "height" }, { "slopes", "layers", "cliff", "cliff" },
			{ "slopes", "layers", "scree", "scree" }, { "faces", "layers", "rock", "mask" }, { "wet", "layers", "grass_wet", "grass_wet" },
			{ "ground", "trees", "height", "height" }, { "trees", "species", "placements", "placements" }, { "ground", "species", "height", "height" },
			{ "slopes", "species", "cliff", "cliff" }, { "slopes", "species", "scree", "scree" }, { "species", "out_trees", "in", "placements" } };
		const char* published[] = { "snow", "path", "cliff", "scree", "rock", "gravel", "mud", "mud_dry", "grass_wet", "forest", "dry", "sand", "grass" };
		for (const char* name : published)
		{
			const std::string slot = std::string("splat_") + name;
			graph->Nodes.push_back(node(("out_" + std::string(name)).c_str(), "output", {}, ""));
			graph->Nodes.back().Source = slot;
			graph->Edges.push_back({ "layers", "out_" + std::string(name), "in", name });
		}
		WorldDesc desc;
		desc.Seed = 3;
		desc.Size = 256.0f;
		desc.Resolution = kN;
		desc.TreeBudget = 3000;
		desc.TerrainGraphDefinition = graph;
		TgResult r;
		Check(EvaluateWorldGraph(desc, r, error), "the layers graph evaluates: " + error);
		const GeneratedWorld world = WorldGenerator::GenerateData(desc);
		const std::vector<SurfaceLayer>& layers = world.TerrainRef->GetSurfaceLayers();
		Check(layers.size() == 13, "all 13 layers reach the terrain (" + std::to_string(layers.size()) + ")");
		bool order = layers.size() == 13;
		for (size_t k = 0; order && k < 6; ++k) order = static_cast<int>(layers[k].Kind) < 6;
		for (size_t k = 6; order && k < 13; ++k) order = static_cast<int>(layers[k].Kind) == static_cast<int>(k);
		Check(order, "the six layers of before first, then cliff..path in the order of the enum");
		double worst = 0.0;
		for (size_t c = 0; !layers.empty() && c < layers[0].Weights.size(); ++c)
		{
			double sum = 0.0;
			for (const SurfaceLayer& l : layers) sum += l.Weights[c];
			worst = std::max(worst, std::abs(sum - 1.0));
		}
		Check(worst <= 1e-5, "the terrain's layers sum to 1 (" + std::to_string(worst) + ")");
		Check(world.TreeRecipes.size() == 4 * 2, "one recipe per species and variety (" + std::to_string(world.TreeRecipes.size()) + ")");
		const std::pair<TreeSpecies, int> expect[] = { { TreeSpecies::Bare, -1 }, { TreeSpecies::Broadleaf, 0 }, { TreeSpecies::Broadleaf, 2 }, { TreeSpecies::Conifer, 1 } };
		bool recipes = world.TreeRecipes.size() == 8;
		for (size_t k = 0; recipes && k < 8; ++k)
			recipes = world.TreeRecipes[k].Species == expect[k / 2].first && (expect[k / 2].first == TreeSpecies::Bare || world.TreeRecipes[k].Style == expect[k / 2].second);
		Check(recipes, "the recipes are the node's species, sorted, with their style");
		std::map<std::pair<float, float>, int> given;
		for (const TgPlacement& p : r.PlacementOutputs["trees"].Items) given[{ p.Position.x, p.Position.y }] = p.Variant;
		size_t kept = 0;
		for (const TreePlacement& t : world.Trees)
		{
			const auto it = given.find({ t.Position.x, t.Position.z });
			kept += it != given.end() && it->second == t.Variant;
		}
		Check(!world.Trees.empty() && kept == world.Trees.size(), "every tree keeps the species tree_species gave it (" + std::to_string(kept) + " of " + std::to_string(world.Trees.size()) + ")");
	}

	if (failures == 0) std::printf("surface_layers_test: OK\n");
	else std::fprintf(stderr, "surface_layers_test: %d failure(s)\n", failures);
	return failures;
}
