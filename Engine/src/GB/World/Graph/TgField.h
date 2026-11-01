#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// A regular scalar field over the XZ plane: what flows between terrain graph nodes (a height, a mask, a
	// moisture value, a biome id...). Pure data, no evaluation and no I/O (docs/GRAFO_TERRENO.md).
	struct TgField
	{
		uint32_t N = 0;            // samples per side
		float Size = 1.0f;         // metres per side
		glm::vec2 Center{ 0.0f };  // world position of the field's centre

		std::vector<float> Values; // N*N, row major (index = j*N + i)

		void Resize(uint32_t n, float size, const glm::vec2& center = { 0.0f, 0.0f })
		{
			N = n; Size = size; Center = center;
			Values.assign(static_cast<size_t>(n) * n, 0.0f);
		}

		float At(uint32_t i, uint32_t j) const { return Values[static_cast<size_t>(j) * N + i]; }
		float& At(uint32_t i, uint32_t j) { return Values[static_cast<size_t>(j) * N + i]; }

		glm::vec2 Min() const { return Center - Size * 0.5f; }
		float Spacing() const { return N > 1 ? Size / static_cast<float>(N - 1) : Size; }

		// World x,z of a grid point.
		glm::vec2 SamplePosition(uint32_t i, uint32_t j) const
		{
			return Min() + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * Spacing();
		}

		// Bilinear value at a world position; clamps to the border outside the field.
		float Sample(float x, float z) const
		{
			if (N == 0) return 0.0f;
			if (N == 1) return Values[0];
			const glm::vec2 min = Min();
			const float spacing = Spacing();
			float fi = (x - min.x) / spacing;
			float fj = (z - min.y) / spacing;
			fi = glm::clamp(fi, 0.0f, static_cast<float>(N - 1));
			fj = glm::clamp(fj, 0.0f, static_cast<float>(N - 1));
			const uint32_t i0 = static_cast<uint32_t>(fi), j0 = static_cast<uint32_t>(fj);
			const uint32_t i1 = i0 + 1 < N ? i0 + 1 : i0, j1 = j0 + 1 < N ? j0 + 1 : j0;
			const float tx = fi - static_cast<float>(i0), tz = fj - static_cast<float>(j0);
			const float a = glm::mix(At(i0, j0), At(i1, j0), tx);
			const float b = glm::mix(At(i0, j1), At(i1, j1), tx);
			return glm::mix(a, b, tz);
		}
	};
}
