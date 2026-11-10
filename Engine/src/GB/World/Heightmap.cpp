#include "GB/World/Heightmap.h"
#include <algorithm>

namespace GB
{
	// (moved from Terrain.cpp unchanged: the worlds are the same bit for bit)
	Heightmap::Heightmap(uint32_t resolution, float size, const glm::vec2& center)
		: m_Resolution(std::max(resolution, 2u)), m_Size(size), m_Center(center),
		  m_Heights(static_cast<size_t>(m_Resolution) * m_Resolution, 0.0f)
	{
	}

	bool Heightmap::Contains(float x, float z) const
	{
		const glm::vec2 lo = GetMin(), hi = GetMax();
		return x >= lo.x && x <= hi.x && z >= lo.y && z <= hi.y;
	}

	glm::vec2 Heightmap::SamplePosition(uint32_t i, uint32_t j) const
	{
		return GetMin() + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * GetSpacing();
	}

	float Heightmap::GetHeight(float x, float z) const
	{
		const glm::vec2 lo = GetMin();
		const float maxIndex = static_cast<float>(m_Resolution - 1);
		const float fx = glm::clamp((x - lo.x) / GetSpacing(), 0.0f, maxIndex);
		const float fz = glm::clamp((z - lo.y) / GetSpacing(), 0.0f, maxIndex);
		const uint32_t i0 = std::min(static_cast<uint32_t>(fx), m_Resolution - 2), j0 = std::min(static_cast<uint32_t>(fz), m_Resolution - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		return glm::mix(glm::mix(At(i0, j0), At(i0 + 1, j0), tx), glm::mix(At(i0, j0 + 1), At(i0 + 1, j0 + 1), tx), tz);
	}

	glm::vec3 Heightmap::GetNormal(float x, float z) const
	{
		const float e = GetSpacing();
		const float dx = GetHeight(x + e, z) - GetHeight(x - e, z);
		const float dz = GetHeight(x, z + e) - GetHeight(x, z - e);
		return glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
	}

	float Heightmap::GetMinHeight() const { return *std::min_element(m_Heights.begin(), m_Heights.end()); }
	float Heightmap::GetMaxHeight() const { return *std::max_element(m_Heights.begin(), m_Heights.end()); }
}
