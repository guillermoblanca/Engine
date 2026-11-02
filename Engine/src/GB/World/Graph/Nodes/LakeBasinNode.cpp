#include "GB/World/Graph/Nodes/LakeBasinNode.h"
#include "GB/Water/WaterCarver.h"
#include "GB/World/Graph/Nodes/LakeSite.h"
#include "GB/World/Graph/TerrainGraph.h"

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

	bool LakeBasinNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "lake_basin node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N < 3) { error = "lake_basin node '" + node.Id + "' got a field too small to hold a lake"; return false; }

		const LakeSiteResult site = FindLakeSite(in, static_cast<int>(Param(node, "Coarse", 65.0)), static_cast<int>(Param(node, "Margin", 12.0)));
		LakeShape lake;
		lake.Center = site.Position;
		lake.Radius = static_cast<float>(Param(node, "Radius", 22.0));
		lake.Depth = static_cast<float>(Param(node, "Depth", 3.0));
		lake.Level = site.Height + static_cast<float>(Param(node, "LevelAbove", 1.0));
		lake.BankSlope = static_cast<float>(Param(node, "BankSlope", 0.3));
		lake.Outline = MakeLakeOutline(ctx.Seed, static_cast<float>(Param(node, "Irregularity", 0.0)));

		out = in;
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i)
				out.At(i, j) = ApplyLakeBasin(in.At(i, j), in.SamplePosition(i, j), lake);
		return true;
	}
}
