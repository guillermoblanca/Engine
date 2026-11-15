// T214: nobody gets stuck at the river. On the editor's world (`generate temperate <seed>`: 360 m, 641 samples, 420 trees, 70 rocks) of seeds 5, 7
// and 77, walkers that start in wadeable water or on its bank are led 20 m away by an AIDriver, as the editor's actors are (a controller that refuses
// every step steeper than its MaxSlope). The grid read the slope of a cell as the mean over it, so a river bank whose step fell between two cell
// centres looked walkable from both, and the walker stood at its foot, in the water, for good (20 of 178 walkers before). It fails when one stays
// a minute or more within 3 m of water. CPU only, about 5 s.
//
// By hand (GBSimTests river_stuck <mode> "<arguments>"):
//   probe "seed x z [half]"                         the ground, the water and the grid around a point, and the cells of its column
//   walk "seed fromx fromz tox toz [height]"        one walker: where it is every second, and the ground on its way where it was first blocked
//   water "seed size resolution trees rocks" f.csv  depth and metres to water of the points "x,z" of a file (GBSim's world: its --size, ...)
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Navigation/NavGrid.h"
#include "GB/Log.h"
#include "GB/World/Terrain.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace GB;

namespace
{
	GeneratedWorld EditorWorld(uint32_t seed, float size = 0.0f, int resolution = 0, int trees = -1, int rocks = -1)
	{
		WorldDesc d;
		d.Type = WorldType::Temperate;
		d.Seed = seed;
		d.GeneratorVersion = kWorldGeneratorLatest;
		if (size > 0.0f) d.Size = size;   // (GBSim's world: --size, --resolution, --trees, --rocks)
		if (resolution > 0) d.Resolution = static_cast<uint32_t>(resolution);
		if (trees >= 0) d.TreeBudget = trees;
		if (rocks >= 0) d.RockBudget = rocks;
		return WorldGenerator::GenerateData(d);
	}

	CharacterWorld Ground(const GeneratedWorld& world)
	{
		CharacterWorld g = MakeTerrainCharacterWorld(*world.TerrainRef);
		for (const TreePlacement& tree : world.Trees) g.Obstacles.push_back({ { tree.Position.x, tree.Position.z }, 0.3f * tree.Scale });
		for (const RockPlacement& rock : world.Rocks)   // (as SimRun does)
		{
			const float size = rock.Variant >= 0 && rock.Variant < static_cast<int>(world.RockRecipes.size()) ? world.RockRecipes[static_cast<size_t>(rock.Variant)].Size : 1.0f;
			g.Obstacles.push_back({ { rock.Position.x, rock.Position.z }, 0.8f * size * rock.Scale });
		}
		return g;
	}

	// Metres to the nearest water of the grid (a cell deeper than 5 cm) within `range`; range + 1 when there is none.
	float WaterDistance(const NavGrid& nav, const glm::vec2& p, float range)
	{
		int ci = 0, cj = 0;
		if (!nav.CellAt(p, ci, cj)) return range + 1.0f;
		const int r = static_cast<int>(std::ceil(range / nav.CellSize()));
		float best = range + 1.0f;
		for (int j = std::max(cj - r, 0); j <= std::min(cj + r, nav.Height() - 1); ++j)
			for (int i = std::max(ci - r, 0); i <= std::min(ci + r, nav.Width() - 1); ++i)
				if (nav.Depth(i, j) > 0.05f) best = std::min(best, glm::distance(nav.CellCenter(i, j), p));
		return best;
	}

