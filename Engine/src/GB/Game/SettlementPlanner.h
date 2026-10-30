#pragma once
// What the clan builds and where (H8): the decisions of the settlement apart from the Tribe. The planner reads a SettlementFacts (a snapshot of what the
// clan has, wants and knows, gathered by Tribe::BuildingNeeds) and answers with a list of needs, most urgent first; PlaceSite finds a spot for a site
// from a SitePlacement. Neither knows the Tribe, so both run (and are tested) alone; Tribe keeps BuildingNeeds / PlanSettlement as the facade.
//
// Made to grow into "several works at once with a priority": the answer is already a ranked list, and the caller decides how many it starts
// (SettlementFacts::MaxOpenSites). See docs/PLAN_JUGABILIDAD.md, section 6.
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
		std::function<bool(const BuildingDef&)> Known;                           // the clan knows how (H11)
		std::function<bool(const BuildingDef&)> Payable;                         // every material is in the store, in some node or made by a recipe the clan knows
		std::function<bool(const std::string& station)> StationThere;            // a finished building's or the camp's station
		std::function<bool(const std::string& station)> StationIsImprovised;     // the camp has this station only as a makeshift one (EconomyRules::Production.CampStations)
		std::function<std::string(const std::string& station)> RecipeNeeding;    // a recipe at this station that the clan wants made now ("" = none)
	};

	// The needs now, most urgent first (ties by building id). Empty when the clan has no buildings, nobody alive or as many sites open as allowed.
	std::vector<BuildingNeed> PlanBuildingNeeds(const SettlementFacts& facts);

	// Where a member stands to work a site: in front of it (the "work" point of BuildSite), a little to the side for each builder.
	glm::vec2 SiteFront(const glm::vec2& pos, const BuildingDef& def, float side = 0.0f);

	// What a spot for a new site has to clear.
	struct SitePlacement
	{
		const BuildingDef* Def = nullptr;
		glm::vec2 Camp{ 0.0f };
		glm::vec2 Fire{ 0.0f };
		float SiteRadius = 7.0f;                  // the first ring round the camp
		std::vector<PlannerSite> Others;          // the sites and buildings already there
		std::vector<glm::vec2> Obstacles;         // resource nodes and the like (a site keeps 1.2 m off them)
		std::function<bool(const glm::vec2&)> CanStand;   // ground a member can stand on (null: all of it)
	};

	// Rings round the camp, each building at its own angle: the first spot that fits, or nothing. The same scene gives the same place.
	std::optional<glm::vec2> PlaceSite(const SitePlacement& placement);
}
