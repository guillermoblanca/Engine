#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// Where to settle, the trails between the settlements and the levelling of both (GRAFO_TERRENO, "Asentamientos y sendas"; the logic is
	// GB/World/Settle.h). New nodes with an integer parameter Revision (1 when a graph does not write it; another value is an error), like
	// drainage and lake_fill: a later change of behaviour comes as Revision 2 so a saved world keeps its ground. The site list, the trails,
	// the fords and the cols travel as Paths encoded by Settle.h (EncodeSites, EncodeTrails...): one record per item.

	// "settlement_sites": every candidate scored by fresh water at WaterMin..WaterMax metres, the slope of the camp and of the village's disc,
	// the flat ground there, the height above the nearest water, the distance to the border, forest and bare rock near; then the best Count,
	// Separation apart, chosen greedily (ties: the lower cell). Outputs sites (Paths), score (Field: 0 where refused) and water_distance.
	class SettlementSitesNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "route_network": least-cost trails (one Dijkstra from all the sites at once over a grid of Cell metres, 16 neighbours) along the minimum
	// spanning tree of the sites' regions plus the extra links that save the most, and spurs to each site's nearest water, to the cols between
	// big drainage basins and to the fords. The cost weighs the grade along the step (steep past MaxGrade), the cross slope, and forbids lakes,
	// walls and the river save at a ford. Outputs trails and trail_info (Paths), path (Field mask), fords and cols (Paths).
	class RouteNetworkNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "path_grade": a level platform per site (the median of its disc: the least earth moved) with talus around it; each trail's profile
	// averaged and held within MaxGrade, cut and filled up to MaxCut / MaxFill with Talus side slopes; no fill over a channel (a culvert), no
	// cut by the water, the bed of a built ford raised; and the ground filled where that closed a hollow (no pit after it). Outputs height,
	// path and platform (masks), earthwork (metres added, < 0 cut), and the sites, trails, trail_info and fords measured.
	class PathGradeNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
