#include "GB/World/Graph/Nodes/ThermalErosionNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"
#include "GB/World/ThermalErosion.h"

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

	bool ThermalErosionNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "erosion_thermal node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "erosion_thermal node '" + node.Id + "' got an empty field"; return false; }

		Heightmap heightmap(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) heightmap.At(i, j) = in.At(i, j);

		ThermalErosionParams params;
		params.Iterations = static_cast<int>(Param(node, "Iterations", static_cast<double>(params.Iterations)));
		params.TalusAngleDegrees = static_cast<float>(Param(node, "TalusAngleDegrees", static_cast<double>(params.TalusAngleDegrees)));
		params.Strength = static_cast<float>(Param(node, "Strength", static_cast<double>(params.Strength)));

		ThermalErosion::Run(heightmap, params);

		out.Resize(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) out.At(i, j) = heightmap.At(i, j);
		return true;
	}
}
