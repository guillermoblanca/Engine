// Knowledge and technology (H11, E4 + E5): the "techs" section (defaults identical to assets/data/era1.json, round trip, atomic load), recipes gated
// by what an agent knows, deterministic discovery by experimenting, observation and teaching that spread a technology, the clan forgetting what nobody
// alive knows, saving (actor names and the "knowledge-clan" section, old saves without them) and the reachability check aware of technologies. CPU only.
// The tribe itself working out a technology is in tests/tribe_test.cpp (KnowledgeInTheTribe).
#include "GB/Game/GameContent.h"
#include "GB/Game/Knowledge.h"
#include "GB/Save/SaveGame.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
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

	bool Said(const std::vector<GB::KnowledgeEvent>& events, const std::string& part)
	{
		return std::any_of(events.begin(), events.end(), [&](const GB::KnowledgeEvent& e) { return e.Text.find(part) != std::string::npos; });
	}

	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}
}

using namespace GB;

static void TestData()
{
	const TechTree defaults = DefaultTechs();
	TechTree fromFile;
	fromFile.Techs.clear();
	Check(LoadTechs(ReadFile(std::string(GB_ASSETS_DIR) + "/data/era1.json"), fromFile).empty(), "era1.json has readable techs");
	Check(fromFile == defaults, "the techs of era1.json are DefaultTechs() (the code and the data say the same)");
	Check(defaults.Starting() == std::vector<std::string>({ "cordage", "fire_making", "stone_tools" }), "stone tools, fire making and cordage are known from the start");
	Check(defaults.ForRecipe("flint_axe") == std::vector<std::string>({ "flint_knapping" }) && defaults.ForRecipe("plank").empty(), "a recipe no technology names is common knowledge");

	TechTree back;
	Check(LoadTechs(TechsToJson(defaults), back).empty() && back == defaults, "TechsToJson reads back the same tree");

	TechTree tree = defaults;
	const std::string bad = R"({ "techs": [ { "id": "weaving", "recipes": [ "cloth" ] }, { "id": "x", "discovery": { "experiment": 2 } } ] })";
	Check(!LoadTechs(bad, tree).empty() && tree == defaults, "a chance above 1 refuses the whole document, and nothing changes");
	Check(!LoadTechs(R"({ "techs": [ { "id": "a", "discovery": { "seconds": 0 } } ] })", tree).empty(), "a try of 0 seconds is refused");
	Check(!LoadTechs(R"({ "techs": [ { "id": "a", "requires": [ "a" ] } ] })", tree).empty(), "a technology cannot require itself");
	Check(!LoadTechs(R"({ "techs": [ { "id": "a", "discovery": { "chance": 0.5 } } ] })", tree).empty(), "an unknown discovery field is refused");
	Check(LoadTechs(R"({ "items": [] })", tree).empty() && tree == defaults, "a document without techs changes nothing");
	Check(LoadTechs(R"({ "techs": [ { "id": "weaving", "requires": [ "basketry" ], "recipes": [ "cordage" ] } ], "knowledge": { "seed": 7 } })", tree).empty()
		&& tree.Find("weaving") && tree.Rules.Seed == 7 && tree.Rules.LessonSeconds == 20.0f && tree.Techs.size() == defaults.Techs.size() + 1, "a technology is added by id and the rules replaced");
}

