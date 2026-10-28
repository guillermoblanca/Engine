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
	// A node holds the attribute "Amount" and the tags "Resource.Node", "Resource.Node.<id>", "Resource.<Kind>" (the kind comes from the node's data: Wood,
	// Stone, Stone.Flint, Plant, Loose.Clay...) and, while empty, "State.Depleted". The abilities Chop (Resource.Wood), Mine (Resource.Stone and below),
	// Gather (Resource.Plant), PullFiber (fibre tufts) and Collect (Resource.Loose: clay, reeds) raise "Event.Resource.Gather" on the node, the first two
	// needing a tool the actor holds; the ResourceSystem takes the units out of the node and hands them to whoever carries them away (the actor's bag).

	// The tag that says what kind of node it is (E1): "Resource." + its kind ("Resource.Stone.Flint"); a definition without a kind (an older content
	// file) is worked out from its item: wood -> Resource.Wood, stone -> Resource.Stone, anything else -> Resource.Plant.
	std::string ResourceKindTag(const ResourceNodeDef& def);

	// Effects and abilities (Chop, Mine, Gather, PullFiber, Collect) and one regrowth effect per node kind of `nodes` that regrows.
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

	// The patch by data (E1): every node type whose definition has a patch (ResourceNodeDef::Patch) is put in, Count + PerWorker * workers of each, laid out as
	// its shape says (row, ring or cluster; types with "near" stand beside another type). The result is the same for the same data and workers.
	ResourceLayout BuildResourcePatch(EntityWorld& world, const glm::vec3& origin, const std::map<std::string, ResourceNodeDef>& nodes, int workers);
	// What BuildResourcePatch puts in for `workers`: node id and count, in id order.
	std::vector<std::pair<std::string, int>> PatchCounts(const std::map<std::string, ResourceNodeDef>& nodes, int workers);

	// How many nodes of each kind a patch needs for `workers` members of a tribe (the default patch of 3, 2, 2 and 2 feeds one or two). Rocks never
	// grow back, so they get a few more. The four legacy types only (their patch fields say the same: BuildResourcePatch).
	struct ResourceCounts { int Trees = 3, Rocks = 2, Bushes = 2, Grass = 2; };
	ResourceCounts ScaledResources(int workers);

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
		// SyncTools also keeps, per tool family, the attributes "Power:Item.Tool.Axe" and "Speed:Item.Tool.Axe" of the best tool carried (E2): a
		// node gives its PerGather plus the power of the tool its RequiresTag asks for, and the ability swings that much faster.
		static std::string PowerAttribute(const std::string& toolTag);
		static std::string SpeedAttribute(const std::string& toolTag);
		static int Amount(const AbilitySystemComponent& node);
	private:
		std::map<std::string, ResourceNodeDef> m_Defs;
		Yield m_Yield;
	};
}
