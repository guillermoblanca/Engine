#pragma once
#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// The carved river: a polyline of (x, water level, z) with the flow speed (m/s) at each point.
	struct RiverData
	{
		std::vector<glm::vec3> Points;
		std::vector<float> Speed;
		float HalfWidth = 2.5f;      // meters; the widest half-width of the river (all of it when HalfWidths is empty)
		float Depth = 1.3f;          // meters below the water level (the deepest point when the river deepens downstream)
		float MouthLength = 0.0f;    // > 0: the river ends in a lake; over this length (m) the current fades to zero and the surface blends into the lake
		// Half-width at each point when the river widens downstream (the hydraulic geometry of GB/World/Water/RiverHydraulics.h); empty = HalfWidth everywhere.
		std::vector<float> HalfWidths;

		// Half-width at fraction `t` (0..1) along segment k (from Points[k] to Points[k + 1]).
		float HalfWidthAt(size_t k, float t) const
		{
			if (HalfWidths.size() != Points.size() || k + 1 >= HalfWidths.size()) return HalfWidth;
			return HalfWidths[k] + (HalfWidths[k + 1] - HalfWidths[k]) * t;
		}
	};
}
