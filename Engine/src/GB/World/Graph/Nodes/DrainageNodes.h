#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// The drainage of the terrain and the lakes it fills (TERRENO_V5 E1, the first phase of generator 5). New nodes: the ones of generators
	// 1-4 (depression_fill, flow_accumulation, lake_site, lake_carve...) stay as they are, bit for bit. Both carry an integer parameter Revision
	// (1 when a graph does not write it): a later change of behaviour comes as Revision 2, written explicitly in the graphs that want it, so a
	// saved world built with 1 keeps its ground.

	// "drainage": one Priority-Flood over the height (GB/World/Drainage.h). Outlets are the map's border, the water carved before (its lakes
	// and the ribbon of its river, as GB/World/Water/WaterRaster.h rasterises it) and the cells of the optional outlet mask. Outputs, all fields:
	//   filled   the height raised so that every cell has a strictly lower neighbour (an epsilon per cell across the hollows)
	//   level    the exact level water stands at in every hollow, its spill height (the ground where nothing pools)
	//   area     m2 that drain through each cell (times the optional precipitation)
	//   receiver the flat index (j*N + i) of the cell each one drains to, -1 at an outlet
	//   order    the rank of each cell in the drainage order: every cell after its receiver (walk it backwards to accumulate, forwards to solve)
	// receiver and order hold integers in floats: exact up to 2^24 cells (4096 x 4096).
	class DrainageNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "lake_fill": turns the hollows of a drainage into lakes filled to the level they overflow at, with the shore on the contour line of that
	// level, and lets the rest of the ground drain. A hollow deeper than MinDepth and larger than MinArea becomes a lake (the MaxLakes largest);
	// a smaller one is filled, its cells raised just enough that each drains to the next (the height a pond's silt would leave). The lakes' cells
	// keep their ground: the water covers them. Outputs: height (every dry cell has a strictly lower neighbour or is an outlet), water (the
	// incoming water with each lake added, WaterField::AddLake: the main lake stays the incoming one), lakes (1 in a lake), depth (metres of water)
	// and raised (metres the filling added to the ground).
	class LakeFillNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
