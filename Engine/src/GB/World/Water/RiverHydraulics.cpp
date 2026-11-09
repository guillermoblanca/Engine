#include "GB/World/Water/RiverHydraulics.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		float Clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }
	}

	float HydraulicWidthFactor(float q, const HydraulicRules& rules)
	{
		return std::max(std::pow(Clamp01(q), rules.WidthExponent), rules.MinWidthFraction);
	}

	float HydraulicDepthFactor(float q, const HydraulicRules& rules)
	{
		return std::max(std::pow(Clamp01(q), rules.DepthExponent), rules.MinDepthFraction);
	}

	float HydraulicRadius(float width, float depth)
	{
		const float perimeter = width + 2.0f * depth;
		return perimeter > 1e-6f ? width * depth / perimeter : 0.0f;
	}

	float ManningSpeed(float width, float depth, float slope, const HydraulicRules& rules)
	{
		const float radius = HydraulicRadius(width, depth);
		const float s = std::max(slope, rules.MinSlope);
		const float v = std::pow(radius, 2.0f / 3.0f) * std::sqrt(s) / rules.Roughness;
		return std::min(std::max(v, rules.MinSpeed), rules.MaxSpeed);
	}
}
