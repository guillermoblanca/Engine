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
		constexpr int columns = RiverColumns;
		const float half = river.HalfWidth * widthScale;

		// Smooth (Catmull-Rom) resampling of the centerline: position, level and speed at every RiverStep meters.
		struct Sample { glm::vec3 P; float Speed; };
		std::vector<Sample> path;
		auto cubic = [](const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t)
		{
			return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t * t * t);
		};
		for (size_t i = 0; i + 1 < n; ++i)
		{
			const glm::vec3 p0 = river.Points[i == 0 ? 0 : i - 1], p1 = river.Points[i], p2 = river.Points[i + 1], p3 = river.Points[std::min(i + 2, n - 1)];
			const int steps = std::max(1, static_cast<int>(std::ceil(glm::length(glm::vec2(p2.x - p1.x, p2.z - p1.z)) / RiverStep)));
			for (int k = 0; k < steps; ++k)
			{
				const float t = static_cast<float>(k) / static_cast<float>(steps);
				glm::vec3 q = cubic(p0, p1, p2, p3, t);
				q.y = glm::mix(p1.y, p2.y, t);   // the level never overshoots
				path.push_back({ q, glm::mix(river.Speed[i], river.Speed[i + 1], t) });
			}
		}
		path.push_back({ river.Points[n - 1], river.Speed[n - 1] });
		if (river.MouthLength > 0.0f)   // continue into the lake at the same level, standing still
		{
			const glm::vec3 end = path.back().P, before = path[path.size() - 2].P;
			glm::vec2 dir(end.x - before.x, end.z - before.z);
			dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
			const int extra = static_cast<int>(std::ceil(river.MouthLength * 0.35f / RiverStep));
			for (int k = 1; k <= extra; ++k)
				path.push_back({ { end.x + dir.x * RiverStep * static_cast<float>(k), end.y, end.z + dir.y * RiverStep * static_cast<float>(k) }, 0.0f });
		}

		const size_t count = path.size();
		std::vector<float> arc(count, 0.0f);
		for (size_t i = 1; i < count; ++i)
			arc[i] = arc[i - 1] + glm::length(glm::vec2(path[i].P.x - path[i - 1].P.x, path[i].P.z - path[i - 1].P.z));
		const float total = arc.back();
		for (size_t i = 0; i < count; ++i)
		{
			const Sample& s = path[i];
			const glm::vec3 next = path[std::min(i + 1, count - 1)].P, prev = path[i == 0 ? 0 : i - 1].P;
			glm::vec2 dir(next.x - prev.x, next.z - prev.z);
			dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
			const glm::vec2 side(-dir.y, dir.x);
			// 1 along the river, 0 at the very end of the extension: opacity, and how much the mouth opens up.
			const float blend = river.MouthLength > 0.0f ? 1.0f - glm::smoothstep(total - river.MouthLength * 0.65f, total, arc[i]) : 1.0f;
			const float width = half * (1.0f + 0.7f * (1.0f - blend));
			for (int c = 0; c < columns; ++c)
			{
				const float u = static_cast<float>(c) / static_cast<float>(columns - 1) * 2.0f - 1.0f;   // -1..1 across
				Vertex v;
				v.Position = { s.P.x + side.x * u * width, s.P.y, s.P.z + side.y * u * width };
				v.Normal = { blend, 1.0f, 0.0f };   // x = opacity toward the lake (the shader reads it in place of a normal)
				v.UV = { u, arc[i] / 4.0f };
				// xy = flow direction (xz), z = speed m/s (slower toward the banks), w = |across|
				v.Tangent = { dir.x, dir.y, s.Speed * (1.0f - 0.55f * u * u), std::fabs(u) };
				m_RiverVertices.push_back(v);
			}
		}
		for (size_t i = 0; i + 1 < count; ++i)
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
