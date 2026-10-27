#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// The carved river: a polyline of (x, water level, z) with the flow speed (m/s) at each point.
	struct RiverData
	{
		std::vector<glm::vec3> Points;
		std::vector<float> Speed;
		float HalfWidth = 2.5f;
		float Depth = 1.3f;
		float MouthLength = 0.0f;   // > 0: the river ends in a lake; over this length (m) the current fades to zero and the surface blends into the lake
	};
}
