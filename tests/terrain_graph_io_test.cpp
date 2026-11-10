// Terrain graphs as files (docs/GRAFO_TERRENO.md, section 10.5): TerrainGraphToJson/FromJson are lossless (every preset goes to JSON and back to
// the same graph, which evaluates bit for bit like the original and builds the same world), bad files are refused with a reason, and
// TerrainGraphLibrary keeps the presets and saves and loads graphs by name. The shipped assets/data/terrain_graphs/*.json must match the presets
// of the same id (an exported preset that drifted from the code would be a different world).
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/WorldGen.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	bool SameGraph(const TerrainGraph& a, const TerrainGraph& b)
	{
		if (a.Id != b.Id || a.Name != b.Name || a.Version != b.Version || a.Nodes.size() != b.Nodes.size() || a.Edges.size() != b.Edges.size()) return false;
		for (size_t i = 0; i < a.Nodes.size(); ++i)
		{
			const TgNode& x = a.Nodes[i];
			const TgNode& y = b.Nodes[i];
			if (x.Id != y.Id || x.Type != y.Type || x.Label != y.Label || x.Source != y.Source || x.X != y.X || x.Y != y.Y || x.Params != y.Params) return false;
		}
		for (size_t i = 0; i < a.Edges.size(); ++i)
		{
			const TgEdge& x = a.Edges[i];
			const TgEdge& y = b.Edges[i];
			if (x.From != y.From || x.To != y.To || x.Port != y.Port || x.FromPort != y.FromPort) return false;
		}
		return true;
	}

	// The data of a generated world, bit for bit: the heights, the water, the biome map, the trees and the rocks.
	uint64_t WorldHash(const GeneratedWorld& w)
	{
		uint64_t h = kTgHashSeed;
		const Heightmap& hm = w.TerrainRef->GetHeightmap();
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
			for (uint32_t i = 0; i < hm.GetResolution(); ++i) { const float v = hm.At(i, j); h = TgHashBytes(&v, sizeof v, h); }
		TgValue water;
		water.Kind = TgPortKind::Water;
		water.Water = w.TerrainRef->Water();
		h = TgHashValue(water, h);
		if (!w.Maps.Biomes.empty()) h = TgHashBytes(w.Maps.Biomes.data(), w.Maps.Biomes.size(), h);
		for (const TreePlacement& t : w.Trees) { h = TgHashBytes(&t.Position, sizeof t.Position, h); h = TgHashBytes(&t.Variant, sizeof t.Variant, h); }
		for (const RockPlacement& r : w.Rocks) { h = TgHashBytes(&r.Position, sizeof r.Position, h); h = TgHashBytes(&r.Variant, sizeof r.Variant, h); }
		return h;
	}

	TgContext ContextFor(const std::string& preset)
	{
		TgContext ctx;
		ctx.Seed = 5;
		ctx.Resolution = 65;
		ctx.Size = preset == "highland_valley" ? 3000.0f : 360.0f;
		return ctx;
	}

	std::string ReadText(const std::filesystem::path& p)
	{
		std::ifstream f(p, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
}

int GBTest_terrain_graph_io(int, char**)
{
	const TerrainNodeRegistry registry = MakeDefaultTerrainNodeRegistry();

	// ---------------------------------------------------------------- every preset: to JSON and back, the same graph and the same evaluation
	for (const std::string& name : TerrainGraphPresetNames())
	{
		TerrainGraph original;
		Check(MakeTerrainGraphPreset(name, original), "preset " + name + " exists");
		const std::string json = TerrainGraphToJson(original);
		TerrainGraph read;
		const std::string error = TerrainGraphFromJson(json, read);
		Check(error.empty(), name + ": the JSON reads back (" + error + ")");
		Check(SameGraph(original, read), name + ": the graph read back is the graph written (nodes, parameters, edges, in order)");
		Check(TerrainGraphToJson(read) == json, name + ": writing it again gives the same text");

		TgResult a, b;
		std::string ea, eb;
		const bool okA = EvaluateTerrainGraph(original, registry, ContextFor(name), a, ea);
		const bool okB = EvaluateTerrainGraph(read, registry, ContextFor(name), b, eb);
		Check(okA && okB, name + ": both evaluate (" + ea + eb + ")");
		Check(!a.Outputs.empty() && a.Outputs.count("height") == 1, name + ": publishes a height");
		Check(TgHashResult(a) == TgHashResult(b), name + ": the graph read back evaluates bit for bit like the preset");
	}

	// ---------------------------------------------------------------- what is refused, and why
	{
		TerrainGraph untouched;
		untouched.Id = "keep";
		Check(!TerrainGraphFromJson("not json", untouched).empty() && untouched.Id == "keep", "not JSON: refused, the graph left as it was");
		Check(!TerrainGraphFromJson(R"({"kind":"priority_graph","id":"x","nodes":[]})", untouched).empty(), "another kind of graph is refused");
		Check(!TerrainGraphFromJson(R"({"kind":"terrain_graph","version":99,"id":"x","nodes":[]})", untouched).empty(), "a newer version is refused");
		Check(!TerrainGraphFromJson(R"({"kind":"terrain_graph","nodes":[]})", untouched).empty(), "a graph with no id is refused");
		Check(!TerrainGraphFromJson(R"({"kind":"terrain_graph","id":"x","nodes":[{"id":"a","type":"constant","params":{"Value":"high"}}]})", untouched).empty(),
			"a parameter that is not a number is refused");
		TerrainGraph v1;
		Check(TerrainGraphFromJson(R"({"kind":"terrain_graph","version":1,"id":"old","nodes":[{"id":"c","type":"constant"},{"id":"o","type":"output","source":"height"}],
			"edges":[{"from":"c","to":"o"}]})", v1).empty(), "a version 1 graph reads");
		Check(v1.Version == 1 && v1.Edges.size() == 1 && v1.Edges[0].Port == "in" && v1.Edges[0].FromPort == "out", "and keeps its version and its unnamed ports");
	}

	// ---------------------------------------------------------------- the library: presets, save and load by name
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::temp_directory_path() / "gb_terrain_graph_io_test";
		std::error_code ec;
		fs::remove_all(dir, ec);

		TerrainGraphLibrary lib;
		for (const std::string& name : TerrainGraphPresetNames()) Check(lib.Find(name) && lib.IsPreset(name), "the library starts with preset " + name);
		Check(!lib.Duplicate("highland_valley", "my valley").empty(), "an id with a space is refused (it is the file name)");
		Check(lib.Duplicate("highland_valley", "my_valley").empty() && lib.Find("my_valley"), "a preset duplicated under a new id");
		Check(!lib.Duplicate("highland_valley", "my_valley").empty(), "an id that exists is not overwritten by Duplicate");
		TerrainGraph broken = *lib.Find("my_valley");
		broken.Id = "broken";
		broken.Edges.push_back({ "nowhere", broken.Nodes[0].Id });
		Check(!lib.Add(broken).empty() && !lib.Find("broken"), "a graph with errors does not enter the library");

		Check(lib.SaveGraph(dir.string(), "my_valley").empty() && fs::exists(dir / "my_valley.json"), "a graph saved as <id>.json");
		Check(lib.SaveGraph(dir.string(), "temperate").empty(), "a preset can be exported too");
		TerrainGraphLibrary other;
		other.Remove("temperate");
		Check(other.LoadDirectory(dir.string()).empty(), "the folder loads back");
		Check(other.Find("my_valley") && SameGraph(*other.Find("my_valley"), *lib.Find("my_valley")), "the graph loaded is the graph saved");
		Check(other.Find("temperate") && SameGraph(*other.Find("temperate"), *lib.Find("temperate")), "the exported preset loads back as it was");
		{
			std::ofstream bad(dir / "zz_bad.json");
			bad << "{ \"kind\": \"terrain_graph\", \"id\": \"bad\" }";
		}
		const std::string report = other.LoadDirectory(dir.string());
		Check(!report.empty() && report.find("zz_bad.json") != std::string::npos && other.Find("my_valley"), "a bad file is reported by name and the rest still loads");
		std::string id;
		Check(other.LoadFile((dir / "my_valley.json").string(), &id).empty() && id == "my_valley", "one file loads by path and says which graph it was");
		fs::remove_all(dir, ec);
	}

	// ---------------------------------------------------------------- the shipped files are the presets of their id
#ifdef GB_TERRAIN_GRAPHS_DIR
	{
		namespace fs = std::filesystem;
		int shipped = 0;
		std::error_code ec;
		for (const auto& entry : fs::directory_iterator(GB_TERRAIN_GRAPHS_DIR, ec))
		{
			if (entry.path().extension() != ".json") continue;
			TerrainGraph g;
			const std::string error = TerrainGraphFromJson(ReadText(entry.path()), g);
			Check(error.empty(), entry.path().filename().string() + " reads (" + error + ")");
			Check(!HasTgErrors(ValidateTerrainGraph(g, registry)), entry.path().filename().string() + " is a valid graph");
			Check(entry.path().stem().string() == g.Id, entry.path().filename().string() + " is named after its id");
			TerrainGraph preset;
			if (MakeTerrainGraphPreset(g.Id, preset))
			{
				++shipped;
				Check(SameGraph(g, preset), entry.path().filename().string() + " is the preset " + g.Id + " exactly (export it again after changing the preset)");
			}
		}
		Check(shipped >= 1, "assets/data/terrain_graphs ships at least one exported preset (highland_valley)");
	}
#endif

	// ---------------------------------------------------------------- the same world, bit for bit, from the preset and from its file
	{
		for (const char* name : { "highland_valley", "temperate_valley" })
		{
			TerrainGraph preset;
			MakeTerrainGraphPreset(name, preset);
			TerrainGraph read;
			TerrainGraphFromJson(TerrainGraphToJson(preset), read);
			auto build = [&](const TerrainGraph& g)
			{
				WorldDesc desc;
				desc.Seed = 5;
				desc.Size = std::string(name) == "highland_valley" ? 3000.0f : 180.0f;
				desc.Resolution = 129;
				desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(g);
				return WorldGenerator::GenerateData(desc);
			};
			const GeneratedWorld a = build(preset);
			const GeneratedWorld b = build(read);
			Check(a.TerrainRef && b.TerrainRef, std::string(name) + ": both worlds build");
			if (a.TerrainRef && b.TerrainRef)
				Check(WorldHash(a) == WorldHash(b), std::string(name) + ": the world from the JSON is the world from the preset (heights, water, biomes, trees, rocks)");
		}
	}

	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("terrain_graph_io: all checks passed\n");
	return failures ? 1 : 0;
}
