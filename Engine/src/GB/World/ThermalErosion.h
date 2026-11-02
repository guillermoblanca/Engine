#pragma once
#include "GB/World/Terrain.h"
#include <cstdint>

namespace GB
{
	struct ThermalErosionParams
	{
		int Iterations = 40;            // passes over the whole map
		float TalusAngleDegrees = 33.0f; // the angle of repose: slopes steeper than this slump (loose rock settles around 30-35 degrees)
		float Strength = 0.5f;           // fraction of a cell's excess (above the talus angle) moved to its lower neighbours per pass; 0 = no-op
	};

	struct ThermalErosionStats
	{
		double Moved = 0.0;              // total height (texels * metres) redistributed, summed over every pass
		int MinI = 1 << 30, MinJ = 1 << 30, MaxI = -1, MaxJ = -1;   // texel box any cell actually moved material in or out of
	};

	// Grid-based talus/angle-of-repose erosion: material steeper than TalusAngleDegrees slides to its lower
	// neighbours (8-connected), a little at a time over several passes, the way scree and loose soil settle on
	// a slope -- the companion GB::HydraulicErosion (GB/World/Erosion.h) carves with running water, this one
	// settles what water alone leaves too steep to stand. Deterministic (no randomness).
	class ThermalErosion
	{
	public:
		static ThermalErosionStats Run(Heightmap& heightmap, const ThermalErosionParams& params);
	};
}
