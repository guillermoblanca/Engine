// Seed sweep of the SIMULATION (H20), not only of the generator (sweep_test.cpp): many generated worlds (seeds x biomes), each with the resource nodes
// the data spreads over it (PlanWorldResources) and a tribe of a few sizes whose members walk the real navigation grid (SimBody: the same task text an
// actor runs, on the grid's paths), simulated for days with no window. At every step the table of invariants of GB/Game/SimQA.h is checked (counts,
// claims, stuck members, deaths explained by a need, store within its bound, fire consistent with its interactive, no NaN, nobody in deep water or
// inside a wall); each run prints one line of metrics (gathered per item, idle %, failures, deaths, meals...), and the same run twice must end
// identically. CPU only.
//
//   (default)            a small set that fits the CI budget (under a minute on a loaded machine)
//   GB_SIM_SWEEP=clan    only the two clans of 20 for 5 days (H7's acceptance; some ten minutes)
//   GB_SIM_SWEEP=full    seeds 1-4 x temperate/alpine/desert x 3, 6 and 12 members x 3 days, abstract runs, and a clan of 20 for 5 days (H7)
//   GB_SIM_SWEEP="one temperate 2 3 3"   one run (biome, seed, members, days; "abstract" at the end for abstract members): to reproduce a run of the sweep
//
// Another lane extends it by adding a function to SimInvariants() or SimMetricTable() (GB/Game/SimQA.h): nothing here changes.
#include "GB/Game/SimQA.h"
#include "GB/AI/Needs.h"
#include "GB/Log.h"
#include "GB/World/WorldGen.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}
	constexpr double kDay = 1200.0;   // NeedsConfig::DaySeconds

	struct Case
	{
		GB::WorldType Type = GB::WorldType::Temperate;
		uint32_t Seed = 1;
		int Members = 4;
		double Days = 1.0;
		bool Abstract = false;
	};

	const char* TypeName(GB::WorldType t) { return t == GB::WorldType::Alpine ? "alpine" : t == GB::WorldType::Desert ? "desert" : "temperate"; }

	GB::WorldDesc Describe(const Case& c)
	{
		GB::WorldDesc d;   // the size of the generator's sweep: a 192 m map is enough for a camp, its surroundings and a few hundred nodes
		d.Type = c.Type;
		d.Seed = c.Seed;
		d.Size = 192.0f;
		d.Resolution = 129;
		d.TreeBudget = 120;
		d.RockBudget = 30;
		return d;
	}

	struct Outcome
	{
		std::string Fingerprint, Line;
		int Violations = 0;
		GB::SimMetrics Metrics;
	};

	Outcome RunCase(const Case& c, const GB::GeneratedWorld& world)
	{
		const auto t0 = std::chrono::steady_clock::now();
		GB::SimSetup setup;
		setup.World = &world;
		setup.Members = c.Members;
		setup.Abstract = c.Abstract;
		GB::SimRun run(setup);
		Outcome out;
		if (!run.Ok()) { Check(false, std::string(TypeName(c.Type)) + " seed " + std::to_string(c.Seed) + ": the run could not start: " + run.Error()); return out; }
		run.Run(c.Days * kDay);
		run.Finish();
		const GB::SimMonitor& mon = run.Monitor();
		out.Fingerprint = run.Fingerprint();
		out.Violations = mon.Violations();
		out.Metrics = mon.Metrics();
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		char head[160];
		std::snprintf(head, sizeof head, "%-9s seed %u n%-2d %.1fd%s | %zu nodes | violations %d | %.1f s | ", TypeName(c.Type), c.Seed, c.Members, c.Days, c.Abstract ? " abstract" : "",
			run.Nodes(), out.Violations, took);
		out.Line = head + mon.Metrics().Line();
		std::fprintf(stderr, "  %s\n", out.Line.c_str());
		if (out.Violations)
		{
			std::fprintf(stderr, "%s\n", mon.Describe(20).c_str());
			int shown = 0;   // the tribe's own words about what failed: where to start looking
			for (const std::string& line : run.Brain().Log())
				if (line.find("could not") != std::string::npos && shown++ < 4) std::fprintf(stderr, "    log: %s\n", line.c_str());
		}
		Check(out.Violations == 0, std::string(TypeName(c.Type)) + " seed " + std::to_string(c.Seed) + " n" + std::to_string(c.Members) + (c.Abstract ? " abstract" : "") + ": every invariant holds at every step");
		return out;
	}

	// The checks catch what they promise: a test of the test, on a hand-made state.
	void CheckTheChecks()
	{
		using namespace GB;
		// the ground: deep water, a trunk and the edge of the map are not places to stand
		CharacterWorld w;
		w.GroundHeight = [](float x, float) { return x > 10.0f ? -3.0f : 0.0f; };
		w.WaterLevel = [](float x, float) { return x > 10.0f ? 0.0f : -1e9f; };
		w.Obstacles.push_back({ { 0.0f, 5.0f }, 0.5f });
		w.BoundsMin = { -50.0f, -50.0f };
		w.BoundsMax = { 50.0f, 50.0f };
		Check(SimGroundAt(w, { 0.0f, 0.0f }).empty(), "dry open ground is fine");
		Check(SimGroundAt(w, { 20.0f, 0.0f }).find("water") != std::string::npos, "3 m of water is too deep");
		Check(SimGroundAt(w, { 0.0f, 5.1f }).find("obstacle") != std::string::npos, "the middle of a trunk is a wall");
		Check(SimGroundAt(w, { 80.0f, 0.0f }) == "off the map", "beyond the bounds is off the map");

		// a node with a negative amount is found, and reported once however many steps it lasts
		GameplayWorld gameplay;
		AbilitySystemComponent* node = gameplay.Create("tree 1");
		node->Tags().Add(GameplayTag("Resource.Node"));
		node->Attributes().Define("Amount", -2.0f, -10.0f, 8.0f);
		SimView view;
		view.Gameplay = &gameplay;
		SimMonitor mon;
		for (int i = 0; i < 5; ++i) { view.Clock = i; mon.Step(view); }
		Check(mon.Violations() == 1 && mon.Findings()[0].Check == "counts" && mon.Findings()[0].Count == 5 && mon.Findings()[0].Subject == "tree 1",
			"a negative node is one finding of 'counts', seen five times");
		Check(mon.CountByCheck().size() == SimInvariants().size(), "every invariant of the table is listed, held or not");
		node->Attributes().SetBase("Amount", 3.0f);
		SimMonitor clean;
		clean.Step(view);
		Check(clean.Violations() == 0, "and a sound node is not");
	}
}

