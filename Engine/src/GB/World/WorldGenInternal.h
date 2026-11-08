#pragma once
#include "GB/World/WorldGen.h"
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace GB::WorldGenDetail
{
	// Shared by WorldGen.cpp (generator 4 and what every generator shares) and WorldGenLegacy.cpp (the frozen generators 1-3). Not public.

	inline float Hash2(float x, float z, uint32_t seed)
	{
		uint32_t h = static_cast<uint32_t>(static_cast<int>(std::floor(x * 8.0f))) * 374761393u + static_cast<uint32_t>(static_cast<int>(std::floor(z * 8.0f))) * 668265263u + seed * 2246822519u;
		h = (h ^ (h >> 13)) * 1274126177u;
		return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
	}
	inline float Noise2(float x, float z, uint32_t seed)
	{
		const float fx = std::floor(x), fz = std::floor(z);
		float u = x - fx, v = z - fz;
		u = u * u * (3.0f - 2.0f * u); v = v * v * (3.0f - 2.0f * v);
		auto h = [&](float dx, float dz) { return Hash2((fx + dx) * 0.125f, (fz + dz) * 0.125f, seed); };
		return glm::mix(glm::mix(h(0, 0), h(1, 0), u), glm::mix(h(0, 1), h(1, 1), u), v);
	}
	inline float Fbm2(float x, float z, uint32_t seed)
	{
		float v = 0.0f, a = 0.5f;
		for (int i = 0; i < 4; ++i) { v += a * Noise2(x, z, seed + static_cast<uint32_t>(i)); x = x * 2.0f + 11.0f; z = z * 2.0f + 5.0f; a *= 0.5f; }
		return v / 0.9375f;
	}

	// Generators 1-3: the ground with its lake, river and erosion (and the snow line of an alpine world), into w.TerrainPtr and w.Hills. Frozen:
	// a saved world rebuilds its baseline from this code, bit for bit (GB/World/WorldGenVersion.h).
	void LegacyTerrain(const WorldDesc& desc, GeneratedWorld& w);
}
