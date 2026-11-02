#include "GB/World/Graph/Nodes/ErosionNode.h"
#include "GB/World/Erosion.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"

namespace GB
{
	namespace
	{
		double Param(const TgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}
	}

	bool HydraulicErosionNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "erosion_hydraulic node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "erosion_hydraulic node '" + node.Id + "' got an empty field"; return false; }

		// GB::HydraulicErosion::Run works on a GB::Heightmap (GB/World/Erosion.h:39): build one from the input
		// field, erode it, then copy the result back out. The node adds no erosion logic of its own.
		Heightmap heightmap(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) heightmap.At(i, j) = in.At(i, j);

		ErosionParams params;
		params.Droplets = static_cast<int>(Param(node, "Droplets", static_cast<double>(params.Droplets)));
		params.Lifetime = static_cast<int>(Param(node, "Lifetime", static_cast<double>(params.Lifetime)));
		params.Inertia = static_cast<float>(Param(node, "Inertia", params.Inertia));
		params.Capacity = static_cast<float>(Param(node, "Capacity", params.Capacity));
		params.MinSlope = static_cast<float>(Param(node, "MinSlope", params.MinSlope));
		params.ErodeSpeed = static_cast<float>(Param(node, "ErodeSpeed", params.ErodeSpeed));
		params.DepositSpeed = static_cast<float>(Param(node, "DepositSpeed", params.DepositSpeed));
		params.Evaporation = static_cast<float>(Param(node, "Evaporation", params.Evaporation));
		params.Gravity = static_cast<float>(Param(node, "Gravity", params.Gravity));
		params.BrushRadius = static_cast<int>(Param(node, "BrushRadius", static_cast<double>(params.BrushRadius)));
		params.Strength = static_cast<float>(Param(node, "Strength", params.Strength));
		params.Seed = ctx.Seed + static_cast<uint32_t>(Param(node, "Seed", 0.0)) * 97u;

		HydraulicErosion::Run(heightmap, params);

		out.Resize(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) out.At(i, j) = heightmap.At(i, j);
		return true;
	}
}
