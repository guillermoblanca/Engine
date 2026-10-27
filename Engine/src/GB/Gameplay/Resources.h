#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// Resource nodes in the world (H2): trees, rocks, berry bushes and grass tufts as entities with an ability system, like the garden's plots.
	// A node holds the attribute "Amount" and the tags "Resource.Node", "Resource.Node.<id>", "Resource.<Wood|Stone|Plant>" and, while empty,
	// "State.Depleted". The abilities Chop, Mine and Gather (the first two need a tool the actor holds) raise "Event.Resource.Gather" on the
	// node; the ResourceSystem takes the units out of the node and hands them to whoever carries them away (the actor's bag).

	// Effects and abilities (Chop, Mine, Gather) and one regrowth effect per node kind of `nodes` that regrows.
	void RegisterResourceContent(GameplayLibrary& library, const std::map<std::string, ResourceNodeDef>& nodes);

	struct ResourceLayout
	{
		std::string Root;
		std::vector<std::string> Nodes;
	};

	// Puts a small patch of nodes in the scene under "Resources" (an older one is replaced): rows of trees, rocks, berry bushes and fibre
	// tufts around `origin`. Everything is made of the engine's primitives.
	ResourceLayout BuildResources(EntityWorld& world, const glm::vec3& origin, const std::map<std::string, ResourceNodeDef>& nodes,
		int trees = 3, int rocks = 2, int bushes = 2, int grass = 2);

	class ResourceSystem
	{
	public:
		// Called with the gatherer's name, the item and how many units came out of the node.
		using Yield = std::function<void(const std::string& actor, const std::string& item, int count)>;

		void Bind(GameplayWorld& gameplay, const std::map<std::string, ResourceNodeDef>& nodes, Yield onYield);
		// Regrows the nodes: each holds a timer (the attribute "Regrow", saved with it) that runs at `rate` times real time (0 in deep winter).
		void Update(GameplayWorld& gameplay, float dt, float rate = 1.0f) const;
		// Marks nodes as depleted or not and shows each as its state says (what is gathered from it disappears). Only writes changes.
		void SyncVisuals(GameplayWorld& gameplay, EntityWorld& entities) const;
		// Gives an ability system the item tags ("Item.Tool.Axe", "Item.Resource.Wood") of what an inventory holds, and takes away those that left it.
		static void SyncTools(AbilitySystemComponent& who, const Inventory& bag, const ItemLibrary& items);
		static int Amount(const AbilitySystemComponent& node);
	private:
		std::map<std::string, ResourceNodeDef> m_Defs;
		Yield m_Yield;
	};
}
