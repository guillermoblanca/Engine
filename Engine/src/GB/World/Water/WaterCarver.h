#pragma once
#include "GB/World/Water/LakeOutline.h"
#include "GB/World/Water/RiverHydraulics.h"
#include "GB/World/Water/WaterField.h"
#include "GB/World/Terrain.h"
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Digging water into a heightmap. Terrain used to do this itself inside CreateHills; now the terrain only
	// holds the heights and the carver (SRP) shapes them: the lake basin is applied to each height as it is
	// generated, the river is carved afterwards along its path. The numbers that were literals inside that code
	// live in these structs, so a generator can choose them and a test can vary them.

	// The lake of a terrain recipe. Radius 0 = no lake.
	struct LakeShape
	{
		glm::vec2 Center{ 0.0f, 0.0f };
		float Radius = 0.0f;        // meters of the water edge
		float Depth = 2.5f;         // meters below Level at the centre
		float Level = 1.0f;         // height of the water surface
		// Steepest rise of the ground away from the shore (meters per meter, 0 = unlimited): hills next to the basin are cut down to this
		// cone, so the lake gets a gentle bank instead of a cliff with a jagged edge.
		float BankSlope = 0.3f;
		float BankLift = 0.45f;     // how far above Level the raised bank stands
		LakeOutline Outline;        // the shape of the shore (a circle by default)
	};

	// The river of a terrain recipe. An empty path = no river.
	struct RiverShape
	{
		std::vector<glm::vec2> Path;   // control points (xz), smoothed with a Catmull-Rom spline
		float Width = 5.0f;            // meters of water (at the mouth, when there is a Discharge)
		float Depth = 1.3f;            // meters below the water level (at the mouth, when there is a Discharge)
		// Relative discharge q (0..1) at each control point of Path: the drainage area that reaches it over the area that reaches the mouth. Empty = a
		// uniform river (one width, one depth, the old rule of thumb for the speed). With one the river widens and deepens downstream and its current
		// follows Manning's equation: see GB/World/Water/RiverHydraulics.h.
		std::vector<float> Discharge;
		HydraulicRules Hydraulics;
		float MouthLength = 16.0f;     // meters over which a river blends into the lake it flows into
	};

	// The height of one terrain sample once the lake is dug: `h` is what the hills alone give at `pos`.
	float ApplyLakeBasin(float h, const glm::vec2& pos, const LakeShape& lake);

	// Carves the valley of the river into `heights` and returns the river as data (its points, the current and the
	// width). The water level only ever descends along the path; near a lake it comes down to the lake's level.
	RiverData CarveRiver(Heightmap& heights, const RiverShape& river, const LakeShape& lake);
}
