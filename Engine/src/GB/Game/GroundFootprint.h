#pragma once
// T260: the ground a building, a site or a field takes, and the one query every placement asks before it claims some: is this rectangle free?
// A footprint is a rectangle on the ground (x, z), turned by a yaw. A building's is its data footprint (BuildingDef::Footprint) where it stands; a
// field's covers its plots and the store beside the first (Tribe::MarkField). The clan's planner (SiteFits, FieldOriginFor), the god's sign
// (WorldDirector::PreviewSign through SiteFits) and every site that starts (Tribe::StartSite: the planner's, the player's, a script's) ask
// GroundFree; GBSim's report counts what overlaps at the end (GroundOverlaps). Pure CPU and deterministic: no scene type beyond the data.
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Farming.h"
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	class Tribe;
	class EntityWorld;
	class GameplayWorld;
	class ClanWorld;

	struct GroundRect
	{
		glm::vec2 Centre{ 0.0f };
		glm::vec2 Half{ 0.0f };   // half its size along its own x and z
		float Yaw = 0.0f;         // degrees about the vertical (as Transform.Rotation.y)
		std::string What;         // what stands there, for the reason ("Hut", "a field")
	};

	// How much two footprints may share before they count as one on top of the other (a contiguous wall touches: it is not an overlap).
	inline constexpr float kGroundTouch = 0.05f;
	// The gap the clan keeps between a building and a field, and between two fields, when it chooses the place itself (a path to walk).
	inline constexpr float kFieldGap = 1.0f;

	GroundRect BuildingGround(const BuildingDef& def, const glm::vec2& at, float yawDegrees = 0.0f);
	// A field of `plots` plots (`columns` a row, `spacing` m apart, the first plot's middle at `origin`: Field::PlotPos), its store one plot to the
	// -x of the first included.
	GroundRect FieldGround(const glm::vec2& origin, int plots, int columns, float spacing);
	GroundRect FieldGround(const Field& field);

	// Whether two footprints, each grown by `gap` / 2 on every side, overlap by more than kGroundTouch (separating axes).
	bool GroundOverlaps(const GroundRect& a, const GroundRect& b, float gap = 0.0f);
	// "" when `want` overlaps nothing of `taken` (with `gap`), else why: "it would stand on a field", "it would stand on Hut".
	std::string GroundFree(const GroundRect& want, const std::vector<GroundRect>& taken, float gap = 0.0f);

	// The ground taken round a clan now: every site and building near its camp (Tribe::SettlementSites: any clan's), its fields and the fields of
	// the clans around it (TribeOutside::Ground). `fields` false: the buildings only.
	std::vector<GroundRect> TakenGround(const Tribe& tribe, bool fields = true);
	// The hard rule of every new site: "" when a `def` at `at` overlaps no site, building or field, else why (Tribe::StartSite refuses it).
	std::string GroundFreeFor(const Tribe& tribe, const BuildingDef& def, const glm::vec2& at);
	// Where a site of `building` goes when nobody said where (`settlement start <id>` without a place): `wanted` when its ground is free, else the
	// spot the planner would choose (PlaceSite of Tribe::SitePlacementFor), else `wanted` (and StartSite says why not).
	glm::vec2 FreeSiteSpot(const Tribe& tribe, const std::string& building, const glm::vec2& wanted);

	// Every footprint of the world as it stands (each site and building of the scene with its yaw, each field of every clan) and the pairs that
	// overlap (GBSim --report, the regression test). `clans` null: the fields of `alone` only.
	std::vector<GroundRect> WorldGround(const EntityWorld& entities, const GameplayWorld& gameplay, const std::map<std::string, BuildingDef>& defs, const ClanWorld* clans, const Tribe* alone);
	std::vector<std::pair<size_t, size_t>> OverlappingPairs(const std::vector<GroundRect>& ground);
}
