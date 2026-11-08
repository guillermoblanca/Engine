#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "output": the graph's own plumbing node. Copies its single input through unchanged; EvaluateTerrainGraph
	// collects results by the Source name of each output node once every node ran, so the evaluator needs no
	// special case for it -- it is a node like any other (OCP).
	class OutputNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
		// Takes any kind of value (a field, placements, paths, water) and passes it through.
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
