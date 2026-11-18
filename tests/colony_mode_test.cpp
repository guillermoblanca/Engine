// T269 (C1-c, documentation/01_diseno/MODO_COLONIA.md §3 C-1, C-2, C-4, §4.4, §6) and T276: the colony mode of the director (WorldDirector::
// SetColony), the player's game. Checked, over a real world of two clans with buildings (SimRun, as director_test):
//   (a) [Build] in the colony: a hut turned 90 degrees starts its site at once (no sign waiting, no vote, no influence spent) and the site and the
//       building it becomes keep the turn; the footprint is checked turned (a long wall that fits along x does not fit turned along z);
//   (b) the defeat: with the stakes on, the player's clan with somebody alive is not lost; with nobody alive it is ("your clan is gone");
//   (c) the stages count the player's clan, whatever its faith, and no other clan, however much it believes;
//   (d) research and change trade: at once, with no cost; the trade stays fixed past the review of the trades; the god's hand (omen) is refused;
//   (e) saving and loading keeps the colony and its clan; a director without the colony saves as before (no "colony" key);
//   (f) T276: three plans in a row are three sites; calling one off takes it away and what was brought for it goes back to the store.
#include "GB/Game/Clans.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorldDirector.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	GB::DirectorReply Order(GB::WorldDirector& d, const std::string& line)
	{
		std::vector<std::string> words;
		std::istringstream in(line);
		for (std::string w; in >> w;) words.push_back(w);
		return d.Command(words);
	}
	int Alive(const GB::Tribe& t)
	{
		int n = 0;
		for (const GB::TribeMember& m : t.Members()) if (!m.Dead) ++n;
		return n;
	}
}

