#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{

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
		// here, not a new branch threaded into the others. InWater/NearWater come from the world's lake and river when a water input is
		// connected, else from WaterLevel/ShoreBand.
		// HasForest: a forest input decides where forest grows (a suitability the graph built from the terrain); without one the moisture does.
		struct Context { float Height, Slope, Moisture, SnowHeight, RockSlopeDeg, ForestMoisture, Forest; bool HasForest, InWater, NearWater; };
		struct Rule { Biome Kind; bool (*Test)(const Context&); };

		const Rule kRules[] = {
			{ Biome::Water, [](const Context& c) { return c.InWater; } },
			{ Biome::Shore, [](const Context& c) { return c.NearWater; } },
			{ Biome::Snow, [](const Context& c) { return c.Height >= c.SnowHeight; } },
			{ Biome::Rock, [](const Context& c) { return c.Slope >= c.RockSlopeDeg; } },
			{ Biome::Forest, [](const Context& c) { return c.HasForest ? c.Forest >= 0.5f : c.Moisture >= c.ForestMoisture; } },
			{ Biome::Meadow, [](const Context&) { return true; } },   // catches everything else
		};

		bool Classify(const TgNode& node, const TgContext& ctx, const TgField* heightIn, const TgField* moisture, const WaterField* water, const TgField* forest, TgField& out, std::string& error)
		{
			if (heightIn == nullptr) { error = "biome_classify node '" + node.Id + "' has no height input connected"; return false; }
			const TgField& height = *heightIn;
			if (height.N == 0) { error = "biome_classify node '" + node.Id + "' got an empty height field"; return false; }

			const float waterLevel = static_cast<float>(TgParam(node, "WaterLevel", 0.0));
			const float shoreBand = static_cast<float>(TgParam(node, "ShoreBand", 1.0));
			float snowHeight = static_cast<float>(TgParam(node, "SnowHeight", 1e9));
			const float snowRelative = static_cast<float>(TgParam(node, "SnowRelative", 0.0));
			if (snowRelative > 0.0f && !height.Values.empty())   // a snow line at a fraction of the height range: independent of the graph's scale
			{
				const auto [lo, hi] = std::minmax_element(height.Values.begin(), height.Values.end());
				snowHeight = *lo + snowRelative * (*hi - *lo);
			}
			const float rockSlope = static_cast<float>(TgParam(node, "RockSlopeDegrees", 30.0));
			const float forestMoisture = static_cast<float>(TgParam(node, "ForestMoisture", 0.5));
			const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;

			out.Resize(height.N, height.Size, height.Center);
			const float spacing = height.Spacing();
			for (uint32_t j = 0; j < height.N; ++j)
				for (uint32_t i = 0; i < height.N; ++i)
				{
					const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < height.N ? i + 1 : i;
					const uint32_t j0 = j > 0 ? j - 1 : j, j1 = j + 1 < height.N ? j + 1 : j;
					const float dhdx = (height.At(i1, j) - height.At(i0, j)) / (static_cast<float>(i1 - i0) * spacing + 1e-6f);
					const float dhdz = (height.At(i, j1) - height.At(i, j0)) / (static_cast<float>(j1 - j0) * spacing + 1e-6f);
					const glm::vec2 p = height.SamplePosition(i, j);

					Context c{};
					c.Height = height.At(i, j);
					c.Slope = glm::degrees(std::atan(std::sqrt(dhdx * dhdx + dhdz * dhdz)));
					c.Moisture = moisture ? moisture->Sample(p.x, p.y) : FallbackMoisture(static_cast<int>(i), static_cast<int>(j), seed);
					c.SnowHeight = snowHeight;
					c.RockSlopeDeg = rockSlope;
					c.ForestMoisture = forestMoisture;
					c.HasForest = forest != nullptr;
					c.Forest = forest ? forest->Sample(p.x, p.y) : 0.0f;
					if (water)
					{
						// the same measures as the world's own classification (WorldGen.cpp, Classify): under the lake's level near it, inside the
						// river's band under its level; a shore is less than ShoreBand above the water close to it
						float above = 1e9f;
						if (water->HasLake())
						{
							const LakeBody& lake = water->Lake();
							const float d = lake.Outline.EffectiveDistance(p, lake.Center);
							if (c.Height < lake.Level - 0.02f && d < lake.Radius * 1.3f) c.InWater = true;
							if (d <= lake.Radius * 1.8f) above = std::min(above, std::max(c.Height - lake.Level, 0.0f));
						}
						if (water->River())
						{
							const RiverSample r = water->NearestRiver(p.x, p.y);
							if (r.Found && r.Distance <= r.HalfWidth * 1.2f && c.Height < r.Level) c.InWater = true;
							if (r.Found && r.Distance < r.HalfWidth * 1.8f) above = std::min(above, std::max(c.Height - r.Level, 0.0f));
						}
						c.NearWater = above < shoreBand;
					}
					else
					{
						c.InWater = c.Height <= waterLevel;
						c.NearWater = c.Height <= waterLevel + shoreBand;
					}

					Biome biome = Biome::Meadow;
					for (const Rule& rule : kRules) if (rule.Test(c)) { biome = rule.Kind; break; }
					out.At(i, j) = static_cast<float>(biome);
				}
			return true;
		}
	}

	TgNodeSchema BiomeClassifyNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Biome";
		s.Summary = "A GB::Biome id per cell from an ordered table of rules: water, shore, snow, rock (slope), forest (moisture), meadow.";
		s.Color = 0x968C32;
		s.Inputs = { { "height", TgPortKind::Field }, { "moisture", TgPortKind::Field, true, "0..1 (unconnected: a hash noise)" },
			{ "water", TgPortKind::Water, true, "the world's lake and river: water and shore come from them instead of WaterLevel" },
			{ "forest", TgPortKind::Field, true, "0..1 where forest may grow (forest at 0.5 and up), instead of ForestMoisture" } };
		s.Outputs = { { "biome", TgPortKind::Field } };
		s.Params = {
			{ "WaterLevel", 0, -50, 100, false, "metres; below is water (only without a water input)" },
			{ "ShoreBand", 1, 0, 5, false, "metres above the water that are shore" },
			{ "SnowHeight", 1e9, -100, 1000, false, "metres; above is snow (1e9 = off)" },
			{ "SnowRelative", 0, 0, 1, false, "> 0: the snow line at this fraction of the height range (overrides SnowHeight)" },
			{ "RockSlopeDegrees", 30, 5, 80, false, "steeper is rock" },
			{ "ForestMoisture", 0.5, 0, 1, false, "moister is forest, drier is meadow" },
			{ "Seed", 0, 0, 100, true, "of the fallback moisture" },
		};
		return s;
	}

	bool BiomeClassifyNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		return Classify(node, ctx, inputs.empty() ? nullptr : inputs[0], inputs.size() > 1 ? inputs[1] : nullptr, nullptr, nullptr, out, error);
	}

	bool BiomeClassifyNode::Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		auto field = [&](size_t i) { return inputs.size() > i && inputs[i] && inputs[i]->Kind == TgPortKind::Field ? &inputs[i]->Field : nullptr; };
		const WaterField* water = inputs.size() > 2 && inputs[2] && inputs[2]->Kind == TgPortKind::Water ? &inputs[2]->Water : nullptr;
		TgValue& value = outputs["biome"];
		value.Kind = TgPortKind::Field;
		return Classify(node, ctx, field(0), field(1), water, field(3), value.Field, error);
	}
}
