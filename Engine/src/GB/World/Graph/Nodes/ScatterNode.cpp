#include "GB/World/Graph/Nodes/ScatterNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"

namespace GB
{
	namespace
	{
		double Param(const TgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}
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

		const int count = static_cast<int>(Param(node, "Count", 120.0));
		const int variants = std::max(1, static_cast<int>(Param(node, "Variants", 1.0)));
		const float minSpacing = static_cast<float>(Param(node, "MinSpacing", 4.0));
		const float minHeight = static_cast<float>(Param(node, "MinHeight", -1e9));
		const float maxHeight = static_cast<float>(Param(node, "MaxHeight", 1e9));
		const float maxSlope = static_cast<float>(Param(node, "MaxSlopeDegrees", 28.0));
		const float densityThreshold = static_cast<float>(Param(node, "DensityThreshold", 0.5));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(Param(node, "Seed", 0.0)) * 97u;

		const auto allow = density ? std::function<bool(float, float)>([density, densityThreshold](float x, float z) { return density->Sample(x, z) >= densityThreshold; })
			: std::function<bool(float, float)>();
		const std::vector<TreePlacement> spots = ScatterTrees(heightmap, count, variants, minSpacing, seed, minHeight, maxHeight, maxSlope, allow);

		out.Items.reserve(spots.size());
		for (const TreePlacement& p : spots) out.Items.push_back({ { p.Position.x, p.Position.z }, p.Yaw, p.Scale, p.Variant });
		return true;
	}
}
