#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "river_trace": the course of the main river, followed down the drainage network instead of invented. On a height field where every cell
	// drains (after depression_fill) the water of a cell goes to its steepest downhill neighbour (D8), so the cells form a forest of flow lines
	// that end at an outlet. The river is the longest of them: the cell whose water has the longest way to go before it reaches the outlet
	// (the lake's site when one is given, else wherever the map drains) is the source, and the river is that water's way down.
	//
	// That is how a real river is born: it follows the valleys the ground already has, and the drainage area behind it (the flow accumulation, in
	// cells) grows downstream -- which the hydraulic geometry of the river (width, depth, speed: GB/World/Water/RiverHydraulics.h) is made of.
	//
	// Inputs, in edge order: [0] the height field to follow (filled: see depression_fill); [1] the flow accumulation of that field
	// (flow_accumulation); [2] (optional) the outlet mask (lake_site): the river is then the longest flow line that ends at the lake.
	// Output field: the flow accumulation on the cells of the river, 0 elsewhere. As an IPathNode: the course, from source to outlet, one point
	// every Spacing metres, each with the flow accumulation (cells) at it as its value.
	//
	// Params: MinStartDistance (the source is at least this fraction of the map's size from the outlet, default 0.3; relaxed if nothing
	// qualifies), Spacing (metres between points of the course, default 6), Smooth (half-width, in cells, of the moving average that rounds the
	// staircase of the D8 steps, default 3).
	class RiverTraceNode final : public ITerrainNode, public IPathNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
		const IPathNode* AsPathNode() const override { return this; }
		bool Trace(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgPaths& out, std::string& error) const override;
	};
}
