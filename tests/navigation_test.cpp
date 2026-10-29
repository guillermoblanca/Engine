// Navigation (H5): the grid, the paths over it and the walkers that follow them. CPU only, no window, deterministic.
// Checked: 50 walkers cross a world of trees, rocks, a lake, a pond, a stream and a cliff to 50 resource points without getting stuck, around the trees,
// rocks and each other; the water is waded or walked around according to its cost; the grid follows a tree felled or planted by redoing only the cells
// around it and the walkers plan again; the hierarchy finds paths as good as the plain search with fewer cells; two runs end the same. The CPU time of
// each part is printed (docs/NAVEGACION.md has the budget).
#include "GB/Game/AIDriver.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Navigation/NavGrid.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	using Clock = std::chrono::steady_clock;
	double Since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

	// Budgets (docs/NAVEGACION.md). The checks use 4x these so that a slow or busy machine does not fail the run; the numbers are printed.
	constexpr double kBuildBudget = 0.25;          // s to build the grid of a 160 x 160 m world
	constexpr double kPathBudget = 0.002;          // s per path query, mean
	constexpr double kFrameBudget = 0.0005;        // s per frame for the navigation of 50 walkers (planning, following, avoiding)
	constexpr double kWorstFrameBudget = 0.010;     // s in the costliest frame (the crowd sets off: MaxPlansPerFrame searches)
	constexpr double kUpdateBudget = 0.005;        // s to update the grid after a tree is felled
	constexpr float dt = 1.0f / 30.0f;
}

using namespace GB;

struct Rng
{
	uint32_t S = 12345u;
	float Next() { S = S * 1664525u + 1013904223u; return static_cast<float>((S >> 8) & 0xFFFF) / 65535.0f; }
};

// A world of 160 x 160 m: gentle swells, a lake with a deep core and a wadeable rim, a shallow pond, a stream across it and a steep hill.
static const float kLakeR = 18.0f, kPondR = 8.0f;
static const glm::vec2 kLake(0.0f, 0.0f), kPond(40.0f, -30.0f), kHill(35.0f, 35.0f);
static float StreamX() { return -40.0f; }

static float Ground(float x, float z)
{
	float g = 0.3f * std::sin(0.1f * x) * std::cos(0.1f * z);
	const float rl = glm::distance(glm::vec2(x, z), kLake);
	if (rl < kLakeR) g -= 3.0f * (1.0f - (rl / kLakeR) * (rl / kLakeR));
	const float rp = glm::distance(glm::vec2(x, z), kPond);
	if (rp < kPondR) g -= 0.6f * (1.0f - (rp / kPondR) * (rp / kPondR));
	const float sx = std::fabs(x - StreamX());
	if (sx < 2.5f) g -= 0.6f * (1.0f - sx / 2.5f);
	const float rh = glm::distance(glm::vec2(x, z), kHill);
	if (rh < 7.0f) g += 9.0f * (1.0f - rh / 7.0f);   // slope 1.3: a wall
	return g;
}

static float Water(float x, float z)
{
	const bool in = glm::distance(glm::vec2(x, z), kLake) < kLakeR || glm::distance(glm::vec2(x, z), kPond) < kPondR || std::fabs(x - StreamX()) < 2.5f;
	return in ? 0.0f : -1e9f;
}

static CharacterWorld MakeWorld()
{
	CharacterWorld w;
	w.GroundHeight = Ground;
	w.WaterLevel = Water;
	w.BoundsMin = { -79.0f, -79.0f };
	w.BoundsMax = { 79.0f, 79.0f };
	Rng rng;
	auto dry = [](const glm::vec2& p) { return Water(p.x, p.y) < -1e8f && glm::distance(p, kHill) > 9.0f; };
	for (int i = 0; i < 260; ++i)   // trees
	{
		const glm::vec2 p(-76.0f + 152.0f * rng.Next(), -76.0f + 152.0f * rng.Next());
		if (dry(p)) w.Obstacles.push_back({ p, 0.3f });
	}
	for (int i = 0; i < 40; ++i)    // rocks
	{
		const glm::vec2 p(-76.0f + 152.0f * rng.Next(), -76.0f + 152.0f * rng.Next());
		if (dry(p)) w.Obstacles.push_back({ p, 0.8f + 0.7f * rng.Next() });
	}
	return w;
}

static float PathLength(const glm::vec2& from, const std::vector<glm::vec2>& pts)
{
	float len = 0.0f;
	glm::vec2 p = from;
	for (const glm::vec2& q : pts) { len += glm::distance(p, q); p = q; }
	return len;
}

