#include "GB/Scene/MeshData.h"
#include <cmath>

namespace GB
{
	namespace
	{
		void AddTri(MeshData& m, uint32_t a, uint32_t b, uint32_t c) { m.Indices.insert(m.Indices.end(), { a, b, c }); }
	}

	void MeshData::Append(const MeshData& other, const glm::mat4& transform)
	{
		const uint32_t base = static_cast<uint32_t>(Vertices.size());
		const glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(transform)));
		const bool mirrored = glm::determinant(glm::mat3(transform)) < 0.0f;
		Vertices.reserve(Vertices.size() + other.Vertices.size());
		for (Vertex v : other.Vertices)
		{
			v.Position = glm::vec3(transform * glm::vec4(v.Position, 1.0f));
			const glm::vec3 n = normalM * v.Normal;
			v.Normal = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0, 1, 0);
			const glm::vec3 t = glm::mat3(transform) * glm::vec3(v.Tangent);
			v.Tangent = glm::vec4(glm::length(t) > 1e-8f ? glm::normalize(t) : glm::vec3(1, 0, 0), mirrored ? -v.Tangent.w : v.Tangent.w);
			Vertices.push_back(v);
		}
		Indices.reserve(Indices.size() + other.Indices.size());
		for (size_t i = 0; i + 2 < other.Indices.size(); i += 3)
		{
			const uint32_t a = other.Indices[i] + base, b = other.Indices[i + 1] + base, c = other.Indices[i + 2] + base;
			if (mirrored) AddTri(*this, a, c, b); else AddTri(*this, a, b, c);
		}
	}

	void MeshData::ComputeTangents()
	{
		std::vector<glm::vec3> tan(Vertices.size(), glm::vec3(0.0f)), bit(Vertices.size(), glm::vec3(0.0f));
		for (size_t i = 0; i + 2 < Indices.size(); i += 3)
		{
			const Vertex& a = Vertices[Indices[i]], & b = Vertices[Indices[i + 1]], & c = Vertices[Indices[i + 2]];
			const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
			const glm::vec2 d1 = b.UV - a.UV, d2 = c.UV - a.UV;
			const float det = d1.x * d2.y - d2.x * d1.y;
			if (std::fabs(det) < 1e-12f) continue;
			const float r = 1.0f / det;
			const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r, s = (e2 * d1.x - e1 * d2.x) * r;
			for (int k = 0; k < 3; ++k) { tan[Indices[i + static_cast<size_t>(k)]] += t; bit[Indices[i + static_cast<size_t>(k)]] += s; }
		}
		for (size_t i = 0; i < Vertices.size(); ++i)
		{
			const glm::vec3 n = Vertices[i].Normal;
			glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
			if (glm::length(t) < 1e-8f)
			{
				t = glm::cross(std::fabs(n.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), n);
				if (glm::length(t) < 1e-8f) t = glm::vec3(1, 0, 0);
			}
			t = glm::normalize(t);
			const float w = glm::dot(glm::cross(n, t), bit[i]) < 0.0f ? -1.0f : 1.0f;
			Vertices[i].Tangent = glm::vec4(t, w);
		}
	}

	void MeshData::Bounds(glm::vec3& min, glm::vec3& max) const
	{
		if (Vertices.empty()) { min = max = glm::vec3(0.0f); return; }
		min = max = Vertices[0].Position;
		for (const Vertex& v : Vertices) { min = glm::min(min, v.Position); max = glm::max(max, v.Position); }
	}

	uint64_t MeshData::Hash() const
	{
		uint64_t h = 1469598103934665603ull;
		auto mix = [&](const void* data, size_t bytes)
		{
			const auto* p = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; }
		};
		for (const Vertex& v : Vertices) { mix(&v.Position, sizeof v.Position); mix(&v.Normal, sizeof v.Normal); mix(&v.UV, sizeof v.UV); }
		mix(Indices.data(), Indices.size() * sizeof(uint32_t));
		return h;
	}
}
