#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "lake_basin": digs the lake into the ground: the same basin and banks the terrain will be carved with (GB/World/Water/WaterCarver.h, ApplyLakeBasin),
	// at the site lake_site finds. A lake is a hollow with raised banks, and the water of the land around it runs down into it; analysed on the bare
	// hills the basin is only the lowest point allowed, on a slope that keeps falling beyond it, and the rain of its surroundings drains past it. With
	// the basin dug, depression_fill and flow_accumulation see what the finished world will have, and the river that river_trace follows ends in it.
	//
	// Input: [0] the height field. Params: Radius (m, default 22), Depth (m below the surface at the centre, default 3), LevelAbove (the surface stands
	// this far above the ground at the site, default 1), BankSlope (default 0.3), Irregularity (the lobes of the shore, default 0), Coarse and
	// Margin as in lake_site. The shape of the shore takes its phases from the graph's seed (TgContext::Seed).
	class LakeBasinNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
