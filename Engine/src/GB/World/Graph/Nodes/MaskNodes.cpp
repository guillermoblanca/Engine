#include "GB/World/Graph/Nodes/MaskNodes.h"
#include "GB/World/Graph/Nodes/FieldMath.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/TerrainNoise.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	using namespace FieldMath;

	namespace
	{
		constexpr uint32_t kMask = 0x7A4F9A;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		// 1 inside [lo, hi], fading to 0 over `falloff` outside it.
		float Band(float v, float lo, float hi, float falloff)
		{
			if (falloff <= 0.0f) return v >= lo && v <= hi ? 1.0f : 0.0f;
			return Smoothstep(lo - falloff, lo, v) * (1.0f - Smoothstep(hi, hi + falloff, v));
		}

		// Separable Gaussian blur of `sigma` samples (3 sigma each way), clamped at the border. Isotropic, unlike FieldMath::BoxBlur.
		void GaussianBlur(TgField& f, float sigma)
		{
			if (sigma <= 0.0f || f.N < 2) return;
			const int n = static_cast<int>(f.N), r = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
			std::vector<float> k(static_cast<size_t>(2 * r + 1));
			float sum = 0.0f;
			for (int t = -r; t <= r; ++t) sum += k[static_cast<size_t>(t + r)] = std::exp(-0.5f * static_cast<float>(t * t) / (sigma * sigma));
			for (float& v : k) v /= sum;
			std::vector<float> tmp(f.Values.size());
			for (int j = 0; j < n; ++j)
			{
				const float* row = f.Values.data() + static_cast<size_t>(j) * f.N;
				for (int i = 0; i < n; ++i)
				{
					float a = 0.0f;
					for (int t = -r; t <= r; ++t) a += k[static_cast<size_t>(t + r)] * row[std::clamp(i + t, 0, n - 1)];
					tmp[static_cast<size_t>(j) * f.N + static_cast<size_t>(i)] = a;
				}
			}
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					float a = 0.0f;
					for (int t = -r; t <= r; ++t) a += k[static_cast<size_t>(t + r)] * tmp[static_cast<size_t>(std::clamp(j + t, 0, n - 1)) * f.N + static_cast<size_t>(i)];
					f.Values[static_cast<size_t>(j) * f.N + static_cast<size_t>(i)] = a;
				}
		}
	}

	// ------------------------------------------------------------------ mask_height
	TgNodeSchema HeightMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "1 between Min and Max height, fading over Falloff.";
		s.Color = kMask;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = {
			{ "Relative", 1, 0, 1, true, "1: Min/Max/Falloff are fractions of the height range; 0: metres" },
			{ "Min", 0.5, -500, 500, false, "" },
			{ "Max", 1, -500, 500, false, "" },
			{ "Falloff", 0.1, 0, 200, false, "" },
		};
		return s;
	}

	bool HeightMaskNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		float lo = static_cast<float>(TgParam(node, "Min", 0.5)), hi = static_cast<float>(TgParam(node, "Max", 1.0)), falloff = static_cast<float>(TgParam(node, "Falloff", 0.1));
		if (TgParam(node, "Relative", 1.0) != 0.0)
		{
			const auto [a, b] = std::minmax_element(in->Values.begin(), in->Values.end());
			const float range = std::max(*b - *a, 1e-6f);
			lo = *a + lo * range; hi = *a + hi * range; falloff *= range;
		}
		out = *in;
		for (float& v : out.Values) v = Band(v, lo, hi, falloff);
		return true;
	}

	// ------------------------------------------------------------------ mask_slope
	TgNodeSchema SlopeMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "1 between MinDegrees and MaxDegrees of slope, fading over Falloff degrees.";
		s.Color = kMask;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = {
			{ "MinDegrees", 35, 0, 90, false, "" },
			{ "MaxDegrees", 90, 0, 90, false, "" },
			{ "Falloff", 8, 0, 45, false, "degrees" },
		};
		return s;
	}

	bool SlopeMaskNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		const float lo = static_cast<float>(TgParam(node, "MinDegrees", 35.0)), hi = static_cast<float>(TgParam(node, "MaxDegrees", 90.0));
		const float falloff = static_cast<float>(TgParam(node, "Falloff", 8.0));
		out.Resize(in->N, in->Size, in->Center);
		for (uint32_t j = 0; j < in->N; ++j)
			for (uint32_t i = 0; i < in->N; ++i) out.At(i, j) = Band(SlopeDegrees(*in, i, j), lo, hi, falloff);
		return true;
	}

	// ------------------------------------------------------------------ mask_curvature
	TgNodeSchema CurvatureMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "Hollows (Mode 0: gullies, couloirs) or crests (Mode 1: ridges, edges), from the concavity Radius cells around.";
		s.Color = kMask;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = {
			{ "Mode", 0, 0, 1, true, "0 concave (hollows), 1 convex (crests)" },
			{ "Radius", 4, 1, 32, true, "cells: the size of the features measured" },
			{ "Gain", 4, 0.1, 50, false, "how much concavity (m/m) makes a full 1" },
		};
		return s;
	}

	bool CurvatureMaskNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		const float sign = TgParam(node, "Mode", 0.0) != 0.0 ? -1.0f : 1.0f;
		const int radius = static_cast<int>(TgParam(node, "Radius", 4.0));
		const float gain = static_cast<float>(TgParam(node, "Gain", 4.0));
		out.Resize(in->N, in->Size, in->Center);
		for (uint32_t j = 0; j < in->N; ++j)
			for (uint32_t i = 0; i < in->N; ++i) out.At(i, j) = std::clamp(sign * Concavity(*in, i, j, radius) * gain, 0.0f, 1.0f);
		return true;
	}

	// ------------------------------------------------------------------ mask_water
	TgNodeSchema WaterMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "0 at the world's water, 1 past Reach + Falloff metres from it (keeps erosion out of the lake and the river's channel).";
		s.Color = kMask;
		s.Inputs = { { "height", TgPortKind::Field, false, "gives the footprint of the mask" }, { "water", TgPortKind::Water } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = {
			{ "Reach", 2, 0, 50, false, "metres past the water's edge that stay 0 (the river's: in half-widths)" },
			{ "Falloff", 3, 0, 100, false, "metres (river: half-widths) over which it rises to 1" },
		};
		return s;
	}

	bool WaterMaskNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		const TgValue* height = inputs.size() > 0 ? inputs[0] : nullptr;
		const TgValue* water = inputs.size() > 1 ? inputs[1] : nullptr;
		if (!height || height->Field.N == 0) { error = Who(node) + ": input 'height' is not connected"; return false; }
		if (!water) { error = Who(node) + ": input 'water' is not connected"; return false; }
		const float reach = static_cast<float>(TgParam(node, "Reach", 2.0)), falloff = static_cast<float>(TgParam(node, "Falloff", 3.0));
		const TgField& footprint = height->Field;
		const WaterField& w = water->Water;
		TgValue& value = outputs["mask"];
		value.Kind = TgPortKind::Field;
		value.Field.Resize(footprint.N, footprint.Size, footprint.Center);
		for (uint32_t j = 0; j < footprint.N; ++j)
			for (uint32_t i = 0; i < footprint.N; ++i)
			{
				const glm::vec2 p = footprint.SamplePosition(i, j);
				float m = 1.0f;
				if (w.HasLake())
				{
					const LakeBody& lake = w.Lake();
					const float past = (lake.Outline.EffectiveDistance(p, lake.Center) - lake.Radius);
					m = std::min(m, Smoothstep(reach, reach + falloff, past));
				}
				if (w.River())
				{
					const RiverSample r = w.NearestRiver(p.x, p.y);
					if (r.Found && r.HalfWidth > 0.0f) m = std::min(m, Smoothstep(1.0f + reach, 1.0f + reach + falloff, r.Distance / r.HalfWidth));
				}
				value.Field.At(i, j) = m;
			}
		return true;
	}

	// ------------------------------------------------------------------ biome_mask
	TgNodeSchema BiomeMaskNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "1 where a biome field holds the chosen biome (0 Water, 1 Shore, 2 Meadow, 3 Forest, 4 Rock, 5 Snow, 6 Dunes).";
		s.Color = kMask;
		s.Inputs = { { "biome", TgPortKind::Field } };
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = { { "Biome", 3, 0, 6, true, "GB::Biome id" }, { "Blur", 0, 0, 16, true, "cells of soft edge" } };
		return s;
	}

	bool BiomeMaskNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "biome", error);
		if (!in) return false;
		const float id = static_cast<float>(static_cast<int>(TgParam(node, "Biome", 3.0)));
		out = *in;
		for (float& v : out.Values) v = std::fabs(std::round(v) - id) < 0.5f ? 1.0f : 0.0f;
		BoxBlur(out, static_cast<int>(TgParam(node, "Blur", 0.0)), 2);
		return true;
	}

	// ------------------------------------------------------------------ snow_accumulate
	TgNodeSchema SnowAccumulateNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Material";
		s.Summary = "Where snow lies: off the steep faces, gathered in hollows and in the channels the water carved, above a height.";
		s.Color = 0xB07030;
		s.Inputs = { { "height", TgPortKind::Field }, { "flow", TgPortKind::Field, true, "flow accumulation: snow fills the couloirs it marks" } };
		s.Outputs = { { "snow", TgPortKind::Field } };
		s.Params = {
			{ "FullSlopeDegrees", 32, 0, 90, false, "gentler than this holds snow fully" },
			{ "BareSlopeDegrees", 50, 0, 90, false, "steeper than this sheds it" },
			{ "HollowBoost", 0.6, 0, 3, false, "extra snow in hollows (lets it lie on steeper ground there)" },
			{ "HollowRadius", 3, 1, 24, true, "cells: the size of the hollows" },
			{ "RidgeLoss", 0.5, 0, 3, false, "snow blown off convex crests" },
			{ "FlowBoost", 0.4, 0, 3, false, "extra snow along the drainage channels" },
			{ "MinRelativeHeight", 0, 0, 1, false, "fraction of the height range below which there is no snow" },
			{ "Breakup", 0.25, 0, 1, false, "noise that breaks the edges up" },
			{ "BreakupScale", 9, 0.5, 100, false, "metres of the breakup noise" },
			{ "Seed", 0, 0, 100, true, "" },
		};
		return s;
	}

	bool SnowAccumulateNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		const TgField* flow = Optional(inputs, 1);
		const float full = static_cast<float>(TgParam(node, "FullSlopeDegrees", 32.0)), bare = static_cast<float>(TgParam(node, "BareSlopeDegrees", 50.0));
		const float hollowBoost = static_cast<float>(TgParam(node, "HollowBoost", 0.6)), ridgeLoss = static_cast<float>(TgParam(node, "RidgeLoss", 0.5));
		const int radius = static_cast<int>(TgParam(node, "HollowRadius", 3.0));
		const float flowBoost = static_cast<float>(TgParam(node, "FlowBoost", 0.4));
		const float minRelative = static_cast<float>(TgParam(node, "MinRelativeHeight", 0.0));
		const float breakup = static_cast<float>(TgParam(node, "Breakup", 0.25)), breakupScale = std::max(static_cast<float>(TgParam(node, "BreakupScale", 9.0)), 0.1f);
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;

		const auto [lo, hi] = std::minmax_element(in->Values.begin(), in->Values.end());
		const float low = *lo, range = std::max(*hi - *lo, 1e-6f);
		const float spacing = in->Spacing();

		// Every measure is taken so that it favours no direction of the grid (T112). Taken raw, the slope by central differences of the bare
		// height turned its sample-to-sample roughness into speckle and diamonds, the concavity from a 3x3 ring of samples Radius cells away drew
		// stars along the 8 grid directions, and a breakup finer than the grid aliased (the D8 flow's one-sample lines are barely felt, but
		// they are blurred too):
		//  - slope and concavity are measured on the height blurred by a Gaussian of 1 sample;
		//  - the concavity is the mean of a circle of 24 bilinear samples Radius cells away minus the centre, over the distance (the old ring's
		//    measure without its corners: over a paraboloid it gives 2/3 of the ring's, whose aliasing in rough ground added hollows that offset
		//    the crests' loss; with this one the cover of the graphs that use the node stays within 6 % of what it was);
		//  - the channel term is blurred (1.5 samples) and the breakup's wavelength is kept at 4 samples or more (its finer octaves fade out);
		//  - the clamped result is blurred by 1 sample, which keeps its mean (the cover) and softens what is left at the grid's scale (wider, it
		//    spreads the snow over the steep faces next to the gentle ground: terrain_graph's alpine_peaks check).
		constexpr float kPreBlur = 1.0f, kChannelBlur = 1.5f, kFinalBlur = 1.0f, kBreakupMinSamples = 4.0f;
		constexpr int kCircle = 24;
		TgField smooth = *in;
		GaussianBlur(smooth, kPreBlur);
		TgField channel;
		if (flow)
		{
			const float flowMax = std::max(*std::max_element(flow->Values.begin(), flow->Values.end()), 2.0f);
			channel.Resize(in->N, in->Size, in->Center);
			for (uint32_t j = 0; j < in->N; ++j)
				for (uint32_t i = 0; i < in->N; ++i)
				{
					const glm::vec2 p = in->SamplePosition(i, j);
					const float f = std::log(std::max(flow->Sample(p.x, p.y), 1.0f)) / std::log(flowMax);   // 0 on a crest .. 1 at the outlet
					channel.At(i, j) = Smoothstep(0.25f, 0.6f, f) * flowBoost;
				}
			GaussianBlur(channel, kChannelBlur);
		}
		glm::vec2 ring[kCircle];
		const float ringRadius = static_cast<float>(radius) * spacing;
		for (int k = 0; k < kCircle; ++k)
		{
			const float a = 6.2831853f * (static_cast<float>(k) + 0.5f) / static_cast<float>(kCircle);
			ring[k] = { ringRadius * std::cos(a), ringRadius * std::sin(a) };
		}
		const float breakupWave = std::max(breakupScale, kBreakupMinSamples * spacing);

		out.Resize(in->N, in->Size, in->Center);
		for (uint32_t j = 0; j < in->N; ++j)
			for (uint32_t i = 0; i < in->N; ++i)
			{
				const glm::vec2 p = in->SamplePosition(i, j);
				float around = 0.0f;
				for (const glm::vec2& d : ring) around += smooth.Sample(p.x + d.x, p.y + d.y);
				const float concavity = (around / static_cast<float>(kCircle) - smooth.At(i, j)) / (static_cast<float>(radius) * spacing);   // m/m: > 0 hollow, < 0 crest
				// a hollow holds snow on steeper ground than an open slope: shift the slope at which it sheds
				const float shift = std::clamp(concavity * 40.0f, -1.0f, 1.0f) * (bare - full) * 0.5f;
				float snow = 1.0f - Smoothstep(full + shift, bare + shift, SlopeDegrees(smooth, i, j));
				snow += std::max(concavity, 0.0f) * 30.0f * hollowBoost - std::max(-concavity, 0.0f) * 30.0f * ridgeLoss;
				if (flow) snow += channel.At(i, j);
				// TerrainNoise::Fbm with 3 octaves, each faded out as its wavelength nears kBreakupMinSamples / 2
				float noise = 0.0f, amp = 0.5f, norm = 0.0f, x = p.x / breakupWave, y = p.y / breakupWave, wave = breakupWave / spacing;
				for (int o = 0; o < 3; ++o, amp *= 0.5f, x *= 2.0f, y *= 2.0f, wave *= 0.5f)
				{
					noise += amp * Smoothstep(kBreakupMinSamples * 0.5f, kBreakupMinSamples, wave) * (TerrainNoise::ValueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u) - 0.5f);
					norm += amp;
				}
				snow += noise / norm * 2.0f * breakup;
				snow *= Smoothstep(minRelative - 0.04f, minRelative + 0.04f, (in->At(i, j) - low) / range);
				out.At(i, j) = std::clamp(snow, 0.0f, 1.0f);
			}
		GaussianBlur(out, kFinalBlur);
		return true;
	}

	// ------------------------------------------------------------------ biome_weights
	namespace
	{
		const char* const kBiomeWeightNames[] = { "Water", "Shore", "Meadow", "Forest", "Rock", "Snow", "Dunes" };   // GB::Biome order
	}

	TgNodeSchema BiomeWeightsNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Mask";
		s.Summary = "A value per biome (the chance of a tree there, the share of rocks...).";
		s.Color = kMask;
		s.Inputs = { { "biome", TgPortKind::Field } };
		s.Outputs = { { "weight", TgPortKind::Field } };
		for (const char* name : kBiomeWeightNames) s.Params.push_back({ name, 0, 0, 1, false, "" });
		return s;
	}

	bool BiomeWeightsNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "biome", error);
		if (!in) return false;
		float weights[7];
		for (int b = 0; b < 7; ++b) weights[b] = static_cast<float>(TgParam(node, kBiomeWeightNames[b], 0.0));
		out = *in;
		for (float& v : out.Values) v = weights[std::clamp(static_cast<int>(std::lround(v)), 0, 6)];
		return true;
	}
}
