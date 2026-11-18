// T310 (C2, documentation/01_diseno/ARRANQUE_Y_LLEGADAS.md §2, §5): the founding of the player's clan (GB/Game/Founding.h, FoundingRules.h).
//   (a) colony.json's "founding" comes through the era1 pack and says what DefaultFoundingRules says; every item of the kit is an item of the game;
//       a bad block is refused whole;
//   (b) the kit: the base for any clan (today's wood 2 + 2 n, stone 3 n), the extra of its number only with a party; 6 people = today's kit;
//   (c) the candidates: 8, deterministic, half women; roll draws one again (the others stay), lock keeps one through `roll all`, pick checks
//       its 1..6 and the party is the picked ones; the default choice of 1 is the most versatile, of 4 two women and two men;
//   (d) a run founded with party 1 (SimRun::Party, as GBSim --party=1 and GB.exe's `tribe build ... party 1`): one member, the person drawn
//       with its trade's aptitude, the kit of 1; over two days it eats and sleeps and lives;
//   (e) saving and loading the clan keeps that person as drawn (the load's Join is overwritten), its aptitude and the store;
//   (f) party 6 and no party: the same store (today's); a party's neighbours never take a party member's name.
#include "GB/Game/Founding.h"
#include "GB/Game/GameContent.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	// The content GBSim loads (Sim/src/main.cpp's EraContent): the era1 pack into the same targets.
	struct Content
	{
		GB::ItemLibrary Items;
		GB::RecipeBook Recipes;
		std::map<std::string, GB::ResourceNodeDef> Nodes;
		GB::TribeConfig Config;
		GB::TechTree Techs;
		std::map<std::string, GB::BuildingDef> Buildings;
		GB::CombatRules Combat;
		GB::DiplomacyRules Diplomacy = GB::DefaultDiplomacyRules();
		std::map<std::string, GB::SpeciesDef> Species = GB::DefaultSpecies();
		std::map<std::string, GB::InteractiveDef> Interactives;
		GB::ControlRules Controls;
		GB::NatureConfig Nature;
		GB::ContentPacks Packs;
		std::string Error;
		Content()
		{
			GB::ContentTargets to;
			to.Items = &Items; to.Recipes = &Recipes; to.Nodes = &Nodes; to.Techs = &Techs; to.Buildings = &Buildings;
			to.Roles = &Config.Roles; to.Agenda = &Config.Agenda; to.Economy = &Config.Economy; to.Work = &Config.Work;
			to.Explore = &Config.Explore; to.Econ = &Config.Econ; to.Combat = &Combat; to.Player = &Config.Player; to.Controls = &Controls;
			to.Social = &Config.Social; to.Society = &Config.Society; to.Nature = &Nature; to.Diplomacy = &Diplomacy; to.Species = &Species; to.Interactives = &Interactives;
			to.Needs = &Config.DataNeeds;
			to.Climate = &Config.Seasons.Climate;
			to.Founding = &Config.Founding;
			GB::AddContentSections(Packs, to);
			Error = Packs.Load(GB::GamePack(), std::string(GB_ASSETS_DIR) + "/data");
		}
	};

	GB::SimSetup SetupOf(const Content& c, const GB::GeneratedWorld& world)
	{
		GB::SimSetup s;
		s.World = &world;
		s.Items = &c.Items; s.Recipes = &c.Recipes; s.Nodes = &c.Nodes; s.Config = &c.Config; s.Techs = &c.Techs; s.Buildings = &c.Buildings;
		s.Diplomacy = &c.Diplomacy; s.CombatRules = &c.Combat; s.Species = &c.Species;
		s.GameSeeds = true;
		return s;
	}

	std::string StoreText(const GB::Inventory& store)
	{
		std::map<std::string, int> n;
		for (const GB::ItemSlot& s : store.Stacks()) n[s.Id] += s.Count;
		std::string t;
		for (const auto& [id, k] : n) t += id + " " + std::to_string(k) + ", ";
		return t;
	}
}

