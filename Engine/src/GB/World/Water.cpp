#include "Water.h"
#include <cmath>

namespace GB
{
	WaterSurface::WaterSurface(float size, uint32_t subdivisions) : m_Size(size), m_Subdivisions(subdivisions) {}

	const Mesh& WaterSurface::GetMesh() const
	{
		if (!m_Mesh)
			m_Mesh = std::make_unique<Mesh>(Mesh::CreateGrid(m_Size, m_Subdivisions));
		return *m_Mesh;
	}

	float WaterSurface::HeightAt(float x, float z, float time) const
	{
		const WaterParams& p = Params;
		const glm::vec2 base = glm::length(p.Direction) > 1e-4f ? glm::normalize(p.Direction) : glm::vec2(1.0f, 0.0f);
		float h = 0.0f;
		for (int i = 0; i < WaveSet::Count; ++i)
		{
			const float a = WaveSet::Angle[i], c = std::cos(a), s = std::sin(a);
			const glm::vec2 d(base.x * c - base.y * s, base.x * s + base.y * c);
			const float len = p.WaveLength * WaveSet::Length[i];
			const float k = 6.2831853f / len;
			const float speed = std::sqrt(9.81f / k);
			const float amp = p.WaveHeight * WaveSet::Amp[i] * (i == 0 ? 1.0f : p.Choppiness);
			h += amp * std::sin(k * (d.x * x + d.y * z) - k * speed * time);
		}
		return h;
	}
}