static void TestGating()
{
	const TechTree techs = DefaultTechs();
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	RegisterEraOneItems(items, recipes, nodes);

	ClanKnowledge open;   // no technologies: everything is known, as before H11
	open.Join("Ana");
	Check(open.CanMake("Ana", "flint_axe") && open.CanGather("Ana", "copper_vein") && open.Knows("Ana", "smelting"), "without technologies everything is known");
	Check(open.KnownRecipes("Ana", recipes).size() == recipes.Ids().size(), "and every recipe can be made");

	ClanKnowledge k;
	k.SetTechs(&techs);
	k.Join("Ana");
	Check(k.CanMake("Ana", "stone_axe") && k.CanMake("Ana", "campfire") && k.CanMake("Ana", "plank"), "the technologies of the start (and what no technology locks) are known");
	Check(!k.CanMake("Ana", "flint_axe") && !k.CanMake("Ana", "basket_wicker"), "flint tools and baskets are not");
	Check(k.CanLearn("Ana", "flint_knapping") && !k.CanLearn("Ana", "smelting"), "flint knapping can be learnt; smelting needs it first");

	Inventory store(40, 0.0f);
	store.Add(items, "branch", 4);
	store.Add(items, "cordage", 2);
	store.Add(items, "flint", 4);
	const std::set<std::string> known = k.KnownRecipes("Ana", recipes);
	Check(recipes.WhyNot(items, store, "flint_axe", "workbench").empty(), "with every input the flint axe can be made by someone who knows how ...");
	Check(recipes.WhyNot(items, store, "flint_axe", "workbench", &known).find("does not know how") != std::string::npos, "... but not by Ana");
	Inventory copy = store;
	Check(!recipes.Craft(items, copy, "flint_axe", "workbench", &known) && copy.Count("flint") == 4, "and crafting it does nothing");
	Check(recipes.Craft(items, copy, "flint_axe", "workbench", nullptr) && copy.Count("flint_axe") == 1, "nullptr knows everything (the old callers)");
	Check(recipes.Known(&known).size() + 4 == recipes.Ids().size(), "four recipes are locked: two flint tools, the basket and the copper ingot");

	TechTree gated = techs;   // a node only those who know a technology work
	gated.Techs["flint_knapping"].Nodes = { "flint_outcrop" };
	ClanKnowledge g;
	g.SetTechs(&gated);
	g.Join("Ana");
	Check(!g.CanGather("Ana", "flint_outcrop") && g.CanGather("Ana", "rock"), "a locked node is not worked by who does not know its technology");
	Check(g.Grant("Ana", "flint_knapping").empty() && g.CanGather("Ana", "flint_outcrop") && g.CanMake("Ana", "flint_axe"), "granting it opens the node and the recipes");
	Check(!g.Grant("Ana", "nothing").empty() && !g.Grant("Nobody", "basketry").empty(), "grant refuses unknown technologies and agents");
}

// Every try until it is found, as text: the same seed, names and clock give the same story.
static std::string Experiments(uint32_t seed, int& tries, int& spoilt)
{
	TechTree techs = DefaultTechs();
	techs.Rules.Seed = seed;
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	RegisterEraOneItems(items, recipes, nodes);
	ClanKnowledge k;
	k.SetTechs(&techs);
	k.Join("Ana");
	Inventory store(40, 0.0f), bag(12, 0.0f);
	store.Add(items, "branch", 20);
	store.Add(items, "reed", 20);
	std::string story;
	tries = 0;
	double now = 0.0;
	for (; tries < 40 && !k.Knows("Ana", "basketry"); now += 1.0)
	{
		const std::vector<KnowledgeEvent> events = k.Experiment("Ana", now, store, bag, recipes);
		for (const KnowledgeEvent& e : events) story += e.Who + ": " + e.Text + "\n";
		if (!events.empty()) ++tries;
	}
	spoilt = 40 - store.Count("branch") - store.Count("reed");
	return story;
}

static void TestExperiment()
{
	int tries1 = 0, tries2 = 0, spoilt1 = 0, spoilt2 = 0;
	const std::string a = Experiments(1, tries1, spoilt1), b = Experiments(1, tries2, spoilt2);
	Check(!a.empty() && a == b && tries1 == tries2 && spoilt1 == spoilt2, "experimenting is deterministic: the same seed tells the same story");
	Check(a.find("discovers Basketry by experimenting with the materials of the basket_wicker") != std::string::npos, "and it ends in the discovery");
	Check(tries1 >= 1 && tries1 <= 8, "the chance grows with each failure: basketry (0.2, growth 0.5) is found within 8 tries");
	Check(spoilt1 <= tries1 - 1, "a failed try spoils at most one unit (a success spoils nothing)");
	bool differs = false;
	for (uint32_t seed = 2; seed < 12 && !differs; ++seed)
	{
		int t = 0, s = 0;
		differs = Experiments(seed, t, s) != a;
	}
	Check(differs, "another seed tells another story");

	// it waits Seconds between tries, needs the materials and what the technology requires
	const TechTree techs = DefaultTechs();
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	RegisterEraOneItems(items, recipes, nodes);
	ClanKnowledge k;
	k.SetTechs(&techs);
	k.Join("Ana");
	Inventory store(40, 0.0f), bag(12, 0.0f);
	Check(k.NextExperiment("Ana", 0.0, store, bag, recipes).empty(), "with nothing at hand there is nothing to try");
	bag.Add(items, "branch", 2);
	store.Add(items, "reed", 5);
	std::string recipe;
	Check(k.NextExperiment("Ana", 0.0, store, bag, recipes, &recipe) == "basketry" && recipe == "basket_wicker", "the store and the bag together hold what the basket needs");
	k.Experiment("Ana", 0.0, store, bag, recipes);
	Check(k.Knows("Ana", "basketry") || k.NextExperiment("Ana", 5.0, store, bag, recipes).empty(), "after a failed try it waits the technology's seconds");
	store.Add(items, "flint", 3);
	store.Add(items, "cordage", 1);
	k.Experiment("Ana", 1000.0, store, bag, recipes);
	Check(!k.Knows("Ana", "smelting") && k.NextExperiment("Ana", 1e6, store, bag, recipes) != "smelting", "smelting is never tried before flint knapping is known");
}

