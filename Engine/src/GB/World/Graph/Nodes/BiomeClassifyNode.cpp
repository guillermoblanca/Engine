#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <cmath>

namespace GB
{
	namespace
	{
		double Param(const TgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}

		uint32_t Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2654435761u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return h ^ (h >> 16);
		}
		// A cheap stand-in for a real moisture field (flow-accumulation/humidity, left for a later node):
		// just enough variation that ForestMoisture produces both forest and grass when nothing is wired in.
		float FallbackMoisture(int i, int j, uint32_t seed) { return static_cast<float>(Hash(i, j, seed) & 0xFFFFu) / 65535.0f; }

		// What one rule needs to decide a cell's biome. Table-driven (SRP/OCP): a new biome is a new entry
		// here, not a new branch threaded into the others.
		struct Context { float Height, Slope, Moisture, WaterLevel, ShoreBand, SnowHeight, RockSlopeDeg, ForestMoisture; };
		struct Rule { Biome Kind; bool (*Test)(const Context&); };

		const Rule kRules[] = {
			{ Biome::Water, [](const Context& c) { return c.Height <= c.WaterLevel; } },
			{ Biome::Shore, [](const Context& c) { return c.Height <= c.WaterLevel + c.ShoreBand; } },
			{ Biome::Snow, [](const Context& c) { return c.Height >= c.SnowHeight; } },
			{ Biome::Rock, [](const Context& c) { return c.Slope >= c.RockSlopeDeg; } },
			{ Biome::Forest, [](const Context& c) { return c.Moisture >= c.ForestMoisture; } },
			{ Biome::Meadow, [](const Context&) { return true; } },   // catches everything else
		};
	}

	bool BiomeClassifyNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "biome_classify node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& height = *inputs[0];
		if (height.N == 0) { error = "biome_classify node '" + node.Id + "' got an empty height field"; return false; }
		const TgField* moisture = inputs.size() > 1 ? inputs[1] : nullptr;

		const Context rules{ 0.0f, 0.0f, 0.0f,
			static_cast<float>(Param(node, "WaterLevel", 0.0)),
			static_cast<float>(Param(node, "ShoreBand", 1.0)),
			static_cast<float>(Param(node, "SnowHeight", 1e9)),
			static_cast<float>(Param(node, "RockSlopeDegrees", 30.0)),
			static_cast<float>(Param(node, "ForestMoisture", 0.5)) };
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(Param(node, "Seed", 0.0)) * 97u;

		out.Resize(height.N, height.Size, height.Center);
		const float spacing = height.Spacing();
		for (uint32_t j = 0; j < height.N; ++j)
			for (uint32_t i = 0; i < height.N; ++i)
			{
				const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < height.N ? i + 1 : i;
				const uint32_t j0 = j > 0 ? j - 1 : j, j1 = j + 1 < height.N ? j + 1 : j;
				const float dhdx = (height.At(i1, j) - height.At(i0, j)) / (static_cast<float>(i1 - i0) * spacing + 1e-6f);
				const float dhdz = (height.At(i, j1) - height.At(i, j0)) / (static_cast<float>(j1 - j0) * spacing + 1e-6f);
				const float slopeDeg = glm::degrees(std::atan(std::sqrt(dhdx * dhdx + dhdz * dhdz)));

				Context c = rules;
				c.Height = height.At(i, j);
				c.Slope = slopeDeg;
				c.Moisture = moisture ? moisture->At(i, j) : FallbackMoisture(static_cast<int>(i), static_cast<int>(j), seed);

				Biome biome = Biome::Meadow;
				for (const Rule& rule : kRules) if (rule.Test(c)) { biome = rule.Kind; break; }
				out.At(i, j) = static_cast<float>(biome);
			}
		return true;
	}
}
