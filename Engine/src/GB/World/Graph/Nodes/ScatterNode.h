#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "scatter": places discrete objects (trees, rocks...) over a height field, by wrapping GB::ScatterTrees
	// (GB/World/Trees.h) as-is -- same dart-throwing-with-minimum-spacing the imperative WorldGen.cpp already
	// uses for its own trees and rocks, reused rather than reimplemented. A terrain graph node that also places
	// things is an IScatterNode (ITerrainNode.h); this is today's only example.
	//
	// Ports: 0 = height (required). 1 = density, optional: a 0..1 field (e.g. a biome mask, or just another
	// noise node) that gates where a spot is accepted, on top of height/slope -- without it every spot that
	// passes height/slope is a candidate, same as the height-only overload of GB::ScatterTrees.
	// Params: Count (120), Variants (1), MinSpacing (4.0 m), MinHeight (-1e9), MaxHeight (1e9),
	// MaxSlopeDegrees (28.0, GB::ScatterTrees' own default), DensityThreshold (0.5, only read with a density
	// input connected). Evaluate() passes the height field through unchanged (so "scatter" can still sit in the
	// middle of a chain of field nodes); the placements themselves come out through Scatter(), keyed by this
	// node's TgNode::Id in TgResult::Placements.
	class ScatterNode final : public ITerrainNode, public IScatterNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
		const IScatterNode* AsScatterNode() const override { return this; }
		bool Scatter(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgPlacements& out, std::string& error) const override;
	};
}
