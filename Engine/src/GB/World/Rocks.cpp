#include "Rocks.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <map>

namespace GB
{
	namespace
	{
		float Hash3(const glm::ivec3& p, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(p.x) * 374761393u + static_cast<uint32_t>(p.y) * 668265263u + static_cast<uint32_t>(p.z) * 2147483647u + seed * 1274126177u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
		}
		float Noise3(const glm::vec3& p, uint32_t seed)
		{
			const glm::vec3 fl = glm::floor(p);
			const glm::ivec3 i(fl);
			glm::vec3 f = p - fl;
			f = f * f * (3.0f - 2.0f * f);
			auto h = [&](int x, int y, int z) { return Hash3(i + glm::ivec3(x, y, z), seed); };
			return glm::mix(glm::mix(glm::mix(h(0, 0, 0), h(1, 0, 0), f.x), glm::mix(h(0, 1, 0), h(1, 1, 0), f.x), f.y),
				glm::mix(glm::mix(h(0, 0, 1), h(1, 0, 1), f.x), glm::mix(h(0, 1, 1), h(1, 1, 1), f.x), f.y), f.z);
		}
		float Fbm3(glm::vec3 p, uint32_t seed)
		{
			float v = 0.0f, a = 0.5f;
			for (int i = 0; i < 4; ++i) { v += a * Noise3(p, seed + static_cast<uint32_t>(i) * 17u); p = p * 2.03f + 5.1f; a *= 0.5f; }
			return v / 0.9375f;
		}
	}

