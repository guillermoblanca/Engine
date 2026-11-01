#include "GB/World/Graph/Nodes/NoiseNode.h"
#include "GB/World/Graph/TerrainGraph.h"
#include <cmath>

namespace GB
{
	namespace
	{
		double Param(const TgNode& n, const char* key, double fallback)
		{
			const auto it = n.Params.find(key);
			return it == n.Params.end() ? fallback : it->second;
		}

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
	}

	bool HeightfieldNoiseNode::Evaluate(const TgNode& node, const TgContext& ctx, const std::vector<const TgField*>&, TgField& out, std::string&) const
	{
		const int octaves = static_cast<int>(Param(node, "Octaves", 5.0));
		const float frequency = static_cast<float>(Param(node, "Frequency", 0.015));
		const float amplitude = static_cast<float>(Param(node, "Amplitude", 20.0));
		const float lacunarity = static_cast<float>(Param(node, "Lacunarity", 2.0));
		const float gain = static_cast<float>(Param(node, "Gain", 0.5));
		const uint32_t seed = ctx.Seed + static_cast<uint32_t>(Param(node, "Seed", 0.0)) * 97u;

		out.Resize(ctx.Resolution, ctx.Size, ctx.Center);
		for (uint32_t j = 0; j < out.N; ++j)
			for (uint32_t i = 0; i < out.N; ++i)
			{
				const glm::vec2 p = out.SamplePosition(i, j);
				out.At(i, j) = Fbm(p.x * frequency, p.y * frequency, seed, octaves, lacunarity, gain) * amplitude;
			}
		return true;
	}
}
