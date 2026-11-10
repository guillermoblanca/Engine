#pragma once
#include "GB/World/Graph/ITerrainNode.h"
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// The ground's material layers and its trees from fields with a meaning (documentation/02_tecnico/motor/GRAFO_TERRENO.md, "Capas de
	// material y árboles del terreno nuevo"). New nodes of the terrain of TERRENO_V5: the nodes of generators 1-4 stay as they are, bit for bit.
	// Every one carries an integer parameter Revision (1 when a graph does not write it; any other value is an error, not a guess), so a later
	// change of behaviour comes as Revision 2 in the graphs that ask for it and a saved world keeps its ground.
	//
	// The layers are 0..1 fields of the height's size. layer_normalize turns them into a partition (they sum to 1 at every sample), which is what
	// the output slots splat_cliff/scree/gravel/mud/mud_dry/grass_wet/path publish next to the six of before (SurfaceLayerKind, Terrain.h).
	// The E4 cliff/scree masks and the path mask of the settlement nodes are optional inputs: unconnected, cliff and scree come from the slope and
	// there is no path.

	// "layer_cliff_scree": the bare faces and the broken rock at their foot. Inputs: height; cliff and scree (optional, 0..1: E4's outputs, used
	// as they come). Without cliff: a ramp of the slope around CliffDegrees (CliffFalloff wide). Without scree: ground between ScreeMinDegrees and
	// ScreeMaxDegrees within ScreeReach metres of a cliff (cliff >= 0.5) and at least ScreeDrop metres below it (the foot, not the crest),
	// fading out with the distance. Outputs: cliff, scree (scree is 0 where cliff is 1).
	class CliffScreeLayerNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "layer_wet": what the water leaves on the ground. Inputs: height; water (optional: its lakes and river, rasterised by
	// GB/World/Water/WaterRaster.h, the one definition of a wet sample); moisture, area (m2 of drainage, fluvial_erosion's) and alluvium (E3's
	// deposit), all optional. Outputs:
	//   gravel    the bed of the river and the banks of the channels: within GravelReach metres of a river sample or of a sample draining more
	//             than ChannelArea m2, at most GravelRise metres above it and under GravelMaxDegrees; plus the alluvium where it is flat;
	//   mud       the lake beds and their shore: within MudReach metres of the water, at most MudRise metres above its level;
	//   mud_dry   the band above it, out to MudDryReach metres and MudDryRise metres (the shore the water left);
	//   grass_wet high moisture (WetMoisture to WetFull) on gentle dry ground, and the damp band out to WetReach metres of the water.
	// Distances are an exact Euclidean distance transform in cells (no search along the river per sample).
	class WetLayerNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "layer_normalize": a partition of the ground among the layers, the same names in and out. Each input is clamped to 0..1 and laid over what
	// is below it, top first: snow, path, cliff, scree, rock, gravel, mud, mud_dry, grass_wet, forest, dry, sand; grass takes what is left (it is
	// the ground nothing else covers, so it has no input). An unconnected layer is 0. The outputs sum to 1 at every sample (to float rounding)
	// and none is negative: snow and path lie on top as before, every other layer shares the rest. `height` only gives the size.
	class NormalizeLayersNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
	// The layers layer_normalize takes and gives, in their order from the top down ("grass", the remainder, last).
	const std::vector<std::string>& NormalizedLayerNames();

	// "biome_water": the biome with every lake and the river of a water field in it. biome_classify (frozen) knows only the main lake, so under
	// a lake of lake_fill it says meadow or forest and grass and trees grow in the water. Here a wet sample (WaterRaster) becomes Water and a dry
	// one within ShoreReach metres of the water and at most ShoreRise metres above its level becomes Shore; the rest is left as it was.
	class BiomeWaterNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "tree_species": the species of every tree a scatter placed, by height band, and the trees that cannot stand where they are taken out.
	// Inputs: placements and height (required); water, cliff, scree, path, exclude and riparian (optional).
	//   - A tree goes when any sample within Margin cells of its cell is wet (WaterRaster: every lake and the river), or has cliff, scree, path or
	//     exclude above ExcludeAbove. So no tree stands in a lake, a river, on a cliff, on scree or on a path, read at any point between samples.
	//   - Its band is its height relative to the field's range (or in metres, AbsoluteHeights 1), with the edges dithered by BandBlend: low below
	//     LowTop, mid below MidTop, high below HighTop, top above. Where riparian >= RiparianAbove a low or mid tree is in the riparian band.
	//   - Its species is a weighted pick, from a hash of its spot, among the parameters "Tree:<band>:<species>" of its band, <species> being
	//     "Broadleaf/<style>", "Conifer/<style>", "Bare" or "Palm[/<style>]" (TreeParams' Species and Style). A band with no entry has no trees.
	//     Without any "Tree:" parameter the node uses TreeSpeciesDefaults().
	//   - Its Variant is kind * Variety + a hashed variety, kind indexing TreeSpeciesPalette(node): the world generator builds Variety recipes
	//     of every kind of that list (different seeds) and keeps the Variant.
	class TreeSpeciesNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// The band entries of a tree_species node: (band, species, weight > 0), from its "Tree:<band>:<species>" parameters in key order, or the
	// defaults when it has none.
	struct TreeSpeciesEntry { std::string Band, Species; double Weight = 0.0; };
	std::vector<TreeSpeciesEntry> TreeSpeciesEntries(const TgNode& node);
	const std::vector<TreeSpeciesEntry>& TreeSpeciesDefaults();
	// The distinct species of a tree_species node, sorted: what (Variant / Variety) indexes.
	std::vector<std::string> TreeSpeciesPalette(const TgNode& node);
	// Recipes per species of a tree_species node (its Variety parameter, at least 1).
	int TreeSpeciesVariety(const TgNode& node);
}
