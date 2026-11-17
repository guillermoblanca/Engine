// T6: the buildings are walls of the navigation (GB::BuildingObstacles, GB::FootprintObstacles). CPU only, no window, deterministic.
// Checked: a path that crossed the ground of a hut goes round it once the hut stands; its door (the open front, SiteFront) stays walkable and every bed
// inside is reached from it; a site is no wall until its walls go up (a bench: until it is done); taking the building away frees its cells and the
// straight path is back; a member caught on a new wall has an open cell next to it (NavGrid::NearestOpen). And for every built-in building: its
// front and the spots beside it (builders, crafters, the wounded) stay walkable and reachable, its beds too, and a dug well is never a wall.
#include "GB/Game/SettlementPlanner.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Navigation/NavGrid.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	CharacterWorld Flat()
	{
		CharacterWorld w;
		w.BoundsMin = { -30.0f, -30.0f };
		w.BoundsMax = { 30.0f, 30.0f };
		return w;
	}

	float Length(const glm::vec2& from, const NavPath& p)
	{
		float len = 0.0f;
		glm::vec2 at = from;
		for (const glm::vec2& q : p.Points) { len += glm::distance(at, q); at = q; }
		return len;
	}

	// A building of `def` called `name` at `pos` (yaw degrees), with its ability system, at phase `phase` (-1: done).
	void Place(EntityWorld& entities, GameplayWorld& gameplay, const BuildingDef& def, const std::string& name, const glm::vec2& pos, int phase, float yaw = 0.0f)
	{
		const EntityId id = BuildSite(entities, def, name, { pos.x, 0.0f, pos.y }, yaw);
		AbilitySystemComponent* a = gameplay.Create(name);
		a->FromComponent(*entities.Find(id)->Gameplay);
		const int phases = static_cast<int>(def.Phases.size());
		a->Attributes().SetBase("Phase", static_cast<float>(phase < 0 ? phases : phase));
		if (phase < 0) { a->Tags().RemoveAll("Building.Site"); a->Tags().Add("Building.Done"); }
	}

	// The beds of a house, as Tribe::SleepSpot puts them (Settlement.cpp): along its x, at its middle.
	std::vector<glm::vec2> Beds(const BuildingDef& def, const glm::vec2& pos)
	{
		std::vector<glm::vec2> out;
		for (int k = 0; k < def.Shelter; ++k)
			out.push_back(pos + glm::vec2((-0.5f + (static_cast<float>(k) + 0.5f) / static_cast<float>(def.Shelter)) * 0.7f * def.Footprint.x, 0.0f));
		return out;
	}
}

