#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "erosion_hydraulic": one input (a height field) eroded in place by GB::HydraulicErosion::Run (GB/World/
	// Erosion.h) -- reused as-is, not reimplemented, so the droplet-based erosion already shipped and tested
	// (tests/world_test.cpp) is what the graph gets. Params mirror GB::ErosionParams: Droplets (60000),
	// Lifetime (40), Inertia (0.05), Capacity (4.0), MinSlope (0.01), ErodeSpeed (0.3), DepositSpeed (0.3),
	// Evaporation (0.012), Gravity (4.0), BrushRadius (2), Strength (1.0). Seeded from TgContext::Seed (mixed
	// with Params["Seed"] when set), so the result is deterministic for a graph.
	class HydraulicErosionNode final : public ITerrainNode
	{
	public:
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
