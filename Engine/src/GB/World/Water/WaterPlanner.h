#pragma once
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Heightmap.h"
#include "GB/World/TerrainParams.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	// Where the water of a world goes, found by a terrain graph (docs/GRAFO_TERRENO.md, section 11) instead of by code written for the purpose:
	//
	//     height_input --> lake_site -------------------------------------+
	//          |               |                                         v
	//          +--> lake_basin --> depression_fill (outlets: the lake) --> flow_accumulation --> river_trace (to the lake)
	//
	// The ground is analysed as it will be: the lake goes in the broadest low basin and is dug there, every pit is filled so that the rain runs somewhere, the flow
	// of the whole map is accumulated, and the river is the longest flow line that ends at the lake, with the drainage area that reaches each of its
	// points (what makes it wider and faster downstream). The graph is plain data: a tool can show it, edit it or replace a node, and the plan below
	// is just its result.
	struct WaterPlanRequest
	{
		bool Lake = true;
		bool River = true;
		WaterGenParams Gen;                   // the size of the lake that will be dug (the basin is dug in the ground the rain is analysed on)
		uint32_t Seed = 1;                    // the seed of the world: the phases of the lake's shore
		float RiverStartMinDistance = 0.3f;   // the river's source is at least this fraction of the map from the lake
		float PathSpacing = 6.0f;             // metres between the points of the river's course
	};

	struct WaterPlan
	{
		bool HasLake = false;
		glm::vec2 LakeCenter{ 0.0f };
		float LakeFloor = 0.0f;               // the smoothed height of the ground at the site: the basin the lake fills
		bool HasRiver = false;
		std::vector<glm::vec2> RiverPath;     // source to outlet, xz
		std::vector<float> RiverDrainage;     // flow accumulation (cells draining through each point), the same length as RiverPath
	};

	// The graph above for what the request asks for (no lake: the river drains wherever the map does).
	TerrainGraph MakeWaterGraph(const WaterPlanRequest& request);

	// Writes a plan into a terrain recipe: the lake where the plan put it, the size `gen` gives it, the river along the plan's course with the
	// relative discharge of each point (its drainage over the mouth's) and its end brought to the shore of the lake.
	void ApplyWaterPlan(const WaterPlan& plan, const WaterGenParams& gen, HillParams& recipe);

	// The relative discharge of each point of a traced river course (its drainage over the mouth's, the same length as `path`) and, with a lake,
	// its end brought to the lake's shore: the points within 1.7 radii are dropped and a last one is put at 0.95 of the shore's radius in that
	// direction. What ApplyWaterPlan and the river_carve node both do with a river_trace course.
	void ShapeRiverCourse(std::vector<glm::vec2>& path, const std::vector<float>& drainage, const LakeBody* lake, std::vector<float>& discharge);

	// Evaluates MakeWaterGraph on `ground` and reads the plan from its results. False (and `error`) when a node fails.
	bool PlanWater(const TerrainNodeRegistry& registry, const Heightmap& ground, const WaterPlanRequest& request, WaterPlan& plan, std::string& error);
}