	void RockFactory::BuildGeometry(const RockParams& p, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		// Icosahedron, subdivided.
		const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
		std::vector<glm::vec3> pos = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
			{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
		for (auto& v : pos) v = glm::normalize(v);
		std::vector<uint32_t> tris = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
			3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
		for (int s = 0; s < std::clamp(p.Detail, 0, 6); ++s)
		{
			std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
			auto midpoint = [&](uint32_t a, uint32_t b)
			{
				const auto key = std::make_pair(std::min(a, b), std::max(a, b));
				const auto it = mid.find(key);
				if (it != mid.end()) return it->second;
				pos.push_back(glm::normalize(pos[a] + pos[b]));
				return mid[key] = static_cast<uint32_t>(pos.size() - 1);
			};
			std::vector<uint32_t> next;
			for (size_t i = 0; i < tris.size(); i += 3)
			{
				const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
				const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
				next.insert(next.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
			}
			tris.swap(next);
		}

		// Shape: a lumpy base (domain-warped fbm) cut by a handful of random planes, which gives flat faces and sharp
		// edges instead of a balloon; then bedding grooves (strata), cracks, and fine erosion pits. Each seed also jitters the proportions.
		const float freq = p.Frequency;
		const glm::vec3 offset(static_cast<float>(p.Seed) * 7.31f, static_cast<float>(p.Seed) * 3.17f, static_cast<float>(p.Seed) * 5.53f);
		auto rnd = [&](uint32_t k) { return Hash3(glm::ivec3(static_cast<int>(k), 7, 3), p.Seed + 977u); };
		const glm::vec3 jitter(1.0f + 0.3f * (rnd(1) - 0.5f), 1.0f + 0.3f * (rnd(2) - 0.5f), 1.0f + 0.3f * (rnd(3) - 0.5f));
		struct Plane { glm::vec3 N; float D; };
		std::vector<Plane> planes;
		const int planeCount = 8 + static_cast<int>(rnd(4) * 5.0f);
		for (int k = 0; k < planeCount; ++k)
		{
			const uint32_t b = 10u + static_cast<uint32_t>(k) * 5u;
			glm::vec3 n(rnd(b) * 2.0f - 1.0f, rnd(b + 1) * 1.4f - 0.5f, rnd(b + 2) * 2.0f - 1.0f);   // a few more faces on top than below
			if (k == 0) n = glm::vec3(0.1f * (rnd(b) - 0.5f), 1.0f, 0.1f * (rnd(b + 1) - 0.5f));       // a top face
			if (glm::length(n) < 0.2f) n = glm::vec3(0, 1, 0);
			planes.push_back({ glm::normalize(n), glm::mix(0.70f, 0.92f, rnd(b + 3)) });
		}
		const float soft = 0.008f + 0.07f * (1.0f - glm::clamp(p.Facets, 0.0f, 1.0f));
		const float layers = 5.0f + std::floor(rnd(5) * 5.0f);
		const float tilt = (rnd(6) - 0.5f) * 0.8f;
		float minY = 1e9f;
		for (auto& v : pos)
		{
			const glm::vec3 d = v;
			const glm::vec3 warp = glm::vec3(Noise3(d * 1.3f + offset + 11.0f, p.Seed + 3u), Noise3(d * 1.3f + offset + 23.0f, p.Seed + 5u), Noise3(d * 1.3f + offset + 37.0f, p.Seed + 7u)) - 0.5f;
			const glm::vec3 dw = d + warp * 0.5f;
			float r = 1.0f + p.Roughness * (Fbm3(dw * freq + offset, p.Seed) - 0.5f) * 1.2f;
			for (const Plane& pl : planes)
			{
				const float c = glm::dot(d, pl.N);
				if (c > 0.05f)
				{
					const float lim = pl.D / c;   // smooth minimum with the plane
					const float h = glm::clamp(0.5f + 0.5f * (lim - r) / soft, 0.0f, 1.0f);
					r = glm::mix(lim, r, h) - soft * h * (1.0f - h);
				}
			}
			const float ridge = 1.0f - std::fabs(Noise3(d * freq * 2.3f + offset * 1.7f, p.Seed + 91u) * 2.0f - 1.0f);
			r += p.Roughness * (ridge - 0.5f) * 0.25f * p.Facets;
			// strata: horizontal bedding grooves, slightly tilted
			const float s = (d.y * 0.5f + d.x * tilt) * layers * 2.0f;
			const float g = std::fabs(s - std::floor(s) - 0.5f) * 2.0f;
			r -= p.Strata * 0.045f * glm::smoothstep(0.82f, 1.0f, g) * (0.5f + Noise3(d * 3.0f + offset, p.Seed + 61u));
			// cracks: narrow valleys where a noise field crosses its midline
			const float cr = std::fabs(Noise3(dw * freq * 1.7f + offset * 2.1f, p.Seed + 131u) - 0.5f);
			r -= p.Cracks * 0.10f * (1.0f - glm::smoothstep(0.0f, 0.03f, cr));
			// erosion: fine pitting, a little more on the upper half
			r += p.Erosion * 0.03f * (Noise3(d * 7.0f + offset, p.Seed + 151u) - 0.5f) * (0.7f + 0.6f * glm::clamp(d.y, 0.0f, 1.0f));
			v = d * std::max(r, 0.35f) * p.Size * p.Stretch * jitter;
			minY = std::min(minY, v.y);
		}
		// Flat base: squash everything below a fraction of the height.
		const float base = minY * (1.0f - p.Flatten * 0.55f);
		for (auto& v : pos)
			if (v.y < base) v.y = base + (v.y - base) * (1.0f - p.Flatten) * 0.25f;
		float lowest = 1e9f;
		for (const auto& v : pos) lowest = std::min(lowest, v.y);
		for (auto& v : pos) v.y -= lowest;   // base at y = 0

		// Smooth normals from the faces, blended toward the face normal for flat-shaded facets.
		std::vector<glm::vec3> smooth(pos.size(), glm::vec3(0.0f));
		for (size_t i = 0; i < tris.size(); i += 3)
		{
			const glm::vec3 n = glm::cross(pos[tris[i + 1]] - pos[tris[i]], pos[tris[i + 2]] - pos[tris[i]]);
			for (int k = 0; k < 3; ++k) smooth[tris[i + static_cast<size_t>(k)]] += n;
		}
		vertices.clear();
		indices.clear();
		for (size_t i = 0; i < tris.size(); i += 3)
		{
			const glm::vec3 fn = glm::normalize(glm::cross(pos[tris[i + 1]] - pos[tris[i]], pos[tris[i + 2]] - pos[tris[i]]));
			for (int k = 0; k < 3; ++k)
			{
				const uint32_t vi = tris[i + static_cast<size_t>(k)];
				Vertex v;
				v.Position = pos[vi];
				v.Normal = glm::normalize(glm::mix(glm::normalize(smooth[vi]), fn, glm::clamp(p.Facets, 0.0f, 1.0f)));
				v.UV = { pos[vi].x, pos[vi].z };
				glm::vec3 tangent = glm::cross(glm::vec3(0, 1, 0), v.Normal);
				if (glm::length(tangent) < 1e-4f) tangent = glm::vec3(1, 0, 0);
				v.Tangent = glm::vec4(glm::normalize(tangent), 1.0f);
				indices.push_back(static_cast<uint32_t>(vertices.size()));
				vertices.push_back(v);
			}
		}
	}

	std::unique_ptr<Rock> RockFactory::Create(const RockParams& params)
	{
		auto rock = std::make_unique<Rock>();
		rock->Params = params;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		BuildGeometry(params, vertices, indices);
		float r = 0.0f, h = 0.0f;
		for (const Vertex& v : vertices) { r = std::max(r, glm::length(glm::vec2(v.Position.x, v.Position.z))); h = std::max(h, v.Position.y); }
		rock->Radius = r;
		rock->Height = h;
		rock->TriangleCount = indices.size() / 3;
		rock->MeshPtr = std::make_unique<Mesh>(vertices, indices);
		Material& m = rock->Mat;
		m.Albedo = params.Color;
		m.Roughness = 0.88f;
		m.HeightScale = 0.0f;
		m.CavityStrength = 0.0f;
		m.Grain = 0.8f;
		m.GrainScale = 2.4f / std::max(params.Size, 0.3f);
		m.Moss = params.Moss;
		m.Sand = params.Sand;
		return rock;
	}

	void Rock::AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale, float sink, float waterLine) const
	{
		RenderObject o;
		o.MeshPtr = MeshPtr.get();
		o.Mat = Mat;
		o.Mat.BaseY = position.y - sink;
		o.Mat.WaterLine = waterLine;
		o.BoundingCenter = { 0.0f, Height * 0.5f, 0.0f };
		o.BoundingRadius = 1.05f * glm::length(glm::vec2(Radius, Height * 0.5f));
		o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position - glm::vec3(0.0f, sink, 0.0f)), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		scene.Objects.push_back(o);
	}
}
