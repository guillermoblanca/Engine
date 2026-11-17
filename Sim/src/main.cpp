// GBSim: the simulation with no window (CPU only), for runs left working on their own and for sweeps (docs/DATOS_INDIVIDUO.md, section 5).
//
//   GBSim --seed=7 --years=2 --stop=clan_dead --stop=stagnant:6 --clans=3 ...   (a year of age is 4 simulated days; --days=N instead of --years=N)
//   GBSim --seed=7 --days=30 --clans=3 --members=6 --biome=temperate --telemetry=run.db [--every=60] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text] [--pack=id] [--know=tech,tech]
//   GBSim --graph=assets/data/terrain_graphs/highland_valley_settle.json --seed=5 --days=3 --clans=3 --members=6 ...   (T118: a world made by a terrain graph,
//         3000 m and 1025 samples with 40000 trees and 900 rocks unless --size, --resolution, --trees, --rocks say otherwise: the editor's valley;
//         and as the editor's `village` there, a patch of resources by each starting camp and no wild nodes: --no-patches keeps the wild nodes)
//   GBSim ... --director="2:omen eclipse all" --director="3.5:status"   (T195: an order of the god's director at that day, repeatable; its reply is printed)
//
// Writes one SQLite file with the telemetry of the run. Ctrl+C stops it cleanly: the file is closed and passes the integrity check.
#include "GB/Game/GameContent.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/SocietyMetrics.h"
#include "GB/Game/StoreLayout.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/WorldAlerts.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Log.h"
#include "GB/Telemetry/TelemetrySink.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>

namespace
{
	std::atomic<bool> g_Stop{ false };
	void OnSignal(int) { g_Stop = true; }

	// The flora and resources of the world by kind ("wild berry bush 55" -> "wild berry bush"): how many nodes, how many are empty, what is left.
	void WriteWorld(const GB::SimRun& run, GB::TelemetrySink& sink, double t)
	{
		const GB::SimView view = run.View();
		if (!view.Gameplay) return;
		struct Sum { int Count = 0, Empty = 0; double Amount = 0.0; };
		std::map<std::string, Sum> kinds;
		for (const std::string& name : view.Gameplay->Names())
		{
			const GB::AbilitySystemComponent* a = view.Gameplay->Find(name);
			if (!a || !a->HasTag("Resource.Node")) continue;
			std::string kind = name;
			while (!kind.empty() && (std::isdigit(static_cast<unsigned char>(kind.back())) || kind.back() == ' ')) kind.pop_back();
			const float amount = a->Attributes().Has("Amount") ? a->Attribute("Amount") : 0.0f;
			Sum& s = kinds[kind];
			++s.Count;
			s.Amount += static_cast<double>(amount);
			if (amount <= 0.0f) ++s.Empty;
		}
		for (const auto& [kind, s] : kinds)
		{
			GB::TelemetryWorldNodes row;
			row.Time = t;
			row.Kind = kind;
			row.Count = s.Count;
			row.Empty = s.Empty;
			row.Amount = s.Amount;
			sink.WorldNodes(row);
		}
	}

