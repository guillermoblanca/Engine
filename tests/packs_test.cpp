// Eras as data packs (H17): manifests, load order of the dependencies, atomicity across files and sections, section loaders that Items does not know,
// and the reachability check (positives and negatives) over hand-made content and over assets/data/era1 and era1_ampliado. CPU only.
#include "GB/Game/GameContent.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	bool Has(const std::vector<std::string>& lines, const std::string& part)
	{
		return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(part) != std::string::npos; });
	}

	void Write(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary);
		out << text;
	}
}

using namespace GB;

// The live content of a game, with its packs.
struct Game
{
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Nodes;
	std::map<std::string, TribeRole> Roles;
	TribeAgenda Agenda;
	TribeEconomy Economy;
	WorkRules Work;   // H7: names, aptitudes, job board
	std::vector<std::string> Seen;   // what a third section, one Items knows nothing of, read: proves the order of the files and the plug-in
	ContentPacks Packs;
	Game()
	{
		AddGameContentSections(Packs, Items, Recipes, Nodes, Roles, &Agenda, &Economy, &Work);
		Packs.AddSection(StagedSection("log", { "log" },
			[](const std::string& json, std::vector<std::string>& seen) -> std::string
			{
				const size_t at = json.find("\"log\": \"");
				if (at == std::string::npos) return {};
				const size_t from = at + 8, to = json.find('"', from);
				if (json.compare(from, to - from, "refuse") == 0) return "the log section refuses this document";
				seen.push_back(json.substr(from, to - from));
				return {};
			}, nullptr, Seen));
	}
	std::string Snapshot() const { return ContentToJson(Items, Recipes, Nodes) + RolesToJson(Roles); }
};

