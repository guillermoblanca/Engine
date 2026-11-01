#include "GB/World/FlowAccumulation.h"
#include <algorithm>
#include <numeric>

namespace GB
{
	namespace
	{
		constexpr int kDI[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		constexpr int kDJ[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		const float kDist[8] = { 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };
	}

	FlowAccumulationResult FlowAccumulation::Run(const Heightmap& heightmap)
	{
		const uint32_t n = heightmap.GetResolution();
		FlowAccumulationResult result;
		result.N = n;
		result.Accumulation.assign(static_cast<size_t>(n) * static_cast<size_t>(n), 1.0f);
		if (n < 3) return result;

		// Where each cell drains to (flat index, or -1 for a pit/the map's own lowest point), found once up
		// front from the unmodified heights -- flow direction never changes while we only read accumulation.
		std::vector<int> downstream(result.Accumulation.size(), -1);
		for (uint32_t j = 0; j < n; ++j)
		{
			for (uint32_t i = 0; i < n; ++i)
			{
				const float h = heightmap.At(i, j);
				float bestSlope = 0.0f;
				int bestK = -1;
				for (int k = 0; k < 8; ++k)
				{
					const int ni = static_cast<int>(i) + kDI[k], nj = static_cast<int>(j) + kDJ[k];
					if (ni < 0 || nj < 0 || ni >= static_cast<int>(n) || nj >= static_cast<int>(n)) continue;
					const float slope = (h - heightmap.At(static_cast<uint32_t>(ni), static_cast<uint32_t>(nj))) / kDist[static_cast<size_t>(k)];
					if (slope > bestSlope) { bestSlope = slope; bestK = k; }
				}
				if (bestK >= 0)
				{
					const int ni = static_cast<int>(i) + kDI[bestK], nj = static_cast<int>(j) + kDJ[bestK];
					downstream[static_cast<size_t>(j) * n + i] = nj * static_cast<int>(n) + ni;
				}
			}
		}

		// Visit every cell highest-first: by the time a cell is processed, every neighbour that could drain
		// into it (strictly higher, so already visited) already has, so its own accumulation is final and safe
		// to hand on to whatever it drains into.
		std::vector<uint32_t> order(result.Accumulation.size());
		std::iota(order.begin(), order.end(), 0u);
		std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b)
		{
			return heightmap.At(a % n, a / n) > heightmap.At(b % n, b / n);
		});

		for (uint32_t idx : order)
		{
			const int down = downstream[idx];
			if (down >= 0) result.Accumulation[static_cast<size_t>(down)] += result.Accumulation[idx];
		}
		return result;
	}
}