	// Whether dry ground can be walked to from the cell of `p` over cells the profile may walk on (a flood of at most `limit` cells). Water that cannot
	// be left this way cannot be walked into either (the walls of the grid are the same both ways): no walker of the game gets there on its feet.
	bool ReachesDryGround(const NavGrid& nav, const NavProfile& profile, const glm::vec2& p, int limit)
	{
		int ci = 0, cj = 0;
		if (!nav.CellAt(p, ci, cj)) return false;
		std::vector<int> open{ cj * nav.Width() + ci };
		std::vector<uint8_t> seen(static_cast<size_t>(nav.Width()) * static_cast<size_t>(nav.Height()), 0);
		seen[static_cast<size_t>(open.front())] = 1;
		for (size_t n = 0; n < open.size() && static_cast<int>(n) < limit; ++n)
		{
			const int i = open[n] % nav.Width(), j = open[n] / nav.Width();
			if (nav.Depth(i, j) <= 0.05f && nav.CellCost(i, j, profile) >= 0.0f) return true;
			for (const glm::ivec2 d : { glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1) })
			{
				const int a = i + d.x, b = j + d.y;
				if (a < 0 || b < 0 || a >= nav.Width() || b >= nav.Height()) continue;
				const size_t k = static_cast<size_t>(b) * static_cast<size_t>(nav.Width()) + static_cast<size_t>(a);
				if (seen[k] || nav.CellCost(a, b, profile) < 0.0f) continue;
				seen[k] = 1;
				open.push_back(static_cast<int>(k));
			}
		}
		return false;
	}

	// One walker of the crowd: where it started, where it went and how it ended.
	struct Trip
	{
		glm::vec2 From{ 0.0f }, To{ 0.0f }, End{ 0.0f };
		bool Wet = false;            // it started in the water (else on the bank)
		bool Arrived = false;
		bool Unreachable = false;    // the grid found no way (a clean failure)
		float Still = 0.0f;          // the longest time it stayed within 1.5 m of one spot (s of game)
		glm::vec2 StillAt{ 0.0f };
		float StillWater = 0.0f;     // metres from the water there
		float StillDepth = 0.0f;
		int GaveUp = 0;              // times the AIDriver gave up (it is ordered again, as the tribe does)
		std::string Why;             // its last reason
	};

	// Walkers that start in the river or on its bank and are sent 20 m away (over the water when that is where the goal falls), each alone, led by an
	// AIDriver over the grid as an editor actor is (a controller of a 1.8 m adult: wades 0.99 m). A walker that gives up is ordered to the same goal
	// again at once (the tribe orders it again). Returns the trips; `wet`/`bank` starts of each kind, spread over the map.
	std::vector<Trip> Crowd(uint32_t seed, int wet, int bank, float seconds, long* walls = nullptr, int* enclosedOut = nullptr)
	{
		const GeneratedWorld world = EditorWorld(seed);
		CharacterWorld ground = Ground(world);
		NavGrid nav;
		std::string error;
		if (!nav.Build(ground, NavConfig(), &error)) { std::fprintf(stderr, "FAIL: seed %u: the grid: %s\n", seed, error.c_str()); return {}; }
		ground.Nav = &nav;
		if (walls) { long costly = 0, wetCells = 0; nav.Count(*walls, costly, wetCells); }
		CharacterController probe;
		probe.Config.MaxWadeDepth = 0.55f * 1.8f;
		probe.Config.Radius = 0.36f;
		NavProfile profile;
		profile.MaxSlope = probe.Config.MaxSlope;
		profile.MaxWadeDepth = probe.Config.MaxWadeDepth;
		// the candidates, in the order of the grid: wadeable water, and dry ground within a metre of water
		std::vector<glm::vec2> wets, banks;
		int enclosed = 0;   // wadeable water no walker can get into (walled in by banks too steep to climb)
		for (int j = 2; j < nav.Height() - 2; j += 2)
			for (int i = 2; i < nav.Width() - 2; i += 2)
			{
				if (nav.Blocked(i, j)) continue;
				const glm::vec2 p = nav.CellCenter(i, j);
				const float d = ground.WaterLevel(p.x, p.y) - ground.GroundHeight(p.x, p.y);
				if (d > 0.05f && d < profile.MaxWadeDepth - 0.1f) { if (ReachesDryGround(nav, profile, p, 20000)) wets.push_back(p); else ++enclosed; }
				else if (d <= 0.0f && WaterDistance(nav, p, 1.0f) <= 1.0f) banks.push_back(p);
			}
		if (enclosedOut) *enclosedOut = enclosed;
		std::vector<std::pair<glm::vec2, bool>> starts;
		for (int k = 0; k < wet && !wets.empty(); ++k) starts.push_back({ wets[wets.size() * static_cast<size_t>(k) / static_cast<size_t>(wet)], true });
		for (int k = 0; k < bank && !banks.empty(); ++k) starts.push_back({ banks[banks.size() * static_cast<size_t>(k) / static_cast<size_t>(bank)], false });
		std::vector<Trip> trips;
		const float dt = 1.0f / 30.0f;
		for (size_t n = 0; n < starts.size(); ++n)
		{
			Trip t;
			t.From = starts[n].first;
			t.Wet = starts[n].second;
			// the goal: 20 m away, the first of eight directions (turned for each walker) that ends on dry ground the walker can stand on
			bool found = false;
			for (int k = 0; k < 8 && !found; ++k)
			{
				const float a = 0.7853982f * static_cast<float>((static_cast<int>(n) + k) % 8);
				const glm::vec2 g = t.From + 20.0f * glm::vec2(std::cos(a), std::sin(a));
				if (nav.Walkable(g, profile) && ground.WaterLevel(g.x, g.y) <= ground.GroundHeight(g.x, g.y)) { t.To = g; found = true; }
			}
			if (!found) continue;
			CharacterController c;
			c.Config = probe.Config;
			c.SetPosition(ground, t.From.x, t.From.y, 0.0f);
			AIDriver ai;
			ai.MoveTo(t.To);
			glm::vec2 anchor = t.From;
			float since = 0.0f;
			for (float time = 0.0f; time < seconds; time += dt)
			{
				nav.NewFrame();
				c.Step(ai.Update(c, ground, dt), ground, dt);
				const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
				if (glm::distance(here, anchor) > 1.5f) { anchor = here; since = time; }
				if (time - since > t.Still) { t.Still = time - since; t.StillAt = anchor; }
				if (ai.Arrived()) { t.Arrived = true; break; }
				if (ai.Stuck())
				{
					++t.GaveUp;
					t.Why = ai.StuckReason();
					// a goal the grid says it cannot reach is a clean failure (the tribe picks something else); a stall is ordered again
					if (t.Why.rfind("no path", 0) == 0 || t.Why.rfind("the goal is cut off", 0) == 0) { t.Unreachable = true; break; }
					ai.MoveTo(t.To);
				}
			}
			t.End = { c.GetPosition().x, c.GetPosition().z };
			t.StillWater = WaterDistance(nav, t.StillAt, 6.0f);
			t.StillDepth = std::max(0.0f, ground.WaterLevel(t.StillAt.x, t.StillAt.y) - ground.GroundHeight(t.StillAt.x, t.StillAt.y));
			trips.push_back(t);
		}
		return trips;
	}

	// The crowd of one seed, printed: every walker that stood still for `limit` s or more, and a summary line. Returns those stuck by the water.
	int Report(uint32_t seed, int wet, int bank, float seconds, float limit)
	{
		long walls = 0;
		int enclosed = 0;
		const std::vector<Trip> trips = Crowd(seed, wet, bank, seconds, &walls, &enclosed);
		int arrived = 0, stuck = 0, still = 0, unreachable = 0, trapped = 0;
		for (const Trip& t : trips)
		{
			arrived += t.Arrived ? 1 : 0;
			unreachable += t.Unreachable ? 1 : 0;
			// a walker that never got out of the water because the grid found no way out of it is stuck too
			const bool caught = t.Unreachable && t.Wet && t.StillDepth > 0.05f && glm::distance(t.End, t.From) < 1.5f;
			trapped += caught ? 1 : 0;
			if (t.Still < limit && !caught) continue;
			++still;
			const bool water = t.StillWater <= 3.0f;
			stuck += water ? 1 : 0;
			std::printf("  seed %u %s start (%.1f, %.1f) -> (%.1f, %.1f): still %.0f s at (%.2f, %.2f), %.1f m from water, depth %.2f, gave up %d%s (%s)\n", seed, t.Wet ? "wet " : "bank",
				static_cast<double>(t.From.x), static_cast<double>(t.From.y), static_cast<double>(t.To.x), static_cast<double>(t.To.y), static_cast<double>(t.Still),
				static_cast<double>(t.StillAt.x), static_cast<double>(t.StillAt.y), static_cast<double>(t.StillWater), static_cast<double>(t.StillDepth), t.GaveUp, caught ? ", trapped" : "", t.Why.substr(0, 70).c_str());
		}
		std::printf("seed %u: %zu walkers, %d arrived, %d unreachable (%d trapped in the water), %d stuck (still >= %.0f s or trapped), %d of them by the water; %ld wall cells, %d of wadeable water walled in\n", seed,
			trips.size(), arrived, unreachable, trapped, still, static_cast<double>(limit), stuck, walls, enclosed);
		return stuck;
	}

	void Probe(const std::string& spec)
	{
		std::istringstream in(spec);
		uint32_t seed = 5;
		float x = 0.0f, z = 0.0f, half = 3.0f;
		in >> seed >> x >> z >> half;
		const GeneratedWorld world = EditorWorld(seed);
		const CharacterWorld ground = Ground(world);
		NavGrid nav;
		std::string error;
		nav.Build(ground, NavConfig(), &error);
		std::printf("seed %u around (%.2f, %.2f): h = ground height, d = water depth, s = nav slope, c = nav cost (- wall)\n", seed, static_cast<double>(x), static_cast<double>(z));
		for (float dz = -half; dz <= half + 1e-3f; dz += 0.5f)
		{
			for (float dx = -half; dx <= half + 1e-3f; dx += 0.5f)
			{
				const float px = x + dx, pz = z + dz;
				const float h = ground.GroundHeight(px, pz);
				const float w = ground.WaterLevel(px, pz);
				int i = 0, j = 0;
				float cost = -2.0f, slope = 0.0f;
				if (nav.CellAt({ px, pz }, i, j))
				{
					cost = nav.CellCost(i, j, NavProfile());
					const float e = 0.25f;
					const float gx = (ground.GroundHeight(px + e, pz) - ground.GroundHeight(px - e, pz)) / (2.0f * e);
					const float gz = (ground.GroundHeight(px, pz + e) - ground.GroundHeight(px, pz - e)) / (2.0f * e);
					slope = std::sqrt(gx * gx + gz * gz);
				}
				std::printf("[%6.2f %5.2f %5.2f %4.1f] ", static_cast<double>(h), static_cast<double>(w > -1e8f ? w - h : -9.99f), static_cast<double>(slope), static_cast<double>(cost));
			}
			std::printf("  z %.2f\n", static_cast<double>(z + dz));
		}
		// the cells of the grid in the column of x: their centre, the slope as the grid reads it (central differences over the cell) and their cost
		int ci = 0, cj = 0;
		if (!nav.CellAt({ x, z }, ci, cj)) return;
		const float cs = nav.CellSize(), hc = cs * 0.5f;
		for (int j = std::max(cj - 6, 0); j <= std::min(cj + 6, nav.Height() - 1); ++j)
		{
			const glm::vec2 p = nav.CellCenter(ci, j);
			const float gx = (ground.GroundHeight(p.x + hc, p.y) - ground.GroundHeight(p.x - hc, p.y)) / cs;
			const float gz = (ground.GroundHeight(p.x, p.y + hc) - ground.GroundHeight(p.x, p.y - hc)) / cs;
			std::printf("cell (%d, %d) at (%.2f, %.2f): h %.3f slope %.2f depth %.2f cost %.1f\n", ci, j, static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(ground.GroundHeight(p.x, p.y)),
				static_cast<double>(std::sqrt(gx * gx + gz * gz)), static_cast<double>(nav.Depth(ci, j)), static_cast<double>(nav.CellCost(ci, j, NavProfile())));
		}
	}

	// One walker (a controller led by an AIDriver, as an editor actor) from a point to a goal: where it ends and why.
	void Walk(const std::string& spec)
	{
		std::istringstream in(spec);
		uint32_t seed = 5;
		float fx = 0.0f, fz = 0.0f, tx = 0.0f, tz = 0.0f, height = 1.8f;
		in >> seed >> fx >> fz >> tx >> tz >> height;
		const GeneratedWorld world = EditorWorld(seed);
		CharacterWorld ground = Ground(world);
		NavGrid nav;
		std::string error;
		nav.Build(ground, NavConfig(), &error);
		ground.Nav = &nav;
		CharacterController c;
		c.Config.MaxWadeDepth = 0.55f * height;
		c.Config.Radius = std::max(0.2f * height, 0.25f);
		c.SetPosition(ground, fx, fz, 0.0f);
		AIDriver ai;
		ai.MoveTo({ tx, tz });
		const float dt = 1.0f / 30.0f;
		int f = 0;
		std::vector<glm::vec2> path;
		size_t index = 0;
		glm::vec2 blockedAt(0.0f);
		for (; f < 30 * 300 && !ai.Arrived() && !ai.Stuck(); ++f)
		{
			c.Step(ai.Update(c, ground, dt), ground, dt);
			if (c.WasBlocked() && path.empty() && !ai.Path().empty()) { path = ai.Path(); index = ai.PathIndex(); blockedAt = { c.GetPosition().x, c.GetPosition().z }; }
			if (f % 30 == 0 && f < 30 * 20)
				std::printf("t %5.1f at (%.2f, %.2f) y %.2f depth %.2f blocked %d waypoint %zu/%zu\n", static_cast<double>(f) * dt, static_cast<double>(c.GetPosition().x),
					static_cast<double>(c.GetPosition().z), static_cast<double>(c.GetPosition().y), static_cast<double>(c.GetWadeDepth()), c.WasBlocked() ? 1 : 0, ai.PathIndex(), ai.Path().size());
		}
		std::printf("after %.1f s: arrived %d stuck %d (%s) at (%.2f, %.2f)\n", static_cast<double>(f) * dt, ai.Arrived() ? 1 : 0, ai.Stuck() ? 1 : 0, ai.StuckReason().c_str(),
			static_cast<double>(c.GetPosition().x), static_cast<double>(c.GetPosition().z));
		std::printf("first blocked at (%.2f, %.2f) heading for waypoint %zu of:", static_cast<double>(blockedAt.x), static_cast<double>(blockedAt.y), index);
		for (size_t k = 0; k < path.size() && k < 12; ++k) std::printf("  (%.2f, %.2f)", static_cast<double>(path[k].x), static_cast<double>(path[k].y));
		std::printf("\n");
		if (index < path.size())
		{
			// the ground and the water on the way to that waypoint, every 5 cm
			const glm::vec2 a = blockedAt, b = path[index];
			const float len = glm::distance(a, b);
			float prev = ground.GroundHeight(a.x, a.y);
			for (float s = 0.05f; s < std::min(len, 2.0f); s += 0.05f)
			{
				const glm::vec2 p = a + (b - a) * (s / len);
				const float h = ground.GroundHeight(p.x, p.y), w = ground.WaterLevel(p.x, p.y);
				std::printf("  s %.2f h %.3f rise %.2f depth %.2f\n", static_cast<double>(s), static_cast<double>(h), static_cast<double>((h - prev) / 0.05f), static_cast<double>(w > -1e8f ? w - h : 0.0f));
				prev = h;
			}
		}
	}
}

