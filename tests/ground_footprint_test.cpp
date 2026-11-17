// T260: no building, site or field is set on another's footprint (GB/Game/GroundFootprint.h). Checked: the footprint query alone (an overlap is
// refused, a contiguous rectangle is not, a turned one is seen turned, a gap is kept when asked); a clan's StartSite refuses a site on a building
// and on a field and takes one beside them; the clan's planner (PlaceSite with its SitePlacementFor) never picks a spot on a field, and a field the
// clan marks out (FieldOriginFor) avoids its buildings and the fields of another clan of the world (ClanWorld's TribeOutside::Ground); a script's
// site with no place (FreeSiteSpot) is not stacked on the last one; the world's footprints then have no overlapping pair. CPU only, no bodies.
#include "GB/Log.h"
#include "GB/Game/Clans.h"
#include "GB/Game/FieldPlacement.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/SettlementPlanner.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

namespace
{
	// A clan with no members on an empty ground (sites and fields only: the footprints are all that is asked)
	struct Camp
	{
		Camp()
		{
			RegisterEraOneItems(Items, Recipes, Defs);
			RegisterResourceContent(Gameplay.Library, Defs);
			RegisterInteractiveContent(Gameplay.Library);
			RegisterBuildingContent(Gameplay.Library);
			Gameplay.Load(Entities);
			Builds.Bind(Gameplay);
			Tribe_.Bind(Entities, Gameplay, Items, Recipes, Defs);
			Tribe_.SetBuildings(&Builds.Defs);
			Tribe_.Config.Economy.Camp = { 0.0f, 0.0f };
			Tribe_.Config.PlanSeconds = 1e9f;
		}
		ItemLibrary Items;
		RecipeBook Recipes;
		std::map<std::string, ResourceNodeDef> Defs;
		EntityWorld Entities;
		GameplayWorld Gameplay;
		BuildingSystem Builds;
		Tribe Tribe_;
	};

	BuildingDef Box(float x, float z)
	{
		BuildingDef d;
		d.Id = d.Name = "box";
		d.Footprint = { x, z };
		return d;
	}

	bool NoOverlaps(const Camp& c, const ClanWorld* clans = nullptr)
	{
		return OverlappingPairs(WorldGround(c.Entities, c.Gameplay, c.Builds.Defs, clans, &c.Tribe_)).empty();
	}
}

