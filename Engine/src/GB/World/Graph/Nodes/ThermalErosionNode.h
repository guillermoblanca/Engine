#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "erosion_thermal": one input (a height field), settled by GB::ThermalErosion::Run (GB/World/
	// ThermalErosion.h) -- reused as-is, same spirit as "erosion_hydraulic" reusing GB::HydraulicErosion.
	// Params mirror GB::ThermalErosionParams: Iterations (40), TalusAngleDegrees (33.0), Strength (0.5).
	// Deterministic (no seed: the algorithm has no randomness).
	class ThermalErosionNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
