#pragma once
#include "GB/World/Terrain.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace GB
{
	struct ErosionParams
	{
		int Droplets = 60000;
		int Lifetime = 40;               // steps per droplet
		float Inertia = 0.05f;           // 0 = follows the slope, 1 = keeps going straight
		float Capacity = 4.0f;           // sediment a droplet can carry per unit of speed*water*slope
		float MinSlope = 0.01f;
		float ErodeSpeed = 0.3f;
		float DepositSpeed = 0.3f;
		float Evaporation = 0.012f;
		float Gravity = 4.0f;
		int BrushRadius = 2;             // texels eroded around the droplet (smooths the gullies)
		float Strength = 1.0f;           // multiplies ErodeSpeed (a step still never digs deeper than its own drop); 0 = no erosion
		uint32_t Seed = 1;
		// Droplets start inside this circle (xz); radius <= 0 = anywhere on the map.
		glm::vec2 Center{ 0.0f };
		float Radius = 0.0f;
	};

	struct ErosionStats
	{
		double Eroded = 0.0;             // meters^3-ish (height units summed over texels)
		double Deposited = 0.0;
		int MinI = 1 << 30, MinJ = 1 << 30, MaxI = -1, MaxJ = -1;   // texel box touched
	};

	// Particle-based hydraulic erosion: droplets roll downhill, pick up sediment on steep ground and drop it
	// where the slope eases, carving gullies and river beds and building fans. Deterministic.
	class HydraulicErosion
	{
	public:
		static ErosionStats Run(Heightmap& heightmap, const ErosionParams& params);
	};
}
