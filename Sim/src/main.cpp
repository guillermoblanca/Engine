// GBSim: the simulation with no window (CPU only), for runs left working on their own and for sweeps (docs/DATOS_INDIVIDUO.md, section 5).
//
//   GBSim --seed=7 --days=30 --clans=3 --members=6 --biome=temperate --telemetry=run.db [--every=60] [--combat] [--hostile] [--profile=x] [--note=text]
//
// Writes one SQLite file with the telemetry of the run. Ctrl+C stops it cleanly: the file is closed and passes the integrity check.
#include "GB/Game/SimQA.h"
#include "GB/Log.h"
#include "GB/Telemetry/TelemetrySink.h"
#include "GB/World/WorldGen.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	std::atomic<bool> g_Stop{ false };
	void OnSignal(int) { g_Stop = true; }

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
	double days = 3.0, every = 60.0;
	int clans = 1, members = 4;
	bool combat = false, hostile = false;
	std::string biome = "temperate", path, profile, note;
	for (int i = 1; i < argc; ++i)
	{
		std::string v;
		if (Arg(argv[i], "--seed", v)) seed = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
		else if (Arg(argv[i], "--days", v)) days = std::atof(v.c_str());
		else if (Arg(argv[i], "--clans", v)) clans = std::max(1, std::atoi(v.c_str()));
		else if (Arg(argv[i], "--members", v)) members = std::max(1, std::atoi(v.c_str()));
		else if (Arg(argv[i], "--biome", v)) biome = v;
		else if (Arg(argv[i], "--telemetry", v)) path = v;
		else if (Arg(argv[i], "--every", v)) every = std::atof(v.c_str());
		else if (Arg(argv[i], "--profile", v)) profile = v;
		else if (Arg(argv[i], "--note", v)) note = v;
		else if (!std::strcmp(argv[i], "--combat")) combat = true;
		else if (!std::strcmp(argv[i], "--hostile")) hostile = combat = true;
		else { std::fprintf(stderr, "unknown argument '%s'\nusage: GBSim --seed=N --days=D --clans=N --members=N --biome=temperate|alpine|desert --telemetry=run.db [--every=S] [--combat] [--hostile] [--profile=x] [--note=text]\n", argv[i]); return 2; }
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
	const double total = days * kDay;
	const auto t0 = std::chrono::steady_clock::now();
	double done = 0.0;
	int lastDay = 0;
	while (done < total && !g_Stop)
	{
		const double chunk = std::min(60.0, total - done);
		run.Run(chunk);
		done += chunk;
		if (static_cast<int>(done / kDay) != lastDay)
		{
			lastDay = static_cast<int>(done / kDay);
			std::fprintf(stderr, "day %d / %.0f  (%.0f s of wall time)\n", lastDay, days, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
		}
	}
	run.Finish();
	sink.Stop(done);
	const GB::TelemetryStats st = sink.Stats();
	std::fprintf(stderr, "%s after %.2f days: %llu samples, %llu dropped, %llu events, %d violations\n", g_Stop ? "stopped" : "finished", done / kDay,
		static_cast<unsigned long long>(st.SamplesWritten), static_cast<unsigned long long>(st.SamplesDropped), static_cast<unsigned long long>(st.EventsWritten), run.Monitor().Violations());
	return 0;
}
