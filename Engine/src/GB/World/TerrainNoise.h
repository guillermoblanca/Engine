#pragma once
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace GB::TerrainNoise
{
	// The noise GB::Terrain builds its hills, dunes and surface textures from, shared with the terrain graph's "hills" and "dunes" nodes so a
	// graph can rebuild those grounds bit for bit (docs/GRAFO_TERRENO.md). Header-only and pure: no renderer, no state.
	inline constexpr float kPi = 3.14159265358979f;

	inline float Hash(int x, int y, uint32_t seed)
	{
		uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
	}

	inline float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

	// Value noise in [0,1]. `period` > 0 makes it tile every `period` cells (for seamless textures).
	inline float ValueNoise(float x, float y, uint32_t seed, int period = 0)
	{
		const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
		const float fx = Smooth(x - static_cast<float>(ix)), fy = Smooth(y - static_cast<float>(iy));
		auto wrap = [&](int v) { return period > 0 ? ((v % period) + period) % period : v; };
		const float a = Hash(wrap(ix), wrap(iy), seed), b = Hash(wrap(ix + 1), wrap(iy), seed);
		const float c = Hash(wrap(ix), wrap(iy + 1), seed), d = Hash(wrap(ix + 1), wrap(iy + 1), seed);
		return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
	}

	inline float Fbm(float x, float y, int octaves, uint32_t seed)
	{
		float sum = 0.0f, amp = 0.5f, norm = 0.0f;
		for (int o = 0; o < octaves; ++o, amp *= 0.5f, x *= 2.0f, y *= 2.0f)
		{
			sum += amp * ValueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u);
			norm += amp;
		}
		return sum / norm;
	}

	// Tileable fractal noise for textures: `cells` base cells per tile, octaves double the frequency.
	inline float TileFbm(float u, float v, int cells, int octaves, uint32_t seed)
	{
		float sum = 0.0f, amp = 0.5f, norm = 0.0f;
		int period = cells;
		for (int o = 0; o < octaves; ++o, amp *= 0.5f, period *= 2)
		{
			sum += amp * ValueNoise(u * static_cast<float>(period), v * static_cast<float>(period), seed + static_cast<uint32_t>(o) * 31u, period);
			norm += amp;
		}
		return sum / norm;
	}

	// Dune cross-section: phase 0..1 along the wind; 0 = trough, `crest` = crest, back to 0 on the steep lee side.
	inline float DuneProfile(float phase, float crest, float sharpness)
	{
		float cosine, triangle;
		if (phase < crest)
		{
			const float t = phase / crest;
			cosine = 0.5f - 0.5f * std::cos(kPi * t);
			triangle = t;
		}
		else
		{
			const float t = (phase - crest) / (1.0f - crest);
			cosine = 0.5f + 0.5f * std::cos(kPi * t);
			triangle = 1.0f - t;
		}
		return glm::mix(cosine, triangle, sharpness);
	}

	// The hills' ground before any water is dug into it (Terrain::CreateHills): rounded hilltops and flatter valleys.
	inline float HillHeight(const glm::vec2& pos, float scale, int octaves, uint32_t seed, float flatten, float height)
	{
		float n = Fbm(pos.x / scale, pos.y / scale, octaves, seed);
		n = glm::clamp((n - 0.25f) / 0.6f, 0.0f, 1.0f);
		n = glm::mix(n, n * n * (3.0f - 2.0f * n), flatten + 0.35f);   // rounded hilltops, flatter valleys
		return n * height;
	}

	// The dunes' ground (Terrain::CreateDunes): crests across the wind that meander, of different sizes, over gentle large hills.
	inline float DuneHeight(const glm::vec2& pos, float windAngleRadians, float wavelength, float height, float crestPosition, float crestSharpness,
		float warpAmount, float warpScale, float sizeVariation, float baseHills, uint32_t seed)
	{
		const glm::vec2 wind(std::cos(windAngleRadians), std::sin(windAngleRadians));
		// Crest lines meander: shift the along-wind coordinate by low-frequency noise.
		const float warp = (Fbm(pos.x / warpScale, pos.y / warpScale, 3, seed) - 0.5f) * 2.0f * warpAmount;
		const float s = glm::dot(pos, wind) / wavelength + warp;
		const float phase = s - std::floor(s);
		// Neighbouring dune ridges differ in size.
		const float cell = std::floor(s);
		const float across = glm::dot(pos, glm::vec2(-wind.y, wind.x));
		const float size01 = ValueNoise(cell * 3.7f, across / 160.0f, seed + 7);
		const float amplitude = height * (1.0f + sizeVariation * (size01 - 0.5f) * 2.0f);
		const float hills = (Fbm(pos.x / 170.0f, pos.y / 170.0f, 3, seed + 3) - 0.5f) * 2.0f * baseHills;
		return amplitude * DuneProfile(phase, crestPosition, crestSharpness) + hills;
	}
}
