// The rocks of the world (documentation/02_tecnico/motor/ROCAS.md): where they stand must not move when their recipes go to data. A hash of the
// placements (position, yaw, scale, variant) and of the recipe sizes (what the navigation blocks around a rock) for generators 1-4, three seeds and
// the three world types, against the values measured on master 21473fb before the refactor.
#include "GB/Log.h"
#include "GB/World/Rocks.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	uint64_t Mix(uint64_t h, const void* data, size_t n)
	{
		const unsigned char* p = static_cast<const unsigned char*>(data);
		for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
		return h;
	}
	template <typename T> uint64_t Pod(uint64_t h, const T& v) { return Mix(h, &v, sizeof v); }

	uint64_t RockHash(const GB::GeneratedWorld& w)
	{
		uint64_t h = 1469598103934665603ull;
		h = Pod(h, static_cast<uint64_t>(w.RockRecipes.size()));
		for (const GB::RockParams& r : w.RockRecipes) h = Pod(h, r.Size);
		h = Pod(h, static_cast<uint64_t>(w.Rocks.size()));
		for (const GB::RockPlacement& r : w.Rocks)
		{
			h = Pod(h, r.Variant);
			h = Pod(h, r.Position.x); h = Pod(h, r.Position.y); h = Pod(h, r.Position.z);
			h = Pod(h, r.Yaw); h = Pod(h, r.Scale);
		}
		return h;
	}
}

