#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// Biome ids this node writes into its output field (one float per cell).
	enum class TgBiome { Water = 0, Shore = 1, Snow = 2, Rock = 3, Forest = 4, Grass = 5 };

	// "biome_classify": a height field (port 0, required) and an optional moisture field (port 1; a cheap
	// internal noise stands in for it when nothing is connected) turned into a field of TgBiome ids, by walking
	// an ordered table of rules instead of the hand-written cascade of `if`s in WorldGen.cpp's Classify
	// (docs/GRAFO_TERRENO.md, "huecos de realismo" #4). The first rule whose test passes wins.
	// Params: WaterLevel (0.0 m), ShoreBand (1.0 m above WaterLevel), SnowHeight (1e9, i.e. off by default),
	// RockSlopeDegrees (30.0), ForestMoisture (0.5, >= is forest, below is grass).
	class BiomeClassifyNode final : public ITerrainNode
	{
	public:
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
