#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// "heightfield_noise": a fresh height field from fractal value noise. No inputs (it is usually the first
	// node of a graph). Params: Octaves (default 5), Frequency (0.015 1/m), Amplitude (20 m), Lacunarity (2.0),
	// Gain (0.5). Seeded from TgContext::Seed, mixed with Params["Seed"] when set, so two noise nodes in the
	// same graph do not line up by accident.
	class HeightfieldNoiseNode final : public ITerrainNode
	{
	public:
		bool Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const override;
	};
}
