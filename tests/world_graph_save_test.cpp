// T84 (docs/GRAFO_TERRENO.md 12.7, docs/GUARDADO.md): a saved game keeps its world's terrain graph whole (Save::WorldParams::TerrainGraph), so it
// reopens with the very same world -- heights, biomes, trees and rocks bit for bit -- even after the graph in the library was edited: the save
// does not refer to the library. Also: the graph round trips to the same text (a save loaded and written again keeps its bytes), a world
// without its own graph writes nothing new (the saves of before are unchanged), and a graph that would not build is refused before the load.
#include "GB/Game/GameSession.h"
#include "GB/Save/SaveFiles.h"
#include "GB/World/Graph/TerrainGraphIO.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include "GB/World/Graph/TgHash.h"
#include "GB/World/WorldGen.h"
#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace GB;
using namespace GB::Save;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	// Heights, baked biomes, and every tree and rock with its variant.
	uint64_t WorldHash(const GeneratedWorld& w)
	{
		uint64_t h = kTgHashSeed;
		const Heightmap& hm = w.TerrainRef->GetHeightmap();
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
			for (uint32_t i = 0; i < hm.GetResolution(); ++i) { const float v = hm.At(i, j); h = TgHashBytes(&v, sizeof v, h); }
		if (!w.Maps.Biomes.empty()) h = TgHashBytes(w.Maps.Biomes.data(), w.Maps.Biomes.size(), h);
		for (const TreePlacement& t : w.Trees) { h = TgHashBytes(&t.Position, sizeof t.Position, h); h = TgHashBytes(&t.Variant, sizeof t.Variant, h); }
		for (const RockPlacement& r : w.Rocks) { h = TgHashBytes(&r.Position, sizeof r.Position, h); h = TgHashBytes(&r.Variant, sizeof r.Variant, h); }
		return h;
	}

	// What the editor's save host does (Editor/src/Gameplay/EditorSave.cpp, ParamsOf / DescOf).
	WorldParams ParamsOf(const WorldDesc& d)
	{
		WorldParams p;
		p.Generated = true;
		p.Seed = d.Seed;
		p.GeneratorVersion = d.GeneratorVersion;
		p.Size = d.Size;
		p.Resolution = d.Resolution;
		p.TreeBudget = d.TreeBudget;
		p.RockBudget = d.RockBudget;
		p.Erosion = d.Erosion;
		p.TerrainGraph = WorldGraphJson(d);
		return p;
	}

	std::string DescOf(const WorldParams& p, WorldDesc& d)
	{
		d = WorldDesc();
		d.Seed = p.Seed;
		d.GeneratorVersion = p.GeneratorVersion;
		d.Size = p.Size;
		d.Resolution = p.Resolution;
		d.TreeBudget = p.TreeBudget;
		d.RockBudget = p.RockBudget;
		d.Erosion = p.Erosion;
		return SetWorldGraphJson(d, p.TerrainGraph);
	}

	// The baseline of a session: the generated trees and rocks as entities, from the save's own world parameters (graph included).
	bool Baseline(const WorldParams& p, EntityWorld& out, std::string& error)
	{
		out = EntityWorld();
		if (!p.Generated) return true;
		WorldDesc d;
		if (std::string e = DescOf(p, d); !e.empty()) { error = e; return false; }
		const GeneratedWorld w = WorldGenerator::GenerateData(d);
		const EntityId trees = out.Create("Trees");
		out.Find(trees)->Tags.push_back("world");
		for (size_t i = 0; i < w.Trees.size(); ++i) out.Find(out.Create("Tree " + std::to_string(i + 1), trees))->Transform.Position = w.Trees[i].Position;
		const EntityId rocks = out.Create("Rocks");
		out.Find(rocks)->Tags.push_back("world");
		for (size_t i = 0; i < w.Rocks.size(); ++i) out.Find(out.Create("Rock " + std::to_string(i + 1), rocks))->Transform.Position = w.Rocks[i].Position;
		return true;
	}
}

