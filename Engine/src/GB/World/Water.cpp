#include "Water.h"
#include <cmath>

namespace GB
{
	WaterSurface::WaterSurface(float size, uint32_t subdivisions) : m_Size(size), m_Subdivisions(subdivisions) {}

	WaterSurface::WaterSurface(const RiverData& river, float widthScale) : m_Size(0.0f)
	{
		const size_t n = river.Points.size();
		if (n < 2)
			return;
		const int columns = 9;   // across the river
		const float half = river.HalfWidth * widthScale;
		float along = 0.0f;
		for (size_t i = 0; i < n; ++i)
		{
			const glm::vec3 p = river.Points[i];
			const glm::vec3 next = river.Points[std::min(i + 1, n - 1)], prev = river.Points[i == 0 ? 0 : i - 1];
			glm::vec2 dir(next.x - prev.x, next.z - prev.z);
			dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
			if (i > 0) along += glm::length(glm::vec2(p.x - prev.x, p.z - prev.z));
			const glm::vec2 side(-dir.y, dir.x);
			for (int c = 0; c < columns; ++c)
			{
				const float u = static_cast<float>(c) / static_cast<float>(columns - 1) * 2.0f - 1.0f;   // -1..1 across
				Vertex v;
				v.Position = { p.x + side.x * u * half, p.y, p.z + side.y * u * half };
				v.Normal = { 0, 1, 0 };
				v.UV = { u, along / 4.0f };
				v.Tangent = { dir.x, dir.y, river.Speed[i], std::fabs(u) };   // xy = flow direction (xz), z = speed m/s, w = |across|
				m_RiverVertices.push_back(v);
			}
		}
		for (size_t i = 0; i + 1 < n; ++i)
			for (int c = 0; c + 1 < columns; ++c)
			{
				const uint32_t a = static_cast<uint32_t>(i) * columns + static_cast<uint32_t>(c), b = a + 1, d = a + columns, e = d + 1;
				m_RiverIndices.insert(m_RiverIndices.end(), { a, d, b, b, d, e });
			}
	}

	const Mesh& WaterSurface::GetMesh() const
	{
		if (!m_Mesh && !m_RiverVertices.empty())
			m_Mesh = std::make_unique<Mesh>(m_RiverVertices, m_RiverIndices);
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