int GBTest_colony_mode(int, char**)
{
	using namespace GB;
	std::ifstream f(std::string(GB_ASSETS_DIR) + "/data/director.json");
	std::stringstream ss;
	ss << f.rdbuf();
	DirectorRules rules;
	Check(LoadDirectorRules(ss.str(), rules).empty(), "assets/data/director.json loads");

	// ---- the footprint turned (pure): a wall 6 x 0.8 beside a taken square fits along x and not turned along z
	{
		static const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		const BuildingDef& wall = defs.at("palisade");
		SitePlacement s;
		s.Def = &wall;
		s.Camp = s.Fire = { 100.0f, 100.0f };
		s.Taken.push_back({ { 0.0f, 2.5f }, { 0.5f, 0.5f }, 0.0f, "a stone" });
		Check(SiteFits(s, { 0.0f, 0.0f }).empty(), "a wall along x fits beside the stone");
		s.Yaw = 90.0f;
		Check(SiteFits(s, { 0.0f, 0.0f }).find("would stand on a stone") != std::string::npos, "turned 90 degrees (along z) it would stand on the stone");
		Check(SiteFrontTurned({ 0.0f, 0.0f }, wall, 0.0f) == SiteFront({ 0.0f, 0.0f }, wall), "at yaw 0 the front is SiteFront's exactly (the planner's numbers do not move)");
	}

	WorldDesc d;
	d.Type = WorldType::Temperate;
	d.Seed = 3;
	d.Resolution = 129;
	const GeneratedWorld world = WorldGenerator::GenerateData(d);
	DirectorRules lab = rules;
	const std::string stages = R"({ "stages": { "lookSeconds": 20, "list": [
		{ "id": "test", "name": "The test", "need": 3, "goals": [
			{ "id": "people", "type": "population", "count": 2 },
			{ "id": "alive", "type": "believers", "count": 1 },
			{ "id": "plans", "type": "signs_built", "count": 1 } ] } ] } })";
	Check(LoadDirectorRules(stages, lab).empty(), "the test's stage loads");
	static const std::map<std::string, BuildingDef> defs = DefaultBuildings();
	SimSetup setup;
	setup.World = &world;
	setup.Members = 12;
	setup.Clans = 2;
	setup.Director = &lab;
	setup.Buildings = &defs;
	setup.Know = { "wattle_and_daub" };   // (the hut)
	SimRun run(setup);
	if (!run.Ok()) std::fprintf(stderr, "the run: %s\n", run.Error().c_str());
	Check(run.Ok(), "a world of two clans of six, with buildings");
	if (!run.Ok()) { std::fprintf(stderr, "colony_mode: %d failures\n", failures); return 1; }

	WorldDirector& god = run.Director();
	Check(!god.Colony() && god.ToJson().find("\"colony\"") == std::string::npos, "(e) a director without the colony (GBSim, the tests) saves as before");
	const int mine = run.Clans().All()[0]->Id, other = run.Clans().All()[1]->Id;
	Tribe& clan = *run.Clans().All()[0]->Folk;
	Tribe& them = *run.Clans().All()[1]->Folk;
	Check(Order(god, "colony on " + std::to_string(mine)).Ok && god.Colony() && god.ColonyClan() == mine, "director colony on <clan>: the player leads that clan");
	god.SetStakes(true);
	run.Run(10.0);

	// ---- (a) a hut turned 90 degrees, at once
	const float influence = god.Influence();
	const size_t sites = clan.SettlementSites().size();
	const DirectorReply built = Order(god, "build " + std::to_string(mine) + " hut near 90");
	if (!built.Ok) std::fprintf(stderr, "build: %s\n", built.Text.c_str());
	Check(built.Ok && built.Text.find("yaw 90.0") != std::string::npos, "(a) director build <clan> hut near 90: placed");
	Check(clan.SettlementSites().size() == sites + 1, "(a) the site is there at once (no sign waiting, no vote)");
	Check(std::fabs(god.Influence() - influence) < 1e-4f, "(a) no influence spent");
	std::string site;
	for (const DirectorSign& s : god.Signs()) if (s.Status == "accepted") site = s.Site;
	Check(!site.empty() && god.OpenSign(mine) == nullptr, "(a) kept as a plan already accepted, never a sign waiting for a messenger");
	auto yawOf = [&](const std::string& name)
	{
		const EntityWorld& e = *run.View().Entities;
		const Entity* x = e.Find(e.FindByName(name));
		return x ? x->Transform.Rotation.y : -1.0f;
	};
	Check(std::fabs(yawOf(site) - 90.0f) < 1e-3f, "(a) the site stands turned 90 degrees");
	{
		GameplayWorld& g = const_cast<GameplayWorld&>(*run.View().Gameplay);   // (the test finishes it as Tribe::RaiseBuilding does)
		AbilitySystemComponent* a = g.Find(site);
		const BuildingDef& hut = defs.at("hut");
		for (size_t phase = 0; a && phase <= hut.Phases.size(); ++phase)
		{
			const SiteState s = ReadSite(*a, hut);
			if (s.Done) break;
			for (const auto& [item, k] : s.Missing) DeliverToSite(*a, hut, item, k);
			WorkOnSite(*a, hut, std::max(s.WorkNeeded, 0.0f) + 1.0f);
		}
		Check(a && ReadSite(*a, hut).Done, "(a) the test raises it");
	}
	run.Run(5.0);
	bool stands = false;
	for (const DirectorSign& s : god.Signs()) stands = stands || (s.Site == site && s.Status == "built");
	Check(stands && god.SignsBuilt() == 1, "(a) the plan is followed to built (and counted for signs_built)");
	Check(std::fabs(yawOf(site) - 90.0f) < 1e-3f, "(a) the building keeps the turn");
	Check(std::fabs(god.Influence() - influence) < 1e-4f, "(a) a plan that stands gives no influence");

	// ---- (c) the stages: the player's clan with no faith counts; the other clan, believing, does not
	god.SetFaith(mine, 0.0f);
	god.SetFaith(other, 0.95f);
	run.Run(60.0);
	Check(god.GoalOf("alive").Done && god.GoalOf("alive").Value == 1, "(c) believers: only the player's clan counts, with faith 0");
	Check(god.GoalOf("people").Value == Alive(clan) && god.GoalOf("people").Leader == mine, "(c) population: the player's clan leads, the believing one is not looked at");
	Check(god.GoalOf("plans").Done, "(c) signs_built: the player's plan that stands");

	// ---- (d) research, change trade; no omen
	{
		const float before = god.Influence();
		const DirectorReply r = Order(god, "research " + std::to_string(mine) + " pottery");
		if (!r.Ok) std::fprintf(stderr, "research: %s\n", r.Text.c_str());
		Check(r.Ok && clan.ResearchAim() == "pottery" && std::fabs(god.Influence() - before) < 1e-4f, "(d) director research: the clan's project at once, no cost");
		Check(!Order(god, "research " + std::to_string(other) + " pottery").Ok, "(d) another clan's research is not the player's");
		std::string who, to;
		for (const TribeMember& m : clan.Members())
			if (!m.Dead && !m.Player && !clan.People().IsChild(clan.Config.Society, m.Name))
				for (const auto& [role, def] : clan.Config.Roles)
					if (who.empty() && !def.Fixed && !def.Skill.empty() && role != m.Role) { who = m.Name; to = role; }
		const DirectorReply t = Order(god, "role " + who + " " + to);
		if (!t.Ok) std::fprintf(stderr, "role: %s\n", t.Text.c_str());
		Check(t.Ok && clan.FindMember(who)->Role == to && god.TradeFixed(who), "(d) director role: the trade changes at once and is fixed");
		run.Run(2000.0);   // (two reviews of the trades)
		Check(clan.FindMember(who) && (clan.FindMember(who)->Dead || clan.FindMember(who)->Role == to), "(d) the review of the trades leaves it in it");
		Check(Order(god, "role " + who + " auto").Ok && !god.TradeFixed(who), "(d) director role <p> auto: the clan may change it again");
		Check(!Order(god, "omen eclipse " + std::to_string(mine)).Ok, "(d) no omen in the colony");
	}

	// ---- (f) T276: three plans in a row, one called off
	{
		const size_t before = clan.SettlementSites().size();
		std::vector<std::string> placed;
		for (int k = 0; k < 3; ++k)
		{
			const DirectorReply r = Order(god, "build " + std::to_string(mine) + " hut near 0");
			if (!r.Ok) std::fprintf(stderr, "plan %d: %s\n", k, r.Text.c_str());
			const size_t at = r.Text.find("; site ");
			if (r.Ok && at != std::string::npos) placed.push_back(r.Text.substr(at + 7, r.Text.find(';', at + 7) - at - 7));
		}
		Check(placed.size() == 3 && clan.SettlementSites().size() == before + 3, "(f) three plans in a row: three sites");
		if (placed.size() == 3)
		{
			GameplayWorld& g = const_cast<GameplayWorld&>(*run.View().Gameplay);
			AbilitySystemComponent* a = g.Find(placed[1]);
			const BuildingDef& hut = defs.at("hut");
			const SiteState s = a ? ReadSite(*a, hut) : SiteState{};
			std::string item;
			int n = 0;
			if (!s.Missing.empty()) { item = s.Missing.begin()->first; n = DeliverToSite(*a, hut, item, 1); }
			const int stored = clan.Stored(item);
			std::map<std::string, int> back, left;
			Check(n == 1 && clan.CancelSite(placed[1], &back, &left).empty(), "(f) the second plan, with one thing brought, is called off");
			Check(back[item] == 1 && left.empty() && clan.Stored(item) == stored + 1, "(f) what was brought goes back to the store");
			bool gone = true;
			for (const Tribe::SettlementSite& x : clan.SettlementSites()) gone = gone && x.Name != placed[1];
			Check(gone && clan.SettlementSites().size() == before + 2 && !g.Find(placed[1]), "(f) the site is gone, the other two stay");
			run.Run(5.0);
			bool dropped = false;
			for (const DirectorSign& x : god.Signs()) dropped = dropped || (x.Site == placed[1] && x.Status == "dropped");
			Check(dropped, "(f) the director's book drops that plan");
			Check(!clan.CancelSite(placed[1]).empty(), "(f) calling it off again says why not");
		}
	}

	// ---- (e) saving and loading keeps the colony and its clan
	{
		WorldDirector back;
		back.Rules = lab;
		back.SetTargets(god.Targets());
		Check(back.FromJson(god.ToJson()) && back.Colony() && back.ColonyClan() == mine && back.ToJson() == god.ToJson(), "(e) a colony save loads as the colony of the same clan");
		Check(back.TradeFixed("nobody") == false, "(e) (and a fixed trade set free is not saved)");
	}

	// ---- (b) the defeat: somebody alive, no; nobody alive, yes
	{
		std::vector<std::string> names;
		for (const TribeMember& m : clan.Members()) if (!m.Dead) names.push_back(m.Name);
		for (size_t i = 0; i + 1 < names.size(); ++i) clan.Kill(names[i], "OldAge");
		run.Run(2.0);
		Check(Alive(clan) == 1 && !god.Defeated(), "(b) one of the player's clan alive: not lost");
		Check(Alive(them) > 0, "(b) (the other clan lives)");
		clan.Kill(names.back(), "OldAge");
		run.Run(2.0);
		Check(god.Defeated(), "(b) nobody of the player's clan alive: lost, though another clan lives and believes");
		Check(Order(god, "defeat status").Text.find("your clan is gone") != std::string::npos, "(b) director defeat says why: your clan is gone");
		Check(Order(god, "status").Text.find("colony: the player's clan") != std::string::npos, "director status says the colony in a line of its own");
	}

	if (failures == 0) std::printf("colony_mode: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
