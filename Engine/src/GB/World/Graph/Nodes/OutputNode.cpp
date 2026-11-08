#include "GB/World/Graph/Nodes/OutputNode.h"
#include "GB/World/Graph/TerrainGraph.h"

namespace GB
{
	bool OutputNode::Evaluate(const TgNode&, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "output node has no input connected"; return false; }
		out = *inputs[0];
		return true;
	}

	TgNodeSchema OutputNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Output";
		s.Summary = "Publishes what it is given under its Source name: height, biome, moisture, splat_rock/snow/grass/sand (fields); trees, rocks (placements); water.";
		s.Color = 0xAA3C3C;
		s.Inputs = { { "in", TgPortKind::Any } };
		s.Outputs = { { "out", TgPortKind::Any } };
		s.UsesSource = true;
		s.SourceDefault = "height";
		return s;
	}

	bool OutputNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "output node '" + node.Id + "' has no input connected"; return false; }
		outputs["out"] = *inputs[0];
		return true;
	}
}
