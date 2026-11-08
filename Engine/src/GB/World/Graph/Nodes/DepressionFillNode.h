#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "depression_fill": raises every pit of a height field until water can leave it, so that every cell drains somewhere. Without it a flow
	// analysis (flow_accumulation, river_trace) stops in each hollow of the noise; with it the water of the whole map reaches an outlet.
	//
	// The algorithm is Priority-Flood with an epsilon (Barnes, Lehman & Mulla, 2014): start from the outlets (the border of the map, and any
	// cell marked in the optional second input), always take the lowest cell waiting, and give each unvisited neighbour at least the height of
	// that cell plus epsilon. Every cell is visited once, in O(N log N), and the epsilon leaves the filled areas with a slight slope toward
	// their outlet instead of flat, so the steepest-descent rule of the D8 flow analysis has a direction everywhere.
	//
	// Inputs, in edge order: [0] the height field; [1] (optional) an outlet mask: cells above 0.5 are outlets too (a lake's basin: the
	// water of its catchment goes there, not to the border). Params: Epsilon (metres, default 0.001).
	class DepressionFillNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
