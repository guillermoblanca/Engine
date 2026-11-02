#include "GB/World/Graph/Nodes/RiverTraceNode.h"
#include "GB/World/FlowAccumulation.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace GB
{
	namespace
	{
		double Param(const TgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}

		struct Course
		{
			std::vector<uint32_t> Cells;   // flat indices, source first
		};

		bool Follow(const TgNode& node, const std::vector<const TgField*>& inputs, Course& course, std::string& error)
		{
			const std::string who = "river_trace node '" + node.Id + "'";
			if (inputs.size() < 2 || inputs[0] == nullptr || inputs[1] == nullptr) { error = who + " needs the height field and its flow accumulation connected"; return false; }
			const TgField& height = *inputs[0];
			const TgField& flow = *inputs[1];
			const TgField* outlets = inputs.size() > 2 ? inputs[2] : nullptr;
			if (height.N < 3 || flow.N != height.N) { error = who + ": the height and flow fields must be the same size (and at least 3x3)"; return false; }
			if (outlets && outlets->N != height.N) { error = who + ": the outlet mask is not the size of the height field"; return false; }

			const uint32_t n = height.N;
			const size_t count = height.Values.size();
			const float spacing = height.Spacing();
			const std::vector<int> down = FlowAccumulation::Directions(height.Values.data(), n);
			auto isOutlet = [&](size_t c) { return outlets && outlets->Values[c] > 0.5f; };

			// Cells from the lowest up: a cell's downstream neighbour is strictly lower, so it is settled before the cell is.
			std::vector<uint32_t> order(count);
			std::iota(order.begin(), order.end(), 0u);
			std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
			{
				return height.Values[a] != height.Values[b] ? height.Values[a] < height.Values[b] : a < b;
			});
			std::vector<int> target(count, -1);   // the cell where the water of this one ends: an outlet cell, or -1 when it leaves the map elsewhere
			std::vector<float> length(count, 0.0f);   // metres of flow line between the cell and that end
			for (uint32_t c : order)
			{
				if (isOutlet(c)) { target[c] = static_cast<int>(c); length[c] = 0.0f; continue; }
				const int d = down[c];
				if (d < 0) continue;   // drains off the map (a border cell): target -1, length 0
				const int di = static_cast<int>(c % n) - d % static_cast<int>(n), dj = static_cast<int>(c / n) - d / static_cast<int>(n);
				target[c] = target[static_cast<size_t>(d)];
				length[c] = length[static_cast<size_t>(d)] + spacing * std::sqrt(static_cast<float>(di * di + dj * dj));
			}

			// The source: the cell with the longest flow line to the outlet (to anywhere, without one) that is far enough from it.
			int outletCell = -1;
			for (size_t c = 0; c < count && outletCell < 0; ++c) if (isOutlet(c)) outletCell = static_cast<int>(c);
			const glm::vec2 outletAt = outletCell >= 0 ? height.SamplePosition(static_cast<uint32_t>(outletCell) % n, static_cast<uint32_t>(outletCell) / n) : glm::vec2(0.0f);
			const float minDistance = static_cast<float>(Param(node, "MinStartDistance", 0.3)) * height.Size;
			auto pick = [&](float farEnough)
			{
				int best = -1;
				float bestLength = 0.0f;
				for (size_t c = 0; c < count; ++c)
				{
					if (outletCell >= 0 ? target[c] != outletCell : target[c] != -1) continue;
					if (static_cast<int>(c) == outletCell || down[c] < 0) continue;
					if (outletCell >= 0 && glm::distance(height.SamplePosition(static_cast<uint32_t>(c) % n, static_cast<uint32_t>(c) / n), outletAt) < farEnough) continue;
					if (length[c] > bestLength) { bestLength = length[c]; best = static_cast<int>(c); }
				}
				return best;
			};
			int source = pick(minDistance);
			if (source < 0) source = pick(0.0f);   // nothing far enough: the longest there is
			if (source < 0) { error = who + ": no flow line reaches the outlet (is the field filled?)"; return false; }

			course.Cells.clear();
			for (int c = source; c >= 0; c = isOutlet(static_cast<size_t>(c)) ? -1 : down[static_cast<size_t>(c)])
			{
				course.Cells.push_back(static_cast<uint32_t>(c));
				if (course.Cells.size() > count) { error = who + ": the flow lines loop"; return false; }
			}
			if (course.Cells.size() < 2) { error = who + ": the river has fewer than two cells"; return false; }
			return true;
		}
	}

	bool RiverTraceNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		Course course;
		if (!Follow(node, inputs, course, error)) return false;
		const TgField& flow = *inputs[1];
		out.Resize(flow.N, flow.Size, flow.Center);
		for (uint32_t c : course.Cells) out.Values[c] = flow.Values[c];
		return true;
	}

	bool RiverTraceNode::Trace(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgPaths& out, std::string& error) const
	{
		Course course;
		if (!Follow(node, inputs, course, error)) return false;
		const TgField& height = *inputs[0];
		const TgField& flow = *inputs[1];
		const uint32_t n = height.N;

		std::vector<glm::vec2> raw;
		raw.reserve(course.Cells.size());
		for (uint32_t c : course.Cells) raw.push_back(height.SamplePosition(c % n, c / n));

		// Round the staircase of the D8 steps with a moving average whose window shrinks toward the ends, so the source and the outlet stay put.
		const int window = std::max(0, static_cast<int>(Param(node, "Smooth", 3.0)));
		const int m = static_cast<int>(raw.size());
		std::vector<glm::vec2> smooth(raw.size());
		for (int i = 0; i < m; ++i)
		{
			const int w = std::min({ window, i, m - 1 - i });
			glm::vec2 sum(0.0f);
			for (int k = i - w; k <= i + w; ++k) sum += raw[static_cast<size_t>(k)];
			smooth[static_cast<size_t>(i)] = sum / static_cast<float>(2 * w + 1);
		}

		// One point every `Spacing` metres of course, and the last one.
		const float spacing = std::max(0.5f, static_cast<float>(Param(node, "Spacing", 6.0)));
		TgPath path;
		auto keep = [&](int i) { path.Points.push_back(smooth[static_cast<size_t>(i)]); path.Values.push_back(flow.Values[course.Cells[static_cast<size_t>(i)]]); };
		keep(0);
		float sinceKept = 0.0f;
		for (int i = 1; i < m; ++i)
		{
			sinceKept += glm::distance(smooth[static_cast<size_t>(i)], smooth[static_cast<size_t>(i) - 1]);
			if (i == m - 1) { keep(i); break; }
			if (sinceKept >= spacing) { keep(i); sinceKept = 0.0f; }
		}
		if (path.Points.size() < 2) { error = "river_trace node '" + node.Id + "': the course is shorter than its spacing"; return false; }
		out.Items.push_back(std::move(path));
		return true;
	}
}