// ---------------------------------------------------------------------------------------------------------------------------------------- the crowd

struct CrowdResult
{
	std::vector<int> ArrivedFrame;           // -1: never
	std::vector<glm::vec2> Final;
	int Stuck = 0, Arrived = 0;
	long BlockedSteps = 0, Steps = 0;
	float MaxDepth = 0.0f;
	float MinGap = 1e9f;                      // closest two walkers came (between centres)
	float WorstOverlap = 0.0f;                // deepest a walker got into a static obstacle (past its radius)
	double NavSeconds = 0.0;                  // time of AIDriver::Update calls
	double WorstFrame = 0.0;                  // ... in the costliest frame
	long Frames = 0;
	long Replans = 0;
};

static std::vector<glm::vec2> PickTargets(const NavGrid& grid, const CharacterWorld& world, const glm::vec2& camp, int count)
{
	std::vector<glm::vec2> out;
	Rng rng;
	rng.S = 777u;
	const NavProfile profile;
	while (static_cast<int>(out.size()) < count)
	{
		const glm::vec2 p(-76.0f + 152.0f * rng.Next(), -76.0f + 152.0f * rng.Next());
		if (glm::distance(p, camp) < 40.0f || Water(p.x, p.y) > -1e8f || glm::distance(p, kHill) < 10.0f) continue;
		bool free = true;
		for (const CircleObstacle& o : world.Obstacles)
			if (glm::distance(o.Center, p) < o.Radius + 1.0f) { free = false; break; }
		for (const glm::vec2& q : out) if (glm::distance(p, q) < 3.0f) free = false;
		if (!free) continue;
		NavPath path;
		if (!grid.FindPath(profile, camp, p, path) || !path.Found) continue;   // (islands the grid says are cut off are not resource points)
		out.push_back(p);
	}
	return out;
}

static CrowdResult RunCrowd(NavGrid& grid, const CharacterWorld& base, int walkers, float seconds)
{
	const glm::vec2 camp(-62.0f, -52.0f);
	const std::vector<glm::vec2> targets = PickTargets(grid, base, camp, walkers);
	grid.ResetStats();   // (picking the points searched too)
	struct Walker { CharacterController C; AIDriver AI; CharacterWorld W; };
	std::vector<Walker> ws(static_cast<size_t>(walkers));
	CrowdResult r;
	r.ArrivedFrame.assign(static_cast<size_t>(walkers), -1);
	const size_t statics = base.Obstacles.size();
	for (int i = 0; i < walkers; ++i)
	{
		Walker& w = ws[static_cast<size_t>(i)];
		w.W = base;
		w.W.Nav = &grid;
		w.W.DynamicFrom = statics;
		const glm::vec2 spot = FindFreeSpot(base, camp + glm::vec2(static_cast<float>(i % 8), static_cast<float>(i / 8)) * 1.4f, 1.0f);
		w.C.SetPosition(base, spot.x, spot.y, 0.0f);
		w.AI.Run = i % 3 == 0;
		w.AI.MoveTo(targets[static_cast<size_t>(i)]);
	}
	std::vector<CircleObstacle> circles(static_cast<size_t>(walkers));
	const int maxFrames = static_cast<int>(seconds / dt);
	for (int f = 0; f < maxFrames; ++f)
	{
		for (int i = 0; i < walkers; ++i) circles[static_cast<size_t>(i)] = { { ws[static_cast<size_t>(i)].C.GetPosition().x, ws[static_cast<size_t>(i)].C.GetPosition().z }, 0.35f };
		int active = 0;
		grid.NewFrame();
		const double before = r.NavSeconds;
		for (int i = 0; i < walkers; ++i)
		{
			Walker& w = ws[static_cast<size_t>(i)];
			w.W.Obstacles.resize(statics);
			for (int k = 0; k < walkers; ++k) if (k != i) w.W.Obstacles.push_back(circles[static_cast<size_t>(k)]);
			CharacterInput in;
			if (!w.AI.Arrived() && !w.AI.Stuck())
			{
				const auto t0 = Clock::now();
				in = w.AI.Update(w.C, w.W, dt);
				r.NavSeconds += Since(t0);
				++active;
			}
			w.C.Step(in, w.W, dt);
			if (w.AI.Arrived() && r.ArrivedFrame[static_cast<size_t>(i)] < 0) r.ArrivedFrame[static_cast<size_t>(i)] = f;
			if (!w.AI.Arrived() && !w.AI.Stuck()) { ++r.Steps; if (w.C.WasBlocked()) ++r.BlockedSteps; }
			r.MaxDepth = std::max(r.MaxDepth, w.C.GetWadeDepth());
			const glm::vec2 p(w.C.GetPosition().x, w.C.GetPosition().z);
			for (size_t k = 0; k < statics; ++k)
				r.WorstOverlap = std::max(r.WorstOverlap, base.Obstacles[k].Radius + 0.35f - glm::distance(base.Obstacles[k].Center, p));
			if (i < 8 || f % 15 == 0)   // (the pairwise check is the expensive part: every walker for the first frames, every half second after)
				for (int k = 0; k < i; ++k) r.MinGap = std::min(r.MinGap, glm::distance(p, circles[static_cast<size_t>(k)].Center));
		}
		++r.Frames;
		r.WorstFrame = std::max(r.WorstFrame, r.NavSeconds - before);
		if (active == 0) break;
	}
	for (int i = 0; i < walkers; ++i)
	{
		const Walker& w = ws[static_cast<size_t>(i)];
		r.Arrived += w.AI.Arrived() ? 1 : 0;
		r.Stuck += w.AI.Stuck() ? 1 : 0;
		if (w.AI.Stuck()) std::fprintf(stderr, "  walker %d gave up: %s (goal %.1f, %.1f)\n", i, w.AI.StuckReason().c_str(), static_cast<double>(targets[static_cast<size_t>(i)].x), static_cast<double>(targets[static_cast<size_t>(i)].y));
		r.Final.push_back({ w.C.GetPosition().x, w.C.GetPosition().z });
	}
	return r;
}

