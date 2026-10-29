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
	// On top of that come the "patches" (forests, stony ground, flower beds) of a NatureConfig: groups of nodes with clearings and ragged edges (docs/BOSQUES.md).

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
		int TreeVariants = 0, RockVariants = 0;                  // how many procedural trees and rocks the world has (0 = none: the nodes are drawn with primitives)
	};

	struct ResourceSpot
	{
		std::string Node;           // node type id
		glm::vec3 Position{ 0.0f }; // on the ground
		float Yaw = 0.0f;           // degrees
		float Scale = 1.0f;         // size of its model (ResourceSpawnDef::ScaleMin..ScaleMax)
		int Variant = -1;           // which procedural tree or rock of the world draws it (-1 = primitives)
		int Stack = 0;              // rocks piled on top of this one
		float Radius = 0.0f;        // the room it asked for: no other node is closer than the larger of the two radii
	};

	// Where every node goes. The same ground, data and seed always give the same spots, in the same order. `density` scales every PerHectare
	// (and Max), for a richer or poorer world. Rare types are placed first, so the common ones cannot crowd them out.
	std::vector<ResourceSpot> PlanWorldResources(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, uint32_t seed, float density = 1.0f);

	// How many spots of each type, in id order (for the editor and the tests).
	std::map<std::string, int> CountSpots(const std::vector<ResourceSpot>& spots);

	// ---- patches: forests, stony ground and flower beds (assets/data/bosques.json)
	struct NaturePatchMix { std::string Node; float Weight = 1.0f; };

	// A kind of patch. The map is cut in cells of `Cell` metres; a cell whose centre is on one of `Biomes` gets a patch with that chance (times the abundance).
	// A patch has a radius between RadiusMin and RadiusMax and a ragged edge; inside, nodes go on a jittered grid of `Spacing` metres, thinned toward the edge
	// (`Fill` at the centre), with clearings where a noise field is below `Clearings`. Each node is one of `Mix` (by weight), sized by its own spawn data.
	struct NaturePatchDef
	{
		std::string Id;
		std::map<std::string, float> Biomes;    // biome -> chance of a patch in a cell centred there (0..1)
		float Cell = 64.0f;
		float RadiusMin = 12.0f, RadiusMax = 30.0f;
		float Spacing = 3.6f;
		float Fill = 0.9f;
		float Clearings = 0.25f;                // 0..0.6: nodes are left out where the noise is below this (glades)
		float MaxSlope = 30.0f;
		int Max = 0;                            // most nodes of this kind over the map (0 = no limit)
		std::vector<NaturePatchMix> Mix;
	};

	struct NatureConfig
	{
		float Abundance = 1.0f;                 // multiplies the density of every wild node and the chance and size of every patch: 1 = as the node data says
		int MaxNodes = 3000;                    // most nodes the patches add over the whole map (the entity budget)
		std::vector<NaturePatchDef> Patches;
		bool operator==(const NatureConfig& o) const;
	};

	// "" or what is wrong; the config is left as it was on a mistake. Unknown biomes, empty mixes and values out of range are refused.
	std::string ParseNatureConfig(const std::string& json, NatureConfig& out);
	std::string LoadNatureConfigFile(const std::string& path, NatureConfig& out);
	std::string NatureConfigToJson(const NatureConfig& cfg);

	// PlanWorldResources(density = Abundance) and then the patches, in the room the wild nodes left. Same ground, data, config and seed = same spots.
	std::vector<ResourceSpot> PlanWorldNature(const ResourceGround& ground, const std::map<std::string, ResourceNodeDef>& nodes, const NatureConfig& config, uint32_t seed);

	// The ground of a generated world, with its trees and rocks as blockers. The world must outlive the result (it is read through it).
	ResourceGround ResourceGroundOf(const GeneratedWorld& world);

	// Puts the planned nodes in the scene under "World resources" (an older set is replaced), named "wild <type> <n>" so they never clash with the
	// patch's "tree 1". The nodes behave like any other: the ResourceSystem, the tribe and the abilities find them by their tags. The root is tagged
	// "world": the nodes come from the seed, so a saved game rebuilds them with the world (its baseline) and keeps only the ones that changed.
	ResourceLayout BuildWorldResources(EntityWorld& world, const std::vector<ResourceSpot>& spots, const std::map<std::string, ResourceNodeDef>& nodes);

	// Sets every node of a layout on the ground (the patch is laid out flat around its origin; on a hillside its nodes would float or sink).
	void SnapToGround(EntityWorld& world, const ResourceLayout& layout, const std::function<float(float, float)>& height);
}
