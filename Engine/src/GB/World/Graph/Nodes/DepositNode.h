#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "deposit": the deposit of TERRENO_V5 E3 (GB/World/Deposit.h), the phase of generator 5 after the talus (E2 -> talus -> deposit). The
	// valleys are filled part of the way up to their sides (Fill of the way to the closing of the ground over FillRadius metres: a gully
	// keeps its shape, shallower; the crests do not move), and where a channel of FanArea m2 or more leaves a reach steeper than FanApexAngle
	// for one gentler than FanAngle it lays a fan of FanAngle holding FanDepth metres of its basin. Optional: an easing of the long slopes
	// (EaseAngle > 0), a cut down to that angle where the slope is long. Lakes (the hollows of the input and the water given), the cells beside
	// them and the border do not move, and along the drainage of the input every cell stays over the one it drains to: no new hollow. A new
	// node; nothing before it changes. Revision 1 (when a graph does not write it; anything else is an error).
	//   inputs : height; optional hardness (0 soft .. 1 hard: talus' output; hard rock is neither filled nor eased), water (its lakes and
	//            river do not move).
	//   outputs: height; alluvium (metres laid down: fill and fans; for layers, biomes, clay and the meadows); fan (metres of fan);
	//            eased (metres the easing cut).
	class DepositNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