static std::vector<KnowledgeAgent> Camp(bool anaAlive = true)
{
	std::vector<KnowledgeAgent> v(4);
	v[0] = { "Ana", { 0.0f, 0.0f }, anaAlive, true, false };   // knows it, works at the camp
	v[1] = { "Biel", { 4.0f, 0.0f }, true, true, true };       // learns, idle at the camp
	v[2] = { "Cleo", { 40.0f, 0.0f }, true, false, true };     // far away
	v[3] = { "Dan", { 3.0f, 3.0f }, true, true, false };        // at the camp but busy
	return v;
}

static void TestSpread()
{
	const TechTree techs = DefaultTechs();
	ClanKnowledge k;
	k.SetTechs(&techs);
	for (const char* n : { "Ana", "Biel", "Cleo", "Dan" }) k.Join(n);
	k.Grant("Ana", "flint_knapping");

	// observation: Ana makes a flint axe; Biel and Dan are near, Cleo is not
	std::vector<KnowledgeEvent> events = k.WitnessRecipe("Ana", "flint_axe", { 0.0f, 0.0f }, Camp());
	Check(k.Of("Biel")->Progress.at("flint_knapping") > 0.33f && k.Of("Dan")->Progress.count("flint_knapping") && !k.Of("Cleo")->Progress.count("flint_knapping"),
		"those within the observe radius understand a third of it; who is far does not");
	Check(Said(events, "understands 34% of Flint knapping by watching Ana"), "and the log says so");
	k.WitnessRecipe("Ana", "flint_axe", { 0.0f, 0.0f }, Camp());
	events = k.WitnessRecipe("Ana", "flint_pickaxe", { 0.0f, 0.0f }, Camp());
	Check(k.Knows("Biel", "flint_knapping") && k.Knows("Dan", "flint_knapping") && !k.Knows("Cleo", "flint_knapping"), "three sightings and they know it: observation spreads knowledge");
	Check(Said(events, "learns Flint knapping by watching Ana") && !k.Of("Biel")->Progress.count("flint_knapping"), "what is learnt leaves the list of what is half learnt");
	Check(k.WitnessRecipe("Ana", "stone_axe", { 0.0f, 0.0f }, Camp()).empty(), "watching a technology everyone knows teaches nothing");

	// teaching: lessons at the camp for the idle ones only, one technology a lesson, from someone at the camp who knows it
	ClanKnowledge t;
	t.SetTechs(&techs);
	for (const char* n : { "Ana", "Biel", "Cleo", "Dan" }) t.Join(n);
	t.Grant("Ana", "basketry");
	Check(!t.Tick(19.0f) && t.Tick(1.5f), "a lesson every 20 s");
	events = t.Lesson(Camp());
	Check(t.Of("Biel")->Progress.at("basketry") == 0.25f && !t.Of("Dan")->Progress.count("basketry") && !t.Of("Cleo")->Progress.count("basketry"),
		"only the idle learner at the camp gets the lesson");
	Check(Said(events, "understands 25% of Basketry from Ana at the camp"), "the lesson is in the log");
	for (int i = 0; i < 3; ++i) t.Lesson(Camp());
	Check(t.Knows("Biel", "basketry"), "four lessons and Biel knows it");

	// the clan: what the members show at the camp, and what is lost when nobody alive knows it
	Check(!t.Clan().count("basketry"), "the clan does not know it until someone shows it at the camp");
	events = t.Share("Ana");
	Check(t.Clan().count("basketry") && Said(events, "shows the clan how Basketry is done") && t.Share("Ana").empty(), "Ana shows it once");
	ClanKnowledge lone;
	lone.SetTechs(&techs);
	for (const char* n : { "Ana", "Biel", "Cleo", "Dan" }) lone.Join(n);
	lone.Grant("Ana", "basketry");
	lone.Share("Ana");
	std::vector<KnowledgeAgent> dead = Camp(false);
	for (KnowledgeAgent& a : dead) a.Idle = false;   // nobody learns in this lesson
	events = lone.Lesson(dead);
	Check(!lone.Clan().count("basketry") && Said(events, "the clan forgets Basketry"), "the last one who knew it died: the clan forgets it");
	Check(lone.Clan().count("stone_tools"), "the technologies of the start are never forgotten");
}