// ---------------------------------------------------------------------------------------------------------------------------------------- a wall with gaps

// A wall of trunks across x = 0 from z = -29 to 29 with gaps (z in the list, 3 m wide).
static std::vector<CircleObstacle> Wall(const std::vector<float>& gaps)
{
	std::vector<CircleObstacle> v;
	for (float z = -29.2f; z <= 29.2f; z += 0.8f)
	{
		bool gap = false;
		for (float g : gaps) if (std::fabs(z - g) < 1.5f) gap = true;
		if (!gap) v.push_back({ { 0.0f, z }, 0.3f });
	}
	return v;
}

static CharacterWorld FlatWorld(const std::vector<CircleObstacle>& obstacles)
{
	CharacterWorld w;
	w.BoundsMin = { -30.0f, -30.0f };
	w.BoundsMax = { 30.0f, 30.0f };
	w.Obstacles = obstacles;
	return w;
}

int main()
{
	const NavProfile walker;

	// ---- the grid of the world
	const CharacterWorld world = MakeWorld();
	NavGrid grid;
	std::string why;
	const auto tb = Clock::now();
	Check(grid.Build(world, NavConfig(), &why), "the grid builds");
	const double buildSeconds = Since(tb);
	Check(grid.Ready() && grid.Width() == 316 && grid.Height() == 316, "the grid covers the bounds in half-metre cells");
	Check(!grid.Walkable({ -200.0f, 0.0f }) && grid.Walkable({ -60.0f, -70.0f }, walker) == grid.Walkable({ -60.0f, -70.0f }), "outside the area nothing is walkable");
	{
		long blocked, costly, wet;
		grid.Count(blocked, costly, wet);
		Check(blocked > 2000 && wet > 1000 && costly > 500, "the grid has blocked, wet and costly cells");
		std::printf("grid: %dx%d cells, %ld blocked, %ld costly, %ld wet; built in %.1f ms\n", grid.Width(), grid.Height(), blocked, costly, wet, buildSeconds * 1000.0);
		Check(buildSeconds < kBuildBudget * 4.0, "the grid is built within the budget");
	}
	{
		// the walls are where they should be
		int i, j;
		Check(grid.CellAt(world.Obstacles[0].Center, i, j) && grid.Blocked(i, j), "a trunk blocks its cell");
		Check(grid.CellAt(kHill, i, j) && grid.CellCost(i, j, walker) < 0.0f, "the steep hill is a wall");
		Check(grid.CellAt(kLake, i, j) && grid.CellCost(i, j, walker) < 0.0f && grid.Depth(i, j) > 2.5f, "the deep core of the lake is a wall");
		Check(grid.CellAt(kPond, i, j) && grid.CellCost(i, j, walker) > 3.0f, "the pond can be waded, at a price");
		NavProfile swimmer = walker;
		swimmer.MaxWadeDepth = 4.0f;
		Check(grid.CellAt(kLake, i, j) && grid.CellCost(i, j, swimmer) > 0.0f, "a profile that swims has the lake open");
		NavProfile loaded = walker;
		loaded.Load = 1.0f;
		Check(grid.CellAt(kPond, i, j) && grid.CellCost(i, j, loaded) > grid.CellCost(i, j, walker), "water costs more with a load");
		Check(grid.Matches(world), "the grid matches its own world");
		CharacterWorld other = world;
		other.GroundHeight = [](float, float) { return 5.0f; };
		Check(!grid.Matches(other), "a different ground does not match");
	}

	// ---- water: walk around or wade, by cost
	{
		const glm::vec2 a(kPond.x - 14.0f, kPond.y), b(kPond.x + 14.0f, kPond.y);
		NavPath around, through;
		Check(grid.FindPath(walker, a, b, around) && around.Found, "a path across the pond is found");
		NavProfile amphibious = walker;
		amphibious.WaterWeight = 0.1f;
		Check(grid.FindPath(amphibious, a, b, through) && through.Found, "and one for a walker that does not mind the water");
		const float straight = glm::distance(a, b);
		const float lenAround = PathLength(a, around.Points), lenThrough = PathLength(a, through.Points);
		std::printf("pond: straight %.1f m, around %.1f m, wading %.1f m\n", straight, lenAround, lenThrough);
		Check(lenAround > straight * 1.15f, "the ordinary walker goes around the pond");
		Check(lenThrough < straight * 1.06f, "the one that does not mind wades across it");
		// wet metres of each path
		auto wetMetres = [&](const glm::vec2& from, const std::vector<glm::vec2>& pts)
		{
			float m = 0.0f;
			glm::vec2 p = from;
			for (const glm::vec2& q : pts)
			{
				const int n = std::max(1, static_cast<int>(glm::distance(p, q) / 0.25f));
				for (int s = 0; s < n; ++s)
				{
					const glm::vec2 x = p + (q - p) * ((static_cast<float>(s) + 0.5f) / static_cast<float>(n));
					if (Water(x.x, x.y) > -1e8f && Water(x.x, x.y) - Ground(x.x, x.y) > 0.05f) m += glm::distance(p, q) / static_cast<float>(n);
				}
				p = q;
			}
			return m;
		};
		Check(wetMetres(a, around.Points) < 2.0f, "the path around the pond stays dry");
		Check(wetMetres(a, through.Points) > 8.0f, "the path of the one that wades is mostly in the water");

		// the lake's core is never crossed; the rim can be
		NavPath lake;
		Check(grid.FindPath(walker, { -30.0f, 0.0f }, { 30.0f, 0.0f }, lake) && lake.Found, "a path across the lake is found");
		bool deep = false;
		glm::vec2 p(-30.0f, 0.0f);
		for (const glm::vec2& q : lake.Points)
		{
			for (int s = 0; s <= 40; ++s)
			{
				const glm::vec2 x = p + (q - p) * (static_cast<float>(s) / 40.0f);
				if (Water(x.x, x.y) - Ground(x.x, x.y) > 1.0f) deep = true;
			}
			p = q;
		}
		Check(!deep, "no path goes through the deep water");
		Check(PathLength({ -30.0f, 0.0f }, lake.Points) > 70.0f, "it goes around the lake (60 m across)");
		// a goal a little inside the deep water (a node on a steep bank): the path ends on the bank, it does not walk on into the lake
		NavPath bank;
		grid.FindPath(walker, { 30.0f, 0.0f }, { 13.0f, 0.0f }, bank);
		float deepest = 0.0f;
		p = { 30.0f, 0.0f };
		for (const glm::vec2& q : bank.Points)
		{
			for (int s = 0; s <= 40; ++s)
			{
				const glm::vec2 x = p + (q - p) * (static_cast<float>(s) / 40.0f);
				if (Water(x.x, x.y) > -1e8f) deepest = std::max(deepest, Water(x.x, x.y) - Ground(x.x, x.y));
			}
			p = q;
		}
		Check(!bank.Points.empty() && glm::distance(bank.Points.back(), glm::vec2(13.0f, 0.0f)) < 3.0f && deepest <= walker.MaxWadeDepth + 0.05f,
			"a goal in the deep water: the path stops on the bank, never deeper than it can wade");
		// the stream cannot be avoided: it is waded where it has to be
		NavPath stream;
		Check(grid.FindPath(walker, { -60.0f, 40.0f }, { -20.0f, 40.0f }, stream) && stream.Found, "a path across the stream is found (waded)");
		// an unreachable place (the top of the hill): the path ends as near as it can
		NavPath hill;
		const bool reached = grid.FindPath(walker, { -20.0f, 20.0f }, kHill, hill);
		Check(!reached && hill.Partial && !hill.Points.empty() && glm::distance(hill.Points.back(), kHill) < 10.0f, "the top of the hill is not reachable: the path goes as near as it can");
	}

	// ---- the hierarchy: as good as the plain search, with fewer cells
	{
		NavConfig plain;
		plain.CoarseFrom = 1 << 20;
		NavGrid flat;
		Check(flat.Build(world, plain), "a grid without the hierarchy builds");
		grid.ResetStats();
		flat.ResetStats();
		Rng rng;
		rng.S = 99u;
		long expandedCoarse = 0, expandedFlat = 0;
		double costCoarse = 0.0, costFlat = 0.0;
		int pairs = 0;
		for (int k = 0; k < 30; ++k)
		{
			glm::vec2 a(-70.0f + 20.0f * rng.Next(), -70.0f + 20.0f * rng.Next()), b(50.0f + 20.0f * rng.Next(), 50.0f + 20.0f * rng.Next());
			a = FindFreeSpot(world, a, 1.0f);
			b = FindFreeSpot(world, b, 1.0f);
			NavPath p1, p2;
			if (!grid.FindPath(walker, a, b, p1) || !flat.FindPath(walker, a, b, p2)) continue;
			expandedCoarse += p1.Expanded;
			expandedFlat += p2.Expanded;
			costCoarse += p1.Cost;
			costFlat += p2.Cost;
			++pairs;
		}
		std::printf("hierarchy: %d long paths, %ld cells opened with the corridor, %ld without; cost %.0f vs %.0f\n", pairs, expandedCoarse, expandedFlat, costCoarse, costFlat);
		Check(pairs >= 25, "the long paths are found");
		Check(costCoarse <= costFlat * 1.1, "the corridor does not make the paths worse");
		Check(expandedCoarse < expandedFlat, "the corridor opens fewer cells");
	}

	// ---- 50 walkers to 50 resource points
	grid.ResetStats();
	const CrowdResult one = RunCrowd(grid, world, 50, 240.0f);
	const NavStats stats = grid.Stats();
	{
		const double frame = one.NavSeconds / static_cast<double>(std::max(one.Frames, 1L));
		const double perPath = stats.PathSeconds / static_cast<double>(std::max(stats.Paths, 1L));
		std::printf("crowd: %d arrived, %d stuck in %.0f s; %ld paths (%ld replans), %.3f ms each, %ld cells opened; navigation %.3f ms per frame for 50 walkers (%.1f us each), %.2f ms in the worst frame\n",
			one.Arrived, one.Stuck, static_cast<double>(one.Frames) * dt, stats.Paths, stats.Replans, perPath * 1000.0, stats.Expanded, frame * 1000.0, frame / 50.0 * 1e6, one.WorstFrame * 1000.0);
		std::printf("crowd: blocked %.2f%% of steps, deepest wade %.2f m, closest two walkers %.2f m, deepest into an obstacle %.2f m\n",
			100.0 * static_cast<double>(one.BlockedSteps) / static_cast<double>(std::max(one.Steps, 1L)), one.MaxDepth, one.MinGap, one.WorstOverlap);
		Check(one.Arrived == 50 && one.Stuck == 0, "50 walkers reach their resource points without getting stuck");
		Check(100.0 * static_cast<double>(one.BlockedSteps) / static_cast<double>(std::max(one.Steps, 1L)) < 2.0, "they go around the trees, rocks and each other instead of pushing against them");
		Check(one.MaxDepth <= 1.0f + 1e-3f, "nobody wades deeper than the limit");
		Check(one.MinGap > 0.5f, "nobody walks through another");
		Check(one.WorstOverlap < 0.1f, "nobody walks into a trunk or a rock");
		Check(perPath < kPathBudget * 4.0, "a path takes less than the budget");
		Check(one.WorstFrame < kWorstFrameBudget * 4.0, "even the costliest frame stays within the budget");
		Check(frame < kFrameBudget * 4.0, "the navigation of 50 walkers takes less than the budget per frame");
	}
	// ---- two runs end the same
	{
		NavGrid again;
		Check(again.Build(world), "the grid builds again");
		const CrowdResult two = RunCrowd(again, world, 50, 240.0f);
		bool same = two.ArrivedFrame == one.ArrivedFrame && two.Frames == one.Frames;
		for (size_t i = 0; i < one.Final.size() && same; ++i) same = one.Final[i] == two.Final[i];
		Check(same, "two runs end identically (same arrival frames, same final positions)");
	}

	// ---- scale: 100 walkers
	{
		const CrowdResult hundred = RunCrowd(grid, world, 100, 300.0f);
		const NavStats s = grid.Stats();
		const double seconds = static_cast<double>(hundred.Frames) * dt;
		const double frame = hundred.NavSeconds / static_cast<double>(std::max(hundred.Frames, 1L));
		size_t regions = 0;
		const size_t bytes = grid.MemoryBytes(&regions);
		std::printf("scale: 100 walkers: %d arrived, %d stuck in %.0f s; %ld paths, %ld replans (%.2f per second, %.2f per walker per minute); navigation %.3f ms per frame (%.1f us each), %.2f ms in the worst frame; "
			"grid %.2f MB, of which the graph of regions %.1f KB\n", hundred.Arrived, hundred.Stuck, seconds, s.Paths, s.Replans, static_cast<double>(s.Replans) / seconds,
			static_cast<double>(s.Replans) / seconds * 0.6, frame * 1000.0, frame / 100.0 * 1e6, hundred.WorstFrame * 1000.0, static_cast<double>(bytes) / 1048576.0, static_cast<double>(regions) / 1024.0);
		Check(hundred.Arrived >= 98 && hundred.Stuck <= 2, "100 walkers reach their points (at most two give up)");
		Check(hundred.MaxDepth <= 1.0f + 1e-3f && hundred.WorstOverlap < 0.1f, "... without wading too deep or walking into trunks");
		Check(frame < kFrameBudget * 8.0, "the navigation of 100 walkers takes less than twice the budget per frame");
		Check(hundred.WorstFrame < kWorstFrameBudget * 4.0, "... and its costliest frame stays within the budget");
		Check(static_cast<double>(s.Replans) / seconds < 5.0, "the replans stay under five a second");
	}

	// ---- the world changes: a gap closes, a tree is felled
	{
		const CharacterWorld open = FlatWorld(Wall({ 0.0f, 18.0f }));
		NavGrid g;
		Check(g.Build(open), "the grid of the wall builds");
		NavPath before;
		Check(g.FindPath(walker, { -10.0f, 1.0f }, { 10.0f, 1.0f }, before) && before.Found, "a path through the near gap");
		Check(PathLength({ -10.0f, 1.0f }, before.Points) < 24.0f, "it takes the near gap");

		// a walker on its way when the gap closes
		CharacterController c;
		c.SetPosition(open, -10.0f, 1.0f, 0.0f);
		AIDriver ai;
		CharacterWorld w = open;
		w.Nav = &g;
		ai.MoveTo({ 10.0f, 1.0f });
		int frame = 0;
		bool closed = false;
		int cells = 0;
		double updateSeconds = 0.0;
		for (; frame < 30 * 60 && !ai.Arrived() && !ai.Stuck(); ++frame)
		{
			if (!closed && c.GetPosition().x > -6.0f)
			{
				closed = true;
				std::vector<CircleObstacle> now = open.Obstacles;
				const std::vector<CircleObstacle> plug = Wall({ 18.0f });
				for (const CircleObstacle& o : plug) if (std::fabs(o.Center.y) < 3.0f) now.push_back(o);   // the near gap fills with trunks
				const auto t0 = Clock::now();
				cells = g.SyncObstacles(now);
				updateSeconds = Since(t0);
			}
			c.Step(ai.Update(c, w, dt), w, dt);
		}
		std::printf("wall: the gap closed after %d frames; %d cells redone (%.1f%% of the grid) in %.3f ms; arrived %d after %d frames, %ld replans\n", frame, cells,
			100.0 * cells / (g.Width() * g.Height()), updateSeconds * 1000.0, ai.Arrived() ? 1 : 0, frame, g.Stats().Replans);
		Check(closed && cells > 0 && cells < g.Width() * g.Height() / 20, "only the cells around the change are redone");
		Check(updateSeconds < kUpdateBudget * 4.0, "the update takes less than the budget");
		Check(ai.Arrived(), "the walker plans again and arrives through the far gap");
		Check(g.Stats().Replans >= 1, "it planned again because the world changed under its path");
		Check(frame < 30 * 40, "(and not by wandering)");

		// fell the trees of the near gap's neighbours: the way gets shorter
		NavPath long1, short1;
		Check(g.FindPath(walker, { -10.0f, 1.0f }, { 10.0f, 1.0f }, long1) && long1.Found, "a path with the near gap closed");
		Check(g.SyncObstacles(open.Obstacles) > 0, "the gap opens again");
		Check(g.FindPath(walker, { -10.0f, 1.0f }, { 10.0f, 1.0f }, short1) && short1.Found, "a path with it open");
		Check(PathLength({ -10.0f, 1.0f }, short1.Points) < PathLength({ -10.0f, 1.0f }, long1.Points) - 10.0f, "the felled trees shorten the way");
		Check(g.SyncObstacles(open.Obstacles) == 0, "nothing changed, nothing is redone");

		// every gap closed: no way through; the walker ends stuck instead of walking forever
		const CharacterWorld sealed = FlatWorld(Wall({}));
		NavGrid s;
		Check(s.Build(sealed), "the grid of the sealed wall builds");
		CharacterController c2;
		c2.SetPosition(sealed, -10.0f, 1.0f, 0.0f);
		AIDriver ai2;
		CharacterWorld w2 = sealed;
		w2.Nav = &s;
		ai2.MoveTo({ 10.0f, 1.0f });
		int f2 = 0;
		for (; f2 < 30 * 120 && !ai2.Arrived() && !ai2.Stuck(); ++f2) c2.Step(ai2.Update(c2, w2, dt), w2, dt);
		Check(ai2.Stuck() && !ai2.Arrived(), "behind a sealed wall the walker gives up");
		Check(f2 < 30 * 60, "... after walking to the wall, not after wandering");
	}

	// ---- steering around each other: two walkers that meet head-on pass
	{
		const CharacterWorld empty = FlatWorld({});
		NavGrid g;
		Check(g.Build(empty), "the empty grid builds");
		CharacterController a, b;
		a.SetPosition(empty, -10.0f, 0.0f, 0.0f);
		b.SetPosition(empty, 10.0f, 0.0f, 0.0f);
		AIDriver aa, bb;
		aa.MoveTo({ 10.0f, 0.0f });
		bb.MoveTo({ -10.0f, 0.0f });
		CharacterWorld wa = empty, wb = empty;
		wa.Nav = wb.Nav = &g;
		wa.DynamicFrom = wb.DynamicFrom = 0;
		float gap = 1e9f;
		int f = 0;
		for (; f < 30 * 40 && !(aa.Arrived() && bb.Arrived()) && !aa.Stuck() && !bb.Stuck(); ++f)
		{
			wa.Obstacles = { { { b.GetPosition().x, b.GetPosition().z }, 0.35f } };
			wb.Obstacles = { { { a.GetPosition().x, a.GetPosition().z }, 0.35f } };
			a.Step(aa.Update(a, wa, dt), wa, dt);
			b.Step(bb.Update(b, wb, dt), wb, dt);
			gap = std::min(gap, glm::distance(glm::vec2(a.GetPosition().x, a.GetPosition().z), glm::vec2(b.GetPosition().x, b.GetPosition().z)));
		}
		std::printf("head-on: both arrived %d after %.1f s, closest %.2f m\n", aa.Arrived() && bb.Arrived() ? 1 : 0, static_cast<double>(f) * dt, gap);
		Check(aa.Arrived() && bb.Arrived(), "two walkers that meet head-on both arrive");
		Check(gap > 0.65f, "... without touching");
	}

	// ---- the old behaviour: no grid, a straight line
	{
		const CharacterWorld empty = FlatWorld({});
		CharacterController c;
		c.SetPosition(empty, 0.0f, 0.0f, 0.0f);
		AIDriver ai;
		ai.MoveTo({ 6.0f, 0.0f });
		for (int f = 0; f < 600 && !ai.Arrived(); ++f) c.Step(ai.Update(c, empty, dt), empty, dt);
		Check(ai.Arrived(), "without a grid it still walks in a straight line");
	}

	// ---- a door: shut it is a wall, open it is a way through
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		RegisterInteractiveContent(gameplay.Library);
		BuildInteractives(entities, glm::vec3(0.0f));   // the gate stands at (-6, 0), its fence along x
		gameplay.Load(entities);
		std::vector<CircleObstacle> shut;
		DoorObstacles(entities, gameplay, shut);
		Check(shut.size() > 10, "a door has a fence and a leaf");
		CharacterWorld w = FlatWorld(shut);
		NavGrid g;
		Check(g.Build(w), "the grid of the door builds");
		const glm::vec2 from(-6.0f, -5.0f), to(-6.0f, 5.0f);
		// walks from `from` to `to`; how far to the side the walker went at most, and whether it arrived
		auto walk = [&](const CharacterWorld& world, float& widest, bool& arrived)
		{
			CharacterController c;
			c.SetPosition(world, from.x, from.y, 0.0f);
			AIDriver ai;
			CharacterWorld wn = world;
			wn.Nav = &g;
			ai.MoveTo(to);
			widest = 0.0f;
			for (int f = 0; f < 30 * 90 && !ai.Arrived() && !ai.Stuck(); ++f)
			{
				c.Step(ai.Update(c, wn, dt), wn, dt);
				widest = std::max(widest, std::fabs(c.GetPosition().x + 6.0f));
			}
			arrived = ai.Arrived();
		};
		NavPath closed;
		Check(g.FindPath(walker, from, to, closed) && closed.Found, "a path past the shut door is found");
		const float lenShut = PathLength(from, closed.Points);
		float widest = 0.0f;
		bool arrived = false;
		walk(w, widest, arrived);
		std::printf("door: shut %.1f m (widest %.1f m from the door)", lenShut, widest);
		Check(arrived && widest > 3.0f && lenShut > 11.5f, "a shut door is walked around, past the end of the fence");

		gameplay.Find("Gate")->Tags().RemoveAll("State.Closed");
		gameplay.Find("Gate")->Tags().Add("State.Open");
		std::vector<CircleObstacle> open;
		DoorObstacles(entities, gameplay, open);
		Check(open.size() < shut.size(), "an open door has no leaf");
		const int cells = g.SyncObstacles(open);
		w.Obstacles = open;
		NavPath through;
		Check(cells > 0 && cells < g.Width() * g.Height() / 5, "opening it redoes only the cells around it");
		Check(g.FindPath(walker, from, to, through) && through.Found, "a path through the open door is found");
		const float lenOpen = PathLength(from, through.Points);
		walk(w, widest, arrived);
		std::printf(", open %.1f m (widest %.1f m)\n", lenOpen, widest);
		Check(arrived && widest < 1.2f && lenOpen < 11.0f, "an open door is crossed through the opening");

		// and shut again, with the walker already on its way through: it plans again around
		gameplay.Find("Gate")->Tags().RemoveAll("State.Open");
		gameplay.Find("Gate")->Tags().Add("State.Closed");
		std::vector<CircleObstacle> again;
		DoorObstacles(entities, gameplay, again);
		g.SyncObstacles(again);
		NavPath back;
		Check(g.FindPath(walker, from, to, back) && PathLength(from, back.Points) > 11.5f, "shut again, the way round is back");
	}

	// ---- long frames (the editor at `timescale 20`, a hitch while a model loads): the walkers must not take them for standing still
	{
		const CharacterWorld w = FlatWorld(Wall({ 0.0f }));
		NavGrid g;
		Check(g.Build(w), "the grid of the long frames builds");
		int ok = 0, total = 0;
		for (float step : { 0.25f, 0.5f, 1.0f, 2.0f })
		{
			CharacterController c;
			c.SetPosition(w, -12.0f, 6.0f, 0.0f);
			AIDriver ai;
			CharacterWorld wn = w;
			wn.Nav = &g;
			wn.DynamicFrom = w.Obstacles.size();
			ai.MoveTo({ 12.0f, -6.0f });
			for (int f = 0; f < static_cast<int>(120.0f / step) && !ai.Arrived() && !ai.Stuck(); ++f) c.Step(ai.Update(c, wn, step), wn, step);
			++total;
			ok += !ai.Stuck() && (ai.Arrived() || step > 0.6f) ? 1 : 0;   // (steps of a second or two overshoot a 0.3 m arrival radius: they only must not give up)
			if (ai.Stuck()) std::fprintf(stderr, "  (frame of %.2f s: %s)\n", static_cast<double>(step), ai.StuckReason().c_str());
		}
		Check(ok == total, "a walker does not give up when the frames are 0.25 to 2 s long, and arrives in the shorter ones");
	}

	// ---- a grid needs bounds
	{
		NavGrid g;
		CharacterWorld unbounded;
		std::string e;
		Check(!g.Build(unbounded, NavConfig(), &e) && !e.empty() && !g.Ready(), "a world without bounds cannot be covered");
		NavConfig area;
		area.AreaMin = { -10.0f, -10.0f };
		area.AreaMax = { 10.0f, 10.0f };
		Check(g.Build(unbounded, area) && g.Width() == 40, "unless the area is given");
	}

	if (failures == 0) std::printf("navigation_test: ok (build %.1f ms)\n", buildSeconds * 1000.0);
	return failures == 0 ? 0 : 1;
}
