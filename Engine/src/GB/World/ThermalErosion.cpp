#include "GB/World/ThermalErosion.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace GB
{
	namespace
	{
		// The 8 neighbours of a cell, with how far away they are in cell units (1 for the four orthogonal ones,
		// sqrt(2) for the diagonals) so the talus check compares like with like regardless of direction.
		constexpr int kDI[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		constexpr int kDJ[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		const float kDist[8] = { 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };
	}

	ThermalErosionStats ThermalErosion::Run(Heightmap& heightmap, const ThermalErosionParams& params)
	{
		ThermalErosionStats stats;
		const int n = static_cast<int>(heightmap.GetResolution());
		if (n < 3 || params.Iterations <= 0 || params.Strength <= 0.0f) return stats;

		const float spacing = heightmap.GetSpacing();
		const float maxDrop = std::tan(glm::radians(std::clamp(params.TalusAngleDegrees, 1.0f, 89.0f))) * spacing;
		const float strength = std::clamp(params.Strength, 0.0f, 1.0f);

		for (int pass = 0; pass < params.Iterations; ++pass)
		{
			// In place, one cell at a time (Gauss-Seidel, not a separate delta applied all at once): a cell a few
			// steps down a uniform slope can be the "lowest steep neighbour" of several cells above it, and
			// crediting all of them from a stale snapshot in the same pass can push it, briefly, above the
			// original summit -- reading the latest height as each transfer happens rules that out: every amount
			// a cell gives is bounded by max(0, its OWN current height minus the talus drop), which by induction
			// is always <= the terrain's original maximum, so nothing this does can ever exceed it.
			for (int j = 0; j < n; ++j)
			{
				for (int i = 0; i < n; ++i)
				{
					const float h = heightmap.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
					int bestK = -1;
					float bestExcess = 0.0f;
					for (int k = 0; k < 8; ++k)
					{
						const int ni = i + kDI[k], nj = j + kDJ[k];
						if (ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
						const float drop = h - heightmap.At(static_cast<uint32_t>(ni), static_cast<uint32_t>(nj)) - maxDrop * kDist[static_cast<size_t>(k)];
						if (drop > bestExcess) { bestExcess = drop; bestK = k; }
					}
					if (bestK < 0) continue;

					const int ni = i + kDI[bestK], nj = j + kDJ[bestK];
					const float amount = strength * bestExcess;
					heightmap.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)) = h - amount;
					heightmap.At(static_cast<uint32_t>(ni), static_cast<uint32_t>(nj)) += amount;
					stats.Moved += static_cast<double>(amount);
					stats.MinI = std::min(stats.MinI, std::min(i, ni)); stats.MaxI = std::max(stats.MaxI, std::max(i, ni));
					stats.MinJ = std::min(stats.MinJ, std::min(j, nj)); stats.MaxJ = std::max(stats.MaxJ, std::max(j, nj));
				}
			}
		}
		return stats;
	}
}
