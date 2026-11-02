#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "lake_site": where the lake goes: the lowest point of the map away from its borders. The ground is smoothed first (a 3x3 box blur on a coarse
	// grid) so that one noisy dip does not beat a broad basin, which is where a lake really collects.
	//
	// As a field it is an outlet mask: 1 at the cell of the site, 0 elsewhere -- what depression_fill and river_trace take as their second and third
	// input, so the water of the catchment runs to the lake and not off the map. As an IPathNode it is one point, the site, with the smoothed
	// height there as its value (the lake's surface is built from it).
	//
	// Input: [0] the height field. Params: Coarse (cells per side of the smoothed grid, default 65), Margin (coarse cells kept clear of the
	// border, default 12).
	class LakeSiteNode final : public ITerrainNode, public IPathNode
	{
	public:
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
		const IPathNode* AsPathNode() const override { return this; }
		bool Trace(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgPaths& out, std::string& error) const override;
	};
}