static void TestSave()
{
	const TechTree techs = DefaultTechs();
	ClanKnowledge k;
	k.SetTechs(&techs);
	for (const char* n : { "Ana", "Biel" }) k.Join(n);
	k.Grant("Ana", "flint_knapping");
	k.Share("Ana");
	k.WitnessRecipe("Ana", "flint_axe", { 0.0f, 0.0f }, Camp());
	k.Tick(7.25f);
	k.SetNames("Ana", { "tech.flint_knapping", "tech.stone_tools", "place.camp" });   // a name that is not a technology is kept

	// through a whole saved game: the agents' names in ActorState::Knowledge and the rest in the knowledge-clan section
	Save::SaveGame game;
	for (const char* n : { "Ana", "Biel" })
	{
		Save::ActorState a;
		a.Name = n;
		a.Knowledge = k.Names(n);
		game.Actors.push_back(a);
	}
	game.Sections.push_back({ "knowledge-clan", 1, k.ToJson() });
	Save::SaveGame parsed;
	Check(Save::ParseSaveGame(Save::SaveGameToString(game), parsed).Ok, "the game saves and loads");
	Check(parsed.Actors[0].Knowledge == std::vector<std::string>({ "tech.flint_knapping", "tech.stone_tools", "place.camp" }), "Ana's names, the other kind kept");

	ClanKnowledge loaded;
	loaded.SetTechs(&techs);
	for (const char* n : { "Ana", "Biel" }) loaded.Join(n);
	for (const Save::ActorState& a : parsed.Actors) loaded.SetNames(a.Name, a.Knowledge);
	Check(loaded.FromJson(parsed.Sections[0].Json), "the section reads back");
	Check(loaded.Agents() == k.Agents() && loaded.Clan() == k.Clan(), "every agent and the clan come back the same (what is known, half learnt, the clocks)");
	Check(loaded.ToJson() == k.ToJson() && Save::SaveGameToString(game) == [&] { Save::SaveGame g = game; g.Sections[0].Json = loaded.ToJson(); return Save::SaveGameToString(g); }(),
		"save, load, save: the same bytes");
	Check(!k.Tick(12.0f) && !loaded.Tick(12.0f) && k.Tick(1.0f) && loaded.Tick(1.0f), "the lesson clock went on where it was");

	// a save from before knowledge: no names and no section leave each member with the technologies of the start
	ClanKnowledge old;
	old.SetTechs(&techs);
	old.Join("Ana");
	old.SetNames("Ana", {});
	Check(old.Of("Ana")->Known == std::set<std::string>({ "cordage", "fire_making", "stone_tools" }), "an old save: the technologies of the start");
	Check(old.FromJson("{\"known\":[\"recipe.stone_axe\"]}") && old.Clan().empty() == false, "the reserved slot of tests/golden/saves/v1_slots reads (its name is kept, not a technology)");
	Check(old.ToJson().find("recipe.stone_axe") != std::string::npos, "and is written back");
	Check(!old.FromJson("[1, 2]") && !old.FromJson("{\"known\": 3}"), "a section that is not readable is refused");
	Check(Save::ParseSaveGame(Save::SaveGameToString(Save::SaveGame()), parsed).Ok && parsed.Actors.empty(), "a game without actors has no knowledge");
}

