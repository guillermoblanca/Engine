#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "flow_accumulation": one input (a height field), turned into GB::FlowAccumulation's drainage-area field
	// (GB/World/FlowAccumulation.h) -- reused as-is. Values are raw cell counts (1 at a ridge, growing toward
	// the valleys), not normalised to 0..1: a node downstream (a future "mask"/threshold node) decides what
	// counts as "this is a river". No params; the algorithm has none to expose.
	class FlowAccumulationNode final : public ITerrainNode
	{
	public:
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
