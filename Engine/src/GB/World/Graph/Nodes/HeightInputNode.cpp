#include "GB/World/Graph/Nodes/HeightInputNode.h"
#include "GB/World/Graph/TerrainGraph.h"

namespace GB
{
	bool HeightInputNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string& error) const
	{
		const std::string name = node.Source.empty() ? "height" : node.Source;
		if (!ctx.Inputs) { error = "height_input node '" + node.Id + "': the context carries no inputs (asked for '" + name + "')"; return false; }
		const auto it = ctx.Inputs->find(name);
		if (it == ctx.Inputs->end()) { error = "height_input node '" + node.Id + "': no input field named '" + name + "'"; return false; }
		if (it->second.N == 0) { error = "height_input node '" + node.Id + "': the input field '" + name + "' is empty"; return false; }
		out = it->second;
		return true;
	}
}
