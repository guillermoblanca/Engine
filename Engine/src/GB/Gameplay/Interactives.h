#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	// Interactive things of the world with a state of their own (H3): a campfire that burns its fuel, a well that gives water and fills again,
	// a door that opens and closes. Like the garden's plots they are entities with an ability system: the state is tags (State.Lit / State.Out,
	// State.Open / State.Closed) and attributes (Fuel, Water), changed by the effects abilities apply and by the InteractiveSystem over time,
	// so it is saved and loaded with the rest of the gameplay state and the characters use them with the same abilities as the player.
	//
	// Abilities: Stoke (puts a piece of wood or a branch in a campfire; needs one in the bag), DrawWater (fills the bag with water from a well),
	// OpenDoor and CloseDoor.

	void RegisterInteractiveContent(GameplayLibrary& library);

	// The obstacles (circles on the ground plane) the doors of the scene put in the way: the fence on either side of a door always, and the leaf
	// while the door is closed. Appended to `out`; the host adds them to the static obstacles of its world, so the characters and the navigation
	// grid see a shut door as a wall and an open one as a way through.
	void DoorObstacles(const EntityWorld& world, const GameplayWorld& gameplay, std::vector<CircleObstacle>& out);

	struct InteractiveLayout
	{
		std::string Root, Campfire, Well, Door;
	};

	// A campfire (unlit, with a little fuel), a well and a door in a fence, under "Interactives" (an older one is replaced).
	InteractiveLayout BuildInteractives(EntityWorld& world, const glm::vec3& origin);

	class InteractiveSystem
	{
	public:
		struct Config
		{
			float BurnSeconds = 6.0f;        // one unit of fuel lasts this long
			int FuelPerWood = 2;             // heat of a wood piece whose item says no fuel of its own (ItemDef::Fuel), and the heat of "wood" without an item library
			int WaterPerDraw = 2;
		};
		Config Settings;
		// The items (E1): with a library, Stoke puts in whatever the actor carries that is tagged Item.Resource.Wood (a log, a branch...) and the fire gets
		// that item's Fuel (wood 2, branch 1). Without one it puts in "wood" for Settings.FuelPerWood, as before.
		const ItemLibrary* Items = nullptr;

		// Takes `n` of an item out of the bag of an actor / gives it one. Return how many were moved.
		using Take = std::function<int(const std::string& actor, const std::string& item, int n)>;
		using Give = std::function<int(const std::string& actor, const std::string& item, int n)>;

		void Bind(GameplayWorld& gameplay, Take take, Give give);
		// Burns fuel, refills wells and keeps the state tags and the visuals in step. `entities` may be null (headless logic).
		void Update(float dt, EntityWorld* entities);

		// The timers of the system (the part of a second a fire has burnt, the time to the next drop of the well): what the tags and attributes do not keep.
		// FromJson goes after Bind; "" or a damaged text gives false and leaves the timers at zero.
		std::string ToJson() const;
		bool FromJson(const std::string& text);

		static std::string CampfireState(const AbilitySystemComponent& a);   // "unlit", "lit", "out" ("" = not a campfire)
		static std::string DoorState(const AbilitySystemComponent& a);       // "open", "closed" ("" = not a door)

	private:
		int FuelOf(const std::string& item) const;
		int TakeFuel(const std::string& actor) const;   // takes one fuel item out of the actor's bag; the heat it gives (0 = none to give)
		GameplayWorld* m_Gameplay = nullptr;
		Take m_Take;
		Give m_Give;
		std::vector<std::pair<std::string, float>> m_Burn;   // campfire name -> time into the current unit of fuel
	};
}
