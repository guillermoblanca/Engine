// GBSim: the simulation with no window (CPU only), for runs left working on their own and for sweeps (docs/DATOS_INDIVIDUO.md, section 5).
//
//   GBSim --seed=7 --years=2 --stop=clan_dead --stop=stagnant:6 --clans=3 ...   (a year of age is 4 simulated days; --days=N instead of --years=N)
//   GBSim --seed=7 --days=30 --clans=3 --members=6 --biome=temperate --telemetry=run.db [--every=60] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text] [--pack=id] [--know=tech,tech]
//
// Writes one SQLite file with the telemetry of the run. Ctrl+C stops it cleanly: the file is closed and passes the integrity check.
#include "GB/Game/GameContent.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/Tribe.h"
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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

	// The content of the era the Editor loads (assets/data/era1.pack.json), so the clans of a run know the buildings and the technologies:
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
		std::string Error;

		explicit EraContent(const std::string& assets, const std::string& pack)
		{
			GB::ContentTargets to;   // the sections the simulation reads (the list of sections is GameContent's, the same as the editor's)
			to.Items = &Items; to.Recipes = &Recipes; to.Nodes = &Nodes; to.Techs = &Techs; to.Buildings = &Buildings;
			to.Roles = &Config.Roles; to.Agenda = &Config.Agenda; to.Economy = &Config.Economy; to.Work = &Config.Work;
			to.Explore = &Config.Explore; to.Econ = &Config.Econ; to.Combat = &Combat; to.Player = &Config.Player; to.Controls = &Controls;
			to.Social = &Config.Social; to.Society = &Config.Society; to.Nature = &Nature; to.Diplomacy = &Diplomacy; to.Species = &Species; to.Interactives = &Interactives;
			to.Needs = &Config.DataNeeds;   // V5
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
	bool combat = false, hostile = false, content = true, forests = false, report = false;
	std::string biome = "temperate", path, profile, note, pack;
	std::vector<std::string> know;
	for (int i = 1; i < argc; ++i)
	{
		std::string v;
		if (Arg(argv[i], "--seed", v)) seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
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
		else if (Arg(argv[i], "--pack", v)) pack = v;   // V5: the content pack to play (default the game's, GamePack(); "era2" adds the Bronze Age)
		else if (!std::strcmp(argv[i], "--forest")) forests = true;
		else if (!std::strcmp(argv[i], "--report")) report = true;   // at the end, each clan's settlement, fields, economy and goals
		else if (!std::strcmp(argv[i], "--combat")) combat = true;
		else if (!std::strcmp(argv[i], "--no-content")) content = false;   // the bare engine: no buildings, no technologies (the old runs)
		else if (!std::strcmp(argv[i], "--hostile")) hostile = combat = true;
		else { std::fprintf(stderr, "unknown argument '%s'\nusage: GBSim --seed=N --days=D --clans=N --members=N --biome=temperate|alpine|desert --telemetry=run.db [--every=S] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text]\n", argv[i]); return 2; }
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
	const GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(desc);

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
	}
	if (forests && era) setup.Nature = &era->Nature;   // --forest: the patches of bosques.json (docs/BOSQUES.md); the game's pack already has the forest nodes
	setup.Members = members * clans;
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
	GB::SimRun run(setup);
	if (!run.Ok()) { std::fprintf(stderr, "the run could not start: %s\n", run.Error().c_str()); sink.Stop(); return 1; }

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
	while (done < total && !g_Stop)
	{
		const double chunk = std::min(60.0, total - done);
		run.Run(chunk);
		done += chunk;
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
	sink.Stop(done);
	const GB::TelemetryStats st = sink.Stats();
	std::fprintf(stderr, "%s after %.2f days: %llu samples, %llu dropped, %llu events, %d violations\n", g_Stop ? "stopped" : "finished", done / kDay,
		static_cast<unsigned long long>(st.SamplesWritten), static_cast<unsigned long long>(st.SamplesDropped), static_cast<unsigned long long>(st.EventsWritten), run.Monitor().Violations());
	if (report)
	{
		std::vector<const GB::Tribe*> tribes;
		if (run.Clans().Count() > 1) for (const auto& c : run.Clans().All()) tribes.push_back(c->Folk);
		else tribes.push_back(&run.Brain());
		for (const GB::Tribe* t : tribes)
		{
			int alive = 0;
			for (const GB::TribeMember& m : t->Members()) if (!m.Dead) ++alive;
			std::string goals;
			for (const std::string& g : t->Knowledge().Goals()) goals += g + " ";
			std::fprintf(stderr, "\n==== %d alive, era %s, %zu households, %zu fields (wants %d), store %d slots used; goals: %s\n%s\n%s\n%s\n", alive, t->EraName().c_str(),
				t->Households().size(), t->Economy().Fields.size(), t->Config.Econ.Farming.FieldsFor(alive), t->Store().UsedSlots(), goals.c_str(), t->SettlementReport().c_str(),
				t->FieldReport().c_str(), t->EconomyReport().c_str());
			for (const GB::BuildingNeed& n : t->PublicBuildingNeeds()) std::fprintf(stderr, "  needs %s (%.2f): %s\n", n.Building.c_str(), n.Urgency, n.Why.c_str());
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
	}
	for (const GB::SimFinding& f : run.Monitor().Findings())   // what broke an invariant, so a run left alone says it
		std::fprintf(stderr, "violation %s: %s: %s (x%d, %.0f..%.0f s)\n", f.Check.c_str(), f.Subject.c_str(), f.What.c_str(), f.Count, f.First, f.Last);
	return 0;
}
