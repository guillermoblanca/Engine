#include "GB/World/Graph/Nodes/TalusNode.h"
#include "GB/World/Talus.h"
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

		float SmoothStep(float a, float b, float x)
		{
			const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	TgNodeSchema TalusNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Erode";
		s.Summary = "Talus with hardness: cuts only what passes the angle of repose (cliffs in hard rock, slopes in soft), scree at the foot (E4).";
		s.Color = kErosion;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "hardness", TgPortKind::Field, true, "0 soft .. 1 hard rock (without it: noise patches and strata, see HardCover and StrataSpacing)" },
			{ "water", TgPortKind::Water, true, "water carved before: its lakes and its river do not move" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "scree", TgPortKind::Field }, { "cliff", TgPortKind::Field }, { "hardness", TgPortKind::Field },
			{ "cut", TgPortKind::Field } };
		s.Params = {
			{ "SoftAngle", 36, 20, 60, false, "degrees: the angle of repose of soft rock (hardness 0)" },
			{ "HardAngle", 70, 30, 89, false, "degrees: the angle of repose of hard rock (hardness 1): its cliffs" },
			{ "ScreeAngle", 33, 20, 45, false, "degrees: the angle the fallen material rests at, at the foot" },
			{ "ChannelArea", 2000, 0, 200000, false, "m2: a cell draining more is a channel and carries the scree away (0 = none)" },
			{ "Deposit", 1, 0, 1, true, "1: what falls rests at the foot as scree; 0: all of it is carried away" },
			{ "HardCover", 0.1, 0, 1, false, "without a hardness input: the fraction of the map in hard patches" },
			{ "NoiseScale", 250, 10, 3000, false, "metres: the size of the hard patches" },
			{ "Seed", 0, 0, 1000, true, "the noise of the hard patches and the beds" },
			{ "Edge", 0.08, 0, 1, false, "the soft edge of the patches (of the noise's range) and of the beds (of a period)" },
			{ "StrataSpacing", 0, 0, 200, false, "metres between hard beds (horizontal, by height); 0 = no beds" },
			{ "StrataHard", 0.35, 0, 1, false, "the fraction of each period that is a hard bed" },
			{ "StrataWarp", 0, 0, 100, false, "metres the beds wave up and down" },
			{ "StrataCover", 1, 0, 1, false, "the beds only on this fraction of the map (a noise; 1 = everywhere)" },
			{ "Steep", 0, 0, 1, false, "how much the hard patches and the districts of beds follow the steep ground instead of the noise" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool TalusNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (const double revision = TgParam(node, "Revision", 1.0); revision != 1.0)
		{
			error = Who(node) + ": Revision " + std::to_string(revision) + " is unknown (this engine has 1)";
			return false;
		}
		const TgValue* height = Input(inputs, 0, TgPortKind::Field);
		const TgValue* hardIn = Input(inputs, 1, TgPortKind::Field);
		const TgValue* water = Input(inputs, 2, TgPortKind::Water);
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		const TgField& h = height->Field;
		if (hardIn && hardIn->Field.N != h.N) { error = Who(node) + ": the hardness field is not the size of the height field"; return false; }
		const size_t count = h.Values.size();
		const float sp = h.Spacing();

		TgField& hard = FieldOut(outputs, "hardness", h);
		if (hardIn) hard.Values = hardIn->Field.Values;
		else
		{
			TalusHardnessParams hp;
			hp.Cover = static_cast<float>(TgParam(node, "HardCover", 0.1));
			hp.NoiseScale = static_cast<float>(TgParam(node, "NoiseScale", 250.0));
			hp.Seed = static_cast<uint32_t>(std::max(0.0, TgParam(node, "Seed", 0.0)));
			hp.Edge = static_cast<float>(TgParam(node, "Edge", 0.08));
			hp.StrataSpacing = static_cast<float>(TgParam(node, "StrataSpacing", 0.0));
			hp.StrataHard = static_cast<float>(TgParam(node, "StrataHard", 0.35));
			hp.StrataWarp = static_cast<float>(TgParam(node, "StrataWarp", 0.0));
			hp.StrataCover = static_cast<float>(TgParam(node, "StrataCover", 1.0));
			hp.Steep = static_cast<float>(TgParam(node, "Steep", 0.0));
			const glm::vec2 origin = h.Min();
			hard.Values = TalusHardness(h.Values.data(), h.N, sp, origin.x, origin.y, hp);
		}

		std::vector<uint8_t> fixed;
		if (water)
		{
			fixed.assign(count, 0);
			const WaterRaster wet = RasterizeWater(water->Water, h.N, h.Min(), sp);
			for (size_t c = 0; c < count; ++c) fixed[c] = wet.Wet(c) ? 1 : 0;
		}
		TalusOptions o;
		o.SoftAngle = static_cast<float>(TgParam(node, "SoftAngle", 36.0));
		o.HardAngle = static_cast<float>(TgParam(node, "HardAngle", 70.0));
		o.ScreeAngle = static_cast<float>(TgParam(node, "ScreeAngle", 33.0));
		o.ChannelArea = static_cast<float>(std::max(0.0, TgParam(node, "ChannelArea", 2000.0)));
		o.Deposit = TgParam(node, "Deposit", 1.0) >= 0.5;
		o.Hardness = hard.Values.data();
		o.Fixed = fixed.empty() ? nullptr : fixed.data();

		TgField& out = FieldOut(outputs, "height", h);
		TgField& scree = FieldOut(outputs, "scree", h);
		TgField& cliff = FieldOut(outputs, "cliff", h);
		TgField& cut = FieldOut(outputs, "cut", h);
		if (h.N < 3) { out.Values = h.Values; return true; }
		TalusResult r = TalusErode(h.Values.data(), h.N, sp, o);
		out.Values = std::move(r.Heights);
		scree.Values = std::move(r.Scree);
		cut.Values = std::move(r.Cut);
		// the cliffs: hard rock steeper than 45-55 degrees (central differences, one-sided on the border)
		const uint32_t n = h.N;
		for (uint32_t j = 0; j < n; ++j)
			for (uint32_t i = 0; i < n; ++i)
			{
				const uint32_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < n ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j + 1 < n ? j + 1 : j;
				const float gx = (out.At(i1, j) - out.At(i0, j)) / (static_cast<float>(i1 - i0) * sp);
				const float gz = (out.At(i, j1) - out.At(i, j0)) / (static_cast<float>(j1 - j0) * sp);
				const float deg = std::atan(std::sqrt(gx * gx + gz * gz)) * 57.2957795f;
				const size_t c = static_cast<size_t>(j) * n + i;
				cliff.Values[c] = SmoothStep(45.0f, 55.0f, deg) * SmoothStep(0.3f, 0.7f, hard.Values[c]);
			}
		return true;
	}
}
