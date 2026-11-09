#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// The world's water, carved by the graph (docs/GRAFO_TERRENO.md, phase 2): the analysis nodes (lake_site, depression_fill,
	// flow_accumulation, river_trace) say where the lake and the river go; these dig them into the ground with the same functions the built-in
	// terrain uses (GB/World/Water/WaterCarver.h) and publish them as a GB::WaterField, which an "output" node named "water" hands to the world.

	// "lake_carve": digs the lake at a site (lake_site's path) and adds it to the water. Its surface stands LevelAbove over the smoothed
	// basin; its shore is lobed by Irregularity, with the world's seed.
	class LakeCarveNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "river_carve": carves the river along a course (river_trace's path), wider and deeper downstream with the drainage at each point
	// (GB/World/Water/RiverHydraulics.h), ending at the shore of the lake of the incoming water when there is one.
	class RiverCarveNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