int main()
{
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gb_packs_test";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);

	// A small chain of packs: c needs b and a, b needs a. b's file uses an item of a, so it only loads after a.
	Write(dir / "a.pack.json", R"({ "id": "a", "name": "Base", "era": 1, "version": "1", "files": [ "a.json" ], "given": { "twig": "lying around" }, "stations": [ "bench" ] })");
	Write(dir / "a.json", R"({ "log": "a", "items": [ { "id": "twig" }, { "id": "rope" } ], "recipes": [ { "id": "rope", "inputs": { "twig": 2 }, "output": "rope" } ],
		"roles": { "picker": { "weights": { "Resource.Plant": 1 } } } })");
	Write(dir / "b.pack.json", R"({ "id": "b", "era": 2, "depends": [ "a" ], "files": [ "b1.json", "b2.json" ] })");
	Write(dir / "b1.json", R"({ "log": "b1", "items": [ { "id": "snare" } ], "recipes": [ { "id": "snare", "station": "bench", "inputs": { "rope": 1, "twig": 1 }, "output": "snare" } ] })");
	Write(dir / "b2.json", R"({ "log": "b2", "techs": [ { "id": "knots" } ] })");
	Write(dir / "c.pack.json", R"({ "id": "c", "era": 2, "depends": [ "b", "a" ], "files": [ "c.json" ] })");
	Write(dir / "c.json", R"({ "log": "c", "items": [ { "id": "trap" } ], "recipes": [ { "id": "trap", "inputs": { "snare": 2 }, "output": "trap" } ] })");

	// Manifests.
	{
		PackManifest m;
		Check(ParsePackManifest(R"({ "id": "x", "era": 3, "depends": [ "y" ], "files": [ "x.json" ], "given": { "a": "b" }, "stations": [ "s" ], "deferred": { "d": "later" } })", m).empty()
			&& m.Era == 3 && m.Depends.size() == 1 && m.Rules.Given.count("a") && m.Rules.Stations.count("s") && m.Rules.Deferred.at("d") == "later", "a full manifest reads");
		for (const char* bad : { R"({ "name": "no id" })", R"({ "id": "x", "depends": "y" })", R"({ "id": "x", "era": -1 })", R"({ "id": "x", "given": { "a": 1 } })",
			R"({ "id": "x", "depends": [ "x" ] })", R"({ "id": "a b" })", "[1]" })
		{
			PackManifest keep;
			keep.Id = "kept";
			Check(!ParsePackManifest(bad, keep).empty() && keep.Id == "kept", "a bad manifest is refused and changes nothing");
		}
	}

	// Load order: the dependencies first, each once, then the pack; the files of a pack in their order. Every section reads every file.
	{
		Game g;
		ContentReport report;
		const std::string error = g.Packs.Load("c", dir.generic_string(), &report);
		Check(error.empty(), "pack c loads with its dependencies");
		if (!error.empty()) std::fprintf(stderr, "  %s\n", error.c_str());
		Check(g.Seen == std::vector<std::string>({ "a", "b1", "b2", "c" }), "the files are read in dependency order, each once");
		Check(g.Packs.Loaded().size() == 3 && g.Packs.Loaded()[0].Id == "a" && g.Packs.Loaded()[1].Id == "b" && g.Packs.Loaded()[2].Id == "c", "the packs are recorded in load order");
		Check(g.Items.Find("trap") && g.Recipes.Find("snare") && g.Roles.count("picker"), "items, recipes and roles of all of them are in");
		Check(report.Clean(), "and everything can be had (twig given, the bench provided by a)");
		Check(Has(report.Notes, "section 'techs' has no loader"), "a section nobody reads yet is a note, not a mistake");
		// loading b again re-reads b only (a is already in); loading a pack that is loaded through a path works too
		g.Seen.clear();
		Check(g.Packs.Load("b", dir.generic_string()).empty() && g.Seen == std::vector<std::string>({ "b1", "b2" }), "a loaded pack is loaded again alone");
		g.Seen.clear();
		Check(g.Packs.Load((dir / "a.pack.json").generic_string(), "").empty() && g.Seen == std::vector<std::string>({ "a" }) && g.Packs.Loaded().size() == 3, "a manifest by its path");
		const auto available = ContentPacks::Available(dir.generic_string());
		Check(available.size() == 3 && available[0].Id == "a" && available[1].Id == "b", "the packs of a directory are listed by era and id");
	}

	// Atomicity: a mistake anywhere (any file of any pack, any section, a missing or circular dependency) changes nothing.
	{
		Write(dir / "bad_items.pack.json", R"({ "id": "bad_items", "depends": [ "a" ], "files": [ "ok.json", "bad.json" ] })");
		Write(dir / "ok.json", R"({ "log": "ok", "items": [ { "id": "fine" } ], "roles": { "fine": { "default": 1 } } })");
		Write(dir / "bad.json", R"({ "items": [ { "id": "broken", "stack": 0 } ] })");
		Write(dir / "bad_roles.pack.json", R"({ "id": "bad_roles", "files": [ "ok.json", "bad_roles.json" ] })");
		Write(dir / "bad_roles.json", R"({ "roles": { "r": { "tool": "x" } } })");
		Write(dir / "bad_log.pack.json", R"({ "id": "bad_log", "files": [ "ok.json", "bad_log.json" ] })");
		Write(dir / "bad_log.json", R"({ "log": "refuse" })");
		Write(dir / "missing.pack.json", R"({ "id": "missing", "depends": [ "nowhere" ], "files": [] })");
		Write(dir / "nofile.pack.json", R"({ "id": "nofile", "files": [ "ok.json", "ghost.json" ] })");
		Write(dir / "loop1.pack.json", R"({ "id": "loop1", "depends": [ "loop2" ] })");
		Write(dir / "loop2.pack.json", R"({ "id": "loop2", "depends": [ "loop1" ] })");
		Write(dir / "liar.pack.json", R"({ "id": "someone_else" })");

		Game g;
		Check(g.Packs.Load("a", dir.generic_string()).empty(), "the base loads");
		const std::string before = g.Snapshot();
		const std::vector<std::string> seen = g.Seen;
		for (const char* id : { "bad_items", "bad_roles", "bad_log", "missing", "nofile", "loop1", "liar", "nothere" })
		{
			const std::string error = g.Packs.Load(id, dir.generic_string());
			Check(!error.empty(), "a broken pack is refused");
			if (error.empty()) std::fprintf(stderr, "  (accepted: %s)\n", id);
		}
		Check(g.Packs.Load("loop1", dir.generic_string()).find("circle") != std::string::npos, "a circle of dependencies is named");
		Check(g.Packs.Load("bad_items", dir.generic_string()).find("bad.json") != std::string::npos, "the error names the file");
		Check(g.Snapshot() == before && g.Seen == seen && g.Packs.Loaded().size() == 1 && !g.Items.Find("fine") && !g.Roles.count("fine"), "and none of them changed anything");
		Check(!g.Packs.LoadText(R"({ "items": [ { "id": "y" } ], "roles": { "r": { "weights": [] } } })").empty() && !g.Items.Find("y"), "one document whose roles are wrong adds no items");
	}

	// The check: positives and negatives on hand-made content.
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		const std::string doc = R"({
			"items": [ { "id": "rock" }, { "id": "axe", "tags": [ "Item.Tool.Axe" ] }, { "id": "flint_axe", "tags": [ "Item.Tool.Axe.Flint" ] }, { "id": "log" },
			           { "id": "pick", "tags": [ "Item.Tool.Pickaxe" ] }, { "id": "ore" }, { "id": "ingot" }, { "id": "egg" }, { "id": "hen" }, { "id": "ghost" },
			           { "id": "plank" } ],
			"nodes":   [ { "id": "quarry", "item": "rock" }, { "id": "tree", "item": "log", "requires": "Item.Tool.Axe" }, { "id": "vein", "item": "ore", "requires": "Item.Tool.Pickaxe" } ],
			"recipes": [ { "id": "axe", "inputs": { "rock": 2 }, "output": "axe" },
			             { "id": "ingot", "station": "forge", "inputs": { "ore": 1 }, "output": "ingot" },
			             { "id": "egg", "inputs": { "hen": 1 }, "output": "egg" }, { "id": "hen", "inputs": { "egg": 1 }, "output": "hen" },
			             { "id": "plank", "inputs": { "log": 1 }, "output": "plank", "count": 2 }, { "id": "log", "inputs": { "plank": 3 }, "output": "log" } ] })";
		Check(LoadContent(doc, items, recipes, nodes).empty(), "the test content loads");
		PackRules rules;
		const ContentReport r = CheckReachability(items, recipes, nodes, rules);
		Check(!r.Clean(), "it has problems");
		Check(!Has(r.Problems, "'axe'") && !Has(r.Problems, "'log'") && !Has(r.Problems, "'plank'"), "a node gives, a recipe makes, a node with a tool made from it gives");
		Check(Has(r.Problems, "item 'ghost' cannot be had: no node, recipe or given entry"), "an item nothing makes is a problem");
		Check(Has(r.Problems, "node 'vein' can never be worked: it needs a tool tagged Item.Tool.Pickaxe"), "a node whose tool cannot be made is a problem");
		Check(Has(r.Problems, "item 'pick' cannot be had"), "and the tool itself too");
		Check(Has(r.Problems, "recipe 'ingot' can never be made"), "a recipe whose input cannot be had is a problem");
		Check(Has(r.Problems, "recipe cycle among egg, hen that nothing outside it feeds"), "a cycle nothing feeds is a problem");
		Check(Has(r.Notes, "recipe cycle among log, plank: fine"), "a cycle fed from outside is a note");

		// a given pickaxe, the forge provided, the chicken-and-egg deferred: clean
		rules.Given["pick"] = "a gift";
		rules.Given["flint_axe"] = "a gift";
		rules.Stations.insert("forge");
		rules.Deferred["egg"] = "hens arrive with E6";
		rules.Deferred["hen"] = "hens arrive with E6";
		rules.Deferred["ghost"] = "not drawn yet";
		const ContentReport ok = CheckReachability(items, recipes, nodes, rules);
		Check(ok.Clean(), "given, stations and deferrals clear every problem");
		if (!ok.Clean()) std::fprintf(stderr, "%s", ok.Text().c_str());
		Check(Has(ok.Notes, "item 'ghost' cannot be had") && Has(ok.Notes, "deferred: not drawn yet"), "a deferred gap stays visible as a note");
		// a station nobody provides, a tool given by a variant, a stale deferral, a given that is not an item
		rules.Stations.clear();
		rules.Deferred["axe"] = "old note";
		rules.Given["unicorn"] = "dreams";
		const ContentReport mixed = CheckReachability(items, recipes, nodes, rules);
		Check(Has(mixed.Problems, "needs the station 'forge', which no pack provides"), "a recipe at a missing station is a problem");
		Check(Has(mixed.Notes, "'axe' is deferred but can be had now"), "a deferral that is no longer a gap is a note");
		Check(Has(mixed.Problems, "given item 'unicorn' is not an item"), "a given that is not an item is a problem");
		ItemLibrary vi = items; RecipeBook vr; std::map<std::string, ResourceNodeDef> vn = nodes;
		PackRules variant;
		variant.Given["flint_axe"] = "found";
		variant.Given["rock"] = "found";
		const ContentReport v = CheckReachability(vi, vr, vn, variant);
		Check(!Has(v.Problems, "node 'tree'"), "a tool variant (Item.Tool.Axe.Flint) works a node that needs an axe");

		// strict refuses what the check finds; without strict it loads and reports
		Game g;
		ContentReport report;
		ContentPacks::Options strict;
		strict.Strict = true;
		const std::string before = g.Snapshot();
		Check(!g.Packs.LoadText(doc, &report, strict).empty() && !report.Clean() && g.Snapshot() == before, "strict: a problem refuses the load");
		Check(g.Packs.LoadText(doc, &report).empty() && !report.Clean() && g.Items.Find("ghost"), "not strict: it loads and the problems are reported");
		Check(!g.Packs.Check().Clean(), "and a later check finds them again");
	}

	// The real packs: era1 (the code content plus era1.json) is clean, and era1_ampliado, which depends on it, too, even strict.
	{
		Game g;
		RegisterEraOneItems(g.Items, g.Recipes, g.Nodes);
		g.Roles = DefaultRoles();
		const std::string data = std::string(GB_ASSETS_DIR) + "/data";
		ContentReport report;
		ContentPacks::Options strict;
		strict.Strict = true;
		const std::string error = g.Packs.Load("era1", data, &report, strict);
		Check(error.empty(), "the era1 pack loads, strict");
		if (!error.empty() || !report.Clean()) std::fprintf(stderr, "  %s\n%s", error.c_str(), report.Text().c_str());
		Check(report.Clean() && Has(report.Notes, "copper_ingot"), "every item of era 1 can be had; the copper ingot waits for the forge");
		Check(g.Roles == DefaultRoles(), "the roles come in through their section and say what the code says");
		Check(g.Agenda == DefaultAgenda() && g.Economy == DefaultEconomy(), "and the tribe's agenda, food and stock through theirs");
		Check(g.Work == DefaultWorkRules(), "and the clan's names, aptitudes and job board through \"work\"");

		// the same content read straight from era1.json, as before the packs
		ItemLibrary fi; RecipeBook fr; std::map<std::string, ResourceNodeDef> fn;
		RegisterEraOneItems(fi, fr, fn);
		Check(LoadContentFile(data + "/era1.json", fi, fr, fn).empty() && ContentToJson(fi, fr, fn) == ContentToJson(g.Items, g.Recipes, g.Nodes), "the pack gives what era1.json gave");

		ContentReport more;
		const std::string error2 = g.Packs.Load("era1_ampliado", data, &more, strict);
		Check(error2.empty() && more.Clean(), "era1_ampliado loads on top, strict, with no problems");
		if (!error2.empty() || !more.Clean()) std::fprintf(stderr, "  %s\n%s", error2.c_str(), more.Text().c_str());
		Check(g.Packs.Loaded().size() == 2 && g.Packs.Loaded()[1].Id == "era1_ampliado", "after era1, which was already in");
		Check(g.Items.Find("plank") && !Has(more.Notes, "item 'plank'") && Has(more.Notes, "item 'grain' cannot be had"), "planks come from the fallen trunks; grain waits for the fields");

		// era1_ampliado alone pulls era1 in first
		Game h;
		RegisterEraOneItems(h.Items, h.Recipes, h.Nodes);
		Check(h.Packs.Load("era1_ampliado", data, nullptr, strict).empty() && h.Packs.Loaded().size() == 2 && h.Packs.Loaded()[0].Id == "era1", "a pack loads its dependency first");
		Check(ContentToJson(h.Items, h.Recipes, h.Nodes) == ContentToJson(g.Items, g.Recipes, g.Nodes), "and ends with the same content");
	}

	std::filesystem::remove_all(dir);
	if (failures == 0) std::printf("packs: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
