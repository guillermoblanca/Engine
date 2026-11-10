#include "GB/Visual/ProcMesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
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

		MeshData Card(float width, float height, float bend, int segments)
		{
			MeshData m;
			segments = std::clamp(segments, 1, 8);
			for (int s = 0; s <= segments; ++s)
			{
				const float t = static_cast<float>(s) / static_cast<float>(segments);
				const float y = height * t, z = bend * t * t;
				const glm::vec3 n = glm::normalize(glm::vec3(0.0f, -2.0f * bend * t / std::max(height, 1e-4f), 1.0f));
				m.Vertices.push_back(MakeVertex({ -0.5f * width, y, z }, n, { 0.0f, t }));
				m.Vertices.push_back(MakeVertex({ 0.5f * width, y, z }, n, { 1.0f, t }));
			}
			for (int s = 0; s < segments; ++s)
			{
				const uint32_t a = static_cast<uint32_t>(s * 2);
				AddTri(m, a, a + 1, a + 2);
				AddTri(m, a + 1, a + 3, a + 2);
			}
			m.ComputeTangents();
			return m;
		}

		MeshData CurvedCard(float width, float height, int segmentsAcross, int segmentsAlong, float bend, float fold, float twist, float jitter, uint32_t seed)
		{
			MeshData m;
			const int na = std::clamp((segmentsAcross + 1) / 2 * 2, 2, 8), nl = std::clamp(segmentsAlong, 1, 16);
			const glm::vec3 x0(1.0f, 0.0f, 0.0f);
			for (int j = 0; j <= nl; ++j)
			{
				const float t = static_cast<float>(j) / static_cast<float>(nl);
				// the spine as Card's (y up, the top bent toward +Z along t^2); the cross-section turned about it by the twist so far
				const glm::vec3 spine(0.0f, height * t, bend * t * t);
				const glm::vec3 along = glm::normalize(glm::vec3(0.0f, std::max(height, 1e-4f), 2.0f * bend * t));
				const glm::vec3 out0 = glm::normalize(glm::cross(x0, along));
				const float a = twist * t;
				const glm::vec3 across = x0 * std::cos(a) + out0 * std::sin(a);
				const glm::vec3 out = glm::cross(across, along);
				for (int i = 0; i <= na; ++i)
				{
					const float u = static_cast<float>(i) / static_cast<float>(na), s = 2.0f * u - 1.0f;
					const float noise = j == 0 ? 0.0f : jitter * (2.0f * HashUnit(VisualHash(static_cast<uint32_t>(i), static_cast<uint32_t>(j), seed ^ 0x2C1B3C6Du)) - 1.0f);
					m.Vertices.push_back(MakeVertex(spine + across * (0.5f * width * s) + out * (fold * s * s + noise), out, { u, t }));
				}
			}
			const uint32_t row = static_cast<uint32_t>(na + 1);
			for (int j = 0; j < nl; ++j)
				for (int i = 0; i < na; ++i)
				{
					const uint32_t a = static_cast<uint32_t>(j) * row + static_cast<uint32_t>(i), b = a + 1, c = a + row, d = c + 1;
					AddTri(m, a, b, c);
					AddTri(m, b, d, c);
				}
			// normals from the bent sheet: the area-weighted sum of the faces around every vertex
			std::vector<glm::vec3> normals(m.Vertices.size(), glm::vec3(0.0f));
			for (size_t i = 0; i + 2 < m.Indices.size(); i += 3)
			{
				const glm::vec3 fn = glm::cross(m.Vertices[m.Indices[i + 1]].Position - m.Vertices[m.Indices[i]].Position,
				                                m.Vertices[m.Indices[i + 2]].Position - m.Vertices[m.Indices[i]].Position);
				for (int k = 0; k < 3; ++k) normals[m.Indices[i + static_cast<size_t>(k)]] += fn;
			}
			for (size_t i = 0; i < m.Vertices.size(); ++i)
				if (glm::length(normals[i]) > 1e-12f) m.Vertices[i].Normal = glm::normalize(normals[i]);
			m.ComputeTangents();
			return m;
		}

		namespace
		{
			// The placement of a card leaning out of an axis: turned `yaw` about +Y, lifted to `y`, pushed `out` metres from the axis and
			// leaned `lean` radians from the vertical, its top going outward (its front, +Z, then faces up and in, toward the axis).
			glm::mat4 Leaning(float yaw, float y, float out, float lean)
			{
				glm::mat4 m = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
				m = glm::translate(m, glm::vec3(0.0f, y, -out));
				return glm::rotate(m, -lean, glm::vec3(1.0f, 0.0f, 0.0f));
			}
		}

		MeshData CardShrub(float cardWidth, float cardHeight, int cards, uint32_t seed)
		{
			MeshData out;
			auto h01 = [seed](int a, uint32_t b) { return HashUnit(VisualHash(static_cast<uint32_t>(a), b, seed ^ 0x6B43A9B5u)); };
			const float w = cardWidth, h = cardHeight;
			// the core: three upright cards, crossed, a little folded (T107: and a fourth between them, so the middle is closed)
			for (int c = 0; c < 4; ++c)
			{
				const float width = c < 3 ? 0.9f : 0.8f, height = c < 3 ? 1.05f : 0.95f;
				const float yaw = c < 3 ? static_cast<float>(c) * glm::radians(60.0f) + 0.3f * h01(c, 2) : glm::radians(30.0f) + 0.3f * h01(c, 2);
				const MeshData card = CurvedCard(w * width, h * height, 4, 4, -0.06f * h, 0.08f * w, 0.3f * (h01(c, 1) - 0.5f), 0.012f * w, seed * 131u + static_cast<uint32_t>(c));
				out.Append(card, Leaning(yaw, 0.0f, 0.0f, glm::radians(6.0f * (h01(c, 3) - 0.5f))));
			}
			// T107: an inner ring at mid height, leaning less than the outer one: it fills the space between the core and the ring (where the
			// gallery saw through to the dark ground). Hidden mostly, so coarser (three segments along).
			for (int k = 0; k < 5; ++k)
			{
				const float scale = 0.7f + 0.2f * h01(k, 21);
				const MeshData card = CurvedCard(w * scale, h * scale, 4, 3, -0.1f * h * scale, 0.07f * w * scale, 0.4f * (h01(k, 22) - 0.5f), 0.015f * w * scale,
				                                 seed * 131u + 211u + static_cast<uint32_t>(k));
				out.Append(card, Leaning(static_cast<float>(k) * glm::radians(72.0f) + 0.5f * h01(k, 23), h * (0.12f + 0.12f * h01(k, 24)), 0.04f * w,
				                         glm::radians(26.0f + 14.0f * h01(k, 25))));
			}
			// the ring: low cards lean far out, high ones stand up; all droop at the tips, the fold opening upward
			for (int k = 0; k < std::clamp(cards, 0, 64); ++k)
			{
				const float f = h01(k, 5), scale = 0.75f + 0.35f * h01(k, 6);
				const float lean = glm::radians(58.0f - 38.0f * f + 12.0f * (h01(k, 4) - 0.5f));
				const MeshData card = CurvedCard(w * scale, h * scale, 4, 4, -(0.08f + 0.14f * h01(k, 7)) * h * scale, (0.07f + 0.06f * h01(k, 8)) * w * scale,
				                                 0.5f * (h01(k, 9) - 0.5f), 0.02f * w * scale, seed * 131u + 17u + static_cast<uint32_t>(k));
				out.Append(card, Leaning(static_cast<float>(k) * 2.39996f + 0.4f * (h01(k, 3) - 0.5f), h * (0.02f + 0.4f * f), 0.08f * w, lean));
			}
			// the top: two cards nearly upright over the core
			for (int c = 0; c < 2; ++c)
			{
				const MeshData card = CurvedCard(w * 0.7f, h * 0.7f, 4, 4, -0.08f * h, 0.07f * w, 0.2f, 0.015f * w, seed * 131u + 101u + static_cast<uint32_t>(c));
				out.Append(card, Leaning(glm::radians(45.0f + 90.0f * static_cast<float>(c)), 0.32f * h, 0.03f * w, glm::radians(18.0f)));
			}
			// T107: three more up there, higher, turned between those and leaning out a little more: the crown closes over the core (the
			// gallery saw holes at the top)
			for (int c = 0; c < 3; ++c)
			{
				const float scale = 0.55f + 0.15f * h01(c, 31);
				const MeshData card = CurvedCard(w * scale, h * scale, 4, 3, -0.1f * h * scale, 0.07f * w * scale, 0.3f * (h01(c, 32) - 0.5f), 0.015f * w * scale,
				                                 seed * 131u + 307u + static_cast<uint32_t>(c));
				out.Append(card, Leaning(glm::radians(15.0f + 120.0f * static_cast<float>(c)) + 0.4f * h01(c, 33), h * (0.42f + 0.1f * h01(c, 34)), 0.06f * w,
				                         glm::radians(28.0f + 12.0f * h01(c, 35))));
			}
			return out;
		}

		MeshData CardConifer(float height, float radius, int tiers, uint32_t seed)
		{
			MeshData out;
			auto h01 = [seed](int a, uint32_t b) { return HashUnit(VisualHash(static_cast<uint32_t>(a), b, seed ^ 0x1B873593u)); };
			tiers = std::clamp(tiers, 1, 12);
			int n = 0;
			for (int i = 0; i < tiers; ++i)
			{
				const float f = static_cast<float>(i) / static_cast<float>(tiers);
				// T107: the first whorl at 0.18 of the height (was 0.05), so the trunk (CardConiferTrunk) shows under the crown
				const float y = height * (0.18f + 0.6f * f), length = radius * (1.0f - 0.75f * f);
				for (int b = 0; b < 6; ++b, ++n)
				{
					const float size = length * (0.85f + 0.3f * h01(n, 1));
					const MeshData card = CurvedCard(size, size, 4, 5, -(0.12f + 0.12f * h01(n, 2)) * size, 0.1f * size, 0.4f * (h01(n, 3) - 0.5f), 0.015f * size,
					                                 seed * 131u + static_cast<uint32_t>(n));
					const float yaw = static_cast<float>(b) * glm::radians(60.0f) + static_cast<float>(i) * 0.63f + 0.3f * (h01(n, 4) - 0.5f);
					out.Append(card, Leaning(yaw, y + 0.04f * height * (h01(n, 5) - 0.5f), 0.01f * radius, glm::radians(50.0f + 12.0f * h01(n, 6) - 12.0f * f)));
				}
				// T107: three short sprays a whorl nearer the axis and more upright, between the long ones: the crown is fuller near the trunk
				// (from the second whorl up, so the trunk still shows under the first)
				if (i == 0) continue;
				for (int b = 0; b < 3; ++b)
				{
					const uint32_t k = 1000u + static_cast<uint32_t>(i * 3 + b);
					const float size = length * (0.5f + 0.15f * h01(static_cast<int>(k), 1));
					const MeshData card = CurvedCard(size, size, 4, 3, -0.12f * size, 0.1f * size, 0.3f * (h01(static_cast<int>(k), 3) - 0.5f), 0.015f * size, seed * 131u + k);
					const float yaw = (static_cast<float>(b) + 0.5f) * glm::radians(120.0f) + static_cast<float>(i) * 0.63f + 0.4f * (h01(static_cast<int>(k), 4) - 0.5f);
					out.Append(card, Leaning(yaw, y + 0.06f * height * h01(static_cast<int>(k), 5), 0.0f, glm::radians(30.0f + 10.0f * h01(static_cast<int>(k), 6))));
				}
			}
			// the leader
			const float lead = 0.32f * height;
			for (int c = 0; c < 2; ++c)
			{
				const MeshData card = CurvedCard(0.7f * lead, lead, 4, 4, -0.05f * lead, 0.06f * lead, 0.15f, 0.01f * lead, seed * 131u + 997u + static_cast<uint32_t>(c));
				out.Append(card, Leaning(glm::radians(90.0f * static_cast<float>(c)) + 0.3f, height - lead, 0.0f, glm::radians(5.0f)));
			}
			return out;
		}

		MeshData CardConiferTrunk(float height, float radius, int sides)
		{
			// a tapered pole to just under the leader, open at both ends (the base is in the ground, the top inside the crown)
			sides = std::clamp(sides, 3, 24);
			const float top = 0.92f * height, r1 = 0.18f * radius;
			const float slope = (radius - r1) / std::max(top, 1e-4f);
			MeshData m;
			constexpr int kRings = 3;
			for (int r = 0; r <= kRings; ++r)
			{
				const float t = static_cast<float>(r) / static_cast<float>(kRings), rr = radius + (r1 - radius) * t;
				for (int s = 0; s <= sides; ++s)
				{
					const float a = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(sides);
					const glm::vec3 dir(std::cos(a), 0.0f, std::sin(a));
					// u in metres around the base's girth (the bark's plates keep their size; one seam at the back, as Cylinder's), v in metres up
					m.Vertices.push_back(MakeVertex(glm::vec3(dir.x * rr, top * t, dir.z * rr), glm::normalize(glm::vec3(dir.x, slope, dir.z)),
					                                { 2.0f * kPi * radius * static_cast<float>(s) / static_cast<float>(sides), top * t }));
				}
			}
			for (int r = 0; r < kRings; ++r)
				for (int s = 0; s < sides; ++s)
				{
					const uint32_t a = static_cast<uint32_t>(r * (sides + 1) + s), b = a + 1, c = a + static_cast<uint32_t>(sides + 1), d = c + 1;
					AddTri(m, a, c, b);
					AddTri(m, b, c, d);
				}
			m.ComputeTangents();
			return m;
		}

		MeshData CardTuft(float width, float height, int cards, uint32_t seed)
		{
			MeshData out;
			auto h01 = [seed](int a, uint32_t b) { return HashUnit(VisualHash(static_cast<uint32_t>(a), b, seed ^ 0x7F4A7C15u)); };
			cards = std::clamp(cards, 1, 8);
			for (int c = 0; c < cards; ++c)
			{
				// T107: the heights vary more (0.7..1.2 of `height`, was 0.85..1.15), and the droop with them
				const float sw = width * (0.85f + 0.3f * h01(c, 1)), sh = height * (0.7f + 0.5f * h01(c, 2));
				const MeshData card = CurvedCard(sw, sh, 4, 4, -(0.08f + 0.22f * h01(c, 3)) * sh, 0.16f * sw, 0.5f * (h01(c, 4) - 0.5f), 0.01f * sw,
				                                 seed * 131u + static_cast<uint32_t>(c));
				// crossed planes spread over half a turn, every other one turned round so the tuft leans out on all sides; T107: the turn
				// jittered by up to +-0.45 rad (was +-0.25), the lean out 2..24 degrees (was 4..18) and a lean to one side, +-14 degrees about
				// the card's normal, so the blades do not all stand in one plane
				const float yaw = static_cast<float>(c) * kPi / static_cast<float>(cards) + ((c & 1) ? kPi : 0.0f) + 0.9f * (h01(c, 5) - 0.5f);
				const glm::mat4 side = glm::rotate(glm::mat4(1.0f), glm::radians(28.0f * (h01(c, 7) - 0.5f)), glm::vec3(0.0f, 0.0f, 1.0f));
				out.Append(card, Leaning(yaw, 0.0f, 0.0f, glm::radians(2.0f + 22.0f * h01(c, 6))) * side);
			}
			return out;
		}
	}
}