	// Alive members of every clan (a run of one clan is its Brain()).
	std::vector<int> AliveByClan(const GB::SimRun& run)
	{
		std::vector<int> out;
		auto count = [](const GB::Tribe& t) { int n = 0; for (const GB::TribeMember& m : t.Members()) if (!m.Dead) ++n; return n; };
		if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) out.push_back(count(*c->Folk));
		else out.push_back(count(run.Brain()));
		return out;
	}

	// The content the Editor loads (assets/data/era1.pack.json: every file, one tree, no eras), so the clans of a run know the buildings and the technologies:
	// without it they build nothing. The sections go over the same objects the Editor uses (EditorApp.cpp), and the pack must outlive the run.
	struct EraContent
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
		GB::ContentPacks Packs;
		GB::NatureConfig Nature;
		GB::SocietyMetricRules Metrics;   // T204: the "metrics" section (how the quality of life is weighed)
		GB::DirectorRules Director;       // T195: the "director" section (the god's faith and influence)
		GB::AlertRules Alerts;            // T220: the "alerts" section (the thresholds of the god's alerts; only --report counts them)
		std::string Error;

		explicit EraContent(const std::string& assets, const std::string& pack)
		{
			GB::ContentTargets to;   // the sections the simulation reads (the list of sections is GameContent's, the same as the editor's)
			to.Items = &Items; to.Recipes = &Recipes; to.Nodes = &Nodes; to.Techs = &Techs; to.Buildings = &Buildings;
			to.Roles = &Config.Roles; to.Agenda = &Config.Agenda; to.Economy = &Config.Economy; to.Work = &Config.Work;
			to.Explore = &Config.Explore; to.Econ = &Config.Econ; to.Combat = &Combat; to.Player = &Config.Player; to.Controls = &Controls;
			to.Social = &Config.Social; to.Society = &Config.Society; to.Nature = &Nature; to.Diplomacy = &Diplomacy; to.Species = &Species; to.Interactives = &Interactives;
			to.Needs = &Config.DataNeeds;   // V5
			to.Climate = &Config.Seasons.Climate;   // T14
			to.Metrics = &Metrics;          // T204
			to.Director = &Director;        // T195
			to.Alerts = &Alerts;            // T220
			GB::AddContentSections(Packs, to);
			GB::ContentReport report;
			Error = Packs.Load(pack.empty() ? std::string(GB::GamePack()) : pack, assets + "/data", &report);
			for (const std::string& problem : report.Problems) std::fprintf(stderr, "content check: %s\n", problem.c_str());
		}
	};

	bool Arg(const char* a, const char* name, std::string& out)
	{
		const size_t n = std::strlen(name);
		if (std::strncmp(a, name, n) != 0 || a[n] != '=') return false;
		out = a + n + 1;
		return true;
	}
}

int main(int argc, char** argv)
{
	GB::Log::Init();
	uint32_t seed = 1;
	double days = 3.0, every = 60.0, years = 0.0, stagnantDays = 0.0;
	bool stopClanDead = false, stopExtinct = false;
	int clans = 1, members = 4;
	bool combat = false, hostile = false, content = true, forests = false, report = false, patches = true, game = false;
	std::string biome = "temperate", path, profile, note, pack, graph;
	float size = 0.0f;
	int resolution = 0, trees = -1, rocks = -1;
	std::vector<std::string> know;
	std::vector<std::pair<double, std::string>> orders;   // T195: --director=<day>:<order>
	for (int i = 1; i < argc; ++i)
	{
		std::string v;
		if (Arg(argv[i], "--director", v))
		{
			const size_t colon = v.find(':');
			char* end = nullptr;
			const double at = colon == std::string::npos ? -1.0 : std::strtod(v.c_str(), &end);
			if (colon == std::string::npos || end != v.c_str() + colon || at < 0.0 || colon + 1 >= v.size()) { std::fprintf(stderr, "--director expects <day>:<order>, as --director=\"2:omen eclipse all\"\n"); return 2; }
			orders.emplace_back(at, v.substr(colon + 1));
		}
		else if (Arg(argv[i], "--seed", v)) seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
		else if (Arg(argv[i], "--days", v)) days = std::atof(v.c_str());
		else if (Arg(argv[i], "--years", v)) years = std::atof(v.c_str());
		else if (Arg(argv[i], "--stop", v))
		{
			if (v == "clan_dead") stopClanDead = true;
			else if (v == "extinct") stopExtinct = true;
			else if (v.rfind("stagnant:", 0) == 0) stagnantDays = std::atof(v.c_str() + 9);
			else { std::fprintf(stderr, "unknown stop condition '%s' (clan_dead, extinct, stagnant:<days>)\n", v.c_str()); return 2; }
		}
		else if (Arg(argv[i], "--clans", v)) clans = std::max(1, std::atoi(v.c_str()));
		else if (Arg(argv[i], "--members", v)) members = std::max(1, std::atoi(v.c_str()));
		else if (Arg(argv[i], "--biome", v)) biome = v;
		else if (Arg(argv[i], "--telemetry", v)) path = v;
		else if (Arg(argv[i], "--every", v)) every = std::atof(v.c_str());
		else if (Arg(argv[i], "--profile", v)) profile = v;
		else if (Arg(argv[i], "--note", v)) note = v;
		else if (Arg(argv[i], "--know", v)) { for (size_t s = 0, e; s <= v.size(); s = e + 1) { e = v.find(',', s); if (e == std::string::npos) e = v.size(); if (e > s) know.push_back(v.substr(s, e - s)); } }   // V5: --know=smelting,... every clan starts knowing them
		else if (Arg(argv[i], "--graph", v)) graph = v;   // T118: the world of a terrain graph (a .json of assets/data/terrain_graphs)
		else if (Arg(argv[i], "--size", v)) size = static_cast<float>(std::atof(v.c_str()));
		else if (Arg(argv[i], "--resolution", v)) resolution = std::atoi(v.c_str());
		else if (Arg(argv[i], "--trees", v)) trees = std::atoi(v.c_str());
		else if (Arg(argv[i], "--rocks", v)) rocks = std::atoi(v.c_str());
		else if (Arg(argv[i], "--pack", v)) pack = v;   // V5: the content pack to play (default the game's, GamePack(); "era2" adds the Bronze Age)
		else if (!std::strcmp(argv[i], "--forest")) forests = true;
		// GB.exe's new game (`shell new`: `generate <biome> <seed>` and `village <clans> <members>`): its world, its wild nodes (the content's
		// nature, --forest), its camps and wells, its game animals (--combat); the run is then the menu's start with the same seed
		else if (!std::strcmp(argv[i], "--game")) game = forests = combat = true;
		else if (!std::strcmp(argv[i], "--no-patches")) patches = false;   // T118: a graph's world with the wild nodes instead of a patch by each camp
		else if (!std::strcmp(argv[i], "--report")) report = true;   // at the end, each clan's settlement, fields, economy and goals
		else if (!std::strcmp(argv[i], "--combat")) combat = true;
		else if (!std::strcmp(argv[i], "--no-content")) content = false;   // the bare engine: no buildings, no technologies (the old runs)
		else if (!std::strcmp(argv[i], "--hostile")) hostile = combat = true;
		else { std::fprintf(stderr, "unknown argument '%s'\nusage: GBSim --seed=N --days=D --clans=N --members=N --biome=temperate|alpine|desert --telemetry=run.db [--every=S] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text] [--director=<day>:<order>]\n", argv[i]); return 2; }
	}
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);
#ifdef SIGBREAK
	std::signal(SIGBREAK, OnSignal);   // Ctrl+Break: how the laboratory asks a run to stop on Windows