namespace
{
	// The water by each point of a list (lines "x,z"): prints "x,z,depth,metres to water" for the GBSim world "seed size resolution trees rocks".
	void Water(const std::string& spec, const std::string& csv)
	{
		std::istringstream in(spec);
		uint32_t seed = 5;
		float size = 0.0f;
		int resolution = 0, trees = -1, rocks = -1;
		in >> seed >> size >> resolution >> trees >> rocks;
		const GeneratedWorld world = EditorWorld(seed, size, resolution, trees, rocks);
		const CharacterWorld ground = Ground(world);
		NavGrid nav;
		std::string error;
		nav.Build(ground, NavConfig(), &error);
		std::FILE* f = std::fopen(csv.c_str(), "r");
		if (!f) return;
		std::printf("x,z,depth,dist\n");
		float x = 0.0f, z = 0.0f;
		while (std::fscanf(f, "%f,%f", &x, &z) == 2)
			std::printf("%.1f,%.1f,%.2f,%.1f\n", static_cast<double>(x), static_cast<double>(z), static_cast<double>(std::max(0.0f, ground.WaterLevel(x, z) - ground.GroundHeight(x, z))),
				static_cast<double>(WaterDistance(nav, { x, z }, 30.0f)));
		std::fclose(f);
	}
}

int GBTest_river_stuck(int argc, char** argv)
{
	if (argc >= 4 && std::string(argv[1]) == "water") { Log::Init(); Water(argv[2], argv[3]); return 0; }
	Log::Init();
	if (argc >= 3 && std::string(argv[1]) == "probe") { Probe(argv[2]); return 0; }
	if (argc >= 3 && std::string(argv[1]) == "walk") { Walk(argv[2]); return 0; }
	int stuck = 0;
	for (const uint32_t seed : { 5u, 7u, 77u }) stuck += Report(seed, 30, 30, 150.0f, 60.0f);
	if (stuck > 0) { std::fprintf(stderr, "FAIL: %d walkers stuck for a minute or more by the water\n", stuck); return 1; }
	std::printf("river_stuck: ok\n");
	return 0;
}