int GBTest_building_nav(int, char**)
{
	const NavProfile walker;
	const std::map<std::string, BuildingDef> defs = DefaultBuildings();
	const BuildingDef& hut = defs.at("hut");
	const glm::vec2 from(-8.0f, 0.3f), to(8.0f, 0.3f), at(0.0f, 0.0f);

	// ---- a hut on the way
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		NavGrid g;
		Check(g.Build(Flat()), "the flat grid builds");
		NavPath before;
		Check(g.FindPath(walker, from, to, before) && before.Found, "a path over open ground is found");
		const float straight = Length(from, before);
		Check(straight < 16.2f, "over open ground it is the straight line");

		// a site with only its floor laid is no wall
		Place(entities, gameplay, hut, "Hut 1", at, 0);
		std::vector<CircleObstacle> walls;
		BuildingObstacles(entities, gameplay, defs, walls);
		Check(walls.empty(), "a hut site with its floor only is no wall");

		// its walls phase started: the walls stand
		int wallsPhase = -1;
		for (size_t i = 0; i < hut.Phases.size(); ++i) if (wallsPhase < 0 && hut.Phases[i].Part == "walls") wallsPhase = static_cast<int>(i);
		gameplay.Find("Hut 1")->Attributes().SetBase("Phase", static_cast<float>(wallsPhase));
		BuildingObstacles(entities, gameplay, defs, walls);
		Check(!walls.empty(), "a hut whose walls are going up is a wall");
		const int cells = g.SyncObstacles(walls);
		Check(cells > 0 && cells < g.Width() * g.Height() / 10, "the hut redoes only the cells around it");
		NavPath round;
		Check(g.FindPath(walker, from, to, round) && round.Found, "a path past the hut is found");
		const float roundLen = Length(from, round);
		bool crosses = false;
		glm::vec2 prev = from;
		for (const glm::vec2& q : round.Points)
		{
			for (int k = 0; k <= 20; ++k)   // no point of the path inside the walls' square
			{
				const glm::vec2 p = prev + (q - prev) * (static_cast<float>(k) / 20.0f);
				crosses = crosses || (std::fabs(p.x) < 0.5f * hut.Footprint.x && std::fabs(p.y) < 0.5f * hut.Footprint.y && p.y < 0.5f * hut.Footprint.y - 0.6f);
			}
			prev = q;
		}
		std::printf("hut: %zu circles, %d cells redone; straight %.1f m, round %.1f m\n", walls.size(), cells, static_cast<double>(straight), static_cast<double>(roundLen));
		Check(roundLen > straight + 0.8f && !crosses, "the path that crossed the hut now goes round it");
		int i, j;
		Check(g.CellAt(at + glm::vec2(-0.5f * hut.Footprint.x, 0.0f), i, j) && g.Blocked(i, j), "the cell of its left wall is blocked");
		Check(g.CellAt(at + glm::vec2(0.0f, -0.5f * hut.Footprint.y), i, j) && g.Blocked(i, j), "the cell of its back wall is blocked");

		// done: the same walls; the door, its middle and the beds are walkable and reached from the front
		Place(entities, gameplay, hut, "Hut 2", { 0.0f, 12.0f }, -1);
		gameplay.Find("Hut 1")->Attributes().SetBase("Phase", static_cast<float>(hut.Phases.size()));
		BuildingObstacles(entities, gameplay, defs, walls = {});
		g.SyncObstacles(walls);
		const glm::vec2 door = SiteFront(at, hut);
		Check(g.Walkable(door) && g.Walkable(at + glm::vec2(0.0f, 0.5f * hut.Footprint.y)), "the door of the hut (its open front) is walkable");
		NavPath in;
		Check(g.FindPath(walker, door, at, in) && in.Found, "the middle of the hut is reached from its door");
		for (const glm::vec2& bed : Beds(hut, at))
		{
			NavPath p;
			Check(g.Walkable(bed) && g.FindPath(walker, door, bed, p) && p.Found, "every bed of the hut is reached from its door");
		}

		// a member caught on a new wall has an open cell next to it
		glm::vec2 open;
		const glm::vec2 onWall = at + glm::vec2(-0.5f * hut.Footprint.x, 0.0f);
		Check(g.NearestOpen(onWall, open) && g.Walkable(open) && glm::distance(open, onWall) < 1.5f, "someone on a new wall gets out to the nearest open cell");

		// taken away: its cells are free again and the straight path is back
		for (const char* name : { "Hut 1", "Hut 2" })
		{
			gameplay.Remove(name);
			entities.Destroy(entities.FindByName(name));
		}
		BuildingObstacles(entities, gameplay, defs, walls = {});
		Check(walls.empty(), "no buildings, no walls");
		Check(g.SyncObstacles(walls) > 0, "taking the hut away redoes its cells");
		Check(g.CellAt(at + glm::vec2(-0.5f * hut.Footprint.x, 0.0f), i, j) && !g.Blocked(i, j), "the cells of the hut are free once it is gone");
		NavPath after;
		Check(g.FindPath(walker, from, to, after) && after.Found && Length(from, after) < straight + 0.05f, "the straight path is back");
	}

	// ---- turned: the walls turn with the building (the open side is its local +z)
	{
		EntityWorld entities;
		GameplayWorld gameplay;
		Place(entities, gameplay, hut, "Hut", at, -1, 90.0f);
		std::vector<CircleObstacle> walls;
		BuildingObstacles(entities, gameplay, defs, walls);
		CharacterWorld w = Flat();
		w.Obstacles = walls;
		NavGrid g;
		Check(g.Build(w), "the grid of the turned hut builds");
		const glm::mat4 m = entities.WorldMatrix(entities.FindByName("Hut"));
		const glm::vec2 front = at + glm::vec2(m[2].x, m[2].z) * (0.5f * hut.Footprint.y + 0.8f);
		NavPath in;
		Check(g.Walkable(front) && g.FindPath(walker, front, at, in) && in.Found && Length(front, in) < 3.5f, "a turned hut is walked into from its turned front");
	}

	// ---- every built-in building
	{
		int checked = 0;
		for (const auto& [id, def] : defs)
		{
			EntityWorld entities;
			GameplayWorld gameplay;
			Place(entities, gameplay, def, def.Name, at, -1);
			std::vector<CircleObstacle> walls;
			BuildingObstacles(entities, gameplay, defs, walls);
			if (def.Well || def.AllowWater) { Check(walls.empty(), id + ": a well or a building over water is no wall"); continue; }
			Check(!walls.empty(), id + ": a finished building is a wall");
			CharacterWorld w = Flat();
			w.Obstacles = walls;
			NavGrid g;
			Check(g.Build(w), id + ": the grid builds");
			int i, j;
			Check(g.CellAt(at + glm::vec2(0.0f, -0.5f * def.Footprint.y + 0.2f), i, j) && g.Blocked(i, j), id + ": the back of it is blocked");
			for (float side : { -0.7f, 0.0f, 0.7f })   // the front and beside it: builders, crafters at a station, the wounded at a healer's (Settlement.cpp)
			{
				const glm::vec2 spot = SiteFront(at, def, side);
				NavPath p;
				Check(g.Walkable(spot) && g.FindPath(walker, { 0.0f, 12.0f }, spot, p) && p.Found, id + ": its front is walkable and reached");
			}
			for (const glm::vec2& bed : Beds(def, at))
			{
				NavPath p;
				Check(g.Walkable(bed) && g.FindPath(walker, SiteFront(at, def), bed, p) && p.Found, id + ": its beds are reached from its door");
			}
			++checked;
		}
		std::printf("buildings: %d checked of %zu\n", checked, defs.size());
	}

	if (failures == 0) std::printf("building_nav_test: ok\n");
	return failures == 0 ? 0 : 1;
}