static void TestReachability()
{
	// The packs of the game with the technologies: clean under strict.
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	std::map<std::string, TribeRole> roles;
	TribeAgenda agenda;
	TribeEconomy economy;
	TechTree techs;
	ContentPacks packs;
	AddGameContentSections(packs, items, recipes, nodes, roles, &agenda, &economy, nullptr, &techs);
	ContentReport report;
	ContentPacks::Options strict;
	strict.Strict = true;
	const std::string dir = std::string(GB_ASSETS_DIR) + "/data";
	Check(packs.Load("era1_ampliado", dir, &report, strict).empty(), "era1 and era1_ampliado load strict with their technologies");
	for (const std::string& p : report.Problems) std::fprintf(stderr, "  %s\n", p.c_str());
	Check(techs.Find("flint_knapping") && techs.Find("tillage") && techs.Find("bone_working"), "both packs' technologies are in");
	Check(Has(report.Notes, "tech 'smelting' can never be discovered") && Has(report.Notes, "deferred"), "smelting waits for the forge: a note, deferred");
	Check(!Has(report.Notes, "section 'techs' has no loader") && !Has(report.Notes, "section 'knowledge' has no loader"), "nothing of the techs section goes unread");

	// Hand-made content: a recipe behind a technology nobody can find.
	TechTree tree;
	TechDef secret;
	secret.Id = "secret";
	secret.Recipes = { "flint_axe" };
	secret.Discovery.Observe = 0.5f;   // only by watching: nobody knows it to be watched
	tree.Techs[secret.Id] = secret;
	ItemLibrary i2;
	RecipeBook r2;
	std::map<std::string, ResourceNodeDef> n2;
	RegisterEraOneItems(i2, r2, n2);
	PackRules rules;
	rules.Given = { { "wood", "" }, { "stone", "" } };
	rules.Stations = { "workbench" };
	rules.Deferred = { { "copper_ingot", "" } };
	ContentReport r = CheckReachability(i2, r2, n2, rules, &tree);
	Check(Has(r.Problems, "tech 'secret' can never be discovered: it cannot be found by experimenting"), "a technology only watching can teach is never discovered");
	Check(Has(r.Problems, "recipe 'flint_axe' can never be made: needs one of the technologies 'secret'"), "and the recipe it locks is unreachable");
	Check(Has(r.Problems, "item 'flint_axe' cannot be had"), "and so is its item");
	tree.Techs["secret"].Discovery.Experiment = 0.1f;
	r = CheckReachability(i2, r2, n2, rules, &tree);
	Check(!Has(r.Problems, "secret") && !Has(r.Problems, "flint_axe"), "found by trying with materials that can be had: everything is reachable again");
	tree.Techs["secret"].Requires = { "ghost" };
	tree.Techs["secret"].Nodes = { "flint_outcrop", "nowhere" };
	r = CheckReachability(i2, r2, n2, rules, &tree);
	Check(Has(r.Problems, "tech 'secret' requires 'ghost', which is not a technology") && Has(r.Problems, "unlocks the node 'nowhere', which does not exist"), "names that point at nothing");
	Check(Has(r.Problems, "node 'flint_outcrop' can never be worked: no technology that unlocks it can be discovered"), "a node behind an undiscoverable technology");
	// a technology whose only recipe needs what it locks (flint behind flint knapping, tried with flint): a loop nothing outside feeds
	TechTree loop;
	TechDef knap;
	knap.Id = "knap";
	knap.Recipes = { "flint_axe" };
	knap.Nodes = { "flint_outcrop" };
	knap.Discovery.Experiment = 0.5f;
	loop.Techs[knap.Id] = knap;
	r = CheckReachability(i2, r2, n2, rules, &loop);
	Check(Has(r.Problems, "tech 'knap' can never be discovered: none of its recipes can be tried"), "a technology tried with what only it unlocks is never found");
	Check(CheckReachability(i2, r2, n2, rules, nullptr).Problems == CheckReachability(i2, r2, n2, rules).Problems, "without technologies the check is the one of before");

	// atomic: a bad technology in a pack's file refuses the whole load
	ItemLibrary i3;
	RecipeBook r3;
	std::map<std::string, ResourceNodeDef> n3;
	TechTree t3 = DefaultTechs();
	ContentPacks p3;
	p3.AddSection(ItemContentSection(i3, r3, n3, t3));
	const TechTree before = t3;
	Check(!p3.LoadText(R"({ "items": [ { "id": "thing" } ], "techs": [ { "id": "bad", "era": -1 } ] })").empty() && t3 == before && !i3.Find("thing"), "a bad tech refuses the items of the same document too");
	Check(p3.LoadText(R"({ "items": [ { "id": "thing" } ], "techs": [ { "id": "good", "recipes": [] } ] })").empty() && t3.Find("good") && i3.Find("thing"), "a good one loads both");
}

int main()
{
	TestData();
	TestGating();
	TestExperiment();
	TestSpread();
	TestSave();
	TestReachability();
	if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
	std::printf("knowledge: all checks passed\n");
	return 0;
}
