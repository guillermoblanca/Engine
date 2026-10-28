#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// Interactive things of the world with a state of their own (H3): a campfire that burns its fuel, a well that gives water and fills again,
	// a door that opens and closes. Like the garden's plots they are entities with an ability system: the state is tags (State.Lit / State.Out,
	// State.Open / State.Closed) and attributes (Fuel, Water), changed by the effects abilities apply and by the InteractiveSystem over time,
	// so it is saved and loaded with the rest of the gameplay state and the characters use them with the same abilities as the player.
	//
	// What each interactive is lives in data (InteractiveDef, the "interactives" section of a content file: assets/data/world.json): its name, its
	// starting tags and attributes (the capacities), its timers and the abilities that use it. The code keeps only the three behaviours ("kinds"):
	//   campfire  - Interactive.Campfire; Fuel burns one unit every burnSeconds; State.Unlit -> State.Lit -> State.Out; listens to Event.Fuel.Add
	//   well      - Interactive.Well; Water comes back refillAmount every refillSeconds up to its max; listens to Event.Water.Draw
	//   door      - Interactive.Door; State.Open / State.Closed, the hinge turns swingDegrees; listens to Event.Door.Open and Event.Door.Close
	// A new interactive of a known kind (a bigger hearth, a cistern, a second gate) is a few lines of JSON and no code.
	//
	// Abilities (built-in): Stoke (puts a piece of wood or a branch in a campfire; needs one in the bag), DrawWater (fills the bag with water from a
	// well), OpenDoor and CloseDoor.

	// An ability that uses an interactive. The target must hold the kind's tag (Interactive.Campfire...) and TargetRequires; the event it sends is
	// what the behaviour listens to.
	struct InteractiveAbility
	{
		std::string Name, Montage, Notify, Event, Note;
		float Duration = 1.5f, Cooldown = 0.3f;
		std::vector<std::string> Requires;          // tags the user must hold (Stoke: Item.Resource.Wood, from what it carries)
		std::vector<std::string> TargetRequires;    // on the target, besides the kind's tag (CloseDoor: State.Open)
		std::vector<std::string> TargetBlocked;     // not on the target (OpenDoor: State.Open)
		std::map<std::string, float> TargetMin;     // attribute of the target >= value (DrawWater: Water 1)
		bool operator==(const InteractiveAbility&) const = default;
	};

	struct InteractiveAttribute
	{
		std::string Name;
		float Value = 0.0f, Min = 0.0f, Max = 0.0f;
		bool operator==(const InteractiveAttribute&) const = default;
	};

	struct InteractiveDef
	{
		std::string Id;                 // "fire_pit"
		std::string Kind;               // the behaviour: "campfire", "well" or "door"
		std::string Name;               // the entity's name ("Fire Pit"); the system finds an instance's def by it, else by its kind
		glm::vec3 At{ 0.0f };           // where BuildInteractives puts it, from its origin
		glm::vec3 Stand{ 0.0f };        // the "work" interaction point, from the interactive
		std::vector<std::string> Tags;  // what it starts with (the kind's tag and its first state)
		std::vector<InteractiveAttribute> Attributes;   // Fuel 0 of 10, Water 6 of 6: the capacities
		float BurnSeconds = 6.0f;       // campfire: one unit of fuel lasts this long (an instance's own BurnSeconds attribute wins: the tribe's fire burns slower)
		int FuelPerWood = 2;            // campfire: heat of a wood piece whose item says no fuel of its own (ItemDef::Fuel), and of "wood" without an item library
		float RefillSeconds = 20.0f;    // well: one refill this often while it is not full ...
		int RefillAmount = 1;           // ... of this much
		int WaterPerDraw = 2;           // well: what one DrawWater gives
		float SwingDegrees = 100.0f;    // door: how far the hinge turns open ...
		float SwingSpeed = 240.0f;      // ... at this many degrees a second
		std::vector<InteractiveAbility> Abilities;
		bool operator==(const InteractiveDef&) const = default;
	};

	// "Interactive.Campfire" for "campfire", "Interactive.Well", "Interactive.Door"; "" for an unknown kind.
	std::string InteractiveKindTag(const std::string& kind);

	// The built-in interactives: "fire_pit", "well" and "gate" (assets/data/world.json says the same in its "interactives" section).
	std::map<std::string, InteractiveDef> DefaultInteractives();
	// Reads the "interactives" section of a content file: { "interactives": { "<id>": { "kind": ..., "name": ..., "at": [x, y, z], "stand": [x, y, z],
	// "tags": [...], "attributes": { "<name>": { "value": v, "min": a, "max": b } }, "burnSeconds": s, ..., "abilities": [ { "name": ..., "event": ... } ] } } }.
	// An interactive is added or replaced whole. A file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadInteractives(const std::string& json, std::map<std::string, InteractiveDef>& defs);
	std::string LoadInteractivesFile(const std::string& path, std::map<std::string, InteractiveDef>& defs);
	// The interactives as the section reads them back (one JSON object: id -> interactive).
	std::string InteractivesToJson(const std::map<std::string, InteractiveDef>& defs);

	// The abilities of the interactives (a later def's ability of the same name replaces an earlier one).
	void RegisterInteractiveContent(GameplayLibrary& library, const std::map<std::string, InteractiveDef>& defs = DefaultInteractives());

	// The obstacles (circles on the ground plane) the doors of the scene put in the way: the fence on either side of a door always, and the leaf
	// while the door is closed. Appended to `out`; the host adds them to the static obstacles of its world, so the characters and the navigation
	// grid see a shut door as a wall and an open one as a way through.
	void DoorObstacles(const EntityWorld& world, const GameplayWorld& gameplay, std::vector<CircleObstacle>& out);

	struct InteractiveLayout
	{
		std::string Root, Campfire, Well, Door;
	};

	// One interactive: the entity (named `name`, under `parent`, at `pos` in the parent's space) with its interaction point, its gameplay component
	// (the def's tags and attributes) and the parts its kind looks like (the flame and light of a campfire start off). Returns the entity.
	EntityId BuildInteractive(EntityWorld& world, const InteractiveDef& def, const std::string& name, EntityId parent, const glm::vec3& pos);
	// Every interactive of `defs` at its place from `origin`, under "Interactives" (an older one is replaced): campfires first, then wells, doors
	// and the rest, so the built-in ones come out as they always did (a campfire, a well and a door in a fence).
	InteractiveLayout BuildInteractives(EntityWorld& world, const glm::vec3& origin, const std::map<std::string, InteractiveDef>& defs = DefaultInteractives());

	class InteractiveSystem
	{
	public:
		// What each interactive is (see InteractiveDef). The editor reads assets/data/world.json into it at start and with `content load`.
		std::map<std::string, InteractiveDef> Defs = DefaultInteractives();
		// The items (E1): with a library, Stoke puts in whatever the actor carries that is tagged Item.Resource.Wood (a log, a branch...) and the fire gets
		// that item's Fuel (wood 2, branch 1). Without one it puts in "wood" for the def's FuelPerWood, as before.
		const ItemLibrary* Items = nullptr;

		// Takes `n` of an item out of the bag of an actor / gives it one. Return how many were moved.
		using Take = std::function<int(const std::string& actor, const std::string& item, int n)>;
		using Give = std::function<int(const std::string& actor, const std::string& item, int n)>;

		void Bind(GameplayWorld& gameplay, Take take, Give give);
		// Burns fuel, refills wells and keeps the state tags and the visuals in step. `entities` may be null (headless logic). An interactive whose
		// ability system appeared after Bind (the tribe lights its own fire pit) is picked up here.
		void Update(float dt, EntityWorld* entities);

		// The def an interactive follows: the one of its name, else the first (by id) of its kind; null when it is no interactive.
		const InteractiveDef* DefOf(const std::string& name, const AbilitySystemComponent& a) const;

		// The timers of the system (the part of a second a fire has burnt, the time to the next drop of the well): what the tags and attributes do not keep.
		// FromJson goes after Bind; "" or a damaged text gives false and leaves the timers at zero.
		std::string ToJson() const;
		bool FromJson(const std::string& text);

		static std::string CampfireState(const AbilitySystemComponent& a);   // "unlit", "lit", "out" ("" = not a campfire)
		static std::string DoorState(const AbilitySystemComponent& a);       // "open", "closed" ("" = not a door)

	private:
		void Listen(const std::string& name, AbilitySystemComponent* a);
		int FuelOf(const std::string& item, int fallback) const;
		int TakeFuel(const std::string& actor, int fallback) const;   // takes one fuel item out of the actor's bag; the heat it gives (0 = none to give)
		GameplayWorld* m_Gameplay = nullptr;
		Take m_Take;
		Give m_Give;
		std::vector<std::pair<std::string, float>> m_Burn;   // interactive name -> time into the current unit of fuel / to the next refill
		std::map<std::string, const AbilitySystemComponent*> m_Bound;   // the systems that have the listeners (by name: a reloaded scene makes new ones)
	};
}
