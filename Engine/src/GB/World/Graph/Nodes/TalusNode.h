#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "talus": the talus with hardness of TERRENO_V5 E4 (GB/World/Talus.h), the phase of generator 5 after the incision (fluvial_erosion). It
	// cuts only what stands above the angle of repose of its rock -- tan(angle) = mix(tan(SoftAngle), tan(HardAngle), hardness) -- and leaves
	// the rest exactly as it was: hard rock keeps its cliffs, soft rock is cut back to its slope, and what falls rests at the foot as scree at
	// ScreeAngle, or is carried away by a channel (ChannelArea), a lake or the border. The crests and the channel floors below the angle do not
	// move, so the gullies of E2 stay. Lakes (the hollows of the input, and the water given) do not move. A new node: the thermal erosion of
	// generators 1-4 (erosion_thermal) stays as it is. Revision 1 (when a graph does not write it; anything else is an error).
	//   inputs : height; optional hardness (0 soft .. 1 hard; without it the node makes its own: patches of a noise covering HardCover of
	//            the map and, with StrataSpacing, horizontal beds), water (its lakes and river do not move).
	//   outputs: height; scree (metres laid down); cliff (0..1: hard rock steeper than 45-55 degrees); hardness (the one used); cut (metres
	//            the cut took away).
	class TalusNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
