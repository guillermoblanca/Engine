#include "GB/World/Scatter.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		// The generator ScatterTrees always used (the same as Trees.cpp's, which keeps its own copy for the tree meshes): the same seed places
		// the same trees.
		struct Rng
		{
			uint64_t s;
			explicit Rng(uint64_t seed) : s(0x9E3779B97F4A7C15ull ^ (seed * 0xBF58476D1CE4E5B9ull)) { next(); next(); }
			float next()
			{
				s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
				return static_cast<float>(((s * 0x2545F4914F6CDD1Dull) >> 40) & 0xFFFFFF) / 16777216.0f;
			}
			float range(float a, float b) { return a + (b - a) * next(); }
		};
	}

	std::vector<TreePlacement> ScatterTrees(const Heightmap& hm, int count, int variants, float minSpacing, uint32_t seed,
		float minHeight, float maxHeight, float maxSlopeDegrees, const std::function<bool(float, float)>& allow)
	{
		std::vector<TreePlacement> out;
		Rng rng(static_cast<uint64_t>(seed) * 40503u + 7u);
		const glm::vec2 lo = hm.GetMin(), hi = hm.GetMax();
		const float cell = std::max(minSpacing, 0.5f);
		std::unordered_map<int64_t, std::vector<glm::vec2>> grid;
		auto key = [&](int cx, int cz) { return static_cast<int64_t>(cx) * 73856093ll ^ static_cast<int64_t>(cz) * 19349663ll; };
		const float cosSlope = std::cos(glm::radians(maxSlopeDegrees));

		for (int attempt = 0; attempt < count * 40 && static_cast<int>(out.size()) < count; ++attempt)
		{
			const float x = rng.range(lo.x + 4.0f, hi.x - 4.0f), z = rng.range(lo.y + 4.0f, hi.y - 4.0f);
			const float y = hm.GetHeight(x, z);
			if (y < minHeight || y > maxHeight || hm.GetNormal(x, z).y < cosSlope || (allow && !allow(x, z)))
				continue;
			const int cx = static_cast<int>(std::floor(x / cell)), cz = static_cast<int>(std::floor(z / cell));
			bool free = true;
			for (int dz = -1; dz <= 1 && free; ++dz)
				for (int dx = -1; dx <= 1 && free; ++dx)
				{
					const auto it = grid.find(key(cx + dx, cz + dz));
					if (it == grid.end())
						continue;
					for (const glm::vec2& q : it->second)
						if (glm::length(q - glm::vec2(x, z)) < minSpacing) { free = false; break; }
				}
			if (!free)
				continue;
			grid[key(cx, cz)].push_back({ x, z });
			TreePlacement tp;
			tp.Variant = variants > 0 ? static_cast<int>(rng.next() * static_cast<float>(variants)) % variants : 0;
			tp.Position = { x, y - 0.05f, z };
			tp.Yaw = rng.range(0.0f, 2.0f * kPi);
			tp.Scale = rng.range(0.8f, 1.25f);
			out.push_back(tp);
		}
		return out;
	}
}