int main()
{
	GB::Log::Init();
	const auto started = std::chrono::steady_clock::now();
	CheckTheChecks();

	const char* mode = std::getenv("GB_SIM_SWEEP");
	const bool full = mode && std::strcmp(mode, "full") == 0;
	const bool clan = mode && std::strcmp(mode, "clan") == 0;   // only the clans of 20 of the full sweep (H7's acceptance)
	const bool one = mode && std::strncmp(mode, "one ", 4) == 0;   // one run, to reproduce what a sweep found
	std::vector<Case> cases;
	if (one)
	{
		char type[32] = {}, flag[32] = {};
		Case c;
		unsigned seed = 1;
		if (std::sscanf(mode + 4, "%31s %u %d %lf %31s", type, &seed, &c.Members, &c.Days, flag) < 4) { std::fprintf(stderr, "GB_SIM_SWEEP=\"one <biome> <seed> <members> <days> [abstract]\"\n"); return 1; }
		c.Type = std::strcmp(type, "alpine") == 0 ? GB::WorldType::Alpine : std::strcmp(type, "desert") == 0 ? GB::WorldType::Desert : GB::WorldType::Temperate;
		c.Seed = seed;
		c.Abstract = std::strcmp(flag, "abstract") == 0;
		cases.push_back(c);
	}
	else if (full)
	{
		for (GB::WorldType type : { GB::WorldType::Temperate, GB::WorldType::Alpine, GB::WorldType::Desert })
			for (uint32_t seed = 1; seed <= 4; ++seed)
				for (int members : { 3, 6, 12 })
					cases.push_back({ type, seed, members, 3.0, false });
		for (GB::WorldType type : { GB::WorldType::Temperate, GB::WorldType::Alpine })
			for (uint32_t seed = 1; seed <= 2; ++seed) cases.push_back({ type, seed, 6, 3.0, true });
		// H7's acceptance: a clan of 20 that organises itself (trades from its needs, skills by practice, the job board) for days
		for (uint32_t seed = 1; seed <= 2; ++seed) cases.push_back({ GB::WorldType::Temperate, seed, 20, 5.0, false });
	}
	else if (clan)
	{
		for (uint32_t seed = 1; seed <= 2; ++seed) cases.push_back({ GB::WorldType::Temperate, seed, 20, 5.0, false });
	}
	else
	{
		// the CI set: every biome, three sizes, one run with abstract members; half a simulated day each (the first one twice: determinism)
		cases = {
			{ GB::WorldType::Temperate, 1, 4, 0.5, false },
			{ GB::WorldType::Temperate, 2, 6, 0.5, false },
			{ GB::WorldType::Alpine, 1, 3, 0.5, false },
			{ GB::WorldType::Desert, 1, 3, 0.5, false },
			{ GB::WorldType::Temperate, 3, 4, 0.5, true },
		};
	}
	std::fprintf(stderr, "sim_sweep: %zu runs (%s)\n", cases.size(), one ? "one" : full ? "full" : clan ? "the clans of 20" : "CI set; GB_SIM_SWEEP=full for the big one");

	std::unique_ptr<GB::GeneratedWorld> world;
	GB::WorldDesc made;
	std::vector<Outcome> outcomes;
	int alive = 0, members = 0;
	for (const Case& c : cases)
	{
		const GB::WorldDesc desc = Describe(c);
		if (!world || made.Type != desc.Type || made.Seed != desc.Seed)
		{
			world = std::make_unique<GB::GeneratedWorld>(GB::WorldGenerator::GenerateData(desc));   // CPU only, no GL
			made = desc;
		}
		outcomes.push_back(RunCase(c, *world));
		alive += outcomes.back().Metrics.Alive;
		members += outcomes.back().Metrics.Members;
		const GB::SimMetrics& m = outcomes.back().Metrics;
		// balance targets (a regression fails here): on a green world a tribe lives its first days and brings home wood and food
		if (c.Type != GB::WorldType::Desert)
		{
			Check(m.Alive == m.Members, std::string(TypeName(c.Type)) + " seed " + std::to_string(c.Seed) + ": the whole tribe lives through the run on a green world");
			Check(m.Gathered.count("wood") && m.Meals > 0, std::string(TypeName(c.Type)) + " seed " + std::to_string(c.Seed) + ": it gathers wood and eats");
		}
	}

	// H12: a camp that moves (a migration forced at a quarter of a day, to a dry place 40 m away): the invariants of the move hold (the camp, the
	// campfire and every member at the new place) and the clan lives on there

	if (!clan && !one)
	{
		const GB::GeneratedWorld moved = GB::WorldGenerator::GenerateData(Describe(cases.front()));
		GB::SimSetup setup;
		setup.World = &moved;
		setup.Members = 4;
		GB::SimRun run(setup);
		Check(run.Ok(), "the migration run starts");
		run.Run(0.25 * kDay);
		const glm::vec2 to = GB::FindFreeSpot(run.Ground(), run.Camp() + glm::vec2(40.0f, 0.0f), 3.0f);
		Check(run.Brain().MoveCamp(to, "a test moves it"), "the camp can be moved");
		run.Run(0.25 * kDay);
		run.Finish();
		int there = 0;
		for (const GB::TribeMember& m : run.Brain().Members()) if (!m.Dead && glm::distance(GB::SimMemberPosition(m), to) < 60.0f) ++there;
		std::fprintf(stderr, "  migration: %s | %d of 4 within the home radius of the new camp\n", run.Monitor().Metrics().Line().c_str(), there);
		if (run.Monitor().Violations()) std::fprintf(stderr, "%s\n", run.Monitor().Describe(20).c_str());
		Check(run.Monitor().Violations() == 0 && run.Monitor().Metrics().Migrations == 1, "a moved camp keeps every invariant (camp, campfire and members at the new place)");
		Check(there == 4 && run.Brain().HasFire(), "and the clan lives there, with its fire");
	}

	// social life (docs/VIDA_SOCIAL.md): a clan with the social rules of the content lives half a day on a generated world (talks, rites, beliefs,
	// diet, traps) and every invariant holds, the social ones too

	if (!clan && !one)
	{
		const GB::GeneratedWorld social = GB::WorldGenerator::GenerateData(Describe(cases.front()));
		GB::TribeConfig config;
		config.Social = GB::DefaultSocialRules();
		GB::SimSetup setup;
		setup.World = &social;
		setup.Members = 5;
		setup.Config = &config;
		GB::SimRun run(setup);
		Check(run.Ok(), "the social run starts");
		run.Run(0.5 * kDay);
		run.Finish();
		int talks = 0;
		for (const auto& [key, n] : run.Brain().Social().Counts) if (key.rfind("talk.", 0) == 0) talks += n;
		std::fprintf(stderr, "  social: %s | %d talks, %d rites held\n", run.Monitor().Metrics().Line().c_str(), talks, static_cast<int>(run.Brain().Social().History.size()));
		if (run.Monitor().Violations()) std::fprintf(stderr, "%s\n", run.Monitor().Describe(20).c_str());
		Check(run.Monitor().Violations() == 0, "a clan with a social life keeps every invariant (the social ones too)");
		Check(talks > 0 && run.Monitor().Metrics().Alive == 5, "and its members talk and live");
	}

	// the same run twice: the same end, step for step (the first case again, on a fresh world from the same seed)

	if (!clan && !one)   // (the clans of 20 are the slow ones: not twice)
	{
		const Case& c = cases.front();
		const GB::GeneratedWorld again = GB::WorldGenerator::GenerateData(Describe(c));
		const Outcome second = RunCase(c, again);
		Check(second.Fingerprint == outcomes.front().Fingerprint, "two runs of the same seed end identically");
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	std::fprintf(stderr, "sim_sweep: %zu runs, %d of %d members alive at the end, took %.1f s\n", cases.size(), alive, members, seconds);
	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("sim_sweep: ok");
	return 0;
}
