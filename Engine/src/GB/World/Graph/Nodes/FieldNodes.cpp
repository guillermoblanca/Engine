#include "GB/World/Graph/Nodes/FieldNodes.h"
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
		constexpr uint32_t kGenerate = 0x5656A0, kShape = 0x6A6A78, kRock = 0x8A6A50;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }
		uint32_t SeedOf(const TgNode& node, const TgContext& ctx) { return ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u; }

		// A terrace of `step` metres: a flat tread and a riser over the last `1 - sharpness` of each step (sharpness 0 = no terrace at all).
		float Terrace(float h, float step, float sharpness, float offset)
		{
			if (step <= 1e-4f || sharpness <= 0.0f) return h;
			const float b = (h - offset) / step;
			const float k = std::floor(b), f = b - k;
			const float riser = 1.0f - std::clamp(sharpness, 0.0f, 1.0f) * 0.95f;
			const float g = Smoothstep(1.0f - riser, 1.0f, f);
			return (k + g) * step + offset;
		}
	}

	// ------------------------------------------------------------------ constant
	TgNodeSchema ConstantNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Generate";
		s.Summary = "One value everywhere.";
		s.Color = kGenerate;
		s.Outputs = { { "value", TgPortKind::Field } };
		s.Params = { { "Value", 0, -100, 500, false, "" } };
		return s;
	}

	bool ConstantNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		std::fill(out.Values.begin(), out.Values.end(), static_cast<float>(TgParam(node, "Value", 0.0)));
		return true;
	}

	// ------------------------------------------------------------------ gradient
	TgNodeSchema GradientNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Generate";
		s.Summary = "0..1: linear across the map along an angle, or radial (1 at a centre, 0 at a radius).";
		s.Color = kGenerate;
		s.Outputs = { { "mask", TgPortKind::Field } };
		s.Params = {
			{ "Mode", 1, 0, 1, true, "0 linear, 1 radial" },
			{ "AngleDegrees", 0, 0, 360, false, "linear: the direction it rises toward (from +X toward +Z)" },
			{ "CenterX", 0, -500, 500, false, "radial: metres" },
			{ "CenterZ", 0, -500, 500, false, "radial: metres" },
			{ "Radius", 150, 1, 1000, false, "radial: metres where it reaches 0" },
			{ "Smooth", 1, 0, 1, true, "1: a smooth falloff (smoothstep) instead of a straight one" },
		};
		return s;
	}

	bool GradientNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		const int mode = static_cast<int>(TgParam(node, "Mode", 1.0));
		const float angle = static_cast<float>(TgParam(node, "AngleDegrees", 0.0)) * 0.0174532925f;
		const glm::vec2 centre(static_cast<float>(TgParam(node, "CenterX", 0.0)), static_cast<float>(TgParam(node, "CenterZ", 0.0)));
		const float radius = std::max(static_cast<float>(TgParam(node, "Radius", 150.0)), 1e-3f);
		const bool smooth = TgParam(node, "Smooth", 1.0) != 0.0;
		const glm::vec2 dir(std::cos(angle), std::sin(angle));
		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				float t = mode == 0 ? glm::dot(p - out.Center, dir) / out.Size + 0.5f : 1.0f - glm::length(p - centre) / radius;
				t = std::clamp(t, 0.0f, 1.0f);
				out.At(i, j) = smooth ? t * t * (3.0f - 2.0f * t) : t;
			}
		return true;
	}

	// ------------------------------------------------------------------ hills / dunes
	TgNodeSchema HillsNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Generate";
		s.Summary = "The engine's built-in hills (temperate: Height 16, Scale 140, Flatten 0.25; alpine: 58, 190, 0.05).";
		s.Color = kGenerate;
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Height", 16, 0, 300, false, "metres" },
			{ "Scale", 140, 10, 1000, false, "metres of the largest features" },
			{ "Octaves", 5, 1, 10, true, "" },
			{ "Flatten", 0.25, 0, 1, false, "pushes valleys flatter" },
			{ "Seed", 0, 0, 100, true, "added to the world's seed (0: the world's own hills)" },
		};
		return s;
	}

	bool HillsNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		const float height = static_cast<float>(TgParam(node, "Height", 16.0)), scale = static_cast<float>(TgParam(node, "Scale", 140.0));
		const int octaves = static_cast<int>(TgParam(node, "Octaves", 5.0));
		const float flatten = static_cast<float>(TgParam(node, "Flatten", 0.25));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0));   // (not mixed: Seed 0 is exactly Terrain::CreateHills' seed)
		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i) out.At(i, j) = TerrainNoise::HillHeight(out.SamplePosition(i, j), scale, octaves, seed, flatten, height);
		return true;
	}

	TgNodeSchema DunesNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Generate";
		s.Summary = "The engine's built-in dunes: crests across the wind that meander, over gentle large hills.";
		s.Color = kGenerate;
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "WindAngleDegrees", 30, 0, 360, false, "the direction the wind blows toward" },
			{ "Wavelength", 45, 5, 300, false, "metres between crests" },
			{ "Height", 6, 0, 60, false, "metres of a typical crest" },
			{ "CrestPosition", 0.78, 0.05, 0.95, false, "fraction of the wavelength taken by the windward slope" },
			{ "CrestSharpness", 0.55, 0, 1, false, "0 rounded, 1 knife edge" },
			{ "Warp", 0.5, 0, 3, false, "how much the crests meander (in wavelengths)" },
			{ "WarpScale", 110, 10, 1000, false, "metres of the meanders" },
			{ "SizeVariation", 0.45, 0, 1, false, "" },
			{ "BaseHills", 2.5, 0, 50, false, "metres of the large undulation underneath" },
			{ "Seed", 0, 0, 100, true, "added to the world's seed" },
		};
		return s;
	}

	bool DunesNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		const float wind = static_cast<float>(TgParam(node, "WindAngleDegrees", 30.0)) * 0.0174532925f;
		const float wavelength = static_cast<float>(TgParam(node, "Wavelength", 45.0)), height = static_cast<float>(TgParam(node, "Height", 6.0));
		const float crest = static_cast<float>(TgParam(node, "CrestPosition", 0.78)), sharp = static_cast<float>(TgParam(node, "CrestSharpness", 0.55));
		const float warp = static_cast<float>(TgParam(node, "Warp", 0.5)), warpScale = static_cast<float>(TgParam(node, "WarpScale", 110.0));
		const float variation = static_cast<float>(TgParam(node, "SizeVariation", 0.45)), base = static_cast<float>(TgParam(node, "BaseHills", 2.5));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0));
		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
				out.At(i, j) = TerrainNoise::DuneHeight(out.SamplePosition(i, j), wind, wavelength, height, crest, sharp, warp, warpScale, variation, base, seed);
		return true;
	}

	// ------------------------------------------------------------------ remap
	TgNodeSchema RemapNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Shape";
		s.Summary = "Maps [InMin, InMax] to [OutMin, OutMax], with a gamma curve and an optional clamp.";
		s.Color = kShape;
		s.Inputs = { { "in", TgPortKind::Field } };
		s.Outputs = { { "out", TgPortKind::Field } };
		s.Params = {
			{ "AutoRange", 0, 0, 1, true, "1: InMin/InMax are the input's own lowest and highest values" },
			{ "InMin", 0, -500, 500, false, "" },
			{ "InMax", 1, -500, 500, false, "" },
			{ "OutMin", 0, -500, 500, false, "" },
			{ "OutMax", 1, -500, 500, false, "" },
			{ "Gamma", 1, 0.1, 5, false, "> 1 pushes values down (sharper peaks), < 1 up (broader)" },
			{ "Clamp", 1, 0, 1, true, "1: keep the result inside [OutMin, OutMax]" },
		};
		return s;
	}

	bool RemapNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "in", error);
		if (!in) return false;
		float inMin = static_cast<float>(TgParam(node, "InMin", 0.0)), inMax = static_cast<float>(TgParam(node, "InMax", 1.0));
		if (TgParam(node, "AutoRange", 0.0) != 0.0)
		{
			const auto [lo, hi] = std::minmax_element(in->Values.begin(), in->Values.end());
			inMin = *lo; inMax = *hi;
		}
		const float outMin = static_cast<float>(TgParam(node, "OutMin", 0.0)), outMax = static_cast<float>(TgParam(node, "OutMax", 1.0));
		const float gamma = std::max(static_cast<float>(TgParam(node, "Gamma", 1.0)), 1e-3f);
		const bool clamp = TgParam(node, "Clamp", 1.0) != 0.0;
		const float span = std::fabs(inMax - inMin) < 1e-6f ? 1e-6f : inMax - inMin;
		out = *in;
		for (float& v : out.Values)
		{
			float t = (v - inMin) / span;
			if (clamp) t = std::clamp(t, 0.0f, 1.0f);
			t = t >= 0.0f ? std::pow(t, gamma) : -std::pow(-t, gamma);
			v = outMin + t * (outMax - outMin);
		}
		return true;
	}

	// ------------------------------------------------------------------ terrace
	TgNodeSchema TerraceNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Shape";
		s.Summary = "Steps of Step metres: flat treads and risers.";
		s.Color = kShape;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Step", 4, 0.1, 50, false, "metres per step" },
			{ "Sharpness", 0.6, 0, 1, false, "0 none, 1 vertical risers" },
			{ "Offset", 0, -50, 50, false, "metres the steps are shifted by" },
		};
		return s;
	}

	bool TerraceNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		const float step = static_cast<float>(TgParam(node, "Step", 4.0)), sharp = static_cast<float>(TgParam(node, "Sharpness", 0.6));
		const float offset = static_cast<float>(TgParam(node, "Offset", 0.0));
		out = *in;
		for (float& v : out.Values) v = Terrace(v, step, sharp, offset);
		return true;
	}

	// ------------------------------------------------------------------ blur
	TgNodeSchema BlurNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Shape";
		s.Summary = "Box blur (Passes 2-3 approximate a gaussian).";
		s.Color = kShape;
		s.Inputs = { { "in", TgPortKind::Field } };
		s.Outputs = { { "out", TgPortKind::Field } };
		s.Params = { { "Radius", 2, 1, 32, true, "cells" }, { "Passes", 2, 1, 6, true, "" } };
		return s;
	}

	bool BlurNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "in", error);
		if (!in) return false;
		out = *in;
		BoxBlur(out, static_cast<int>(TgParam(node, "Radius", 2.0)), static_cast<int>(TgParam(node, "Passes", 2.0)));
		return true;
	}

	// ------------------------------------------------------------------ warp
	TgNodeSchema WarpNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Shape";
		s.Summary = "Reads the field at points pushed by a low noise (domain warp): bends ridges, valleys, terraces.";
		s.Color = kShape;
		s.Inputs = { { "in", TgPortKind::Field } };
		s.Outputs = { { "out", TgPortKind::Field } };
		s.Params = {
			{ "Amount", 20, 0, 200, false, "metres" },
			{ "Frequency", 0.006, 0.0005, 0.1, false, "1/m" },
			{ "Octaves", 3, 1, 6, true, "" },
			{ "Seed", 0, 0, 100, true, "" },
		};
		return s;
	}

	bool WarpNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "in", error);
		if (!in) return false;
		const float amount = static_cast<float>(TgParam(node, "Amount", 20.0)), frequency = static_cast<float>(TgParam(node, "Frequency", 0.006));
		const int octaves = static_cast<int>(TgParam(node, "Octaves", 3.0));
		const uint32_t seed = SeedOf(node, ctx);
		out = *in;
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				const glm::vec2 offset((TerrainNoise::Fbm(p.x * frequency, p.y * frequency, octaves, seed + 11u) - 0.5f) * 2.0f,
					(TerrainNoise::Fbm(p.x * frequency + 31.7f, p.y * frequency + 17.3f, octaves, seed + 23u) - 0.5f) * 2.0f);
				const glm::vec2 q = p + offset * amount;
				out.At(i, j) = in->Sample(q.x, q.y);
			}
		return true;
	}

	// ------------------------------------------------------------------ combine / blend
	TgNodeSchema CombineNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Combine";
		s.Summary = "a (op) b*WeightB: add, subtract, multiply, min, max or average.";
		s.Color = kShape;
		s.Inputs = { { "a", TgPortKind::Field }, { "b", TgPortKind::Field } };
		s.Outputs = { { "out", TgPortKind::Field } };
		s.Params = {
			{ "Mode", 0, 0, 5, true, "0 add, 1 subtract, 2 multiply, 3 min, 4 max, 5 average" },
			{ "WeightB", 1, -10, 10, false, "b is multiplied by this first" },
		};
		return s;
	}

	bool CombineNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* a = Require(inputs, 0, Who(node), "a", error);
		const TgField* b = a ? Require(inputs, 1, Who(node), "b", error) : nullptr;
		if (!a || !b) return false;
		const int mode = static_cast<int>(TgParam(node, "Mode", 0.0));
		const float w = static_cast<float>(TgParam(node, "WeightB", 1.0));
		out = *a;
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				const float x = a->At(i, j), y = (b->N == a->N ? b->At(i, j) : b->Sample(p.x, p.y)) * w;
				float r;
				switch (mode)
				{
				case 1: r = x - y; break;
				case 2: r = x * y; break;
				case 3: r = std::min(x, y); break;
				case 4: r = std::max(x, y); break;
				case 5: r = (x + y) * 0.5f; break;
				default: r = x + y; break;
				}
				out.At(i, j) = r;
			}
		return true;
	}

	TgNodeSchema BlendNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Combine";
		s.Summary = "a where the mask is 0, b where it is 1 (linear in between).";
		s.Color = kShape;
		s.Inputs = { { "a", TgPortKind::Field }, { "b", TgPortKind::Field }, { "mask", TgPortKind::Field } };
		s.Outputs = { { "out", TgPortKind::Field } };
		return s;
	}

	bool BlendNode::Evaluate(const TgNode& node, const TgContext&, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* a = Require(inputs, 0, Who(node), "a", error);
		const TgField* b = a ? Require(inputs, 1, Who(node), "b", error) : nullptr;
		const TgField* m = b ? Require(inputs, 2, Who(node), "mask", error) : nullptr;
		if (!a || !b || !m) return false;
		out = *a;
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				const float t = std::clamp(m->Sample(p.x, p.y), 0.0f, 1.0f);
				out.At(i, j) = a->At(i, j) + (b->Sample(p.x, p.y) - a->At(i, j)) * t;
			}
		return true;
	}

	// ------------------------------------------------------------------ strata
	TgNodeSchema StrataNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Shape";
		s.Summary = "Rock layers: ledges of a tilted, wavering bedding, only where the ground is steep (cliffs, not meadows).";
		s.Color = kRock;
		s.Inputs = { { "height", TgPortKind::Field } };
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Spacing", 6, 0.5, 40, false, "metres between layers" },
			{ "Strength", 0.6, 0, 1, false, "how far the ground is pulled to the ledges" },
			{ "Sharpness", 0.7, 0, 1, false, "of each ledge's riser" },
			{ "TiltDegrees", 8, 0, 45, false, "dip of the bedding" },
			{ "TiltAzimuth", 40, 0, 360, false, "direction of the dip" },
			{ "Waver", 3, 0, 30, false, "metres the layers wander up and down" },
			{ "MinSlopeDegrees", 28, 0, 80, false, "below this the ground is left as it is" },
			{ "FullSlopeDegrees", 45, 0, 89, false, "from this on the layers apply fully" },
			{ "Seed", 0, 0, 100, true, "" },
		};
		return s;
	}

	bool StrataNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string& error) const
	{
		const TgField* in = Require(inputs, 0, Who(node), "height", error);
		if (!in) return false;
		const float spacing = static_cast<float>(TgParam(node, "Spacing", 6.0)), strength = static_cast<float>(TgParam(node, "Strength", 0.6));
		const float sharp = static_cast<float>(TgParam(node, "Sharpness", 0.7));
		const float tilt = std::tan(static_cast<float>(TgParam(node, "TiltDegrees", 8.0)) * 0.0174532925f);
		const float azimuth = static_cast<float>(TgParam(node, "TiltAzimuth", 40.0)) * 0.0174532925f;
		const float waver = static_cast<float>(TgParam(node, "Waver", 3.0));
		const float minSlope = static_cast<float>(TgParam(node, "MinSlopeDegrees", 28.0)), fullSlope = static_cast<float>(TgParam(node, "FullSlopeDegrees", 45.0));
		const uint32_t seed = SeedOf(node, ctx);
		const glm::vec2 dip(std::cos(azimuth), std::sin(azimuth));
		out = *in;
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = in->SamplePosition(i, j);
				const float h = in->At(i, j);
				// the height of the bedding plane through this point: tilted and wavering, so the ledges are not contour lines
				const float shift = glm::dot(p, dip) * tilt + (TerrainNoise::Fbm(p.x / 90.0f, p.y / 90.0f, 3, seed + 5u) - 0.5f) * 2.0f * waver;
				const float layered = Terrace(h + shift, spacing, sharp, 0.0f) - shift;
				const float steep = Smoothstep(minSlope, fullSlope, SlopeDegrees(*in, i, j));
				out.At(i, j) = h + (layered - h) * strength * steep;
			}
		return true;
	}

	// ------------------------------------------------------------------ moisture
	TgNodeSchema MoistureNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Biome";
		s.Summary = "0..1 humidity: a broad noise, wetter along the drainage, drier high up.";
		s.Color = 0x968C32;
		s.Inputs = { { "height", TgPortKind::Field, true, "drier with altitude (and the field's footprint)" }, { "flow", TgPortKind::Field, true, "flow accumulation: wetter valleys" } };
		s.Outputs = { { "moisture", TgPortKind::Field } };
		s.Params = {
			{ "Scale", 60, 5, 500, false, "metres of the patches" },
			{ "FlowWetness", 0.35, 0, 1, false, "how much wetter the drainage channels are" },
			{ "AltitudeDryness", 0.3, 0, 1, false, "how much drier the top of the height range is" },
			{ "FlowBlur", 0, 0, 32, true, "cells the drainage's wetness is spread over (0: none, as it was)" },
			{ "Seed", 0, 0, 100, true, "" },
		};
		return s;
	}

	bool MoistureNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>& inputs, TgField& out, std::string&) const
	{
		const TgField* height = Optional(inputs, 0);
		const TgField* flow = Optional(inputs, 1);
		const float scale = std::max(static_cast<float>(TgParam(node, "Scale", 60.0)), 0.1f);
		const float wet = static_cast<float>(TgParam(node, "FlowWetness", 0.35)), dry = static_cast<float>(TgParam(node, "AltitudeDryness", 0.3));
		const uint32_t seed = SeedOf(node, ctx);
		if (height) out.Resize(height->N, height->Size, height->Center); else out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		float low = 0.0f, range = 1.0f, flowMax = 2.0f;
		if (height)
		{
			const auto [a, b] = std::minmax_element(height->Values.begin(), height->Values.end());
			low = *a; range = std::max(*b - *a, 1e-6f);
		}
		if (flow) flowMax = std::max(*std::max_element(flow->Values.begin(), flow->Values.end()), 2.0f);
		// the wetness of the drainage, spread over FlowBlur cells: D8 drains a plain slope down straight, parallel lines of the grid, which a
		// threshold on the moisture (the forest's) would turn into stripes
		const int flowBlur = static_cast<int>(TgParam(node, "FlowBlur", 0.0));
		TgField wetness;
		if (flow && flowBlur > 0)
		{
			wetness = *flow;
			for (float& v : wetness.Values) v = Smoothstep(0.2f, 0.7f, std::log(std::max(v, 1.0f)) / std::log(flowMax));
			BoxBlur(wetness, flowBlur, 3);
		}
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				float m = TerrainNoise::Fbm(p.x / scale, p.y / scale, 4, seed);
				if (flow && flowBlur > 0) m += wet * wetness.Sample(p.x, p.y);
				else if (flow) m += wet * Smoothstep(0.2f, 0.7f, std::log(std::max(flow->Sample(p.x, p.y), 1.0f)) / std::log(flowMax));
				if (height) m -= dry * (height->Sample(p.x, p.y) - low) / range;
				out.At(i, j) = std::clamp(m, 0.0f, 1.0f);
			}
		return true;
	}
}
