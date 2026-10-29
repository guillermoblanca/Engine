#include "GB/Visual/ProcMesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		void AddTri(MeshData& m, uint32_t a, uint32_t b, uint32_t c) { m.Indices.insert(m.Indices.end(), { a, b, c }); }

		Vertex MakeVertex(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv)
		{
			Vertex v;
			v.Position = p;
			v.Normal = n;
			v.UV = uv;
			v.Tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
			return v;
		}

		// Smooth value noise on the unit sphere (3D lattice, hashed corners, quintic fade): deterministic, no tables.
		float Lattice(int x, int y, int z, uint32_t seed)
		{
			return HashUnit(VisualHash(static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u, static_cast<uint32_t>(z) * 83492791u, seed)) * 2.0f - 1.0f;
		}

		float Noise3(const glm::vec3& p, uint32_t seed)
		{
			const glm::vec3 f = glm::floor(p);
			const glm::vec3 t = p - f;
			const glm::vec3 u = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
			const int x = static_cast<int>(f.x), y = static_cast<int>(f.y), z = static_cast<int>(f.z);
			auto lerp = [](float a, float b, float k) { return a + (b - a) * k; };
			const float x00 = lerp(Lattice(x, y, z, seed), Lattice(x + 1, y, z, seed), u.x);
			const float x10 = lerp(Lattice(x, y + 1, z, seed), Lattice(x + 1, y + 1, z, seed), u.x);
			const float x01 = lerp(Lattice(x, y, z + 1, seed), Lattice(x + 1, y, z + 1, seed), u.x);
			const float x11 = lerp(Lattice(x, y + 1, z + 1, seed), Lattice(x + 1, y + 1, z + 1, seed), u.x);
			return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
		}
	}

	uint32_t VisualHash(uint32_t a, uint32_t b, uint32_t c)
	{
		// (Bob Jenkins' final mix: good avalanche with integer operations only)
		auto rot = [](uint32_t x, int k) { return (x << k) | (x >> (32 - k)); };
		c ^= b; c -= rot(b, 14);
		a ^= c; a -= rot(c, 11);
		b ^= a; b -= rot(a, 25);
		c ^= b; c -= rot(b, 16);
		a ^= c; a -= rot(c, 4);
		b ^= a; b -= rot(a, 14);
		c ^= b; c -= rot(b, 24);
		return c;
	}

	uint32_t VisualHash(const char* text, uint32_t seed)
	{
		uint32_t h = 2166136261u ^ seed;
		for (const char* p = text; p && *p; ++p) { h ^= static_cast<uint8_t>(*p); h *= 16777619u; }
		return VisualHash(h, seed);
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

	namespace ProcMesh
	{
		MeshData Box(const glm::vec3& size)
		{
			MeshData m;
			const glm::vec3 h = size * 0.5f;
			// six faces: normal, the two in-face axes (u, v)
			const glm::vec3 faces[6][3] = {
				{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } }, { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
				{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } }, { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
				{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } }, { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } } };
			for (const auto& f : faces)
			{
				const glm::vec3 n = f[0], u = f[1], v = f[2];
				const float du = std::fabs(glm::dot(u, size)), dv = std::fabs(glm::dot(v, size));
				const glm::vec3 c = n * h;
				const uint32_t b = static_cast<uint32_t>(m.Vertices.size());
				for (int k = 0; k < 4; ++k)
				{
					const float su = (k == 1 || k == 2) ? 0.5f : -0.5f, sv = (k >= 2) ? 0.5f : -0.5f;
					m.Vertices.push_back(MakeVertex(c + u * (su * du) + v * (sv * dv), n, { (su + 0.5f) * du, (sv + 0.5f) * dv }));
				}
				AddTri(m, b, b + 1, b + 2);
				AddTri(m, b, b + 2, b + 3);
			}
			m.ComputeTangents();
			return m;
		}

		MeshData Cylinder(float bottom, float top, float height, int sides, bool caps, float bend)
		{
			MeshData m;
			sides = std::clamp(sides, 3, 48);
			const int rings = std::fabs(bend) > 1e-4f ? 5 : 1;
			const float around = std::max(1.0f, std::round(2.0f * kPi * std::max(bottom, top)));   // whole texture repeats around: no seam in the grain
			const float slope = (bottom - top) / std::max(height, 1e-4f);
			for (int r = 0; r <= rings; ++r)
			{
				const float t = static_cast<float>(r) / static_cast<float>(rings);
				const float radius = bottom + (top - bottom) * t;
				const float off = bend * t * t;   // a smooth lean, growing toward the top
				for (int s = 0; s <= sides; ++s)
				{
					const float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(sides);
					const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a));
					const glm::vec3 n = glm::normalize(glm::vec3(dir.x, slope, dir.z));
					m.Vertices.push_back(MakeVertex(glm::vec3(dir.x * radius + off, height * t, dir.z * radius), n, { around * static_cast<float>(s) / static_cast<float>(sides), height * t }));
				}
			}
			for (int r = 0; r < rings; ++r)
				for (int s = 0; s < sides; ++s)
				{
					const uint32_t a = static_cast<uint32_t>(r * (sides + 1) + s), b = a + 1, c = a + static_cast<uint32_t>(sides + 1), d = c + 1;
					AddTri(m, a, c, b);
					AddTri(m, b, c, d);
				}
			if (caps)
				for (int end = 0; end < 2; ++end)
				{
					const float radius = end ? top : bottom;
					if (radius <= 1e-5f) continue;
					const float y = end ? height : 0.0f, off = end ? bend : 0.0f;
					const glm::vec3 n(0.0f, end ? 1.0f : -1.0f, 0.0f);
					const uint32_t centre = static_cast<uint32_t>(m.Vertices.size());
					m.Vertices.push_back(MakeVertex({ off, y, 0.0f }, n, { 0.5f, 0.5f }));
					for (int s = 0; s <= sides; ++s)
					{
						const float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(sides);
						m.Vertices.push_back(MakeVertex({ std::cos(a) * radius + off, y, std::sin(a) * radius }, n, { 0.5f + std::cos(a) * radius, 0.5f + std::sin(a) * radius }));
					}
					for (int s = 0; s < sides; ++s)
					{
						const uint32_t a = centre + 1 + static_cast<uint32_t>(s), b = a + 1;
						if (end) AddTri(m, centre, b, a); else AddTri(m, centre, a, b);
					}
				}
			m.ComputeTangents();
			return m;
		}

		MeshData Blob(const glm::vec3& scale, int detail, float roughness, uint32_t seed, float flatBase)
		{
			// icosahedron
			const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
			std::vector<glm::vec3> p = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
				{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
			for (glm::vec3& v : p) v = glm::normalize(v);
			std::vector<uint32_t> idx = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
				3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
			for (int d = 0; d < std::clamp(detail, 0, 4); ++d)
			{
				std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
				auto middle = [&](uint32_t a, uint32_t b)
				{
					const auto key = std::make_pair(std::min(a, b), std::max(a, b));
					const auto it = mid.find(key);
					if (it != mid.end()) return it->second;
					p.push_back(glm::normalize(p[a] + p[b]));
					const uint32_t i = static_cast<uint32_t>(p.size() - 1);
					mid.emplace(key, i);
					return i;
				};
				std::vector<uint32_t> next;
				for (size_t i = 0; i + 2 < idx.size(); i += 3)
				{
					const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2], ab = middle(a, b), bc = middle(b, c), ca = middle(c, a);
					next.insert(next.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
				}
				idx.swap(next);
			}
			// displace along the normal by two octaves of smooth noise, then flatten the base and scale
			std::vector<glm::vec3> pos(p.size());
			for (size_t i = 0; i < p.size(); ++i)
			{
				const glm::vec3 d = p[i];
				const float n = Noise3(d * 1.7f + glm::vec3(0.31f, 0.17f, 0.73f), seed) * 0.7f + Noise3(d * 3.9f, seed ^ 0x51ED270Bu) * 0.3f;
				glm::vec3 v = d * (1.0f + roughness * n);
				if (flatBase > -1.0f && v.y < flatBase) v.y = flatBase + (v.y - flatBase) * 0.08f;
				pos[i] = v * scale;
			}
			MeshData m;
			m.Vertices.resize(p.size());
			std::vector<glm::vec3> normals(p.size(), glm::vec3(0.0f));
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				const glm::vec3 fn = glm::cross(pos[idx[i + 1]] - pos[idx[i]], pos[idx[i + 2]] - pos[idx[i]]);
				for (int k = 0; k < 3; ++k) normals[idx[i + static_cast<size_t>(k)]] += fn;
			}
			for (size_t i = 0; i < p.size(); ++i)
			{
				const glm::vec3 n = glm::length(normals[i]) > 1e-10f ? glm::normalize(normals[i]) : p[i];
				// UVs: the direction's longitude around and the height, in metres (a seam at the back is hidden by the grain)
				const float u = (std::atan2(p[i].z, p[i].x) / (2.0f * kPi) + 0.5f) * std::max(1.0f, std::round(2.0f * kPi * std::max(scale.x, scale.z)));
				m.Vertices[i] = MakeVertex(pos[i], n, { u, pos[i].y });
			}
			m.Indices = std::move(idx);
			m.ComputeTangents();
			return m;
		}

		MeshData Blade(float width, float height, float lean, int segments)
		{
			MeshData m;
			segments = std::clamp(segments, 1, 8);
			for (int side = 0; side < 2; ++side)
			{
				const uint32_t base = static_cast<uint32_t>(m.Vertices.size());
				const glm::vec3 n(0.0f, 0.0f, side ? -1.0f : 1.0f);
				for (int s = 0; s <= segments; ++s)
				{
					const float t = static_cast<float>(s) / static_cast<float>(segments);
					const float w = width * 0.5f * (1.0f - t * 0.9f), x = lean * t * t, y = height * t;
					m.Vertices.push_back(MakeVertex({ x - w, y, 0.0f }, n, { 0.0f, y }));
					m.Vertices.push_back(MakeVertex({ x + w, y, 0.0f }, n, { width, y }));
				}
				for (int s = 0; s < segments; ++s)
				{
					const uint32_t a = base + static_cast<uint32_t>(s * 2);
					if (side) { AddTri(m, a, a + 2, a + 1); AddTri(m, a + 1, a + 2, a + 3); }
					else { AddTri(m, a, a + 1, a + 2); AddTri(m, a + 1, a + 3, a + 2); }
				}
			}
			m.ComputeTangents();
			return m;
		}
	}
}
