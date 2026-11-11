#include "GB/World/Graph/Nodes/NoiseNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/ParallelRows.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		// Self-contained hash/value-noise/fbm for this node only: deliberately not shared with
		// GB::Terrain's or GB::WorldGen's own noise (docs/GRAFO_TERRENO.md notes the duplication across
		// those two as existing debt; a Fase 1 node adds a third, isolated, implementation rather than
		// risk touching either of them before the graph has replaced anything in production).
		uint32_t Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2654435761u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return h ^ (h >> 16);
		}
		float ValueNoise(float x, float y, uint32_t seed)
		{
			const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
			const float tx = x - static_cast<float>(xi), ty = y - static_cast<float>(yi);
			auto corner = [&](int dx, int dy) { return static_cast<float>(Hash(xi + dx, yi + dy, seed) & 0xFFFFu) / 65535.0f; };
			const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
			const float a = corner(0, 0), b = corner(1, 0), c = corner(0, 1), d = corner(1, 1);
			const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
			return top + (bottom - top) * sy;
		}
		float Fbm(float x, float y, uint32_t seed, int octaves, float lacunarity, float gain)
		{
			float sum = 0.0f, amp = 0.5f, freq = 1.0f;
			for (int i = 0; i < octaves; ++i)
			{
				sum += (ValueNoise(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 101u) * 2.0f - 1.0f) * amp;
				freq *= lacunarity;
				amp *= gain;
			}
			return sum;
		}

		// Gradient (Perlin-style) noise, about -1..1: value noise creases along the grid axes, which a ridged sum turns into straight ridges.
		float GradientNoise(float x, float y, uint32_t seed)
		{
			const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
			const float tx = x - static_cast<float>(xi), ty = y - static_cast<float>(yi);
			auto dot = [&](int dx, int dy)
			{
				const float angle = static_cast<float>(Hash(xi + dx, yi + dy, seed) & 0xFFFFu) / 65535.0f * 6.2831853f;
				return std::cos(angle) * (tx - static_cast<float>(dx)) + std::sin(angle) * (ty - static_cast<float>(dy));
			};
			auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
			const float sx = fade(tx), sy = fade(ty);
			const float a = dot(0, 0), b = dot(1, 0), c = dot(0, 1), d = dot(1, 1);
			const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
			return (top + (bottom - top) * sy) * 1.41f;
		}
		// Each octave is turned by an irrational angle so the octaves do not line up along the same axes.
		glm::vec2 Turn(const glm::vec2& p) { return { p.x * 0.8f - p.y * 0.6f, p.x * 0.6f + p.y * 0.8f }; }

		// Ridged multifractal (Musgrave): 1 - |noise|, squared, each octave weighted by the one before, so the fine detail gathers on the ridges
		// and the valleys stay smooth -- the sharp crests of a mountain range. About 0..1.
		float Ridged(glm::vec2 p, uint32_t seed, int octaves, float lacunarity, float gain)
		{
			float sum = 0.0f, amp = 0.5f, weight = 1.0f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				float n = 1.0f - std::fabs(GradientNoise(p.x, p.y, seed + static_cast<uint32_t>(i) * 101u));
				n *= n * weight;
				weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
				sum += n * amp;
				norm += amp;
				p = Turn(p) * lacunarity;
				amp *= gain;
			}
			return norm > 0.0f ? sum / norm : 0.0f;
		}
		// Billow: |noise| summed: rounded humps with creased valleys (dunes of rock, rolling foothills). About 0..1.
		float Billow(glm::vec2 p, uint32_t seed, int octaves, float lacunarity, float gain)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			for (int i = 0; i < octaves; ++i)
			{
				sum += std::fabs(GradientNoise(p.x, p.y, seed + static_cast<uint32_t>(i) * 101u)) * amp;
				norm += amp;
				p = Turn(p) * lacunarity;
				amp *= gain;
			}
			return norm > 0.0f ? sum / norm : 0.0f;
		}
		float GradientFbm(glm::vec2 p, uint32_t seed, int octaves)
		{
			float sum = 0.0f, amp = 0.5f;
			for (int i = 0; i < octaves; ++i)
			{
				sum += GradientNoise(p.x, p.y, seed + static_cast<uint32_t>(i) * 101u) * amp;
				p = Turn(p) * 2.0f;
				amp *= 0.5f;
			}
			return sum;
		}
	}

	TgNodeSchema HeightfieldNoiseNode::Describe() const
	{
		TgNodeSchema s;
		s.Category = "Generate";
		s.Summary = "A height field from fractal noise: fbm (rolling), ridged (sharp mountain crests) or billow (rounded humps).";
		s.Color = 0x5656A0;
		s.Outputs = { { "height", TgPortKind::Field } };
		s.Params = {
			{ "Mode", 0, 0, 2, true, "0 fbm (value noise, -1..1), 1 ridged, 2 billow (gradient noise, 0..1)" },
			{ "Octaves", 5, 1, 10, true, "layers of detail" },
			{ "Frequency", 0.015, 0.0005, 0.1, false, "1/m of the largest features" },
			{ "Amplitude", 20, 0, 400, false, "metres" },
			{ "Lacunarity", 2, 1.2, 4, false, "frequency step between octaves" },
			{ "Gain", 0.5, 0, 1, false, "amplitude step between octaves" },
			{ "WarpAmount", 0, 0, 200, false, "metres the sample point is pushed by a low noise (domain warp: bends ridges and valleys)" },
			{ "WarpFrequency", 0.004, 0.0005, 0.05, false, "1/m of the warp" },
			{ "Seed", 0, 0, 100, true, "mixed with the world's seed" },
		};
		return s;
	}

	bool HeightfieldNoiseNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		const int mode = static_cast<int>(TgParam(node, "Mode", 0.0));
		const int octaves = static_cast<int>(TgParam(node, "Octaves", 5.0));
		const float frequency = static_cast<float>(TgParam(node, "Frequency", 0.015));
		const float amplitude = static_cast<float>(TgParam(node, "Amplitude", 20.0));
		const float lacunarity = static_cast<float>(TgParam(node, "Lacunarity", 2.0));
		const float gain = static_cast<float>(TgParam(node, "Gain", 0.5));
		const float warp = static_cast<float>(TgParam(node, "WarpAmount", 0.0));
		const float warpFrequency = static_cast<float>(TgParam(node, "WarpFrequency", 0.004));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(TgParam(node, "Seed", 0.0)) * 97u;

		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		// every sample is a pure function of its position: the rows go to a few threads, the same values bit for bit (ParallelRows.h; T125:
		// the valley's 9-octave warped crests were over a second of its 3 km world)
		ParallelRows(out.N, [&](uint32_t j)
		{
			for (uint32_t i = 0; i < out.N; ++i)
			{
				glm::vec2 p = out.SamplePosition(i, j);
				if (warp > 0.0f)
				{
					const glm::vec2 q = p * warpFrequency;
					p += warp * glm::vec2(GradientFbm(q, seed + 7001u, 3), GradientFbm(q + glm::vec2(5.2f, 1.3f), seed + 7002u, 3));
				}
				float v;
				switch (mode)
				{
				case 1: v = Ridged(p * frequency, seed, octaves, lacunarity, gain); break;
				case 2: v = Billow(p * frequency, seed, octaves, lacunarity, gain); break;
				default: v = Fbm(p.x * frequency, p.y * frequency, seed, octaves, lacunarity, gain); break;
				}
				out.At(i, j) = v * amplitude;
			}
		});
		return true;
	}
}
