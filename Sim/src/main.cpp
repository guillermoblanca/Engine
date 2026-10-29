// GBSim: the simulation with no window (CPU only), for runs left working on their own and for sweeps (docs/DATOS_INDIVIDUO.md, section 5).
//
//   GBSim --seed=7 --years=2 --stop=clan_dead --stop=stagnant:6 --clans=3 ...   (a year of age is 4 simulated days; --days=N instead of --years=N)
//   GBSim --seed=7 --days=30 --clans=3 --members=6 --biome=temperate --telemetry=run.db [--every=60] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text]
//
// Writes one SQLite file with the telemetry of the run. Ctrl+C stops it cleanly: the file is closed and passes the integrity check.
#include "GB/Game/SimQA.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Log.h"
#include "GB/Telemetry/TelemetrySink.h"
#include "GB/World/WorldGen.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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
	bool combat = false, hostile = false;
	std::string biome = "temperate", path, profile, note;
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
		else if (!std::strcmp(argv[i], "--combat")) combat = true;
		else if (!std::strcmp(argv[i], "--hostile")) hostile = combat = true;
		else { std::fprintf(stderr, "unknown argument '%s'\nusage: GBSim --seed=N --days=D --clans=N --members=N --biome=temperate|alpine|desert --telemetry=run.db [--every=S] [--combat] [--hostile] [--years=Y] [--stop=clan_dead|extinct|stagnant:D] [--profile=x] [--note=text]\n", argv[i]); return 2; }
	}
	std::signal(SIGINT, OnSignal);
	std::signal(SIGTERM, OnSignal);

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

	GB::SimSetup setup;
	setup.World = &world;
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
	return 0;
}
