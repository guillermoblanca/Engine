#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Regular grid of heights covering a square of the XZ plane, centered on `Center`. Plain CPU data: the terrain graph, the erosions and the
	// water carving work on it without the rest of GB::Terrain (GB_WorldGen, docs/GRAFO_TERRENO.md section 6). Terrain.h includes it.
	class Heightmap
	{
	public:
		Heightmap(uint32_t resolution, float size, const glm::vec2& center = { 0.0f, 0.0f });

		uint32_t GetResolution() const { return m_Resolution; }   // samples per side
		float GetSize() const { return m_Size; }                  // meters per side
		float GetSpacing() const { return m_Size / static_cast<float>(m_Resolution - 1); }
		glm::vec2 GetMin() const { return m_Center - m_Size * 0.5f; }
		glm::vec2 GetMax() const { return m_Center + m_Size * 0.5f; }
		bool Contains(float x, float z) const;

		float& At(uint32_t i, uint32_t j) { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		float At(uint32_t i, uint32_t j) const { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		glm::vec2 SamplePosition(uint32_t i, uint32_t j) const;   // world x,z of a grid point
		const float* Data() const { return m_Heights.data(); }    // the resolution^2 heights, row major (index = j * resolution + i)

		// Bilinear height at a world position; clamps to the border outside the map.
		float GetHeight(float x, float z) const;
		// Smooth surface normal (central differences) at a world position.
		glm::vec3 GetNormal(float x, float z) const;
		float GetMinHeight() const;
		float GetMaxHeight() const;

	private:
		uint32_t m_Resolution;
		float m_Size;
		glm::vec2 m_Center;
		std::vector<float> m_Heights;
	};
}
