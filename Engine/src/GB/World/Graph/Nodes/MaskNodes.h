#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// Masks: 0..1 fields that say where something applies -- by height, slope, curvature, distance to the water, biome -- and the snow cover
	// that is built from them (docs/GRAFO_TERRENO.md, phases 4 and 4b). What erosion masks, blends and the terrain's material layers take.

	// "mask_height": 1 between Min and Max (absolute metres, or fractions of the height range), fading over Falloff.
	class HeightMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "mask_slope": 1 between MinDegrees and MaxDegrees of slope, fading over Falloff degrees.
	class SlopeMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "mask_curvature": hollows (gullies, cirques, couloirs) or crests (ridges, edges), from the concavity at a radius.
	class CurvatureMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "mask_water": 0 at the world's water (lake, river), 1 past Reach + Falloff metres from it: what keeps erosion out of a carved channel.
	class WaterMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "biome_mask": 1 where a biome field (biome_classify) holds the chosen GB::Biome.
	class BiomeMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "snow_accumulate": where snow lies on a mountain. It slides off what is steep and gathers in hollows and in the channels the water
	// carved (couloirs), above a height; a noise breaks the edges up. 0..1.
	class SnowAccumulateNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "biome_weights": a value per biome of a biome field -- the chance of a tree in each, the share of rocks, a material weight.
	class BiomeWeightsNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
