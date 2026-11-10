#include "GB/World/Graph/Nodes/ScatterNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Heightmap.h"
#include "GB/World/TerrainNoise.h"
#include "GB/World/Scatter.h"
#include <cmath>

namespace GB
{
	std::vector<std::pair<std::string, double>> ScatterPalette(const TgNode& node)
	{
		static const std::string prefix = "Rock:";
		std::vector<std::pair<std::string, double>> palette;
		for (const auto& [name, weight] : node.Params)
			if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 && weight > 0.0) palette.emplace_back(name.substr(prefix.size()), weight);
		return palette;
	}

	bool ScatterNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "scatter node '" + node.Id + "' has no height input connected"; return false; }
		out = *inputs[0];   // a pass-through: scatter's real result is Scatter(), this just lets height keep flowing if something chains off it
		return true;
	}

	bool ScatterNode::Scatter(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgPlacements& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "scatter node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& heightField = *inputs[0];
		if (heightField.N == 0) { error = "scatter node '" + node.Id + "' got an empty height field"; return false; }
		const TgField* density = inputs.size() > 1 ? inputs[1] : nullptr;

		Heightmap heightmap(heightField.N, heightField.Size, heightField.Center);
		for (uint32_t j = 0; j < heightField.N; ++j)
			for (uint32_t i = 0; i < heightField.N; ++i) heightmap.At(i, j) = heightField.At(i, j);

		const int count = static_cast<int>(TgParam(node, "Count", 120.0));
		const int variants = std::max(1, static_cast<int>(TgParam(node, "Variants", 1.0)));
		const float minSpacing = static_cast<float>(TgParam(node, "MinSpacing", 4.0));
		const float minHeight = static_cast<float>(TgParam(node, "MinHeight", -1e9));
		const float maxHeight = static_cast<float>(TgParam(node, "MaxHeight", 1e9));
		const float maxSlope = static_cast<float>(TgParam(node, "MaxSlopeDegrees", 28.0));
		const float densityThreshold = static_cast<float>(TgParam(node, "DensityThreshold", 0.5));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;

		// the density gates a spot either at a threshold, or (Probabilistic) as the chance of keeping it: a 0.06 meadow grows a lone tree on 6 % of its spots
		const bool probabilistic = TgParam(node, "Probabilistic", 0.0) != 0.0;
		const uint32_t chanceSeed = seed * 2654435761u + 0x51ED27u;
		// Clumping: the density is multiplied by a noise of ClumpScale metres, so the objects gather in groves with glades between them
		const float clumping = std::clamp(static_cast<float>(TgParam(node, "Clumping", 0.0)), 0.0f, 1.0f);
		const float clumpScale = std::max(static_cast<float>(TgParam(node, "ClumpScale", 40.0)), 1.0f);
		const uint32_t clumpSeed = seed * 2246822519u + 0xC1u;
		auto densityAt = [density, clumping, clumpScale, clumpSeed](float x, float z)
		{
			const float d = density ? density->Sample(x, z) : 1.0f;
			if (clumping <= 0.0f) return d;
			const float groves = TerrainNoise::Fbm(x / clumpScale, z / clumpScale, 3, clumpSeed);
			const float t = std::clamp((groves - 0.38f) / 0.24f, 0.0f, 1.0f);
			return d * (1.0f - clumping + clumping * t * t * (3.0f - 2.0f * t));
		};
		// the chance of a spot: TerrainNoise::Hash (one round: where the density is low its survivors can line up in rows, kept so the worlds of
		// generator 4 rebuild the same) or, StrongHash, a fully mixed one
		const bool strong = TgParam(node, "StrongHash", 0.0) != 0.0;
		auto chanceAt = [strong, chanceSeed](float x, float z)
		{
			const int ix = static_cast<int>(std::floor(x * 8.0f)), iz = static_cast<int>(std::floor(z * 8.0f));
			if (!strong) return static_cast<float>(TerrainNoise::Hash(ix, iz, chanceSeed));
			uint64_t h = (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32 | static_cast<uint32_t>(iz)) ^ (static_cast<uint64_t>(chanceSeed) * 0x9E3779B97F4A7C15ull);
			h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;   // (the MurmurHash3 finalizer)
			return static_cast<float>(h >> 40) / 16777216.0f;
		};
		std::function<bool(float, float)> allow;
		if ((density || clumping > 0.0f) && probabilistic)
			allow = [densityAt, chanceAt](float x, float z) { return chanceAt(x, z) < densityAt(x, z); };
		else if (density || clumping > 0.0f)
			allow = [densityAt, densityThreshold](float x, float z) { return densityAt(x, z) >= densityThreshold; };
		const std::vector<TreePlacement> spots = ScatterTrees(heightmap, count, variants, minSpacing, seed, minHeight, maxHeight, maxSlope, allow);

		// rocks named by id: a weighted pick per spot (its own hash, so the spots and their yaw and scale are those without a palette)
		const std::vector<std::pair<std::string, double>> palette = ScatterPalette(node);
		double total = 0.0;
		for (const auto& entry : palette) total += entry.second;
		const uint32_t pickSeed = seed * 0x9E3779B1u + 0x2545F491u;
		auto pick = [&](float x, float z)
		{
			const int ix = static_cast<int>(std::floor(x * 8.0f)), iz = static_cast<int>(std::floor(z * 8.0f));
			uint64_t h = (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32 | static_cast<uint32_t>(iz)) ^ (static_cast<uint64_t>(pickSeed) * 0x9E3779B97F4A7C15ull);
			h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull; h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull; h ^= h >> 33;
			double t = static_cast<double>(h >> 40) / 16777216.0 * total;
			for (size_t i = 0; i < palette.size(); ++i)
			{
				t -= palette[i].second;
				if (t < 0.0) return static_cast<int>(i);
			}
			return static_cast<int>(palette.size()) - 1;
		};

		out.Items.reserve(spots.size());
		for (const TreePlacement& p : spots) out.Items.push_back({ { p.Position.x, p.Position.z }, p.Yaw, p.Scale, palette.empty() ? p.Variant : pick(p.Position.x, p.Position.z) });
		return true;
	}

	TgNodeSchema ScatterNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Scatter";
		s.Summary = "Places objects (trees, rocks) by dart throwing with a minimum spacing, gated by height, slope and a density mask. Rocks by id: add parameters \"Rock:<recipe>[/<formation>]\" (assets/data/rocks.json), each its weight.";
		s.Color = 0x968C32;
		s.Inputs = { { "height", TgPortKind::Field }, { "density", TgPortKind::Field, true, "0..1: a spot is kept where it is at least DensityThreshold" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "placements", TgPortKind::Placements } };
		s.Params = {
			{ "Count", 120, 0, 100000, true, "most objects placed" },
			{ "Variants", 1, 1, 16, true, "variant indices handed out (the caller gives them meaning)" },
			{ "MinSpacing", 4, 0.5, 40, false, "metres between objects" },
			{ "MinHeight", -1e9, -100, 1000, false, "metres" },
			{ "MaxHeight", 1e9, -100, 1000, false, "metres" },
			{ "MaxSlopeDegrees", 28, 0, 90, false, "" },
			{ "DensityThreshold", 0.5, 0, 1, false, "only with a density input" },
			{ "Probabilistic", 0, 0, 1, true, "1: the density is the chance of keeping a spot instead of a threshold" },
			{ "StrongHash", 0, 0, 1, true, "1: a fully mixed chance per spot (0 can line spots up in rows where the density is low)" },
			{ "Clumping", 0, 0, 1, false, "how strongly the objects gather in groves (the density times a noise)" },
			{ "ClumpScale", 40, 1, 500, false, "metres across a grove and the glade beside it" },
			{ "Seed", 0, 0, 100, true, "mixed with the world's seed" },
		};
		return s;
	}
}