#endif

	GB::WorldDesc desc;
	desc.Type = biome == "alpine" ? GB::WorldType::Alpine : biome == "desert" ? GB::WorldType::Desert : GB::WorldType::Temperate;
	desc.Seed = seed;
	desc.Size = clans > 1 ? 192.0f * static_cast<float>(1 + clans / 2) : 192.0f;
	desc.Resolution = clans > 1 ? 257 : 129;
	desc.TreeBudget = 120 * clans;
	desc.RockBudget = 30 * clans;
	if (!graph.empty())   // T118: as `terrain_graph preset <graph> <type> <seed> - 3000 1025 40000 900` in the editor
	{
		std::ifstream in(graph, std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (text.empty()) { std::fprintf(stderr, "the terrain graph '%s' does not read\n", graph.c_str()); return 2; }
		if (const std::string error = GB::SetWorldGraphJson(desc, text); !error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 2; }
		desc.GeneratorVersion = GB::kWorldGeneratorLatest;
		desc.Size = 3000.0f;
		desc.Resolution = 1025;
		desc.TreeBudget = 40000;
		desc.RockBudget = 900;
	}
	else if (game)   // GB.exe's `generate <biome> <seed>`: the default world (its size, samples and budgets) with the newest generator
	{
		const GB::WorldDesc made;
		desc.Size = made.Size;
		desc.Resolution = made.Resolution;
		desc.TreeBudget = made.TreeBudget;
		desc.RockBudget = made.RockBudget;
		desc.GeneratorVersion = GB::kWorldGeneratorLatest;
	}
	if (size > 0.0f) desc.Size = std::max(32.0f, size);
	if (resolution > 0) desc.Resolution = static_cast<uint32_t>(std::clamp(resolution, 65, 2049));
	if (trees >= 0) desc.TreeBudget = trees;
	if (rocks >= 0) desc.RockBudget = rocks;
	const auto made = std::chrono::steady_clock::now();
	const GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(desc);
	if (!graph.empty())
	{
		std::fprintf(stderr, "world of the terrain graph %s: %.0f m, %u samples, %zu trees, %zu rocks, %zu settlement sites, %zu trails (%.1f s)\n", graph.c_str(), static_cast<double>(desc.Extent()),
			desc.Resolution, world.Trees.size(), world.Rocks.size(), world.Settlements.size(), world.Trails.size(), std::chrono::duration<double>(std::chrono::steady_clock::now() - made).count());
		double length = 0.0, tread = 0.0;
		for (const GB::Trail& t : world.Trails) length += static_cast<double>(t.Length);
		const GB::TgField& path = world.PathField;
		for (float v : path.Values) tread += v > 0.5f ? 1.0 : 0.0;
		const double cell = static_cast<double>(path.Spacing());
		std::fprintf(stderr, "trails %.0f m; path field %u x %u, %.0f m2 over 0.5 (%.1f m wide on average): the navigation's trodden ground\n", length, path.N, path.N, tread * cell * cell, length > 0.0 ? tread * cell * cell / length : 0.0);
		for (const GB::SettlementSite& s : world.Settlements)
			std::fprintf(stderr, "site %d at (%.0f, %.0f): water %.0f m (the graph's), %.0f m (the baked map's: BiomeMap::WaterDistance)\n", s.Rank, static_cast<double>(s.Position.x), static_cast<double>(s.Position.y),
				static_cast<double>(s.WaterDistance), static_cast<double>(world.WaterDistanceAt(s.Position.x, s.Position.y)));
	}

	GB::TelemetrySink sink;
	if (!path.empty())
	{
		GB::TelemetryRunInfo info;
		info.Seed = seed;
		info.Profile = profile.empty() ? biome : profile;
		info.Note = note;
		const std::string error = sink.Start(path, info);
		if (!error.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
	}

	std::unique_ptr<EraContent> era;
	if (content)
	{
		era = std::make_unique<EraContent>(GB_ASSETS_DIR, pack);
		if (!era->Error.empty()) { std::fprintf(stderr, "the content of the era did not load: %s\n", era->Error.c_str()); return 1; }
	}
	GB::SimSetup setup;
	setup.World = &world;
	setup.Know = know;
	setup.GameSeeds = true;   // T0: a game: every system takes its seed from --seed (GB/Game/GameSeeds.h)
	if (era)
	{
		setup.Items = &era->Items;
		setup.Recipes = &era->Recipes;
		setup.Nodes = &era->Nodes;
		setup.Config = &era->Config;
		setup.Techs = &era->Techs;
		setup.Buildings = &era->Buildings;
		setup.Diplomacy = &era->Diplomacy;
		setup.CombatRules = &era->Combat;
		setup.Species = &era->Species;
		setup.Metrics = &era->Metrics;
		setup.Director = &era->Director;
	}
	if (forests && era) setup.Nature = &era->Nature;   // --forest: the patches of bosques.json (docs/BOSQUES.md); the game's pack already has the forest nodes
	setup.Members = members * clans;
	setup.CampPatches = !graph.empty() && patches && era;   // T118 (the patches need the content's node definitions)
	setup.Clans = clans;
	setup.Combat = combat;
	setup.HostileClans = hostile;
	if (clans > 1)   // the budgets of the scale runs: without them a goal across water makes searches over the whole map (seed 1, 3 clans, did not pass 300 s in a quarter of an hour)
	{
		setup.PlanBudget = 4;
		setup.PlanCells = 20000;
		setup.PathCache = 128;
		setup.ThinkPerFrame = 6;
	}
	setup.Telemetry = path.empty() ? nullptr : &sink;
	setup.TelemetryEvery = every;
	if (game) { setup.GameStart = true; setup.ClanApart = 90.0f; }   // (`clan new` asks ClanWorld::ChooseHome for 90 m apart)
	GB::SimRun run(setup);
	if (!run.Ok()) { std::fprintf(stderr, "the run could not start: %s\n", run.Error().c_str()); sink.Stop(); return 1; }
	if (game)   // where each clan settled and dug its well: GB.exe's `clan list` and `entity info "Well <clan>"` say the same
	{
		const GB::SimView view = run.View();
		size_t k = 0;
		for (const auto& c : run.Clans().All())
		{
			const glm::vec2 camp = c->Folk->Config.Economy.Camp;
			const GB::EntityId well = view.Entities->FindByName(k++ == 0 ? std::string("Well") : "Well " + c->Name);
			const glm::vec4 at = well != GB::NullEntity ? view.Entities->WorldMatrix(well)[3] : glm::vec4(0.0f);
			std::fprintf(stderr, "game start: %s camp (%.1f, %.1f), well (%.1f, %.1f)\n", c->Name.c_str(), static_cast<double>(camp.x), static_cast<double>(camp.y), static_cast<double>(at.x), static_cast<double>(at.z));
		}
	}
	run.Director().SetStakes(true);   // §6.2 A: a run is the god's game (only the director reads it: --report says the day of the defeat)
	if (!graph.empty())   // T118: where each clan settled, and on which of the graph's sites
	{
		std::vector<std::pair<std::string, const GB::Tribe*>> camps;
		if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) camps.emplace_back(c->Name, c->Folk);
		else camps.emplace_back("Clan 1", &run.Brain());
		for (const auto& [name, folk] : camps)
		{
			const glm::vec2 camp = folk->Config.Economy.Camp;
			int site = -1;
			float off = 1e9f;
			for (const GB::SettlementSite& s : world.Settlements) if (glm::distance(s.Position, camp) <= s.Radius && glm::distance(s.Position, camp) < off) { off = glm::distance(s.Position, camp); site = s.Rank; }
			const std::string where = site >= 0 ? "graph site " + std::to_string(site) + ", " + std::to_string(static_cast<int>(off)) + " m from its centre" : std::string("not on a graph site");
			std::fprintf(stderr, "camp of %s at (%.1f, %.1f), ground %.2f (terrain %.2f): %s\n", name.c_str(), static_cast<double>(camp.x), static_cast<double>(camp.y), static_cast<double>(folk->Config.Economy.CampY),
				static_cast<double>(world.TerrainRef->GetHeight(camp.x, camp.y)), where.c_str());
		}
	}

	constexpr double kDay = 1200.0;
	constexpr double kYearDays = 4.0;   // Life.DaysPerYear
	const double total = (years > 0.0 ? years * kYearDays : days) * kDay;
	std::string reason;
	std::vector<int> peakAlive;
	double lastGrowth = 0.0;
	const auto t0 = std::chrono::steady_clock::now();
	double done = 0.0;
	int lastDay = 0;
	if (setup.Telemetry) WriteWorld(run, sink, 0.0);
	// T195: the director's orders, in the order of their day (two on the same day keep the order of the line); each runs between steps, at its
	// exact time, and its reply is printed
	std::stable_sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	size_t nextOrder = 0;
	GB::AlertBoard alerts;   // T220 (--report: how many of each the god would have seen)
	std::map<const GB::Tribe*, std::pair<double, long>> foodIn;   // D47 (--report): each clan's day start and the food it brought in before it
	const GB::AlertRules alertRules = era ? era->Alerts : GB::AlertRules{};
	auto runOrders = [&]()
	{
		for (; nextOrder < orders.size() && orders[nextOrder].first * kDay <= done + 1e-6; ++nextOrder)
		{
			std::vector<std::string> words;
			for (size_t s = 0, e; s < orders[nextOrder].second.size(); s = e + 1)
			{
				e = orders[nextOrder].second.find(' ', s);
				if (e == std::string::npos) e = orders[nextOrder].second.size();
				if (e > s) words.push_back(orders[nextOrder].second.substr(s, e - s));
			}
			const GB::DirectorReply reply = run.Director().Command(words);
			std::fprintf(stderr, "director (day %.2f) %s: %s %s\n", done / kDay, orders[nextOrder].second.c_str(), reply.Ok ? "ok" : "refused", reply.Text.c_str());
		}
	};
	while (done < total && !g_Stop)
	{
		runOrders();
		double chunk = std::min(60.0, total - done);
		if (nextOrder < orders.size() && orders[nextOrder].first * kDay > done) chunk = std::min(chunk, orders[nextOrder].first * kDay - done);
		run.Run(chunk);
		done += chunk;
		if (report)   // D47: the food each clan brought in over the run (the tribe keeps only today's and yesterday's): yesterday's, at each new day of its
		{
			std::vector<const GB::Tribe*> now;
			if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) now.push_back(c->Folk);
			else now.push_back(&run.Brain());
			for (const GB::Tribe* t : now)
			{
				auto [it, fresh] = foodIn.try_emplace(t, t->Economy().DayStart, 0L);
				if (!fresh && it->second.first != t->Economy().DayStart) { it->second.second += t->Economy().FoodYesterday; it->second.first = t->Economy().DayStart; }
			}
		}
		if (report && run.Clans().Count() > 0) alerts.Look(run.Clans(), alertRules);   // T220: the god's alerts, read off the clans (nothing in the run reads them)
		const std::vector<int> alive = AliveByClan(run);
		peakAlive.resize(alive.size(), 0);
		for (size_t i = 0; i < alive.size(); ++i) if (alive[i] > peakAlive[i]) { peakAlive[i] = alive[i]; lastGrowth = done; }
		if (stopClanDead) for (size_t i = 0; i < alive.size(); ++i) if (alive[i] == 0 && peakAlive[i] > 0) reason = "clan " + std::to_string(i) + " died out";
		if (stopExtinct && std::all_of(alive.begin(), alive.end(), [](int n) { return n == 0; })) reason = "every clan died out";
		if (stagnantDays > 0.0 && done - lastGrowth >= stagnantDays * kDay) reason = "no clan has grown its best population for " + std::to_string(static_cast<int>(stagnantDays)) + " days";
		if (!reason.empty()) { std::fprintf(stderr, "stop condition: %s (day %.1f)\n", reason.c_str(), done / kDay); break; }
		if (static_cast<int>(done / kDay) != lastDay)
		{
			lastDay = static_cast<int>(done / kDay);
			if (setup.Telemetry) WriteWorld(run, sink, done);
			std::fprintf(stderr, "day %d / %.0f  (%.0f s of wall time)\n", lastDay, days, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
	}
	run.Finish();
	if (setup.Telemetry) WriteWorld(run, sink, done);
	// T204: the society of the last day, when the clan world's clock stopped short of closing it (one clan: the clan world does not run, no rows)
	if (setup.Telemetry && run.Clans().Count() > 1 && run.Clans().Days() < static_cast<int>(std::lround(done / kDay)))
		GB::WriteSocietyDays(run.Clans(), sink, run.Clans().Time(), static_cast<int>(std::lround(done / kDay)));
	sink.Stop(done);
	const GB::TelemetryStats st = sink.Stats();
	std::fprintf(stderr, "%s after %.2f days: %llu samples, %llu dropped, %llu events, %d violations\n", g_Stop ? "stopped" : "finished", done / kDay,
		static_cast<unsigned long long>(st.SamplesWritten), static_cast<unsigned long long>(st.SamplesDropped), static_cast<unsigned long long>(st.EventsWritten), run.Monitor().Violations());
	if (report)
	{
		// T14: the weather of the run, day by day (the clans share it: the first clan's clock), and the wet days of each season
		{
			const GB::SeasonClock& sky = run.Brain().Config.Seasons;
			std::map<std::string, std::map<std::string, int>> bySeason;
			std::string days;
			for (int d = 0; static_cast<double>(d) * kDay < done; ++d)
			{
				const double at = (static_cast<double>(d) + 0.5) * kDay;
				const std::string w = sky.WeatherAt(at);
				const std::string season = GB::SeasonClock::Name(sky.At(at));
				++bySeason[season][w];
				++bySeason[season]["days"];
				days += w == "clear" ? '.' : w == "lightrain" ? 'l' : w == "rain" ? 'r' : w == "storm" ? 's' : w == "snow" ? 'n' : w == "blizzard" ? 'b' : '?';
			}
			std::fprintf(stderr, "\nclimate: seed %u, days %s (. clear, l light rain, r rain, s storm, n snow, b blizzard)\n", sky.Climate.Seed, days.c_str());
			for (const char* season : { "spring", "summer", "autumn", "winter" })
			{
				const auto it = bySeason.find(season);
				if (it == bySeason.end()) continue;
				const std::map<std::string, int>& n = it->second;
				auto of = [&](const char* k) { const auto f = n.find(k); return f == n.end() ? 0 : f->second; };
				std::fprintf(stderr, "climate: %s %d days: %d wet (%d rain on the fields: %d light, %d rain, %d storm; %d snow, %d blizzard)\n", season, of("days"),
					of("days") - of("clear"), of("lightrain") + of("rain") + of("storm"), of("lightrain"), of("rain"), of("storm"), of("snow"), of("blizzard"));
			}
		}
		std::vector<const GB::Tribe*> tribes;
		if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) tribes.push_back(c->Folk);
		else tribes.push_back(&run.Brain());
		for (const GB::Tribe* t : tribes)
		{
			int alive = 0;
			for (const GB::TribeMember& m : t->Members()) if (!m.Dead) ++alive;
			std::string goals;
			for (const std::string& g : t->Knowledge().Goals()) goals += g + " ";
			std::fprintf(stderr, "\n==== %d alive, knows %zu technologies, %zu households, %zu fields (wants %d), store %d slots used; goals: %s\n%s\n%s\n%s\n", alive, t->Knowledge().Clan().size(),
				t->Households().size(), t->Economy().Fields.size(), t->Config.Econ.Farming.FieldsFor(alive), t->Store().UsedSlots(), goals.c_str(), t->SettlementReport().c_str(),
				t->FieldReport().c_str(), t->EconomyReport().c_str());
			for (const GB::BuildingNeed& n : t->PublicBuildingNeeds()) std::fprintf(stderr, "  needs %s (%.2f): %s\n", n.Building.c_str(), n.Urgency, n.Why.c_str());
			// T248: where the store's things lie (in its store buildings or by the fire)
			const GB::StoreLayout layout = GB::StoreLayout::Of(*t);
			std::string lay;
			for (const GB::StoreLayout::Place& p : layout.Places()) lay += " " + p.Name + " " + std::to_string(p.Units()) + "/" + std::to_string(p.FoodRoom + p.Room) + ";";
			std::fprintf(stderr, "  store layout: %d in buildings, %d by the fire;%s\n", layout.InBuildings(), layout.AtCampUnits(), lay.c_str());
			std::fprintf(stderr, "  store upgrades %d (%zu being built); food lost lately %d\n", t->Stores().Upgraded(), t->Stores().Upgrades().size(),
				t->Stores().SpoiledLately(t->Time(), t->Config.Econ.Logistics.SpoilMemory));   // T256
			// D47: the food stores standing and how full they are; the food brought in over the run, what of it went off and what is kept now
			int foodStores = 0, held = 0, room = 0;
			for (const GB::StoreLayout::Place& p : layout.Places()) if (p.FoodRoom > 0) { ++foodStores; held += p.Units(); room += p.FoodRoom + p.Room; }
			long brought = 0;
			if (const auto f = foodIn.find(t); f != foodIn.end()) brought = f->second.second;
			brought += t->Economy().FoodToday;
			int spoiled = 0, kept = 0;
			for (const auto& [item, n] : t->Economy().Spoiled) if (t->IsFood(item)) spoiled += n;
			for (const GB::ItemSlot& s : t->Store().Stacks()) if (t->IsFood(s.Id)) kept += s.Count;
			std::fprintf(stderr, "  food stores %d, %d%% full; food brought in %ld, went off %d (%.1f%%), kept %d; food days %.1f\n", foodStores, room > 0 ? 100 * held / room : 0, brought, spoiled,
				brought > 0 ? 100.0 * spoiled / static_cast<double>(brought) : 0.0, kept, static_cast<double>(t->FoodDays()));
		}
		// T260: footprints that overlap at the end (sites, buildings and fields of every clan: GB/Game/GroundFootprint.h)
		{
			const GB::SimView view = run.View();
			const auto* defs = run.Brain().Buildings();
			if (view.Entities && view.Gameplay && defs)
			{
				const std::vector<GB::GroundRect> ground = GB::WorldGround(*view.Entities, *view.Gameplay, *defs, run.Clans().Count() > 0 ? &run.Clans() : nullptr, &run.Brain());
				const auto pairs = GB::OverlappingPairs(ground);
				std::fprintf(stderr, "\nfootprints: %zu, %zu overlapping pairs\n", ground.size(), pairs.size());
				for (const auto& [a, b] : pairs) std::fprintf(stderr, "  overlap: %s / %s\n", ground[a].What.c_str(), ground[b].What.c_str());
			}
		}
		// V1: how big the world's history and its memories grew (bounded: GB/Game/History.h)
		const GB::WorldHistory& past = run.Clans().Past();
		std::map<std::string, int> kinds;
		for (const GB::HistoryEvent& e : past.Events()) ++kinds[e.Kind];
		std::string list;
		for (const auto& [k, n] : kinds) list += " " + k + " " + std::to_string(n);
		std::fprintf(stderr, "\nhistory: %zu events, %zu memories;%s\n", past.Events().size(), past.MemoryCount(), list.c_str());
		// V2: rumours: memories heard from someone, and of them those about another clan's event (news that crossed between clans)
		int heard = 0, crossed = 0;
		for (const auto& c : run.Clans().All())
			for (const GB::TribeMember& m : c->Folk->Members())
				if (const std::vector<GB::Recollection>* mem = past.MemoriesOf(m.Name))
					for (const GB::Recollection& r : *mem)
					{
						if (r.How != "heard") continue;
						++heard;
						if (const GB::HistoryEvent* e = past.Find(r.Event); e && e->Clan >= 0 && e->Clan != c->Id) ++crossed;
					}
		// (measured where a rumour is told across clans: an envoy telling news of its own clan; a member who moved keeps its memories but is not a rumour)
		std::vector<double> told = past.Crossings;
		std::sort(told.begin(), told.end());
		double sum = 0.0;
		for (double d : told) sum += d;
		std::fprintf(stderr, "history: rumours %d heard, %d of another clan's events; %zu told across clans, %.2f days after the event on average (median %.2f, quickest %.2f)\n", heard, crossed,
			told.size(), told.empty() ? 0.0 : sum / static_cast<double>(told.size()) / kDay, told.empty() ? 0.0 : told[told.size() / 2] / kDay, told.empty() ? 0.0 : told.front() / kDay);
		// T204 (S1): how each society stands (GB/Game/SocietyMetrics.h; only reads), and the world
		std::fprintf(stderr, "\n");
		if (run.Clans().Count() > 1)
		{
			const std::vector<GB::SocietyMeasure> societies = GB::MeasureSocieties(run.Clans());
			for (const GB::SocietyMeasure& m : societies) std::fprintf(stderr, "%s\n", GB::SocietyLine(m).c_str());
			std::fprintf(stderr, "%s\n", GB::SocietyWorldLine(GB::MeasureWorld(run.Clans(), societies)).c_str());
		}
		else std::fprintf(stderr, "%s\n", GB::SocietyLine(GB::MeasureSociety(run.Brain(), era ? era->Metrics : GB::SocietyMetricRules{}, 0)).c_str());
		// T220: the god's alerts raised over the run (looked at once a minute of game, as WorldAlerts.h; a dismissal is the player's: none here)
		std::fprintf(stderr, "alerts raised: %s; still up at the end: %zu\n", alerts.RaisedLine().c_str(), alerts.Shown().size());
		std::fprintf(stderr, "building walls: %zu circles, grid redone %d times, %d members moved out of a new wall; paths %ld, failed %ld\n", run.Walls().Circles,
			run.Walls().Syncs, run.Walls().Evicted, run.Navigation().Stats().Paths, run.Navigation().Stats().Failed);   // T6
		std::fprintf(stderr, "\n%s\n", run.Director().ReportLine().c_str());   // D43/T241/§6.2: the faith of each clan, the votes, the skeptics, the defeat (only the director's)
		std::fprintf(stderr, "%s\n", run.Director().StageReport().c_str());   // D48: the stage of the legacy, its goals and the day it was won
		if (!orders.empty()) std::fprintf(stderr, "\n%s\n", run.Director().Status().c_str());   // T195 (only a run with orders: the report of one without stays as it was)
	}
	for (const GB::SimFinding& f : run.Monitor().Findings())   // what broke an invariant, so a run left alone says it
		std::fprintf(stderr, "violation %s: %s: %s (x%d, %.0f..%.0f s)\n", f.Check.c_str(), f.Subject.c_str(), f.What.c_str(), f.Count, f.First, f.Last);
	return 0;
}
