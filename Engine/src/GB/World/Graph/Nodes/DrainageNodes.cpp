#include "GB/World/Graph/Nodes/DrainageNodes.h"
#include "GB/World/Drainage.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GB
{
	namespace
	{
		constexpr uint32_t kWater = 0x288282;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		bool CheckRevision(const TgNode& node, std::string& error)
		{
			const double revision = TgParam(node, "Revision", 1.0);
			if (revision == 1.0) return true;
			error = Who(node) + ": Revision " + std::to_string(revision) + " is unknown (this engine has 1)";
			return false;
		}

		TgField& FieldOut(std::map<std::string, TgValue>& outputs, const char* name, const TgField& like)
		{
			TgValue& v = outputs[name];
			v.Kind = TgPortKind::Field;
			v.Field.Resize(like.N, like.Size, like.Center);
			return v.Field;
		}

		float Above(float base, float epsilon)
		{
			const float v = base + epsilon;
			return v > base ? v : std::nextafter(base, std::numeric_limits<float>::infinity());
		}
	}

	// ------------------------------------------------------------------ drainage
	TgNodeSchema DrainageNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "One Priority-Flood: where each cell drains, in what order, how much area, and the level water stands at in each hollow (E1).";
		s.Color = kWater;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "water", TgPortKind::Water, true, "water carved before: its lakes and its river are outlets" },
			{ "outlets", TgPortKind::Field, true, "cells > 0.5 are outlets too" },
			{ "precipitation", TgPortKind::Field, true, "rain of each cell relative to 1 (weights the area)" } };
		s.Outputs = { { "filled", TgPortKind::Field }, { "level", TgPortKind::Field }, { "area", TgPortKind::Field },
			{ "receiver", TgPortKind::Field }, { "order", TgPortKind::Field } };
		s.Params = {
			{ "Epsilon", 0.001, 0, 0.05, false, "metres the filled ground rises per cell across a hollow" },
			{ "Receivers", 1, 0, 1, true, "1: steepest descent among the cells drained before; 0: the cell it was flooded from" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool DrainageNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* outlets = Input(inputs, 2, TgPortKind::Field);
		const TgValue* rain = Input(inputs, 3, TgPortKind::Field);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		if (outlets && outlets->Field.N != h.N) { error = Who(node) + ": the outlet mask is not the size of the height field"; return false; }
		if (rain && rain->Field.N != h.N) { error = Who(node) + ": the precipitation is not the size of the height field"; return false; }
		const size_t count = h.Values.size();

		std::vector<uint8_t> sinks;
		if (water || outlets)
		{
			sinks.assign(count, 0);
			if (water)
			{
				const WaterRaster wet = RasterizeWater(water->Water, h.N, h.Min(), h.Spacing());
				for (size_t c = 0; c < count; ++c) sinks[c] = wet.Wet(c) ? 1 : 0;
			}
			if (outlets)
				for (size_t c = 0; c < count; ++c) if (outlets->Field.Values[c] > 0.5f) sinks[c] = 1;
		}
		DrainageOptions options;
		options.Epsilon = static_cast<float>(TgParam(node, "Epsilon", 0.001));
		options.ReceiverMode = TgParam(node, "Receivers", 1.0) >= 0.5 ? 1 : 0;
		options.Outlets = sinks.empty() ? nullptr : sinks.data();
		options.Precipitation = rain ? rain->Field.Values.data() : nullptr;
		const DrainageResult d = ComputeDrainage(h.Values.data(), h.N, h.Spacing(), options);

		FieldOut(outputs, "filled", h).Values = d.Filled;
		FieldOut(outputs, "level", h).Values = d.Level;
		FieldOut(outputs, "area", h).Values = d.Area;
		TgField& receiver = FieldOut(outputs, "receiver", h);
		TgField& order = FieldOut(outputs, "order", h);
		if (d.N == 0)   // (a field of one sample: it is its own outlet)
		{
			outputs["filled"].Field.Values = h.Values;
			outputs["level"].Field.Values = h.Values;
			std::fill(receiver.Values.begin(), receiver.Values.end(), -1.0f);
			return true;
		}
		for (size_t c = 0; c < count; ++c) receiver.Values[c] = static_cast<float>(d.Receiver[c]);
		for (size_t k = 0; k < count; ++k) order.Values[d.Order[k]] = static_cast<float>(k);
		return true;
	}

	// ------------------------------------------------------------------ lake_fill
	TgNodeSchema LakeFillNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Water";
		s.Summary = "Fills the hollows of a drainage: the big ones become lakes at their overflow level (shore on the contour), the rest drain (E1).";
		s.Color = kWater;
		s.Inputs = { { "height", TgPortKind::Field, false, "the ground the drainage was computed on" },
			{ "level", TgPortKind::Field, false, "drainage: level" }, { "receiver", TgPortKind::Field, false, "drainage: receiver" },
			{ "order", TgPortKind::Field, false, "drainage: order" },
			{ "water", TgPortKind::Water, true, "water carved before (kept; the lakes are added to it)" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water }, { "lakes", TgPortKind::Field },
			{ "depth", TgPortKind::Field }, { "raised", TgPortKind::Field } };
		s.Params = {
			{ "MinDepth", 1.0, 0, 50, false, "metres: a shallower hollow is filled, not a lake" },
			{ "MinArea", 2000, 0, 200000, false, "m2: a smaller hollow is filled, not a lake" },
			{ "MaxLakes", 16, 0, 64, true, "the largest this many hollows (by area) become lakes" },
			{ "Epsilon", 0.001, 0, 0.05, false, "metres a filled cell rises above the one it drains to" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool LakeFillNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* levelIn = Input(inputs, 1, TgPortKind::Field);
		const TgValue* receiverIn = Input(inputs, 2, TgPortKind::Field);
		const TgValue* orderIn = Input(inputs, 3, TgPortKind::Field);
		const TgValue* water = Input(inputs, 4, TgPortKind::Water);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!levelIn || !receiverIn || !orderIn) { error = Who(node) + ": inputs 'level', 'receiver' and 'order' come from a drainage node"; return false; }
		const TgField& hf = height->Field;
		const uint32_t n = hf.N;
		const size_t count = hf.Values.size();
		if (levelIn->Field.N != n || receiverIn->Field.N != n || orderIn->Field.N != n) { error = Who(node) + ": the drainage is not the size of the height field"; return false; }
		const std::vector<float>& h = hf.Values;
		const std::vector<float>& level = levelIn->Field.Values;

		// the drainage back as integers: every receiver a neighbour (or -1), the ranks a permutation
		std::vector<int32_t> receiver(count);
		std::vector<uint32_t> order(count, std::numeric_limits<uint32_t>::max());
		for (size_t c = 0; c < count; ++c)
		{
			const float r = receiverIn->Field.Values[c];
			const float k = orderIn->Field.Values[c];
			if (!(r >= -1.0f && r < static_cast<float>(count)) || !(k >= 0.0f && k < static_cast<float>(count))) { error = Who(node) + ": 'receiver' or 'order' is not a drainage's"; return false; }
			receiver[c] = static_cast<int32_t>(r);
			const size_t rank = static_cast<size_t>(k);
			if (order[rank] != std::numeric_limits<uint32_t>::max()) { error = Who(node) + ": 'order' is not a drainage's (a rank repeats)"; return false; }
			order[rank] = static_cast<uint32_t>(c);
		}

		const float spacing = hf.Spacing(), cell = spacing * spacing;
		const float minDepth = static_cast<float>(TgParam(node, "MinDepth", 1.0)), minArea = static_cast<float>(TgParam(node, "MinArea", 2000.0));
		const int maxLakes = std::max(0, static_cast<int>(TgParam(node, "MaxLakes", 16.0)));
		const float epsilon = static_cast<float>(TgParam(node, "Epsilon", 0.001));

		// the hollows: the cells the drainage raised (its level above the ground, not an outlet), 8-connected. Two such cells side by side have
		// the same level (the water of the higher would run into the lower), so each hollow has one level.
		std::vector<uint8_t> raised(count, 0);
		for (size_t c = 0; c < count; ++c) raised[c] = receiver[c] >= 0 && level[c] > h[c] ? 1 : 0;
		std::vector<uint32_t> rank(count);
		for (size_t k = 0; k < count; ++k) rank[order[k]] = static_cast<uint32_t>(k);
		struct Hollow { std::vector<uint32_t> Cells; float Level = 0.0f, MaxDepth = 0.0f; double Volume = 0.0; uint32_t First = 0; };
		std::vector<Hollow> hollows;
		std::vector<int32_t> hollowOf(count, -1);
		std::vector<uint32_t> stack;
		for (size_t c0 = 0; c0 < count; ++c0)
		{
			if (!raised[c0] || hollowOf[c0] >= 0) continue;
			Hollow hollow;
			hollow.Level = level[c0];
			hollow.First = static_cast<uint32_t>(c0);
			const int32_t id = static_cast<int32_t>(hollows.size());
			stack.assign(1, static_cast<uint32_t>(c0));
			hollowOf[c0] = id;
			while (!stack.empty())
			{
				const uint32_t c = stack.back();
				stack.pop_back();
				hollow.Cells.push_back(c);
				hollow.MaxDepth = std::max(hollow.MaxDepth, level[c] - h[c]);
				hollow.Volume += static_cast<double>(level[c] - h[c]) * cell;
				if (rank[c] < rank[hollow.First]) hollow.First = c;
				const int ci = static_cast<int>(c % n), cj = static_cast<int>(c / n);
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int ni = ci + di, nj = cj + dj;
						if ((di == 0 && dj == 0) || ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
						const uint32_t m = static_cast<uint32_t>(nj) * n + static_cast<uint32_t>(ni);
						if (raised[m] && hollowOf[m] < 0) { hollowOf[m] = id; stack.push_back(m); }
					}
			}
			std::sort(hollow.Cells.begin(), hollow.Cells.end());
			hollows.push_back(std::move(hollow));
		}

		// which hollows are lakes: deep and large enough, the largest first (ties: the first in the map)
		std::vector<size_t> candidates;
		for (size_t k = 0; k < hollows.size(); ++k)
			if (hollows[k].MaxDepth >= minDepth && static_cast<float>(hollows[k].Cells.size()) * cell >= minArea) candidates.push_back(k);
		std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) { return hollows[a].Cells.size() > hollows[b].Cells.size(); });
		if (candidates.size() > static_cast<size_t>(maxLakes)) candidates.resize(static_cast<size_t>(maxLakes));
		std::vector<uint8_t> isLake(hollows.size(), 0);
		for (const size_t k : candidates) isLake[k] = 1;
		auto lakeCell = [&](size_t c) { return hollowOf[c] >= 0 && isLake[static_cast<size_t>(hollowOf[c])]; };

		// the ground: an outlet and a lake keep theirs; every other cell, in the drainage order, rises just above the one it drains to when it
		// is not already above it (inside a filled hollow: the spill height plus an epsilon per cell back from the spill point)
		TgField& ground = FieldOut(outputs, "height", hf);
		for (size_t k = 0; k < count; ++k)
		{
			const uint32_t c = order[k];
			const int32_t r = receiver[c];
			if (r < 0 || lakeCell(c)) { ground.Values[c] = h[c]; continue; }
			const float below = ground.Values[static_cast<size_t>(r)];
			ground.Values[c] = h[c] > below ? h[c] : Above(below, epsilon);
		}

		TgField& lakes = FieldOut(outputs, "lakes", hf);
		TgField& depth = FieldOut(outputs, "depth", hf);
		TgField& lift = FieldOut(outputs, "raised", hf);
		for (size_t c = 0; c < count; ++c)
		{
			lift.Values[c] = ground.Values[c] - h[c];
			if (!lakeCell(c)) continue;
			lakes.Values[c] = 1.0f;
			depth.Values[c] = level[c] - h[c];
		}

		TgValue& out = outputs["water"];
		out.Kind = TgPortKind::Water;
		if (water) out.Water = water->Water;
		for (const size_t k : candidates)
		{
			const Hollow& hollow = hollows[k];
			int i0 = static_cast<int>(n), j0 = static_cast<int>(n), i1 = -1, j1 = -1;
			for (const uint32_t c : hollow.Cells)
			{
				const int i = static_cast<int>(c % n), j = static_cast<int>(c / n);
				i0 = std::min(i0, i); i1 = std::max(i1, i); j0 = std::min(j0, j); j1 = std::max(j1, j);
			}
			LakeBody lake;
			lake.Present = true;
			lake.Level = hollow.Level;
			lake.Mask.Origin = hf.SamplePosition(static_cast<uint32_t>(i0), static_cast<uint32_t>(j0));
			lake.Mask.Spacing = spacing;
			lake.Mask.W = i1 - i0 + 1;
			lake.Mask.H = j1 - j0 + 1;
			lake.Mask.Cells.assign(static_cast<size_t>(lake.Mask.W) * static_cast<size_t>(lake.Mask.H), 0);
			lake.Center = 0.5f * (hf.SamplePosition(static_cast<uint32_t>(i0), static_cast<uint32_t>(j0)) + hf.SamplePosition(static_cast<uint32_t>(i1), static_cast<uint32_t>(j1)));
			float reach = 0.0f;
			for (const uint32_t c : hollow.Cells)
			{
				const int i = static_cast<int>(c % n), j = static_cast<int>(c / n);
				lake.Mask.Cells[static_cast<size_t>(j - j0) * static_cast<size_t>(lake.Mask.W) + static_cast<size_t>(i - i0)] = 1;
				reach = std::max(reach, glm::distance(lake.Center, hf.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j))));
			}
			lake.Radius = reach + spacing;   // (the circle that holds every cell of it and the half cell around them)
			const int32_t spill = receiver[hollow.First];
			lake.Outlet = spill >= 0 ? hf.SamplePosition(static_cast<uint32_t>(spill) % n, static_cast<uint32_t>(spill) / n) : lake.Center;
			lake.Area = static_cast<float>(hollow.Cells.size()) * cell;
			lake.Volume = static_cast<float>(hollow.Volume);
			lake.MaxDepth = hollow.MaxDepth;
			out.Water.AddLake(std::move(lake));
		}
		return true;
	}
}
