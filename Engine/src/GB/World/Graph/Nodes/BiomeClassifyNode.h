#pragma once
#include "GB/World/Biome.h"
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "biome_classify": a height field (port 0, required) and an optional moisture field (port 1; a cheap
	// internal noise stands in for it when nothing is connected) turned into a field of GB::Biome ids (the same
	// enum WorldGen.cpp's GeneratedWorld uses, GB/World/Biome.h), by walking an ordered table of rules instead
	// of the hand-written cascade of `if`s in WorldGen.cpp's Classify (docs/GRAFO_TERRENO.md, "huecos de
	// realismo" #4). The first rule whose test passes wins. Never writes Biome::Dunes (that biome only comes
	// from the desert/dune generation path, which this node has no part in).
	// Params: WaterLevel (0.0 m), ShoreBand (1.0 m above WaterLevel), SnowHeight (1e9, i.e. off by default),
	// RockSlopeDegrees (30.0), ForestMoisture (0.5, >= is forest, below is meadow).
	class BiomeClassifyNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
