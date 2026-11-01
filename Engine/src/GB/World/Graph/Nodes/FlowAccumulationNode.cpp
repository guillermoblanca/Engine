#include "GB/World/Graph/Nodes/FlowAccumulationNode.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"

namespace GB
{
	bool FlowAccumulationNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "flow_accumulation node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "flow_accumulation node '" + node.Id + "' got an empty field"; return false; }

		Heightmap heightmap(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) heightmap.At(i, j) = in.At(i, j);

		const FlowAccumulationResult result = FlowAccumulation::Run(heightmap);

		out.Resize(in.N, in.Size, in.Center);
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i) out.At(i, j) = result.Accumulation[static_cast<size_t>(j) * in.N + i];
		return true;
	}
}
