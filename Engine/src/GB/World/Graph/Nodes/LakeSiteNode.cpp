#include "GB/World/Graph/Nodes/LakeSiteNode.h"
#include "GB/World/Graph/Nodes/LakeSite.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{

		bool Check(const TgNode& node, const std::vector<const TgField*>& inputs, std::string& error)
		{
			if (inputs.empty() || inputs[0] == nullptr) { error = "lake_site node '" + node.Id + "' has no height input connected"; return false; }
			if (inputs[0]->N < 3) { error = "lake_site node '" + node.Id + "' got a field too small to hold a lake"; return false; }
			return true;
		}
	}

	LakeSiteResult FindLakeSite(const TgField& field, int coarse, int margin)
	{
		coarse = std::max(coarse, 5);
		margin = std::clamp(margin, 1, (coarse - 1) / 2 - 1);
		const float step = field.Size / static_cast<float>(coarse - 1);
		const glm::vec2 lo = field.Min();
		auto position = [&](int i, int j) { return lo + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * step; };
		auto smoothed = [&](int i, int j)
		{
			float sum = 0.0f;
			for (int dj = -1; dj <= 1; ++dj)
				for (int di = -1; di <= 1; ++di)
				{
					const glm::vec2 p = position(std::clamp(i + di, 0, coarse - 1), std::clamp(j + dj, 0, coarse - 1));
					sum += field.Sample(p.x, p.y);
				}
			return sum / 9.0f;
		};
		LakeSiteResult best;
		float lowest = 1e9f;
		int bi = margin, bj = margin;
		for (int j = margin; j < coarse - margin; ++j)   // (first lowest in row-major order wins a tie)
			for (int i = margin; i < coarse - margin; ++i)
			{
				const float h = smoothed(i, j);
				if (h < lowest) { lowest = h; bi = i; bj = j; }
			}
		best.Position = position(bi, bj);
		best.Height = lowest;
		return best;
	}

	bool LakeSiteNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		if (!Check(node, inputs, error)) return false;
		const TgField& in = *inputs[0];
		const LakeSiteResult site = FindLakeSite(in, static_cast<int>(TgParam(node, "Coarse", 65.0)), static_cast<int>(TgParam(node, "Margin", 12.0)));

		out.Resize(in.N, in.Size, in.Center);
		const float spacing = in.Spacing();
		const glm::vec2 cell = (site.Position - in.Min()) / spacing;
		const uint32_t ci = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(cell.x)), 0, static_cast<int>(in.N) - 1));
		const uint32_t cj = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(cell.y)), 0, static_cast<int>(in.N) - 1));
		out.At(ci, cj) = 1.0f;
		return true;
	}

	bool LakeSiteNode::Trace(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgPaths& out, std::string& error) const
	{
		if (!Check(node, inputs, error)) return false;
		const LakeSiteResult site = FindLakeSite(*inputs[0], static_cast<int>(TgParam(node, "Coarse", 65.0)), static_cast<int>(TgParam(node, "Margin", 12.0)));
		TgPath path;
		path.Points.push_back(site.Position);
		path.Values.push_back(site.Height);
		out.Items.push_back(std::move(path));
		return true;
	}

	TgNodeSchema LakeSiteNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Where the lake goes: the broadest low basin away from the borders (an outlet mask and a one-point path).";
		s.Color = 0x288282;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "outlets", TgPortKind::Field }, { "site", TgPortKind::Paths } };
		s.Params = {
			{ "Coarse", 65, 5, 257, true, "cells per side of the smoothed grid" },
			{ "Margin", 12, 1, 60, true, "coarse cells kept clear of the border" },
		};
		return s;
	}
}
