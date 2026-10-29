#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// Buildings by data and by phases (H8, E9): a shelter, a store, a workbench, a drying rack. A building is a construction site first: an entity with an
	// ability system, like a resource node or an interactive, whose state is tags and attributes (so it is saved and loaded with the rest of the gameplay):
	//   tags        "Building", "Building.Kind.<id>", "Building.Site" while it is being built and "Building.Done" once it stands, and "Building.Ready"
	//               while the materials of the current phase are all there (only then can it be worked)
	//   attributes  "Phase" (0 .. phases; = phases when done), "Work" (seconds of work put into the current phase) and "Delivered.<item>" (units of
	//               the current phase's materials already brought to the site)
	// Building goes phase by phase (foundation, frame, roof...): the materials of the phase are carried to the site (DeliverToSite), then someone works
	// it with the ability Build (the event "Event.Building.Work" on the site; the BuildingSystem adds the work), and when the phase has had its work
	// the next one starts. Each phase is a visible part that grows while it is built (made of the engine's primitives, like the nodes).
	//
	// What a building provides once done is data too: beds (shelter: rest and warmth for who sleeps inside), storage (more of each item in the clan's
	// store) and a station (recipes with that station can be made there). An optional tech names what has to be known first (H11): this code only
	// carries it.

	struct BuildingPhase
	{
		std::string Name;                       // "foundation", "frame", "roof"
		float Work = 10.0f;                     // seconds of Build work it takes once its materials are there
		std::map<std::string, int> Needs;       // item -> units carried to the site before it can be worked
		std::string Part = "posts";             // how it looks: "slab", "posts", "walls", "roof", "table", "rack"
		bool operator==(const BuildingPhase&) const = default;
	};

	struct BuildingDef
	{
		std::string Id;                         // "shelter"
		std::string Name;                       // "Shelter": the sites are called "<Name> <n>"
		glm::vec2 Footprint{ 3.0f, 3.0f };      // metres on the ground (x, z)
		std::vector<BuildingPhase> Phases;
		// What it provides once done.
		int Shelter = 0;                        // beds: members that sleep inside
		float Rest = 0.0f;                      // extra rest per second for who sleeps inside (on top of NeedsConfig::SleepGain)
		float Warmth = 0.0f;                    // warmth per second for who sleeps inside (a roof against the night's cold)
		std::string Store;                      // a kind of store of the economy rules (H18, EconomyRules::Logistics: "stockpile"): once done it is a secondary store
		std::string Station;                    // recipes with this station can be made at it ("workbench", "drying_rack", "kiln")
		std::string Tech;                       // optional: what must be known first (H11); "" = nothing
		glm::vec3 Color{ 0.45f, 0.32f, 0.18f }; // the colour of its parts
		bool operator==(const BuildingDef&) const = default;

		// All the materials of every phase, item -> units.
		std::map<std::string, int> TotalNeeds() const;
		float TotalWork() const;
		float Radius() const { return 0.5f * glm::length(Footprint); }
	};

	// The built-in era-1 buildings: "shelter", "storage", "workbench" and "drying_rack" (assets/data/era1.json says the same in its "buildings" section).
	std::map<std::string, BuildingDef> DefaultBuildings();
	// Reads the "buildings" section of a content file:
	//   { "buildings": { "<id>": { "name": ..., "footprint": [x, z], "phases": [ { "name": ..., "work": s, "needs": { "<item>": n }, "part": ... } ],
	//                              "shelter": n, "rest": r, "warmth": w, "store": ..., "station": ..., "tech": ..., "color": [r, g, b] } } }
	// A building is added or replaced whole. A file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadBuildings(const std::string& json, std::map<std::string, BuildingDef>& defs);
	std::string LoadBuildingsFile(const std::string& path, std::map<std::string, BuildingDef>& defs);
	// The buildings as the section reads them back (one JSON object: id -> building).
	std::string BuildingsToJson(const std::map<std::string, BuildingDef>& defs);

	// The ability Build: works a site whose phase has its materials ("Building.Site" and "Building.Ready"), sending "Event.Building.Work".
	void RegisterBuildingContent(GameplayLibrary& library);

	// What a site is now, read from its tags and attributes.
	struct SiteState
	{
		std::string Building;                   // the def id ("" = not a building)
		int Phase = 0, Phases = 0;
		std::string PhaseName;
		float Work = 0.0f, WorkNeeded = 0.0f;   // of the current phase
		std::map<std::string, int> Missing;     // materials of the current phase still to be brought
		bool Ready = false;                     // the current phase has all its materials
		bool Done = false;
		float Progress = 0.0f;                  // 0..1 over the whole building (by work)
	};

	// The building an ability system is a site or a building of (by its "Building.Kind.<id>" tag); null when none.
	const BuildingDef* BuildingOf(const AbilitySystemComponent& a, const std::map<std::string, BuildingDef>& defs);
	SiteState ReadSite(const AbilitySystemComponent& a, const BuildingDef& def);
	// Hands `count` units of an item to a site; it takes what its current phase still lacks. Returns how many it took.
	int DeliverToSite(AbilitySystemComponent& a, const BuildingDef& def, const std::string& item, int count);
	// Puts `seconds` of work into the current phase (only while it is Ready). Returns true when that finished the phase (or the building).
	bool WorkOnSite(AbilitySystemComponent& a, const BuildingDef& def, float seconds);

	// A construction site in the scene: the root entity `name` at `pos` (on the ground, x and z the centre of the footprint) turned `yaw` degrees, with
	// its interaction point "work" in front, its gameplay component (phase 0, nothing delivered) and one part per phase, hidden until its phase starts.
	// The caller makes its ability system (GameplayWorld::Create + FromComponent, or a scene load). Returns the entity.
	EntityId BuildSite(EntityWorld& world, const BuildingDef& def, const std::string& name, const glm::vec3& pos, float yaw = 0.0f);
	// Shows a site as its state says: the parts of the phases done at full size, the current one growing with its progress, the later ones hidden.
	// Only writes what changed.
	void SyncSiteVisuals(EntityWorld& world, const std::string& name, const BuildingDef& def, const SiteState& state);

	// The buildings of a scene at run time: listens to Build on every site (also those a load or the tribe made after Bind), moves the work forward and
	// keeps the parts in step with the progress.
	class BuildingSystem
	{
	public:
		std::map<std::string, BuildingDef> Defs = DefaultBuildings();
		float WorkPerUse = 2.0f;   // seconds of work one Build gives (the length of the ability)

		void Bind(GameplayWorld& gameplay);
		// Picks up new sites and updates the visuals. `entities` may be null (headless logic).
		void Update(float dt, EntityWorld* entities);
		// The sites and buildings of the scene, by name (sorted).
		std::vector<std::string> Sites() const;
		// A summary for people: each site with its phase and what it lacks, and each finished building.
		std::string Report() const;

	private:
		void Listen(const std::string& name, AbilitySystemComponent* a);
		GameplayWorld* m_Gameplay = nullptr;
		std::map<std::string, std::pair<AbilitySystemComponent*, int>> m_Bound;   // the system each site's listener is on, and the listener
		std::map<std::string, float> m_Shown;   // the progress last shown of each site (visuals are only written when it changes)
	};
}
