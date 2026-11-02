#include "GB/World/Graph/Nodes/OutputNode.h"

namespace GB
{
	bool OutputNode::Evaluate(const TgNode&, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "output node has no input connected"; return false; }
		out = *inputs[0];
		return true;
	}
}
