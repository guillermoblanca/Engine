#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// The general-purpose field nodes of the terrain graph, the counterpart of Houdini's heightfield SOPs (docs/GRAFO_TERRENO.md, phase 4):
	// generators, reshaping, combining. Each is a small pure function of its input fields; the parameters and ports are in each Describe().

	// "constant": a field with one value everywhere.
	class ConstantNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "gradient": 0..1 across the map along a direction, or 1 at a centre fading to 0 at a radius (a massif's footprint, a coast).
	class GradientNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "hills": the built-in hills of GB::Terrain (Terrain::CreateHills, before any water), bit for bit (GB/World/TerrainNoise.h).
	class HillsNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "dunes": the built-in dunes of GB::Terrain (Terrain::CreateDunes), bit for bit.
	class DunesNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "remap": a linear map from one range to another, with an optional clamp and gamma (curve).
	class RemapNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "terrace": steps of a given height, from soft to sharp.
	class TerraceNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "blur": box blur passes (two or three approximate a gaussian).
	class BlurNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "warp": the field read at points pushed by a low noise: bends straight features.
	class WarpNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "combine": a op b (add, subtract, multiply, min, max, average), b scaled first.
	class CombineNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "blend": a where the mask is 0, b where it is 1.
	class BlendNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "strata": rock layers: terraces of a tilted, wavering bedding, applied where the ground is steep (ledges on a cliff, not on a meadow).
	class StrataNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};

	// "moisture": 0..1 humidity: a broad noise, wetter where the water gathers (a flow accumulation) and drier high up. What biome_classify
	// turns into forest or meadow and the world keeps for its forest clumps (GeneratedWorld::MoistureAt).
	class MoistureNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
