#pragma once
// What the clan builds and where (H8): the decisions of the settlement apart from the Tribe. The planner reads a SettlementFacts (a snapshot of what the
// clan has, wants and knows, gathered by Tribe::BuildingNeeds) and answers with a list of needs, most urgent first; PlaceSite finds a spot for a site
// from a SitePlacement. Neither knows the Tribe, so both run (and are tested) alone; Tribe keeps BuildingNeeds / PlanSettlement as the facade.
//
// Made to grow into "several works at once with a priority": the answer is already a ranked list, and the caller decides how many it starts
// (SettlementFacts::MaxOpenSites). Pending: backlog T153 and T10.
#include "GB/Game/GroundFootprint.h"
#include "GB/Gameplay/Buildings.h"
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GB
{
	// What the clan wants built and why (Tribe::BuildingNeeds): the planner starts the most urgent; H7's job board can publish them.
	struct BuildingNeed
	{
		std::string Building;   // def id
		float Urgency = 0.0f;   // shelter for everyone 3, a full store 2, a station a recipe needs 1.5, food going off 1.2... (see docs/PROGRESION.md)
		std::string Why;
	};

	// A site or a finished building of the scene.
	struct PlannerSite
	{
		const BuildingDef* Def = nullptr;
		glm::vec2 Pos{ 0.0f };
		bool Done = false;
	};

	// The clan as the planner sees it. The callbacks answer what needs the tribe's store, knowledge or recipes; they must not change anything.
	struct SettlementFacts
	{
		const std::map<std::string, BuildingDef>* Buildings = nullptr;
		std::vector<PlannerSite> Sites;   // every site and building of the camp, done or not
		int Alive = 0;                    // living members
		int MaxOpenSites = 1;             // sites open at once (EconomyRules::Construction.SitesFor(Alive)); no need is answered when this many are open
		int Wounded = 0;                  // members with wounds (H14)
		int Spoiled = 0;                  // food the store lost to spoilage (H18)
		int Raided = 0;                   // raids the clan has suffered (H14)
		std::string FullItem;             // the lowest-id item the main store holds to within 2 of its cap ("" = none)
		int FullCount = 0;                // ... and how many it holds
		int StoreCap = 0;                 // ... of this cap
		int CampPile = 0;                 // T255: units by the fire past its reserve that no store building has room for (more stores are wanted)
		int CampFood = 0;                 // ... of them food
		// T256: food lost lately (ClanStores::SpoiledLately) and the rule that answers it with the best food store the clan knows
		// (economy.logistics.foodStoreAt: 0 none; one food store for every foodStoresPer living at most)
		int SpoiledLately = 0;
		int FoodStoreAt = 0;
		int FoodStoresPer = 5;
		int Wells = 0;                    // wells the clan draws from on its home ground
		int MembersPerWell = 0;           // a well dug for every this many living (0: none dug)
		std::map<std::string, float> Wanting;   // V5: a provision the needs of the packs ask (BuildingDef::Provides) -> how short the clan is of it, 0..1
		std::function<bool(const BuildingDef&)> Known;                           // the clan knows how (H11)
		std::function<bool(const BuildingDef&)> Payable;                         // every material is in the store, in some node or made by a recipe the clan knows
		std::function<bool(const std::string& station)> StationThere;            // a finished building's or the camp's station
		std::function<bool(const std::string& station)> StationIsImprovised;     // the camp has this station only as a makeshift one (EconomyRules::Production.CampStations)
		std::function<std::string(const std::string& station)> RecipeNeeding;    // a recipe at this station that the clan wants made now ("" = none)
		// T367i (TALLERES 10): a module that only stands in a yard ("standalone" false) is planned only when a house of the clan has a free slot for it
		// (null: never, as before); a building a size of a house stands for (the longhouse: the hut's L) is never planned on its own (null: none)
		std::function<bool(const BuildingDef&)> ModuleRoom;
		std::function<bool(const BuildingDef&)> Absorbed;
		int BedsComing = 0;   // T367: the beds the houses growing now will add (a growth answers the need for beds as a site does)
	};

	// The needs now, most urgent first (ties by building id). Empty when the clan has no buildings, nobody alive or as many sites open as allowed.
	std::vector<BuildingNeed> PlanBuildingNeeds(const SettlementFacts& facts);

	// Where a member stands to work a site: in front of it (the "work" point of BuildSite), a little to the side for each builder.
	glm::vec2 SiteFront(const glm::vec2& pos, const BuildingDef& def, float side = 0.0f);
	// T269: the same point for a building turned `yawDegrees` (as Transform.Rotation.y; 0 is SiteFront exactly).
	glm::vec2 SiteFrontTurned(const glm::vec2& pos, const BuildingDef& def, float yawDegrees);

	// What a spot for a new site has to clear.
	struct SitePlacement
	{
		const BuildingDef* Def = nullptr;
		glm::vec2 Camp{ 0.0f };
		glm::vec2 Fire{ 0.0f };
		float SiteRadius = 7.0f;                  // the first ring round the camp
		std::vector<PlannerSite> Others;          // the sites and buildings already there
		std::vector<glm::vec2> Obstacles;         // resource nodes and the like (a site keeps 1.2 m off them)
		std::vector<GroundRect> Taken;            // T260: the footprints of the sites, buildings and fields round it (TakenGround): kFieldGap off them
		std::function<bool(const glm::vec2&)> CanStand;   // ground a member can stand on (null: all of it)
		// T245: metres of water over the ground here (0: dry; null: no water anywhere). A building never stands with water under any part of its
		// footprint unless its def allows it (BuildingDef::AllowWater): a shallow river is ground a member can wade, not ground to build on.
		std::function<float(const glm::vec2&)> WaterDepth;
		// N1: how good a spot is for this building (a household's house: its work near, its kin near). Null: the first spot that fits, ring by ring
		// (as always); set: every spot of `Rings` rings x 24 angles is weighed and the best that fits wins (ties: the first).
		std::function<float(const glm::vec2&)> Prefer;
		int Rings = 5;
		float Yaw = 0.0f;                         // T269: the building turned this many degrees (the player's; the planner's are never turned)
	};

	// Whether the site fits at this spot (off the camp and the fire, off the other sites and the obstacles, every corner and its front on ground
	// a member can stand on): "" when it fits, else why ("too near the camp"...). PlaceSite tries its spots with it; T237's sign checks the god's.
	std::string SiteFits(const SitePlacement& placement, const glm::vec2& at);

	// T245: whether any part of the footprint (a grid of points at most kWetStep apart over it, its corners and its front) has water deeper than
	// kWetDepth: "" when dry (or the def allows water, or there is no depth to ask), else why ("the river runs under it, 0.4 m deep").
	inline constexpr float kWetDepth = 0.05f;
	inline constexpr float kWetStep = 0.75f;
	std::string SiteInWater(const BuildingDef& def, const glm::vec2& at, const std::function<float(const glm::vec2&)>& waterDepth, float yawDegrees = 0.0f);   // (T269: turned)

	// Rings round the camp, each building at its own angle: the first spot that fits, or nothing. The same scene gives the same place.
	std::optional<glm::vec2> PlaceSite(const SitePlacement& placement);

	// T215: where a field of `plots` plots (`columns` a row, `spacing` m apart, the first at the origin: Field::PlotPos) is marked out. The wanted
	// origin when every plot is on free ground (`free`: a member can stand there, no rock, trunk or water); else the nearest origin that has it, on
	// rings of `spacing` m round the wanted one (up to `rings`, 12 angles each, in a fixed order). Null `free`: the wanted one. Nothing: no room.
	// T260: `room` (null: always), asked of each origin whose plots are free, says whether the whole field's footprint is off everything taken.
	std::optional<glm::vec2> PlaceField(const glm::vec2& wanted, int plots, int columns, float spacing, const std::function<bool(const glm::vec2&)>& free, int rings = 8,
		const std::function<bool(const glm::vec2&)>& room = {});
}