int GBTest_ground_footprint(int, char**)
{
	Log::Init();

	// ---- the query alone
	{
		const BuildingDef box = Box(3.0f, 3.0f);
		const std::vector<GroundRect> taken = { BuildingGround(box, { 0.0f, 0.0f }) };
		Check(!GroundFree(BuildingGround(box, { 2.0f, 0.0f }), taken).empty(), "a box 2 m from another of 3 m overlaps it");
		Check(GroundFree(BuildingGround(box, { 2.0f, 0.0f }), taken).find("it would stand on box") == 0, "and says on what");
		Check(GroundFree(BuildingGround(box, { 3.0f, 0.0f }), taken).empty(), "a contiguous one (sharing a wall) is free");
		Check(GroundFree(BuildingGround(box, { 3.0f, 3.0f }), taken).empty(), "one touching a corner is free");
		Check(!GroundFree(BuildingGround(box, { 3.0f, 0.0f }), taken, kFieldGap).empty(), "with a gap asked, the contiguous one is too near");
		Check(GroundFree(BuildingGround(box, { 4.0f, 0.0f }), taken, kFieldGap).empty(), "and one a gap away is free");
		// a long box turned 90 degrees: along z now
		const BuildingDef longBox = Box(8.0f, 1.0f);
		Check(!GroundFree(BuildingGround(longBox, { 0.0f, 4.0f }, 90.0f), taken).empty(), "a long box turned across the other's side overlaps it");
		Check(GroundFree(BuildingGround(longBox, { 3.0f, 0.0f }, 90.0f), taken).empty(), "and turned beside it, it does not");
		Check(!GroundFree(BuildingGround(longBox, { 3.0f, 0.0f }), taken).empty(), "the same unturned does");
		// a field covers its plots and its store
		const GroundRect field = FieldGround({ 0.0f, 0.0f }, 6, 3, 2.0f);
		Check(std::abs(field.Half.x - 4.0f) < 1e-4f && std::abs(field.Half.y - 2.0f) < 1e-4f && std::abs(field.Centre.x - 1.0f) < 1e-4f && std::abs(field.Centre.y - 1.0f) < 1e-4f,
			"a field of 6 plots in rows of 3, 2 m apart, covers 8 x 4 m from its store to its last plot");
		Check(GroundFree(BuildingGround(box, { 1.0f, 1.0f }), { field }) == "it would stand on a field", "a building on a field is refused as on a field");
	}

	// ---- the clan's sites: StartSite refuses one on another's footprint, takes one beside it
	{
		Camp c;
		const BuildingDef& shelter = c.Builds.Defs.at("shelter");
		std::string first;
		Check(c.Tribe_.StartSite("shelter", { 10.0f, 0.0f }, &first).empty() && !first.empty(), "a first shelter starts");
		const std::string onTop = c.Tribe_.StartSite("shelter", { 11.0f, 0.5f });
		Check(onTop.find("cannot stand there: it would stand on " + shelter.Name) != std::string::npos, "a second one overlapping it is refused, saying why");
		Check(c.Tribe_.StartSite("shelter", { 10.0f + shelter.Footprint.x, 0.0f }).empty(), "one beside it, wall to wall, starts");
		Check(c.Tribe_.SettlementSites().size() == 2, "and only those two stand");
		Check(NoOverlaps(c), "no footprint of the scene overlaps another");
		// a field: no site on it
		c.Tribe_.MarkField({ -12.0f, 6.0f });
		const GroundRect f = FieldGround(c.Tribe_.Economy().Fields.back());
		Check(c.Tribe_.StartSite("shelter", f.Centre).find("it would stand on a field") != std::string::npos, "a site on a field is refused");
		// a script's site with no place goes where the planner would, not on the last one
		const glm::vec2 spot = FreeSiteSpot(c.Tribe_, "shelter", { 10.0f, 0.0f });
		Check(glm::distance(spot, glm::vec2(10.0f, 0.0f)) > 1.0f && GroundFreeFor(c.Tribe_, shelter, spot).empty(), "a site with no place asked goes to free ground");
		Check(FreeSiteSpot(c.Tribe_, "shelter", { -30.0f, -30.0f }) == glm::vec2(-30.0f, -30.0f), "and where the asked place is free, there");
	}

	// ---- the clan chooses: the planner's spot is never on a field, and its field never on a building
	{
		Camp c;
		const BuildingDef& shelter = c.Builds.Defs.at("shelter");
		SitePlacement where = c.Tribe_.SitePlacementFor(shelter);
		const std::optional<glm::vec2> bare = PlaceSite(where);
		Check(bare.has_value(), "on bare ground the planner finds a spot");
		if (bare)
		{
			// a field right there: the planner looks elsewhere, and its spot keeps off the field
			c.Tribe_.MarkField(*bare - glm::vec2(1.0f, 1.0f));
			where = c.Tribe_.SitePlacementFor(shelter);
			const std::optional<glm::vec2> moved = PlaceSite(where);
			Check(moved && glm::distance(*moved, *bare) > 1.0f, "with a field on its first spot the planner picks another");
			Check(moved && GroundFree(BuildingGround(shelter, *moved), { FieldGround(c.Tribe_.Economy().Fields.back()) }, kFieldGap).empty(), "and that one keeps off the field");
			Check(moved && c.Tribe_.StartSite("shelter", *moved).empty(), "where it starts");
		}
		// a field wanted on a building goes beside it
		std::string hut;
		Check(c.Tribe_.StartSite("shelter", { 0.0f, 20.0f }, &hut).empty(), "a shelter at 0, 20");
		const std::optional<glm::vec2> origin = FieldOriginFor(c.Tribe_, { 0.0f, 20.0f });
		const FarmRules& r = c.Tribe_.Config.Econ.Farming;
		Check(origin && GroundFree(FieldGround(*origin, r.Plots, r.Columns, r.Spacing), TakenGround(c.Tribe_), kFieldGap).empty(), "a field wanted on the shelter is marked off it");
		if (origin) c.Tribe_.MarkField(*origin);
		Check(NoOverlaps(c), "no footprint of the scene overlaps another");
	}

	// ---- two clans on one ground (a shared start site): a field keeps off the other clan's field
	{
		Camp a;
		Tribe b;
		b.Config = a.Tribe_.Config;
		ClanWorld world;
		world.Add("A", &a.Tribe_);
		world.Add("B", &b);
		a.Tribe_.MarkField({ 5.0f, 5.0f });
		const std::optional<glm::vec2> origin = FieldOriginFor(b, { 5.0f, 5.0f });
		const FarmRules& r = b.Config.Econ.Farming;
		Check(origin && !GroundOverlaps(FieldGround(*origin, r.Plots, r.Columns, r.Spacing), FieldGround(a.Tribe_.Economy().Fields.back())), "the second clan's field keeps off the first clan's");
		if (origin) b.MarkField(*origin);
		Check(NoOverlaps(a, &world), "and no field of the world overlaps another");
	}

	if (failures) std::fprintf(stderr, "%d checks failed\n", failures);
	else std::printf("ground footprint test passed\n");
	return failures ? 1 : 0;
}
