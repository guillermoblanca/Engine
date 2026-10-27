#pragma once
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
	// Abilities: Stoke (puts a piece of wood in a campfire; needs wood in the bag), DrawWater (fills the bag with water from a well),
	// OpenDoor and CloseDoor.

	void RegisterInteractiveContent(GameplayLibrary& library);

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
			int FuelPerWood = 2;
			int WaterPerDraw = 2;
		};
		Config Settings;

		// Takes `n` of an item out of the bag of an actor / gives it one. Return how many were moved.
		using Take = std::function<int(const std::string& actor, const std::string& item, int n)>;
		using Give = std::function<int(const std::string& actor, const std::string& item, int n)>;

		void Bind(GameplayWorld& gameplay, Take take, Give give);
		// Burns fuel, refills wells and keeps the state tags and the visuals in step. `entities` may be null (headless logic).
		void Update(float dt, EntityWorld* entities);

		static std::string CampfireState(const AbilitySystemComponent& a);   // "unlit", "lit", "out" ("" = not a campfire)
		static std::string DoorState(const AbilitySystemComponent& a);       // "open", "closed" ("" = not a door)

	private:
		GameplayWorld* m_Gameplay = nullptr;
		Take m_Take;
		Give m_Give;
		std::vector<std::pair<std::string, float>> m_Burn;   // campfire name -> time into the current unit of fuel
	};
}
