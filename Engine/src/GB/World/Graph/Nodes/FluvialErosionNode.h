#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "fluvial_erosion": the stream-power incision of TERRENO_V5 E2 (GB/World/StreamPower.h), the second phase of generator 5. The rivers cut
	// valleys into the relief in proportion to the square root of the area draining through them and the slope: the large channels dig V
	// valleys, their tributaries branch up the slopes into gullies, and the ground left between two valleys stands as a sharp crest. With uplift
	// the range keeps rising where the uplift mask says while it is cut; the hillslope diffusion rounds what the channels leave.
	// A new node: the erosions of generators 1-4 (erosion_hydraulic, erosion_thermal) stay as they are, bit for bit. It carries an integer
	// parameter Revision (1 when a graph does not write it; anything else is an error): a later change of behaviour comes as Revision 2.
	//
	// It runs as a cascade (StreamPowerErodeCascade): CoarseIterations on a CoarseResolution grid over the same extent, then a few iterations on
	// each grid twice as fine up to the field's own. At the coarse resolution (the editor's preview at 257) it runs the coarse stage alone, so
	// the preview's valleys are the world's.
	//   inputs : height; optional uplift (multiplies Uplift), hardness (0 soft .. 1 hard: K times mix(1, HardErodibility, hardness)),
	//            precipitation (weights the area), water (its lakes and river are base level, as the drainage node's outlets) and outlets
	//            (cells > 0.5 are base level).
	//   outputs: height; area (m2 draining through each cell, of the final ground); eroded (metres the ground went down: input - output, the
	//            uplift counted, so negative where it rose); coarse (the coarse stage's ground, interpolated to the field).
	// Lakes (a hollow of the drainage: its level above the ground) are not incised; the base their donors cut toward is the lake's surface.
	// The node leaves the hollows it finds (it does not fill): a lake_fill after it makes the lakes and drains the rest.
	class FluvialErosionNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
