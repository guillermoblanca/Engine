#pragma once
#include "GB/Game/Entity.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Resources.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	struct GeneratedWorld;

	// Resource nodes spread over a generated world (the patch of BuildResourcePatch is a few rows around a camp; this is the whole map): every node type
	// whose data has a spawn (ResourceNodeDef::Spawn) grows on its biomes, on ground that is not too steep, away from the water (or by it), from the
	// world's own trees and rocks and from the other nodes. Pure CPU: the planner sees the world through ResourceGround, so tests can feed it any ground.

	// What the planner needs to know of the ground. Every function takes world x and z.
	struct ResourceGround
	{
		struct Blocker { glm::vec2 At{ 0.0f }; float Radius = 1.0f; };   // a tree or a rock of the world: no node closer than its radius
		glm::vec2 Min{ 0.0f }, Max{ 0.0f };
		std::function<float(float, float)> Height;
		std::function<std::string(float, float)> Biome;          // "Water", "Shore", "Meadow", "Forest", "Rock", "Snow", "Dunes"
		std::function<float(float, float)> SlopeDegrees;
		std::function<float(float, float)> WaterDistance;        // metres to the lake or the river (0 in the water, large when there is none)
		std::vector<Blocker> Blockers;
	};

	struct ResourceSpot
	{
		std::string Node;           // node type id
		glm::vec3 Position{ 0.0f }; // on the ground
		float Yaw = 0.0f;           // degrees
	};

	// Where every node goes. The same ground, data and seed always give the same spots, in the same order. `density` scales every PerHectare
	// (and Max), for a richer or poorer world. Rare types are placed first, so the common ones cannot crowd them out.
	std::vector<ResourceSpot> PlanWorldResources(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, uint32_t seed, float density = 1.0f);

	// How many spots of each type, in id order (for the editor and the tests).
	std::map<std::string, int> CountSpots(const std::vector<ResourceSpot>& spots);

	// The ground of a generated world, with its trees and rocks as blockers. The world must outlive the result (it is read through it).
	ResourceGround ResourceGroundOf(const GeneratedWorld& world);

	// Puts the planned nodes in the scene under "World resources" (an older set is replaced), named "wild <type> <n>" so they never clash with the
	// patch's "tree 1". The nodes behave like any other: the ResourceSystem, the tribe and the abilities find them by their tags. The root is tagged
	// "world": the nodes come from the seed, so a saved game rebuilds them with the world (its baseline) and keeps only the ones that changed.
	ResourceLayout BuildWorldResources(EntityWorld& world, const std::vector<ResourceSpot>& spots, const std::map<std::string, ResourceNodeDef>& nodes);

	// Sets every node of a layout on the ground (the patch is laid out flat around its origin; on a hillside its nodes would float or sink).
	void SnapToGround(EntityWorld& world, const ResourceLayout& layout, const std::function<float(float, float)>& height);
}
