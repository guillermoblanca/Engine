#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <string>
#include <vector>

namespace GB
{
	// The vegetable garden scenario: plots of soil, a sapling spot, a fruit tree, a cow, boxes, a wheelbarrow and the abilities to work them.
	//
	// Plots and trees are entities with an ability system like the characters: their state is tags (State.Soil.Grass / Dug / Planted.Sown /
	// Planted.Sprout / Planted.Grown, State.Tree.Empty / Sapling / Grown, State.Wet) and numbers (Moisture, Growth, Fruit), changed by the
	// effects abilities apply to them and by the GardenSystem, which grows what is watered.

	// Effects and abilities of the garden (Dig, Sow, Water, PlantTree, PickFruit, Pull, Milk, CarryBox, DropBox, PushWheelbarrow,
	// ReleaseWheelbarrow, DumpWheelbarrow).
	void RegisterGardenContent(GameplayLibrary& library);

	struct GardenLayout
	{
		std::string Root, Player, Npc, TreeSpot, FruitTree, Cow, Compost;
		std::vector<std::string> Plots, Boxes;
		glm::vec3 Center{ 0.0f };
	};

	// Puts the garden in the scene (under an entity called "Garden"; an older one is replaced), with `origin` as the middle of the plots.
	// Plots, trees, cow and the two characters ("Gardener" for the player, "Farmhand" for an NPC) carry GameplayComponents; everything is
	// made of the engine's primitives.
	GardenLayout BuildGarden(EntityWorld& world, const glm::vec3& origin);

	struct GardenConfig
	{
		float StageSeconds = 4.0f;      // a watered plant needs this long to go from seed to sprout and from sprout to grown
		float MoistureSeconds = 10.0f;  // one watering keeps the soil wet this long
		float TreeSeconds = 6.0f;       // a watered sapling becomes a tree
	};

	// The rules of the garden over time, and what the plots look like. Bind it to a GameplayWorld whose entities came from BuildGarden (or any
	// scene with the same tags): it listens to the events of the plots and trees and, in Update, grows what is wet.
	class GardenSystem
	{
	public:
		GardenConfig Config;
		void Bind(GameplayWorld& gameplay, EntityWorld* entities = nullptr);
		void Update(float dt);
		// Shows each plot and tree as its state says (sprout, plant, fruit) by switching its child entities. Cheap, and only writes changes.
		void SyncVisuals();

		static std::string SoilState(const AbilitySystemComponent& plot);   // "grass", "dug", "sown", "sprout", "grown" ("" = not a plot)
		static std::string TreeState(const AbilitySystemComponent& tree);   // "empty", "sapling", "grown" ("" = not a tree)

	private:
		GameplayWorld* m_Gameplay = nullptr;
		EntityWorld* m_Entities = nullptr;
	};
}