int GBTest_founding(int, char**)
{
	using namespace GB;
	const Content content;
	Check(content.Error.empty(), "the game's pack loads: " + content.Error);
	const FoundingRules& rules = content.Config.Founding;

	// ---- (a) the data
	Check(rules == DefaultFoundingRules(), "colony.json's founding says what DefaultFoundingRules says (" + FoundingRulesToJson(rules) + ")");
	for (int n = 1; n <= 6; ++n)
		for (const auto& [item, count] : FoundingKit(rules, n, true)) Check(content.Items.Find(item) != nullptr, "the kit's " + item + " is an item of the game");
	{
		FoundingRules keep = rules;
		Check(!LoadFoundingRules(R"({ "founding": { "party": { "min": 3, "max": 2 } } })", keep).empty() && keep == rules, "a party whose max is below its min is refused whole");
		Check(!LoadFoundingRules(R"({ "founding": { "kit": { "extra": { "one": { "wood": 1 } } } } })", keep).empty() && keep == rules, "an extra not keyed by a number of people is refused");
		Check(!LoadFoundingRules(R"({ "founding": { "kit": { "base": { "wood": { "fixed": -1 } } } } })", keep).empty() && keep == rules, "a negative count is refused");
		Check(LoadFoundingRules(R"({ "items": [] })", keep).empty() && keep == rules, "a file without the block changes nothing");
	}

	// ---- (b) the kit
	using Kit = std::vector<std::pair<std::string, int>>;
	Check(FoundingKit(rules, 6, true) == Kit{ { "wood", 14 }, { "stone", 18 } }, "6 people with a party: today's kit (wood 14, stone 18)");
	Check(FoundingKit(rules, 4, false) == Kit{ { "wood", 10 }, { "stone", 12 } }, "4 people without a party: the base alone (today's)");
	Check(FoundingKit(rules, 1, true) == Kit{ { "wood", 8 }, { "stone", 6 }, { "dried_berry", 12 }, { "stone_axe", 1 }, { "cordage", 2 }, { "flint", 4 }, { "hide", 2 }, { "herbs", 3 } },
		"1 person with a party: the base and the extra of 1 (ARRANQUE §2.3)");

	// ---- (c) the candidates
	ColonyFounding pool, again;
	pool.Start(content.Config, 5);
	again.Start(content.Config, 5);
	Check(pool.Candidates().size() == 8, "8 candidates");
	int women = 0;
	bool same = pool.Candidates().size() == again.Candidates().size();
	std::set<std::string> names;
	for (size_t i = 0; i < pool.Candidates().size(); ++i)
	{
		const FoundingCandidate& c = pool.Candidates()[i];
		women += c.Who.Female ? 1 : 0;
		same = same && c.Name == again.Candidates()[i].Name && c.Who == again.Candidates()[i].Who && c.SkillLevel == again.Candidates()[i].SkillLevel;
		names.insert(c.Name);
		Check(c.Who.Age >= 16.0f && c.Who.Age <= 40.0f, c.Name + " is an adult of 16 to 40");
		Check(!c.Trade.empty() && !c.Skill.empty() && c.SkillLevel >= 20.0f && c.SkillLevel <= 35.0f, c.Name + " has a suggested trade and 20..35 of its aptitude");
	}
	Check(same, "the same seed draws the same pool");
	Check(women == 4 && names.size() == 8, "half women, eight names");
	ColonyFounding other;
	other.Start(content.Config, 7);
	Check(!(other.Candidates()[0].Who == pool.Candidates()[0].Who), "another seed, other people");

	const FoundingCandidate first = pool.Candidates()[0], third = pool.Candidates()[2];
	Check(pool.Reroll(2).empty() && pool.Candidates()[2].Name != third.Name && !(pool.Candidates()[2].Who == third.Who) && pool.Candidates()[0].Who == first.Who
		&& pool.Candidates()[2].Who.Female == third.Who.Female, "roll 2 draws candidate 2 again (another name, another person, its sex) and leaves the others");
	Check(pool.Lock(0, true).empty() && !pool.Reroll(0).empty(), "a locked candidate is not drawn again");
	Check(pool.RerollAll() == 7 && pool.Candidates()[0].Who == first.Who, "roll all draws the 7 unlocked and keeps the locked");
	Check(pool.Rolls() == 8, "the rolls are counted");
	Check(!pool.Pick({}).empty() && !pool.Pick({ 0, 1, 2, 3, 4, 5, 6 }).empty() && !pool.Pick({ 1, 1 }).empty() && !pool.Pick({ 9 }).empty() && pool.Picked().empty(),
		"pick refuses none, seven, twice the same and an unknown one");
	Check(pool.Pick({ 5, 1, 3 }).empty() && pool.Picked() == std::vector<int>{ 1, 3, 5 } && pool.PartyOf(3) == std::vector<int>{ 1, 3, 5 }, "pick 5,1,3: the party of 3 is them");
	Check(pool.PartyOf(2) == pool.DefaultPick(2), "a party of another size is the default choice");
	{
		const std::vector<int> one = pool.DefaultPick(1);
		float best = -1.0f;
		for (const FoundingCandidate& c : pool.Candidates()) best = std::max(best, c.Aptitude);
		Check(one.size() == 1 && pool.Candidates()[static_cast<size_t>(one[0])].Aptitude == best, "alone: the most versatile (the best sum of aptitudes)");
		int w = 0;
		const std::vector<int> group = pool.DefaultPick(4);
		for (const int s : group) w += pool.Candidates()[static_cast<size_t>(s)].Who.Female ? 1 : 0;
		Check(group.size() == 4 && w == 2, "a group of 4: two women and two men");
	}

	// ---- (d) a clan founded alone
	WorldDesc d;
	d.Type = WorldType::Temperate;
	d.Seed = 5;
	d.Resolution = 129;
	const GeneratedWorld world = WorldGenerator::GenerateData(d);
	SimSetup setup = SetupOf(content, world);
	setup.Party = 1;
	SimRun run(setup);
	Check(run.Ok(), "a run founded with party 1: " + run.Error());
	if (!run.Ok()) { std::fprintf(stderr, "founding: %d failures\n", failures); return 1; }
	Tribe& clan = run.Brain();
	ColonyFounding drawn;
	drawn.Start(content.Config, d.Seed);
	const FoundingCandidate& alone = drawn.Candidates()[static_cast<size_t>(drawn.DefaultPick(1)[0])];
	Check(clan.Members().size() == 1 && clan.Members()[0].Name == alone.Name, "one member, the founding's choice of 1 (" + alone.Name + ")");
	const Person* person = clan.People().Find(alone.Name);
	Check(person && *person == alone.Who, "its person is the one drawn (age, sex, genes, traits), not the founder Join made");
	Check(SkillLevel(clan.Members()[0].Skills, alone.Skill) == alone.SkillLevel, "it starts with " + alone.Skill + " " + std::to_string(alone.SkillLevel));
	for (const auto& [item, count] : FoundingKit(rules, 1, true)) Check(clan.Store().Count(item) == count, "the kit of 1 is in the store whole: " + item + " " + std::to_string(count) + " (" + StoreText(clan.Store()) + ")");
	const int foodBefore = clan.Store().Count("dried_berry");

	std::set<std::string> jobs;
	const double day = static_cast<double>(clan.Config.Needs.DaySeconds);
	for (double t = 0.0; t < 2.0 * day; t += 2.0)
	{
		run.Run(2.0);
		if (!clan.Members()[0].Dead) jobs.insert(clan.Members()[0].Job);
	}
	std::string seen;
	for (const std::string& j : jobs) seen += j + " ";
	std::fprintf(stderr, "founding: alone for two days: jobs %s; store %s\n", seen.c_str(), StoreText(clan.Store()).c_str());
	Check(!clan.Members()[0].Dead, "alone, it lives two days (" + clan.Members()[0].Cause + ")");
	Check(jobs.count("eat") && jobs.count("sleep"), "it eats and sleeps (jobs seen: " + seen + ")");
	Check(clan.Store().Count("dried_berry") < foodBefore || jobs.count("eat"), "it ate");

	// ---- (e) save and load
	{
		const std::string saved = clan.ToJson();
		const Person keep = *clan.People().Find(alone.Name);
		SimSetup back = SetupOf(content, world);
		back.Roster = { { alone.Name, clan.Members()[0].Role } };   // (as a load makes the members again before Tribe::FromJson)
		SimRun loaded(back);
		Check(loaded.Ok(), "the run to load into");
		Tribe& t = loaded.Brain();
		Check(!(*t.People().Find(alone.Name) == keep), "(before loading, AddMember's Join made another person of that name)");
		Check(t.FromJson(saved, false), "the clan loads");
		Check(*t.People().Find(alone.Name) == keep, "loaded, the person is as drawn and lived");
		Check(t.Members()[0].Skills == clan.Members()[0].Skills, "and its aptitudes");
		Check(StoreText(t.Store()) == StoreText(clan.Store()), "and the store");
	}

	// ---- (f) party 6 is today's start; a party's neighbours take other names
	{
		SimSetup six = SetupOf(content, world);
		six.Party = 6;
		SimRun a(six);
		SimSetup plain = SetupOf(content, world);
		plain.Members = 6;
		SimRun b(plain);
		Check(a.Ok() && b.Ok() && a.Brain().Members().size() == 6 && b.Brain().Members().size() == 6, "party 6 and members 6: six each");
		Check(StoreText(a.Brain().Store()) == StoreText(b.Brain().Store()) && a.Brain().Store().Count("wood") == 14 && a.Brain().Store().Count("stone") == 18,
			"party 6: today's kit, the same store as without a party (" + StoreText(a.Brain().Store()) + ")");

		SimSetup two = SetupOf(content, world);
		two.Party = 1;
		two.Clans = 3;
		two.Members = 18;
		SimRun c(two);
		Check(c.Ok(), "party 1 with two neighbours: " + c.Error());
		if (c.Ok())
		{
			std::set<std::string> all;
			size_t count = 0;
			for (const auto& clanOf : c.Clans().All()) for (const TribeMember& m : clanOf->Folk->Members()) { all.insert(m.Name); ++count; }
			Check(c.Brain().Members().size() == 1 && count == 13 && all.size() == count, "the player's 1 and two neighbours of 6, every name once");
			Check(StoreText(c.Clans().All()[1]->Folk->Store()) == StoreText(b.Brain().Store()), "a neighbour's kit is today's (no party extra)");
		}
	}

	if (failures) std::fprintf(stderr, "founding: %d failures\n", failures);
	return failures ? 1 : 0;
}
