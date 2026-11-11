#include "GB/World/Graph/Nodes/DepositNode.h"
#include "GB/World/Deposit.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Water/WaterRaster.h"
#include <algorithm>

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

	TgNodeSchema DepositNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Erode";
		s.Summary = "Deposit: valleys filled part of the way up (gullies shallower, crests kept) and fans where a channel leaves the mountain (E3).";
		s.Color = kErosion;
		s.Inputs = { { "height", TgPortKind::Field },
			{ "hardness", TgPortKind::Field, true, "0 soft .. 1 hard rock (talus' hardness): hard rock is neither filled nor eased" },
			{ "water", TgPortKind::Water, true, "water carved before: its lakes and its river do not move" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "alluvium", TgPortKind::Field }, { "fan", TgPortKind::Field }, { "eased", TgPortKind::Field } };
		s.Params = {
			{ "Fill", 0.3, 0, 0.95, false, "the fraction of the way each valley is filled up to its sides (0 = no fill)" },
			{ "FillRadius", 50, 0, 500, false, "metres: the valleys filled are those narrower than about twice this" },
			{ "FanArea", 20000, 0, 2000000, false, "m2 a channel drains to lay a fan where it leaves the mountain (0 = no fans)" },
			{ "FanAngle", 5, 1, 20, false, "degrees: the slope of a fan (a channel gentler than this below an apex)" },
			{ "FanApexAngle", 8, 1, 45, false, "degrees: the slope of the channel above an apex, at least" },
			{ "FanReach", 60, 5, 1000, false, "metres: the reach the slopes above and below an apex are measured over" },
			{ "FanDepth", 1, 0, 50, false, "metres of its basin a fan holds (volume = this x the area draining to the apex)" },
			{ "EaseAngle", 0, 0, 60, false, "degrees the long slopes ease to by a cut (0 = no easing)" },
			{ "ReposeAngle", 36, 10, 60, false, "degrees: the angle of repose of the talus before (its SoftAngle)" },
			{ "HardAngle", 55, 10, 89, false, "degrees: the talus' HardAngle" },
			{ "EaseStart", 15, 0, 1000, false, "metres of slope (m2 drained per m of contour) the easing begins at" },
			{ "EaseFull", 60, 0, 2000, false, "metres of slope the easing is full at" },
			{ "ChannelArea", 2000, 0, 200000, false, "m2: a channel is not eased" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool DepositNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
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

		std::vector<uint8_t> fixed;
		if (water)
		{
			fixed.assign(count, 0);
			const WaterRaster wet = RasterizeWater(water->Water, h.N, h.Min(), sp);
			for (size_t c = 0; c < count; ++c) fixed[c] = wet.Wet(c) ? 1 : 0;
		}
		DepositOptions o;
		o.Hardness = hardIn ? hardIn->Field.Values.data() : nullptr;
		o.Fixed = fixed.empty() ? nullptr : fixed.data();
		o.Fill = static_cast<float>(TgParam(node, "Fill", 0.3));
		o.FillRadius = static_cast<float>(std::max(0.0, TgParam(node, "FillRadius", 50.0)));
		o.FanArea = static_cast<float>(std::max(0.0, TgParam(node, "FanArea", 20000.0)));
		o.FanAngle = static_cast<float>(TgParam(node, "FanAngle", 5.0));
		o.FanApexAngle = static_cast<float>(TgParam(node, "FanApexAngle", 8.0));
		o.FanReach = static_cast<float>(std::max(0.0, TgParam(node, "FanReach", 60.0)));
		o.FanDepth = static_cast<float>(std::max(0.0, TgParam(node, "FanDepth", 1.0)));
		o.EaseAngle = static_cast<float>(std::max(0.0, TgParam(node, "EaseAngle", 0.0)));
		o.ReposeAngle = static_cast<float>(TgParam(node, "ReposeAngle", 36.0));
		o.HardAngle = static_cast<float>(TgParam(node, "HardAngle", 55.0));
		o.EaseStart = static_cast<float>(std::max(0.0, TgParam(node, "EaseStart", 15.0)));
		o.EaseFull = static_cast<float>(std::max(0.0, TgParam(node, "EaseFull", 60.0)));
		o.ChannelArea = static_cast<float>(std::max(0.0, TgParam(node, "ChannelArea", 2000.0)));

		TgField& out = FieldOut(outputs, "height", h);
		TgField& alluvium = FieldOut(outputs, "alluvium", h);
		TgField& fan = FieldOut(outputs, "fan", h);
		TgField& eased = FieldOut(outputs, "eased", h);
		if (h.N < 3) { out.Values = h.Values; return true; }
		DepositResult r = DepositSediment(h.Values.data(), h.N, sp, o);
		out.Values = std::move(r.Heights);
		alluvium.Values = std::move(r.Alluvium);
		fan.Values = std::move(r.Fan);
		eased.Values = std::move(r.Eased);
		return true;
	}
}
