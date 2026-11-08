#include "GB/World/Graph/Nodes/DepressionFillNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <functional>
#include <queue>
#include <utility>

namespace GB
{

	bool DepressionFillNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "depression_fill node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N == 0) { error = "depression_fill node '" + node.Id + "' got an empty field"; return false; }
		const TgField* outlets = inputs.size() > 1 ? inputs[1] : nullptr;
		if (outlets && outlets->N != in.N) { error = "depression_fill node '" + node.Id + "': the outlet mask is not the size of the height field"; return false; }

		out = in;
		const uint32_t n = in.N;
		if (n < 3) return true;
		const float epsilon = static_cast<float>(TgParam(node, "Epsilon", 0.001));

		std::vector<uint8_t> closed(out.Values.size(), 0);
		using Item = std::pair<float, uint32_t>;   // (height, flat index): ties go to the lower index, so the result is deterministic
		std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
		auto seed = [&](uint32_t index)
		{
			if (closed[index]) return;
			closed[index] = 1;
			open.push({ out.Values[index], index });
		};
		for (uint32_t i = 0; i < n; ++i)
		{
			seed(i); seed((n - 1) * n + i); seed(i * n); seed(i * n + n - 1);
		}
		if (outlets)
			for (uint32_t index = 0; index < outlets->Values.size(); ++index)
				if (outlets->Values[index] > 0.5f) seed(index);

		while (!open.empty())
		{
			const auto [height, cell] = open.top();
			open.pop();
			const int ci = static_cast<int>(cell % n), cj = static_cast<int>(cell / n);
			for (int dj = -1; dj <= 1; ++dj)
				for (int di = -1; di <= 1; ++di)
				{
					const int ni = ci + di, nj = cj + dj;
					if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
					const uint32_t neighbour = static_cast<uint32_t>(nj) * n + static_cast<uint32_t>(ni);
					if (closed[neighbour]) continue;
					closed[neighbour] = 1;
					float& h = out.Values[neighbour];
					if (h <= height) h = height + epsilon;   // a pit (or a flat): lifted to just above the cell the water leaves it through
					open.push({ h, neighbour });
				}
		}
		return true;
	}

	TgNodeSchema DepressionFillNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Fills every pit so that each cell drains to the border or an outlet (Priority-Flood, Barnes et al. 2014).";
		s.Color = 0x288282;
		s.Inputs = { { "height", TgPortKind::Field }, { "outlets", TgPortKind::Field, true, "cells > 0.5 drain like the border (a lake)" } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = { { "Epsilon", 0.001, 0, 0.05, false, "metres a filled cell rises above the one it drains through" } };
		return s;
	}
}