int main()
{
	const fs::path dir = fs::temp_directory_path() / "gb_world_graph_save_test";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir / "graphs");
	const std::string graphs = (dir / "graphs").string(), savePath = (dir / "graph_world.gbsave").string();

	// a graph of the library, in its folder, as the Terrain Graph panel saves it
	TerrainGraph mine;
	Check(MakeTerrainGraphPreset("temperate_valley", mine), "the temperate_valley preset exists");
	mine.Id = "t84_test";
	mine.Name = "T84 test";
	{
		TerrainGraphLibrary lib;
		Check(lib.Add(mine).empty() && lib.SaveGraph(graphs, "t84_test").empty(), "the graph is saved in the library's folder");
	}
	TerrainGraphLibrary lib;
	Check(lib.LoadDirectory(graphs).empty() && lib.Find("t84_test"), "and read back from it");

	WorldDesc desc;
	desc.Seed = 11;
	desc.Size = 300.0f;
	desc.Resolution = 129;
	desc.TreeBudget = 200;
	desc.RockBudget = 40;
	desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(*lib.Find("t84_test"));
	const uint64_t made = WorldHash(WorldGenerator::GenerateData(desc));

	// save a game on that world
	SessionConfig config;
	config.World = ParamsOf(desc);
	config.Baseline = Baseline;
	GameSession game(config);
	Check(game.Entities().FindByName("Tree 1") != NullEntity, "the session's world has the graph's trees");
	std::string msg;
	game.Command("fell \"Tree 3\"", msg);
	for (int i = 0; i < 30; ++i) game.Step();
	const CaptureResult captured = CaptureGame(game);
	Check(!captured.Game.World.TerrainGraph.empty(), "the save carries the world's graph");
	Check(SaveGameToFile(captured.Game, savePath).Ok, "the game is written");

	// the library's graph changes after the save (the hills get another scale), and is saved over the old file
	{
		TerrainGraph edited = *lib.Find("t84_test");
		TgNode* hills = edited.FindNode("hills");
		Check(hills != nullptr, "temperate_valley has its hills");
		if (hills) hills->Params["Scale"] *= 1.7;
		Check(lib.Add(edited, true).empty() && lib.SaveGraph(graphs, "t84_test").empty(), "the edited graph replaces the one in the library");
	}
	TerrainGraphLibrary later;
	Check(later.LoadDirectory(graphs).empty() && later.Find("t84_test"), "the library reads the edited graph");
	WorldDesc fromLibrary = desc;
	fromLibrary.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(*later.Find("t84_test"));
	Check(WorldHash(WorldGenerator::GenerateData(fromLibrary)) != made, "the library's graph now makes another world");

	// reopen: the save's own graph makes the world it was saved on
	SaveGame loaded;
	Check(LoadSaveGameFromFile(savePath, loaded).Ok, "the save reads");
	Check(loaded.World == captured.Game.World, "with the same world parameters, graph included");
	Check(loaded.World.TerrainGraph == WorldGraphJson(desc), "the graph comes back as the same compact text");
	WorldDesc reopened;
	Check(DescOf(loaded.World, reopened).empty(), "the save's graph builds");
	Check(reopened.TerrainGraphDefinition && TerrainGraphToJsonCompact(*reopened.TerrainGraphDefinition) == TerrainGraphToJsonCompact(*desc.TerrainGraphDefinition),
		"it is the graph the world was made with, not the library's");
	Check(WorldHash(WorldGenerator::GenerateData(reopened)) == made, "the reopened world is the saved one bit for bit (heights, biomes, trees, rocks)");
	SessionConfig other;
	other.Baseline = Baseline;
	GameSession back(other);
	const Result applied = ApplyGame(back, loaded);
	Check(applied.Ok && applied.Warnings.empty(), "the save applies with no drift of its baseline (" + applied.Error + ")");
	Check(back.World() == loaded.World && back.Entities().FindByName("Tree 3") == NullEntity && back.Entities().FindByName("Tree 4") != NullEntity,
		"with its world and the tree that was felled");
	Check(SaveGameToString(CaptureGame(back).Game) == SaveGameToString(captured.Game), "saved again, it is the same bytes");

	// the graph is in the identity of the world: the same seed without it is another world
	{
		WorldParams a = loaded.World, b = loaded.World;
		b.TerrainGraph.clear();
		Check(a != b, "a world with its own graph differs from the generator's of the same seed");
		b = a;
		b.Erosion = false;
		Check(a != b, "and so does one without erosion");
	}
	// a world without its own graph (the saves of before): no "graph" nor "erosion" in the file
	{
		SaveGame plain;
		plain.World.Generated = true;
		plain.World.Seed = 5;
		const std::string text = SaveGameToString(plain);
		Check(text.find("\"graph\"") == std::string::npos && text.find("\"erosion\"") == std::string::npos, "a world without a graph writes neither key");
		SaveGame read;
		Check(ParseSaveGame(text, read).Ok && read.World == plain.World && read.World.TerrainGraph.empty() && read.World.Erosion, "and reads back as it was");
		plain.World.Erosion = false;
		Check(ParseSaveGame(SaveGameToString(plain), read).Ok && !read.World.Erosion, "erosion off round trips");
	}
	// a graph that would not build (a node this engine does not know) is refused, and the desc keeps what it had
	{
		TerrainGraph broken = mine;
		broken.Nodes[0].Type = "no_such_node";
		WorldDesc d = desc;
		Check(!SetWorldGraphJson(d, TerrainGraphToJsonCompact(broken)).empty() && d.TerrainGraphDefinition == desc.TerrainGraphDefinition, "an unknown node type is refused");
		Check(!SetWorldGraphJson(d, "{not json").empty() && d.TerrainGraphDefinition == desc.TerrainGraphDefinition, "and so is text that is not JSON");
		WorldParams p = loaded.World;
		p.TerrainGraph = TerrainGraphToJsonCompact(broken);
		SaveGame bad = loaded;
		bad.World = p;
		GameSession target(other);
		const WorldParams before = target.World();
		const Result r = ApplyGame(target, bad);
		Check(!r.Ok && target.World() == before, "a save whose graph does not build does not load, and the session is untouched");
	}

	fs::remove_all(dir, ec);
	if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
	else std::printf("world_graph_save: all checks passed\n");
	return failures ? 1 : 0;
}
