// Scale and budgets (H16). Checked on the CPU, with no window:
//  * the PLAN BUDGET of the navigation grid is fair and deterministic: walkers turned away keep their ticket and the oldest are served first whatever
//    order they ask in, a crowd that keeps asking shares the searches evenly, a request without a ticket only gets what is left over, and a host
//    that never starts frames is never refused (the old behaviour);
//  * the PATH CACHE answers a repeated trip (camp <-> node) from a neighbouring start and goal, ends at the exact new goal, and forgets a path when
//    the world changes along it;
//  * an ABSTRACT AGENT waits for its turn in the budget before it plans (and does not move meanwhile);
//  * several CLANS on one generated world (SimRun with Clans > 1) share ONE budget of thinking turns (ClanWorld::ThinkPerFrame): the turns of a
//    frame stay within it, every member gets its turn, and two runs end the same.
//
//   GB_SCALE_BENCH=1        also the scale runs: 100 members in 3 clans and 300 in 5 for GB_SCALE_MINUTES simulated minutes (default 3) at 60 Hz,
//                           printing the percentiles of the milliseconds per step (tribes, bodies, whole step) and of the plan queue, and
//                           ASSERTING the budgets (p95 of the tribes' update under 4 ms at 300 members). Timing is never asserted otherwise.
#include "GB/AI/Abstract.h"
#include "GB/Game/SimQA.h"
#include "GB/Log.h"
#include "GB/Navigation/NavGrid.h"
#include "GB/World/WorldGen.h"
#include "GB/World/Regions.h"
#include "GB/World/WorldTiles.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::printf("ok: %s\n", what.c_str());
	}

	GB::CharacterWorld Flat(float half)
	{
		GB::CharacterWorld w;
		w.GroundHeight = [](float, float) { return 0.0f; };
		w.BoundsMin = { -half, -half };
		w.BoundsMax = { half, half };
		return w;
	}

	double Percentile(std::vector<double> v, double p)
	{
		if (v.empty()) return 0.0;
		std::sort(v.begin(), v.end());
		const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
		return v[i];
	}

	// ---- the plan budget
	void PlanBudget()
	{
		using namespace GB;
		NavGrid grid;
		NavConfig config;
		config.MaxPlansPerFrame = 2;
		Check(grid.Build(Flat(20.0f), config), "a flat grid");
		bool always = true;
		for (int i = 0; i < 50; ++i) always = always && grid.AllowPlan();
		Check(always, "a host that never calls NewFrame is never refused");

		// ten walkers that each want one search; they ask in reverse order every frame, and still the first turned away is the first served
		{
			std::vector<PlanTicket> tickets(10);
			std::vector<int> servedAt(10, -1);
			int frame = 0;
			for (; frame < 20; ++frame)
			{
				grid.NewFrame();
				for (int i = 9; i >= 0; --i)
					if (servedAt[static_cast<size_t>(i)] < 0 && grid.AllowPlan(&tickets[static_cast<size_t>(i)])) servedAt[static_cast<size_t>(i)] = frame;
				if (std::all_of(servedAt.begin(), servedAt.end(), [](int f) { return f >= 0; })) break;
			}
			std::string order;
			for (int f : servedAt) order += std::to_string(f) + " ";
			Check(frame == 4, "ten walkers, two searches a frame: all served in five frames (served at: " + order + ")");
			Check(servedAt[9] == 0 && servedAt[8] == 0 && servedAt[7] == 1 && servedAt[6] == 1 && servedAt[1] == 4 && servedAt[0] == 4, "the first turned away are the first served");
			Check(grid.Stats().WaitPeak == 4, "nobody waited more than four frames (peak " + std::to_string(grid.Stats().WaitPeak) + ")");
		}
		// five walkers that keep asking every frame (with the old first-come rule the last in the order would never plan): each gets its share
		{
			std::vector<PlanTicket> tickets(5);
			std::vector<int> grants(5, 0);
			int over = 0;
			for (int frame = 0; frame < 50; ++frame)
			{
				grid.NewFrame();
				over += grid.FrameStats().Planned > 2 ? 1 : 0;   // (the frame before)
				for (int i = 0; i < 5; ++i) grants[static_cast<size_t>(i)] += grid.AllowPlan(&tickets[static_cast<size_t>(i)]) ? 1 : 0;
			}
			Check(over == 0, "never more searches than the budget in a frame");
			const int least = *std::min_element(grants.begin(), grants.end()), most = *std::max_element(grants.begin(), grants.end());
			Check(least >= 15 && most <= 25, "a crowd that keeps asking shares the searches evenly (" + std::to_string(least) + " to " + std::to_string(most) + " of 100)");
			// with a queue, a request without a ticket gets nothing
			grid.NewFrame();
			for (int i = 0; i < 5; ++i) grid.AllowPlan(&tickets[static_cast<size_t>(i)]);
			grid.NewFrame();
			Check(!grid.AllowPlan() && !grid.AllowPlan(), "while walkers wait their turn, a request without a ticket is refused");
		}
	}

	// ---- the path cache
	void PathCache()
	{
		using namespace GB;
		CharacterWorld world = Flat(30.0f);
		for (int k = -12; k <= 12; ++k) world.Obstacles.push_back({ { 0.0f, static_cast<float>(k) }, 0.45f });   // a wall across the way, 25 m long
		NavConfig config;
		config.PathCache = 8;
		NavGrid grid;
		Check(grid.Build(world, config), "a grid with a wall");
		NavPath first, again, other;
		const glm::vec2 a(-9.0f, 0.3f), b(9.0f, -0.6f);   // (a2 and b2 below fall in the same squares of 2 m)
		Check(grid.FindPath({}, a, b, first) && grid.Stats().CacheMisses == 1 && grid.Stats().CacheHits == 0, "the first trip is searched (and kept)");
		const glm::vec2 a2 = a + glm::vec2(0.4f, 0.3f), b2 = b + glm::vec2(-0.3f, 0.4f);   // the same squares of 2 m
		Check(grid.FindPath({}, a2, b2, again) && grid.Stats().CacheHits == 1 && again.Points.back() == b2, "the same trip from a step away comes from the cache, to the exact new goal");
		Check(again.Expanded == 0 && again.Points.size() == first.Points.size(), "a cached path opens no cell");
		bool clear = grid.LineClear({}, a2, again.Points.front());
		for (size_t i = 1; i < again.Points.size(); ++i) clear = clear && grid.LineClear({}, again.Points[i - 1], again.Points[i]);
		Check(clear, "and every leg of it is walkable");
		NavProfile loaded;
		loaded.Load = 0.5f;
		grid.FindPath(loaded, a2, b2, other);
		Check(grid.Stats().CacheHits == 1, "another profile does not share it");
		// break the wall in the middle (three posts around (0, 0)): the cached detour is forgotten and the new path goes through the gap
		std::vector<CircleObstacle> gap = world.Obstacles;
		gap.erase(gap.begin() + 11, gap.begin() + 14);
		grid.SyncObstacles(gap);
		NavPath shorter;
		grid.FindPath({}, a2, b2, shorter);
		Check(grid.Stats().CacheHits == 1 && shorter.Points.size() <= 2, "a change along the way forgets the path: the new one goes through the gap");
	}

	// ---- the abstract agent waits for its turn
	void AgentGate()
	{
		using namespace GB;
		AbstractAgent agent;
		agent.Place({ 0.0f, 0.0f });
		std::string error;
		Check(agent.Do("goto 10 0", error), "an abstract agent with a goto");
		GameplayWorld gameplay;
		int routes = 0;
		bool open = false;
		const AbstractAgent::Route route = [&](const glm::vec2&, const glm::vec2& to, float, std::vector<glm::vec2>& points) { ++routes; points.assign(1, to); return true; };
		for (int f = 0; f < 3; ++f)
			agent.Update(0.1f, nullptr, gameplay, [](const std::string&, glm::vec2&) { return false; }, 3.0f, route, [&]() { return open; });
		Check(routes == 0 && agent.Position() == glm::vec2(0.0f), "refused by the budget, it neither plans nor moves");
		open = true;
		agent.Update(0.1f, nullptr, gameplay, [](const std::string&, glm::vec2&) { return false; }, 3.0f, route, [&]() { return open; });
		Check(routes == 1 && agent.Position().x > 0.2f, "given its turn, it plans once and walks");
	}

	// ---- a world of several tiles (groundwork of the streaming)
	void Tiles(int perSide, float size, uint32_t resolution, bool report)
	{
		using namespace GB;
		WorldDesc d;
		d.Seed = 5;
		d.Size = size;
		d.Resolution = resolution;
		d.TreeBudget = 60;
		d.RockBudget = 15;
		d.Tiles = perSide;
		const auto t0 = std::chrono::steady_clock::now();
		const GeneratedWorld w = WorldGenerator::GenerateData(d);
		const double genMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		const Heightmap& hm = w.TerrainRef->GetHeightmap();
		const std::string tag = std::to_string(perSide) + "x" + std::to_string(perSide) + " tiles of " + std::to_string(static_cast<int>(size)) + " m: ";
		Check(w.Desc.Tiles == perSide && w.Desc.Size == size && hm.GetSize() == size * static_cast<float>(perSide) && hm.GetResolution() == (resolution - 1) * static_cast<uint32_t>(perSide) + 1,
			tag + "one heightmap of the whole extent; the world keeps the desc it was asked for");
		Check(static_cast<int>(w.Trees.size()) <= d.TreeBudget * perSide * perSide && w.Trees.size() > static_cast<size_t>(d.TreeBudget), tag + "the tree budget is per tile");
		WorldTiles tiles(hm.GetMin(), size, perSide, static_cast<int>(resolution));
		// seamless: the right edge of tile 0 is the left edge of tile 1, vertex for vertex
		std::vector<Vertex> a, b;
		std::vector<uint32_t> ia, ib;
		BuildTileMesh(*w.TerrainRef, tiles, 0, a, ia);
		BuildTileMesh(*w.TerrainRef, tiles, 1, b, ib);
		bool seam = a.size() == static_cast<size_t>(resolution) * resolution && b.size() == a.size();
		for (uint32_t j = 0; seam && j < resolution; ++j)
		{
			const Vertex& l = a[static_cast<size_t>(j) * resolution + (resolution - 1)], & r = b[static_cast<size_t>(j) * resolution];
			seam = l.Position == r.Position && l.Normal == r.Normal && l.UV == r.UV;
		}
		Check(seam, tag + "the meshes of two neighbouring tiles share their edge exactly (no seam)");
		// streaming: from the middle of tile 0 the tiles come one a frame, nearest first; far away they go
		const glm::vec2 inFirst = tiles.Min(0) + glm::vec2(size * 0.5f);
		TileStreamConfig stream;
		stream.LoadRadius = size * 0.75f;
		stream.DropRadius = size * 0.9f;
		std::vector<int> built;
		for (int f = 0; f < 10; ++f) for (int t : tiles.Update(inFirst, stream).Build) built.push_back(t);
		Check(!built.empty() && built.front() == 0 && built.size() == static_cast<size_t>(std::min(4, perSide * perSide)), tag + "near the corner tile: it first, then its neighbours, one a frame (" + std::to_string(built.size()) + ")");
		const glm::vec2 farCorner = tiles.Max(tiles.Count() - 1) - glm::vec2(1.0f);
		WorldTiles::Work work;
		for (int f = 0; f < 10; ++f) { const WorldTiles::Work step = tiles.Update(farCorner, stream); work.Drop.insert(work.Drop.end(), step.Drop.begin(), step.Drop.end()); }
		Check(!tiles.Resident(0) && tiles.Resident(tiles.Count() - 1) && !work.Drop.empty(), tag + "at the far corner the first tile is dropped and the last one is in");
		// the navigation over the whole extent: a long trip goes through the coarse graph of chunks
		CharacterWorld ground = MakeTerrainCharacterWorld(*w.TerrainRef);
		NavGrid nav;
		std::string error;
		const auto t1 = std::chrono::steady_clock::now();
		const bool built2 = nav.Build(ground, NavConfig(), &error);
		const double navMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
		Check(built2, tag + "a navigation grid over all the tiles" + (built2 ? std::string() : ": " + error));
		if (!built2) return;
		NavPath path;
		const glm::vec2 from = FindFreeSpot(ground, tiles.Min(0) + glm::vec2(20.0f), 1.0f), to = FindFreeSpot(ground, tiles.Max(tiles.Count() - 1) - glm::vec2(20.0f), 1.0f);
		const auto t2 = std::chrono::steady_clock::now();
		nav.FindPath({}, from, to, path);
		const double pathMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
		Check(!path.Points.empty(), tag + "a trip from corner to corner across the tiles has a way");
		RegionMap regions = RegionMap::Build(w, DefaultExplorationRules().Regions, {});
		Check(!regions.Empty(), tag + "the regions are cut over the whole extent");
		if (report)
			std::printf("   %s generated in %.0f ms; grid %dx%d built in %.0f ms (%.1f MB); corner to corner %.0f m in %.1f ms, %d cells opened; %zu regions\n", tag.c_str(), genMs, nav.Width(), nav.Height(), navMs,
				static_cast<double>(nav.MemoryBytes()) / 1048576.0, glm::distance(from, to), pathMs, path.Expanded, regions.Regions().size());
	}

	GB::WorldDesc Land()
	{
		GB::WorldDesc d;   // the editor's world (360 m): five camps 70 m apart fit in it
		d.Type = GB::WorldType::Temperate;
		d.Seed = 3;
		return d;
	}

	struct Series
	{
		std::vector<double> Tribes, Think, Bodies, Combat, Step;
		std::vector<int> Thinks, Queue, Due;
		int OverBudget = 0;
		std::map<std::string, std::vector<double>> Parts;   // the clocks of the clans, summed over them: economy, society, explore, conflict, abstract
	};

	GB::SimSetup ScaleSetup(const GB::GeneratedWorld& world, int members, int clans, float step)
	{
		GB::SimSetup s;
		s.World = &world;
		s.Members = members;
		s.Clans = clans;
		s.Step = step;
		s.CheckEvery = 50;
		s.PlanBudget = 4;      // searches a step, shared fairly by every body ...
		s.PlanCells = 20000;   // ... and the cells they may open in all (a fight of 300, or goals across water, make long searches)
		s.PathCache = 128;
		s.ViewerAtCamp = true;   // the first clan near the camera, the others abstract (as in the editor)
		return s;
	}

	Series Steps(GB::SimRun& run, double seconds, float step)
	{
		Series s;
		const long frames = static_cast<long>(seconds / static_cast<double>(step) + 0.5);
		for (long i = 0; i < frames; ++i)
		{
			run.Run(static_cast<double>(step));
			const GB::SimRun::StepCost& c = run.LastCost();
			s.Tribes.push_back(c.TribesMs);
			s.Bodies.push_back(c.BodiesMs);
			s.Step.push_back(c.Ms);
			s.Combat.push_back(c.CombatMs);
			s.Think.push_back(c.ThinkMs);
			s.Thinks.push_back(c.Thinks);
			s.Queue.push_back(c.Plans.Queue);
			s.Due.push_back(c.Due);
			double parts[5] = {};
			for (const auto& clan : run.Clans().All())
			{
				const GB::Tribe::FrameCost& t = clan->Folk->LastCost();
				parts[0] += t.EconomyMs; parts[1] += t.SocietyMs; parts[2] += t.ExploreMs; parts[3] += t.ConflictMs; parts[4] += t.AbstractMs;
			}
			const char* names[5] = { "economy", "society", "explore", "conflict", "abstract" };
			for (int k = 0; k < 5; ++k) s.Parts[names[k]].push_back(parts[k]);
		}
		return s;
	}

	// ---- several clans, one budget of thinking turns
	void Clans(const GB::GeneratedWorld& world)
	{
		using namespace GB;
		const float step = 1.0f / 20.0f;
		SimSetup setup = ScaleSetup(world, 36, 3, step);
		setup.ThinkPerFrame = 6;
		SimRun run(setup);
		Check(run.Ok(), "three clans of twelve on a generated world" + (run.Ok() ? std::string() : ": " + run.Error()));
		if (!run.Ok()) return;
		Check(run.Clans().Count() == 3 && run.Bodies() == 36, "three clans, 36 bodies");
		std::map<std::string, double> lastThought;
		const Series s = Steps(run, 120.0, step);
		const int peak = *std::max_element(s.Thinks.begin(), s.Thinks.end());
		// the clans need ceil(12 * 0.05 / 0.5) = 2 turns a frame each: 6 is the floor, and one more (Tribe::ThinksNeeded keeps a spare) at most
		Check(peak <= 7, "the turns of a frame stay within the shared budget (peak " + std::to_string(peak) + ")");
		const int dueMax = *std::max_element(s.Due.begin(), s.Due.end());
		Check(dueMax <= 36, "members waiting for a turn stay bounded (peak " + std::to_string(dueMax) + ")");
		int alive = 0, idleForever = 0;
		for (const auto& c : run.Clans().All())
			for (const TribeMember& m : c->Folk->Members())
			{
				if (m.Dead) continue;
				++alive;
				if (m.Mind.NextThink < run.Brain().Time() - 10.0) ++idleForever;   // its turn came over 10 s ago and never was taken
			}
		Check(alive == 36 && idleForever == 0, "everybody alive and nobody starved of turns (" + std::to_string(idleForever) + " late)");
		const int queuePeak = *std::max_element(s.Queue.begin(), s.Queue.end());
		std::printf("   3 clans x 12, 120 s at 20 Hz: thinks per step peak %d; plan queue peak %d; nav %ld searches, %ld cache hits, %ld refused; step p50 %.2f ms p95 %.2f ms\n", peak, queuePeak,
			run.Navigation().Stats().Paths, run.Navigation().Stats().CacheHits, run.Navigation().Stats().Refused, Percentile(s.Step, 0.5), Percentile(s.Step, 0.95));
		Check(run.Navigation().Stats().Refused > 0 || queuePeak == 0, "the plan budget is in use");

		SimRun twin(setup);
		twin.Run(120.0);
		Check(twin.Fingerprint() == run.Fingerprint(), "two runs of the three clans end the same (budgets, queue and cache are deterministic)");
	}

	void Bench(const GB::GeneratedWorld& world)
	{
		using namespace GB;
		const double minutes = std::getenv("GB_SCALE_MINUTES") ? std::atof(std::getenv("GB_SCALE_MINUTES")) : 3.0;
		const float step = 1.0f / 60.0f;
		struct Case { int Members, Clans; double TribesP95; bool Combat; };
		const char* only = std::getenv("GB_SCALE_CASE");   // (0..3: one case alone)
		int index = -1;
		for (const Case& c : { Case{ 100, 3, 4.0, false }, Case{ 300, 5, 4.0, false }, Case{ 100, 3, 4.0, true }, Case{ 300, 5, 4.0, true } })
		{
			++index;
			if (only && std::atoi(only) != index) continue;
			SimSetup setup = ScaleSetup(world, c.Members, c.Clans, step);
			setup.Combat = setup.HostileClans = c.Combat;   // H14: the fights, threats and raids of hostile clans
			SimRun run(setup);
			if (!run.Ok()) { Check(false, "scale run: " + run.Error()); continue; }
			const auto t0 = std::chrono::steady_clock::now();
			const Series s = Steps(run, minutes * 60.0, step);
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			int alive = 0, abstract = 0;
			for (const auto& clan : run.Clans().All()) { for (const TribeMember& m : clan->Folk->Members()) alive += m.Dead ? 0 : 1; abstract += clan->Folk->AbstractCount(); }
			std::vector<double> thinks(s.Thinks.begin(), s.Thinks.end()), queue(s.Queue.begin(), s.Queue.end());
			const NavStats& nav = run.Navigation().Stats();
			std::printf("-- %d members in %d clans%s, %.1f simulated minutes at 60 Hz (%.0f s of CPU, x%.1f real time), %d alive, %d abstract\n", c.Members, c.Clans,
				c.Combat ? " (hostile, with combat)" : "", minutes, took, minutes * 60.0 / took, alive, abstract);
			if (c.Combat)
			{
				std::map<std::string, int> kinds;
				for (const CombatEvent& e : run.Combat().Events()) ++kinds[e.Kind];
				std::string told;
				for (const auto& [k, n] : kinds) told += " " + k + " " + std::to_string(n);
				int raids = 0;
				for (const auto& clan : run.Clans().All()) raids += clan->Folk->Conflict().Raid.Raids;
				std::printf("   combat  ms per step: p50 %.3f  p95 %.3f  p99 %.3f  max %.2f; events kept:%s; raids made %d\n", Percentile(s.Combat, 0.5), Percentile(s.Combat, 0.95),
					Percentile(s.Combat, 0.99), Percentile(s.Combat, 1.0), told.empty() ? " none" : told.c_str(), raids);
			}
			std::printf("   tribes  ms per step: p50 %.3f  p95 %.3f  p99 %.3f  max %.2f\n", Percentile(s.Tribes, 0.5), Percentile(s.Tribes, 0.95), Percentile(s.Tribes, 0.99), Percentile(s.Tribes, 1.0));
			std::printf("    of it thinking : p50 %.3f  p95 %.3f  p99 %.3f  max %.2f (the rest: the clans' clocks, conflict, abstract walking)\n", Percentile(s.Think, 0.5),
				Percentile(s.Think, 0.95), Percentile(s.Think, 0.99), Percentile(s.Think, 1.0));
			std::printf("    clocks p50/p95 :");
			for (const auto& [part, ms] : s.Parts) std::printf(" %s %.3f/%.3f", part.c_str(), Percentile(ms, 0.5), Percentile(ms, 0.95));
			std::printf("\n");
			std::printf("   bodies  ms per step: p50 %.3f  p95 %.3f  p99 %.3f  max %.2f\n", Percentile(s.Bodies, 0.5), Percentile(s.Bodies, 0.95), Percentile(s.Bodies, 0.99), Percentile(s.Bodies, 1.0));
			std::printf("   step    ms         : p50 %.3f  p95 %.3f  p99 %.3f  max %.2f\n", Percentile(s.Step, 0.5), Percentile(s.Step, 0.95), Percentile(s.Step, 0.99), Percentile(s.Step, 1.0));
			std::printf("   thinks per step p50 %.0f p95 %.0f max %.0f; plan queue p95 %.0f max %.0f (wait peak %ld frames); %ld searches (%.2f ms mean), %ld cache hits, %ld refused\n",
				Percentile(thinks, 0.5), Percentile(thinks, 0.95), Percentile(thinks, 1.0), Percentile(queue, 0.95), Percentile(queue, 1.0), nav.WaitPeak, nav.Paths,
				nav.Paths ? nav.PathSeconds * 1000.0 / static_cast<double>(nav.Paths) : 0.0, nav.CacheHits, nav.Refused);
			Check(Percentile(s.Tribes, 0.95) < c.TribesP95, "p95 of the tribes' update under " + std::to_string(static_cast<int>(c.TribesP95)) + " ms with " + std::to_string(c.Members) + " members");
		}
	}
}

int main()
{
	GB::Log::Init();
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	PlanBudget();
	PathCache();
	AgentGate();
	Tiles(2, 96.0f, 65, true);   // (small: the CI; the editor-sized 2x2 and 3x3 run with GB_SCALE_BENCH)
	const GB::GeneratedWorld world = GB::WorldGenerator::GenerateData(Land());
	Clans(world);
	if (std::getenv("GB_SCALE_BENCH")) { if (!std::getenv("GB_SCALE_CASE")) { Tiles(2, 360.0f, 641, true); Tiles(3, 360.0f, 641, true); } Bench(world); }
	if (failures) { std::fprintf(stderr, "%d scale checks failed\n", failures); return 1; }
	std::puts("scale tests passed");
	return 0;
}
