#include "GB/World/Graph/Nodes/FluvialErosionNode.h"
#include "GB/World/StreamPower.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr uint32_t kErosion = 0x8a6a3c;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		TgField& FieldOut(std::map<std::string, TgValue>& outputs, const char* name, const TgField& like)
		{
			TgValue& v = outputs[name];
			v.Kind = TgPortKind::Field;
			v.Field.Resize(like.N, like.Size, like.Center);
			return v.Field;
		}
	}

	TgNodeSchema FluvialErosionNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Erode";
		s.Summary = "Stream-power incision (implicit, with uplift and hillslope diffusion): branching gullies, sharp crests, V valleys (E2).";
		s.Color = kErosion;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "uplift", TgPortKind::Field, true, "multiplies Uplift per cell (a mask: where the range keeps rising)" },
			{ "hardness", TgPortKind::Field, true, "0 soft .. 1 hard rock: the erodibility times mix(1, HardErodibility, hardness)" },
			{ "precipitation", TgPortKind::Field, true, "rain of each cell relative to 1 (weights the drainage area)" },
			{ "water", TgPortKind::Water, true, "water carved before: its lakes and its river are base level" },
			{ "outlets", TgPortKind::Field, true, "cells > 0.5 are base level too" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "area", TgPortKind::Field }, { "eroded", TgPortKind::Field }, { "coarse", TgPortKind::Field } };
		s.Params = {
			{ "Erodibility", 4.0, 0, 40, false, "K, in 1e-5 per year: incision = K sqrt(A) S (m/yr, A in m2)" },
			{ "Uplift", 0.0, 0, 10, false, "mm per year (times the uplift input)" },
			{ "Diffusion", 0.003, 0, 0.1, false, "hillslope diffusivity, m2 per year" },
			{ "CriticalSlope", 0, 0, 80, false, "degrees: the diffusion grows as the slope nears it, Kd / (1 - (S/Sc)^2) (Roering): steep walls wear back, gentle hollows keep. 0 = linear" },
			{ "CriticalSlopeCap", 0.9, 0.5, 0.99, false, "S/Sc at which that factor stops growing (0.9: at most 5.3 Kd; more sub-steps the higher)" },
			{ "TimeStep", 1000, 10, 20000, false, "years per iteration" },
			{ "HardErodibility", 0.25, 0, 1, false, "the erodibility's factor at hardness 1" },
			{ "MultipleFlow", 1, 0, 1, true, "1: the drainage area by multiple flow (channels where the ground converges); 0: along single receivers (D8)" },
			{ "CoarseResolution", 257, 33, 1025, true, "samples per side of the coarse stage (the preview's resolution)" },
			{ "CoarseIterations", 60, 0, 400, true, "iterations of the coarse stage (they carve the valleys)" },
			{ "MidIterations", 12, 0, 100, true, "iterations of each stage between the coarse and the last" },
			{ "FineIterations", 4, 0, 100, true, "iterations at the field's resolution (the detail)" },
			{ "FineDrainageEvery", 1, 1, 10, true, "the finer stages recompute the drainage every this many iterations" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool FluvialErosionNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (const double revision = TgParam(node, "Revision", 1.0); revision != 1.0)
		{
			error = Who(node) + ": Revision " + std::to_string(revision) + " is unknown (this engine has 1)";
			return false;
		}
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* uplift = Input(inputs, 1, TgPortKind::Field);
		const TgValue* hardness = Input(inputs, 2, TgPortKind::Field);
		const TgValue* rain = Input(inputs, 3, TgPortKind::Field);
		const TgValue* water = Input(inputs, 4, TgPortKind::Water);
		const TgValue* outlets = Input(inputs, 5, TgPortKind::Field);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		for (const TgValue* v : { uplift, hardness, rain, outlets })
			if (v && v->Field.N != h.N) { error = Who(node) + ": an input field is not the size of the height field"; return false; }
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
		StreamPowerOptions o;
		o.Erodibility = static_cast<float>(TgParam(node, "Erodibility", 4.0) * 1e-5);
		o.Uplift = static_cast<float>(TgParam(node, "Uplift", 0.0) * 1e-3);
		o.Diffusion = static_cast<float>(TgParam(node, "Diffusion", 0.003));
		if (const double sc = TgParam(node, "CriticalSlope", 0.0); sc > 0.0)
			o.CriticalSlope = static_cast<float>(std::tan(std::min(sc, 89.0) * 3.14159265358979323846 / 180.0));
		o.CriticalSlopeCap = static_cast<float>(TgParam(node, "CriticalSlopeCap", 0.9));
		o.TimeStep = static_cast<float>(TgParam(node, "TimeStep", 1000.0));
		o.HardErodibility = static_cast<float>(TgParam(node, "HardErodibility", 0.25));
		o.MultipleFlow = TgParam(node, "MultipleFlow", 1.0) >= 0.5;
		o.UpliftMap = uplift ? uplift->Field.Values.data() : nullptr;
		o.Hardness = hardness ? hardness->Field.Values.data() : nullptr;
		o.Precipitation = rain ? rain->Field.Values.data() : nullptr;
		o.Outlets = sinks.empty() ? nullptr : sinks.data();
		StreamPowerCascade cascade;
		cascade.CoarseN = static_cast<uint32_t>(std::max(3.0, TgParam(node, "CoarseResolution", 257.0)));
		cascade.CoarseIterations = std::max(0, static_cast<int>(TgParam(node, "CoarseIterations", 60.0)));
		cascade.MidIterations = std::max(0, static_cast<int>(TgParam(node, "MidIterations", 12.0)));
		cascade.FineIterations = std::max(0, static_cast<int>(TgParam(node, "FineIterations", 4.0)));
		cascade.FineDrainageEvery = std::max(1, static_cast<int>(TgParam(node, "FineDrainageEvery", 1.0)));

		TgField& out = FieldOut(outputs, "height", h);
		TgField& area = FieldOut(outputs, "area", h);
		TgField& eroded = FieldOut(outputs, "eroded", h);
		TgField& coarse = FieldOut(outputs, "coarse", h);
		if (h.N < 3)
		{
			out.Values = h.Values;
			coarse.Values = h.Values;
			return true;
		}
		StreamPowerCascadeResult r = StreamPowerErodeCascade(h.Values.data(), h.N, h.Spacing(), o, cascade);
		out.Values = std::move(r.Heights);
		// the area of the ground as it ends (the last iteration's drainage is from before its incision)
		DrainageOptions dopt;
		dopt.Outlets = o.Outlets;
		dopt.Precipitation = o.Precipitation;
		DrainageResult last = ComputeDrainage(out.Values.data(), h.N, h.Spacing(), dopt);
		if (o.MultipleFlow) AccumulateMultipleFlowArea(last, o.Precipitation);
		area.Values = std::move(last.Area);
		for (size_t c = 0; c < count; ++c) eroded.Values[c] = h.Values[c] - out.Values[c];
		coarse.Values = r.CoarseN == h.N ? r.Coarse : ResampleGrid(r.Coarse.data(), r.CoarseN, h.N);
		return true;
	}
}
