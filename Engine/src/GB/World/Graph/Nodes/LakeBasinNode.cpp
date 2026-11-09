#include "GB/World/Graph/Nodes/LakeBasinNode.h"
#include "GB/World/Water/WaterCarver.h"
#include "GB/World/Graph/Nodes/LakeSite.h"
#include "GB/World/Graph/TerrainGraph.h"

namespace GB
{

	bool LakeBasinNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (inputs.empty() || inputs[0] == nullptr) { error = "lake_basin node '" + node.Id + "' has no height input connected"; return false; }
		const TgField& in = *inputs[0];
		if (in.N < 3) { error = "lake_basin node '" + node.Id + "' got a field too small to hold a lake"; return false; }

		const LakeSiteResult site = FindLakeSite(in, static_cast<int>(TgParam(node, "Coarse", 65.0)), static_cast<int>(TgParam(node, "Margin", 12.0)));
		LakeShape lake;
		lake.Center = site.Position;
		lake.Radius = static_cast<float>(TgParam(node, "Radius", 22.0));
		lake.Depth = static_cast<float>(TgParam(node, "Depth", 3.0));
		lake.Level = site.Height + static_cast<float>(TgParam(node, "LevelAbove", 1.0));
		lake.BankSlope = static_cast<float>(TgParam(node, "BankSlope", 0.3));
		lake.Outline = MakeLakeOutline(ctx.Seed, static_cast<float>(TgParam(node, "Irregularity", 0.0)));

		out = in;
		for (uint32_t j = 0; j < in.N; ++j)
			for (uint32_t i = 0; i < in.N; ++i)
				out.At(i, j) = ApplyLakeBasin(in.At(i, j), in.SamplePosition(i, j), lake);
		return true;
	}

	TgNodeSchema LakeBasinNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Digs the lake's basin at the lake site (for analysing where the rain goes; lake_carve digs the world's lake).";
		s.Color = 0x288282;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Coarse", 65, 5, 257, true, "cells per side of the smoothed grid the site is looked for on" },
			{ "Margin", 12, 1, 60, true, "coarse cells kept clear of the border" },
			{ "Radius", 22, 2, 150, false, "metres" },
			{ "Depth", 3, 0, 20, false, "metres below the surface" },
			{ "LevelAbove", 1, 0, 10, false, "metres the surface stands above the basin" },
			{ "BankSlope", 0.3, 0, 2, false, "steepest rise of the bank (m/m)" },
			{ "Irregularity", 0, 0, 0.3, false, "0 = round shore" },
		};
		return s;
	}
}