int main(int argc, char** argv)
{
	GB::Log::Init();
	const bool print = argc > 1 && std::strcmp(argv[1], "--print") == 0;
	if (argc > 2 && std::strcmp(argv[1], "--list") == 0)   // the rocks of the editor's world of that seed (generate temperate <seed>): where to point a camera
	{
		GB::WorldDesc d;
		d.Seed = static_cast<uint32_t>(std::atoi(argv[2]));
		const GB::GeneratedWorld w = GB::WorldGenerator::GenerateData(d);
		for (const GB::RockPlacement& r : w.Rocks)
			std::printf("%d %.1f %.1f %.1f %.2f\n", r.Variant, r.Position.x, r.Position.y, r.Position.z, r.Scale);
		return 0;
	}

	// ---- placements: the same rocks in the same places as before the refactor
	struct Case { GB::WorldType Type; uint32_t Seed; uint32_t Version; uint64_t Hash; };
	const Case cases[] = {
#include "rocks_test_hashes.inl"
	};
	for (const Case& c : cases)
	{
		GB::WorldDesc d;
		d.Type = c.Type; d.Seed = c.Seed; d.GeneratorVersion = c.Version;
		d.Size = 192.0f; d.Resolution = 161; d.TreeBudget = 150; d.RockBudget = 60;
		const GB::GeneratedWorld w = GB::WorldGenerator::GenerateData(d);
		const uint64_t h = RockHash(w);
		if (print)
			std::printf("\t\t{ GB::WorldType::%s, %uu, %uu, 0x%016" PRIx64 "ull },   // %zu rocks\n", c.Type == GB::WorldType::Alpine ? "Alpine" : c.Type == GB::WorldType::Desert ? "Desert" : "Temperate",
				c.Seed, c.Version, h, w.Rocks.size());
		else
			Check(h == c.Hash, "rock placements of type " + std::to_string(static_cast<int>(c.Type)) + " seed " + std::to_string(c.Seed) + " generator " + std::to_string(c.Version) + " are unchanged");
	}

	// ---- the shipped rocks.json loads, and its kits keep six variants with the sizes of before (what the navigation blocks)
	GB::RockLibrary shipped;
	{
		const std::string e = GB::LoadRockLibraryFile(GB_ROCKS_DATA "/rocks.json", shipped);
		Check(e.empty(), "assets/data/rocks.json loads: " + e);
		const GB::RockLibrary& builtin = GB::BuiltinRockLibrary();
		for (const char* type : { "temperate", "alpine", "desert" })
		{
			const auto* kit = shipped.Kit(type);
			const auto* old = builtin.Kit(type);
			Check(kit && kit->size() == 6, std::string("the ") + type + " kit has six variants");
			if (!kit || !old || kit->size() != old->size()) continue;
			for (size_t i = 0; i < kit->size(); ++i)
			{
				GB::RockParams p;
				Check(shipped.Resolve((*kit)[i], p), std::string(type) + " variant " + std::to_string(i) + " resolves");
				Check(p.Size == (*old)[i].Params.Size, std::string(type) + " variant " + std::to_string(i) + " keeps the size of before");
			}
		}
		// the alpine and desert kits are the built-in recipes, moved to data as they were
		for (const char* type : { "alpine", "desert" })
		{
			const auto* kit = shipped.Kit(type);
			const auto* old = builtin.Kit(type);
			if (!kit || !old || kit->size() != old->size()) continue;
			bool same = true;
			for (size_t i = 0; i < kit->size(); ++i)
			{
				const GB::RockParams& a = (*kit)[i].Params;
				const GB::RockParams& b = (*old)[i].Params;
				auto near = [](float x, float y) { return std::fabs(x - y) < 1e-5f; };
				same = same && a.Detail == b.Detail && a.Planes == b.Planes && near(a.Roughness, b.Roughness) && near(a.Facets, b.Facets) && glm::all(glm::lessThan(glm::abs(a.Stretch - b.Stretch), glm::vec3(1e-5f))) &&
					a.Color == b.Color && a.Moss == b.Moss && a.Sand == b.Sand && near(a.Frequency, b.Frequency) && near(a.Flatten, b.Flatten) && (*kit)[i].Formation == "single";
			}
			Check(same, std::string("the ") + type + " kit is the built-in one");
		}
	}

	// ---- the loader refuses mistakes and leaves the library as it was
	{
		GB::RockLibrary lib;
		lib.Recipes["keep"] = GB::RockParams();
		Check(!GB::LoadRockLibrary(R"({ "recipes": { "a": { "colour": [1, 1, 1] } } })", lib).empty(), "an unknown recipe key is an error");
		Check(!GB::LoadRockLibrary(R"({ "recipes": { "a": {} }, "kits": { "t": [ { "recipe": "b" } ] } })", lib).empty(), "a kit naming a recipe it does not have is an error");
		Check(!GB::LoadRockLibrary(R"({ "recipes": { "a": {} }, "kits": { "t": [ { "recipe": "a", "formation": "heap" } ] } })", lib).empty(), "a kit naming a formation it does not have is an error");
		Check(!GB::LoadRockLibrary(R"({ "formations": { "f": { "archetype": "pyramid" } } })", lib).empty(), "an unknown archetype is an error");
		Check(!GB::LoadRockLibrary(R"({ "rocks": {} })", lib).empty(), "an unknown section is an error");
		Check(lib.Recipes.size() == 1 && lib.Recipes.count("keep"), "a document that fails leaves the library untouched");
		Check(GB::LoadRockLibrary(R"({ "recipes": { "a": { "size": 2, "detail": 2 } }, "formations": { "f": { "archetype": "stack" } }, "kits": { "t": [ { "recipe": "a", "formation": "f", "size": 3 } ] } })", lib).empty(),
			"a small document loads");
		GB::RockParams p;
		Check(lib.Resolve(lib.Kits["t"][0], p) && p.Size == 3.0f && p.Detail == 2 && p.Formation.Archetype == GB::RockArchetype::Stack, "a variant is its recipe with its own keys and its formation");
		Check(lib.Resolve("a", "", p) && p.Size == 2.0f && p.Formation.Archetype == GB::RockArchetype::Single, "a recipe resolves alone, as a single block");
		Check(!lib.Resolve("a", "nope", p) && !lib.Resolve("nope", "", p), "unknown ids do not resolve");
	}

	// ---- formations: finite, on the ground, inside the footprint the navigation blocks (Size x 1.3), and the triangles of each variant
	{
		for (GB::RockArchetype a : { GB::RockArchetype::Single, GB::RockArchetype::Cluster, GB::RockArchetype::Slab, GB::RockArchetype::Spire, GB::RockArchetype::Stack, GB::RockArchetype::Ledge })
		{
			for (uint32_t seed = 1; seed <= 6; ++seed)
			{
				GB::RockParams p;
				p.Seed = seed; p.Size = 1.5f; p.Detail = 3; p.Planes = 6; p.CutNear = 0.6f; p.CutFar = 0.8f; p.Roughness = 0.1f; p.Facets = 0.9f;
				p.Formation.Archetype = a;
				const auto rock = GB::RockFactory::Create(p);
				bool finite = true;
				float lowest = 1e9f;
				for (const GB::Vertex& v : rock->Geometry.Vertices)
				{
					if (!std::isfinite(v.Position.x + v.Position.y + v.Position.z) || !std::isfinite(v.Normal.x + v.Normal.y + v.Normal.z)) finite = false;
					lowest = std::min(lowest, v.Position.y);
				}
				const std::string name = std::string(GB::RockArchetypeName(a)) + " seed " + std::to_string(seed);
				Check(finite, name + ": vertices and normals are finite");
				Check(std::fabs(lowest) < 1e-3f, name + ": its base is on the ground (y = 0)");
				Check(rock->Radius <= p.Size * 1.3f * 1.25f, name + ": it stays about inside the navigation footprint (radius " + std::to_string(rock->Radius) + ")");
				if (seed == 1) std::printf("info: %s: %zu triangles, radius %.2f, height %.2f (size %.1f)\n", GB::RockArchetypeName(a), rock->TriangleCount, rock->Radius, rock->Height, p.Size);
			}
		}
		GB::RockParams one;
		one.Formation.Archetype = GB::RockArchetype::Cluster;
		Check(GB::RockFactory::Create(one)->TriangleCount > GB::RockFactory::Create(GB::RockParams())->TriangleCount, "a formation has more than one block");
		for (const auto& [type, kit] : shipped.Kits)
		{
			std::string line;
			size_t total = 0;
			for (size_t i = 0; i < kit.size(); ++i)
			{
				GB::RockParams p;
				if (!shipped.Resolve(kit[i], p)) continue;
				const auto rock = GB::RockFactory::Create(p);
				total += rock->TriangleCount;
				line += " " + std::to_string(rock->TriangleCount) + "(" + kit[i].Formation + ", r " + std::to_string(rock->Radius).substr(0, 4) + "/" + std::to_string(p.Size * 1.3f).substr(0, 4) + ")";
			}
			std::printf("info: kit %s triangles per variant:%s; %zu in all\n", type.c_str(), line.c_str(), total);
		}
	}

	// ---- a graph names its rocks by id: the spots do not move, the variants index the named rocks
	{
		using GB::TgNode;
		auto node = [](const char* id, const char* type, const char* source, std::map<std::string, double> params)
		{
			TgNode n; n.Id = id; n.Type = type; n.Source = source; n.Params = std::move(params); return n;
		};
		auto graph = [&](std::map<std::string, double> scatter)
		{
			auto g = std::make_shared<GB::TerrainGraph>();
			g->Id = "rocks_by_id";
			scatter["Count"] = 120; scatter["Variants"] = 6; scatter["MinSpacing"] = 5; scatter["MaxSlopeDegrees"] = 60;
			g->Nodes = { node("ground", "heightfield_noise", "", { { "Octaves", 4.0 }, { "Amplitude", 12.0 } }), node("out_height", "output", "height", {}),
				node("stones", "scatter", "", scatter), node("out_rocks", "output", "rocks", {}) };
			g->Edges = { { "ground", "out_height", "in", "out" }, { "ground", "stones", "height", "out" }, { "stones", "out_rocks", "in", "placements" } };
			return g;
		};
		GB::WorldDesc d;
		d.Seed = 5; d.Size = 192.0f; d.Resolution = 161; d.RockBudget = 80; d.TreeBudget = 0; d.Lake = false; d.River = false;
		d.TerrainGraphDefinition = graph({});
		const GB::GeneratedWorld plain = GB::WorldGenerator::GenerateData(d);
		d.TerrainGraphDefinition = graph({ { "Rock:limestone/cluster", 1.0 }, { "Rock:limestone", 3.0 }, { "Rock:nope", 0.0 } });
		const GB::GeneratedWorld named = GB::WorldGenerator::GenerateData(d);
		Check(!plain.Rocks.empty() && plain.Rocks.size() == named.Rocks.size(), "naming the rocks keeps their number (" + std::to_string(plain.Rocks.size()) + ")");
		bool samePlaces = plain.Rocks.size() == named.Rocks.size();
		for (size_t i = 0; samePlaces && i < plain.Rocks.size(); ++i)
			samePlaces = plain.Rocks[i].Position == named.Rocks[i].Position && plain.Rocks[i].Yaw == named.Rocks[i].Yaw && plain.Rocks[i].Scale == named.Rocks[i].Scale;
		Check(samePlaces, "and their places");
		Check(plain.RockRecipes.size() == 6, "without names the world gets the kit of its type");
		Check(named.RockRecipes.size() == 2, "with names, one recipe per named rock (a weight of 0 is left out)");
		int clusters = 0, singles = 0;
		for (const GB::RockPlacement& r : named.Rocks)
		{
			if (r.Variant < 0 || r.Variant >= static_cast<int>(named.RockRecipes.size())) { Check(false, "a named rock's variant indexes the named recipes"); break; }
			(named.RockRecipes[static_cast<size_t>(r.Variant)].Formation.Archetype == GB::RockArchetype::Cluster ? clusters : singles)++;
		}
		std::printf("info: graph by id: %d limestone/cluster, %d limestone\n", clusters, singles);
		Check(named.RockRecipes.size() == 2 && named.RockRecipes[0].Formation.Archetype == GB::RockArchetype::Single && named.RockRecipes[1].Formation.Archetype == GB::RockArchetype::Cluster,
			"the named rocks come in key order: limestone, then limestone/cluster");
		Check(clusters > 0 && singles > clusters, "the weights share the rocks out (3 to 1)");
	}

	// ---- the shipped library is read again when its file changes (an edit shows in the next world, without rebuilding)
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::temp_directory_path() / "gb_rocks_test";
		fs::create_directories(dir);
		auto write = [&](const char* text) { std::ofstream(dir / "rocks.json", std::ios::binary | std::ios::trunc) << text; };
		write(R"({ "recipes": { "a": { "size": 1.0 } }, "kits": { "temperate": [ { "recipe": "a" } ] } })");
		GB::SetRockLibraryDir(dir.string());
		const auto first = GB::ShippedRockLibrary();
		write(R"({ "recipes": { "a": { "size": 1.0 }, "b": { "size": 2.0 } }, "kits": { "temperate": [ { "recipe": "a" }, { "recipe": "b" } ] } })");
		fs::last_write_time(dir / "rocks.json", fs::last_write_time(dir / "rocks.json") + std::chrono::seconds(5));
		const auto second = GB::ShippedRockLibrary();
		Check(first->Recipes.size() == 1 && second->Recipes.size() == 2, "a change to rocks.json is read by the next call");
		write("{ not json");
		fs::last_write_time(dir / "rocks.json", fs::last_write_time(dir / "rocks.json") + std::chrono::seconds(10));
		Check(GB::ShippedRockLibrary()->Source.empty(), "a broken rocks.json falls back to the built-in recipes");
		GB::SetRockLibraryDir("");
		std::error_code ec;
		fs::remove_all(dir, ec);
	}

	if (failures) { std::fprintf(stderr, "%d rocks check(s) failed\n", failures); return 1; }
	std::printf("rocks: all checks passed\n");
	return 0;
}
