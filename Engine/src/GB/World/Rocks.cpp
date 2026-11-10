#include "GB/World/Rocks.h"
#include "GB/Log.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <glm/gtc/constants.hpp>

#ifndef GB_CONTENT_DIR
#define GB_CONTENT_DIR "assets/data"
#endif

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

		// One block: the positions of a cut, displaced icosphere (its base at y = 0) and its triangles.
		void BuildBlock(const RockParams& p, std::vector<glm::vec3>& pos, std::vector<uint32_t>& tris)
		{
			// Icosahedron, subdivided.
			const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
			pos = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
				{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
			for (auto& v : pos) v = glm::normalize(v);
			tris = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
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
			const int planeCount = p.Planes > 0 ? p.Planes + static_cast<int>(rnd(4) * 2.99f) : 8 + static_cast<int>(rnd(4) * 5.0f);
			for (int k = 0; k < planeCount; ++k)
			{
				const uint32_t b = 10u + static_cast<uint32_t>(k) * 5u;
				glm::vec3 n(rnd(b) * 2.0f - 1.0f, rnd(b + 1) * 1.4f - 0.5f, rnd(b + 2) * 2.0f - 1.0f);   // a few more faces on top than below
				if (k == 0) n = glm::vec3(0.1f * (rnd(b) - 0.5f), 1.0f, 0.1f * (rnd(b + 1) - 0.5f));       // a top face
				if (glm::length(n) < 0.2f) n = glm::vec3(0, 1, 0);
				planes.push_back({ glm::normalize(n), glm::mix(p.CutNear, p.CutFar, rnd(b + 3)) });
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
		}

		float TopOf(const std::vector<glm::vec3>& pos)
		{
			float top = 0.0f;
			for (const glm::vec3& v : pos) top = std::max(top, v.y);
			return top;
		}

		// The blocks of a formation, fused: each block is built with its own seed and size, placed (yaw, lean, offset), and set on its lowest point
		// (or on the block under it, for a stack). The footprint stays inside about the recipe's Size around the axis.
		void BuildFormation(const RockParams& p, std::vector<glm::vec3>& pos, std::vector<uint32_t>& tris)
		{
			const RockFormation& f = p.Formation;
			auto rnd = [&](uint32_t k) { return Hash3(glm::ivec3(static_cast<int>(k), 11, 5), p.Seed + 5003u); };
			const float S = p.Size, spread = std::max(f.Spread, 0.0f), var = glm::clamp(f.Variation, 0.0f, 1.0f);
			const int subDetail = f.Detail >= 0 ? f.Detail : std::max(1, p.Detail - 1);
			auto vary = [&](uint32_t k) { return 1.0f + var * (rnd(k) - 0.5f); };

			struct Block { RockParams P; glm::vec3 At{ 0.0f }; float Yaw = 0.0f; glm::vec2 Lean{ 0.0f }; bool OnPrevious = false; };
			std::vector<Block> blocks;
			auto block = [&](uint32_t k, float size, glm::vec3 stretch, bool main)
			{
				Block b;
				b.P = p;
				b.P.Formation = RockFormation();
				b.P.Seed = p.Seed * 7919u + k * 131u + 17u;
				b.P.Size = size;
				b.P.Stretch = stretch;
				if (!main) b.P.Detail = subDetail;
				b.Yaw = rnd(100 + k) * glm::two_pi<float>();
				return b;
			};
			const glm::vec3 st = p.Stretch;
			switch (f.Archetype)
			{
			case RockArchetype::Cluster:
			{
				// a main block, smaller ones around its foot, and (with three or more) one resting on top of it, as in the kit's heaps
				const int n = f.Blocks > 0 ? f.Blocks : 3;
				blocks.push_back(block(0, 0.66f * S * vary(1), st, true));
				const float turn = rnd(2) * glm::two_pi<float>();
				const int around = n >= 3 ? n - 1 : n;
				for (int k = 0; k < around; ++k)
				{
					const uint32_t u = static_cast<uint32_t>(k) + 1u;
					const float a = turn + glm::two_pi<float>() * (static_cast<float>(k) + 0.35f * rnd(10 + u)) / static_cast<float>(around);
					const float d = 0.58f * S * spread * (0.85f + 0.3f * rnd(20 + u));
					Block b = block(u, 0.36f * S * vary(30 + u), st * glm::vec3(1.0f, 0.85f + 0.3f * rnd(40 + u), 1.0f), false);
					b.At = { std::cos(a) * d, 0.0f, std::sin(a) * d };
					b.Lean = { (rnd(50 + u) - 0.5f) * 0.3f, (rnd(60 + u) - 0.5f) * 0.3f };
					blocks.push_back(b);
				}
				if (n >= 3)
				{
					Block b = block(static_cast<uint32_t>(n), 0.34f * S * vary(70), st * glm::vec3(1.1f, 0.75f, 1.0f), false);
					b.At = { (rnd(71) - 0.5f) * 0.2f * S, 0.0f, (rnd(72) - 0.5f) * 0.2f * S };
					b.Lean = { (rnd(73) - 0.5f) * 0.5f, (rnd(74) - 0.5f) * 0.5f };
					b.OnPrevious = true;   // on top of the main block (the one before it in the list is the last around; see below)
					blocks.push_back(b);
				}
				break;
			}
			case RockArchetype::Slab:
			{
				// a wide low plate broken into pieces: a middle piece and the rest around it, with the gaps the rounded edges leave between them
				const int n = f.Blocks > 0 ? f.Blocks : 3;
				const glm::vec3 flat = glm::vec3(st.x, std::min(st.y, 0.32f), st.z);
				blocks.push_back(block(0, 0.5f * S * vary(1), flat, true));
				const float turn = rnd(2) * glm::two_pi<float>();
				for (int k = 0; k < n; ++k)
				{
					const uint32_t u = static_cast<uint32_t>(k) + 1u;
					const float a = turn + glm::two_pi<float>() * (static_cast<float>(k) + 0.25f * rnd(10 + u)) / static_cast<float>(n);
					const float d = 0.5f * S * spread;
					Block b = block(u, 0.44f * S * vary(30 + u), flat * glm::vec3(1.0f, 0.8f + 0.35f * rnd(40 + u), 1.0f), false);
					b.At = { std::cos(a) * d, 0.0f, std::sin(a) * d };
					b.Lean = { (rnd(50 + u) - 0.5f) * 0.08f, (rnd(60 + u) - 0.5f) * 0.08f };
					blocks.push_back(b);
				}
				break;
			}
			case RockArchetype::Spire:
			{
				// a tall block with fewer, deeper cuts (a crystal-like point) and shorter ones leaning out at its foot
				const int n = f.Blocks > 0 ? f.Blocks : 2;
				Block m = block(0, 0.5f * S * vary(1), glm::vec3(st.x * 0.75f, std::max(st.y, 1.0f) * 2.0f, st.z * 0.75f), true);
				blocks.push_back(m);
				const float turn = rnd(2) * glm::two_pi<float>();
				for (int k = 0; k < n; ++k)
				{
					const uint32_t u = static_cast<uint32_t>(k) + 1u;
					const float a = turn + glm::two_pi<float>() * (static_cast<float>(k) + 0.3f * rnd(10 + u)) / static_cast<float>(n);
					const float d = 0.42f * S * spread;
					Block b = block(u, 0.3f * S * vary(30 + u), glm::vec3(st.x * 0.8f, std::max(st.y, 1.0f) * (1.2f + 0.5f * rnd(40 + u)), st.z * 0.8f), false);
					b.At = { std::cos(a) * d, 0.0f, std::sin(a) * d };
					b.Lean = { std::cos(a) * 0.3f, std::sin(a) * 0.3f };   // outward
					blocks.push_back(b);
				}
				break;
			}
			case RockArchetype::Stack:
			{
				// a cairn: flat blocks, each smaller than the one under it
				const int n = f.Blocks > 0 ? f.Blocks + 1 : 4;
				float size = 0.62f * S;
				for (int k = 0; k < n; ++k)
				{
					const uint32_t u = static_cast<uint32_t>(k);
					Block b = block(u, size * vary(30 + u), glm::vec3(st.x, std::min(st.y, 0.55f), st.z), k == 0);
					b.At = { (rnd(10 + u) - 0.5f) * 0.12f * S, 0.0f, (rnd(20 + u) - 0.5f) * 0.12f * S };
					b.Lean = { (rnd(50 + u) - 0.5f) * 0.15f, (rnd(60 + u) - 0.5f) * 0.15f };
					b.OnPrevious = k > 0;
					blocks.push_back(b);
					size *= 0.74f;
				}
				break;
			}
			case RockArchetype::Ledge:
			{
				// a low line of blocks across the footprint, of uneven heights, slightly out of line
				const int n = f.Blocks > 0 ? f.Blocks + 1 : 4;
				const float half = 0.62f * S * spread;
				const float step = n > 1 ? 2.0f * half / static_cast<float>(n - 1) : 0.0f;
				for (int k = 0; k < n; ++k)
				{
					const uint32_t u = static_cast<uint32_t>(k);
					Block b = block(u, 0.34f * S * vary(30 + u), glm::vec3(st.x * 1.15f, st.y * (0.9f + 0.6f * rnd(40 + u)), st.z * 0.8f), k == 0);
					b.At = { -half + step * static_cast<float>(k), 0.0f, (rnd(20 + u) - 0.5f) * 0.18f * S };
					b.Yaw = (rnd(100 + u) - 0.5f) * 0.5f;   // the blocks keep the line's heading
					b.Lean = { (rnd(50 + u) - 0.5f) * 0.12f, (rnd(60 + u) - 0.5f) * 0.12f };
					blocks.push_back(b);
				}
				break;
			}
			case RockArchetype::Single:
				break;
			}

			pos.clear();
			tris.clear();
			float mainTop = 0.0f, previousTop = 0.0f;
			std::vector<glm::vec3> bp;
			std::vector<uint32_t> bt;
			for (size_t i = 0; i < blocks.size(); ++i)
			{
				const Block& b = blocks[i];
				BuildBlock(b.P, bp, bt);
				const float cy = std::cos(b.Yaw), sy = std::sin(b.Yaw);
				const float cx = std::cos(b.Lean.x), sx = std::sin(b.Lean.x), cz = std::cos(b.Lean.y), sz = std::sin(b.Lean.y);
				float lowest = 1e9f;
				for (glm::vec3& v : bp)
				{
					glm::vec3 r(cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z);    // yaw
					r = { r.x, cx * r.y - sx * r.z, sx * r.y + cx * r.z };           // lean about x
					r = { cz * r.x - sz * r.y, sz * r.x + cz * r.y, r.z };           // lean about z
					v = r;
					lowest = std::min(lowest, v.y);
				}
				// a block rests on the ground on its lowest point, or (OnPrevious) sinks a little into the top of the one it rests on: for the
				// cluster's top block, the main block's
				const bool onTop = b.OnPrevious;
				const float floorY = !onTop ? 0.0f : (f.Archetype == RockArchetype::Cluster ? mainTop : previousTop) * 0.82f;
				const glm::vec3 shift = b.At + glm::vec3(0.0f, floorY - lowest, 0.0f);
				const uint32_t first = static_cast<uint32_t>(pos.size());
				for (const glm::vec3& v : bp) pos.push_back(v + shift);
				for (uint32_t idx : bt) tris.push_back(first + idx);
				const float top = TopOf(bp) + shift.y;
				if (i == 0) mainTop = top;
				previousTop = top;
			}
		}

		// Smooth normals from the faces, blended toward the face normal for flat-shaded facets; one vertex per triangle corner.
		void EmitVertices(const RockParams& p, const std::vector<glm::vec3>& pos, const std::vector<uint32_t>& tris, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
		{
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
	}

	const char* RockArchetypeName(RockArchetype archetype)
	{
		switch (archetype)
		{
		case RockArchetype::Single: return "single";
		case RockArchetype::Cluster: return "cluster";
		case RockArchetype::Slab: return "slab";
		case RockArchetype::Spire: return "spire";
		case RockArchetype::Stack: return "stack";
		case RockArchetype::Ledge: return "ledge";
		}
		return "single";
	}

	bool ParseRockArchetype(const std::string& name, RockArchetype& out)
	{
		for (RockArchetype a : { RockArchetype::Single, RockArchetype::Cluster, RockArchetype::Slab, RockArchetype::Spire, RockArchetype::Stack, RockArchetype::Ledge })
			if (name == RockArchetypeName(a)) { out = a; return true; }
		return false;
	}

	void RockFactory::BuildGeometry(const RockParams& p, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		std::vector<glm::vec3> pos;
		std::vector<uint32_t> tris;
		if (p.Formation.Archetype == RockArchetype::Single) BuildBlock(p, pos, tris);
		else BuildFormation(p, pos, tris);
		EmitVertices(p, pos, tris, vertices, indices);
	}

	std::unique_ptr<Rock> RockFactory::Create(const RockParams& params)
	{
		auto rock = std::make_unique<Rock>();
		rock->Params = params;
		BuildGeometry(params, rock->Geometry.Vertices, rock->Geometry.Indices);
		float r = 0.0f, h = 0.0f;
		for (const Vertex& v : rock->Geometry.Vertices) { r = std::max(r, glm::length(glm::vec2(v.Position.x, v.Position.z))); h = std::max(h, v.Position.y); }
		rock->Radius = r;
		rock->Height = h;
		rock->TriangleCount = rock->Geometry.Indices.size() / 3;
		return rock;
	}

	// ---------------------------------------------------------------- the library (assets/data/rocks.json)

	bool RockLibrary::Resolve(const std::string& recipe, const std::string& formation, RockParams& out) const
	{
		const auto r = Recipes.find(recipe);
		if (r == Recipes.end()) return false;
		RockFormation f;
		if (!formation.empty() && formation != "single")
		{
			const auto it = Formations.find(formation);
			if (it == Formations.end()) return false;
			f = it->second;
		}
		out = r->second;
		out.Formation = f;
		return true;
	}

	bool RockLibrary::Resolve(const RockVariantDef& variant, RockParams& out) const
	{
		RockFormation f;
		if (!variant.Formation.empty() && variant.Formation != "single")
		{
			const auto it = Formations.find(variant.Formation);
			if (it == Formations.end()) return false;
			f = it->second;
		}
		out = variant.Params;
		out.Formation = f;
		return true;
	}

	const std::vector<RockVariantDef>* RockLibrary::Kit(const std::string& worldType) const
	{
		const auto it = Kits.find(worldType);
		return it == Kits.end() ? nullptr : &it->second;
	}

	namespace
	{
		using Json = nlohmann::json;

		std::string ReadVec3(const Json& v, glm::vec3& out)
		{
			if (!v.is_array() || v.size() != 3) return "is not an array of three numbers";
			for (size_t i = 0; i < 3; ++i)
			{
				if (!v[i].is_number()) return "is not an array of three numbers";
				out[static_cast<glm::length_t>(i)] = v[i].get<float>();
			}
			return "";
		}

		// The keys of a recipe (also what a kit's variant may change of it). "" or the error.
		std::string ReadRecipeKey(const std::string& key, const Json& v, RockParams& p)
		{
			auto number = [&](float& out) -> std::string { if (!v.is_number()) return "is not a number"; out = v.get<float>(); return ""; };
			auto integer = [&](int& out) -> std::string { if (!v.is_number_integer()) return "is not a whole number"; out = v.get<int>(); return ""; };
			if (key == "size") return number(p.Size);
			if (key == "stretch") return ReadVec3(v, p.Stretch);
			if (key == "roughness") return number(p.Roughness);
			if (key == "frequency") return number(p.Frequency);
			if (key == "facets") return number(p.Facets);
			if (key == "flatten") return number(p.Flatten);
			if (key == "strata") return number(p.Strata);
			if (key == "cracks") return number(p.Cracks);
			if (key == "erosion") return number(p.Erosion);
			if (key == "detail") { const std::string e = integer(p.Detail); if (e.empty() && (p.Detail < 0 || p.Detail > 6)) return "is not between 0 and 6"; return e; }
			if (key == "planes") { const std::string e = integer(p.Planes); if (e.empty() && (p.Planes < 0 || p.Planes > 32)) return "is not between 0 and 32"; return e; }
			if (key == "cut")
			{
				if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) return "is not [near, far]";
				p.CutNear = v[0].get<float>(); p.CutFar = v[1].get<float>();
				return "";
			}
			if (key == "color") return ReadVec3(v, p.Color);
			if (key == "moss") return number(p.Moss);
			if (key == "mossColor") return ReadVec3(v, p.MossColor);
			if (key == "sand") return number(p.Sand);
			if (key == "note") return "";
			return "is not a key of a rock recipe";
		}

		std::string ReadFormation(const Json& o, RockFormation& f)
		{
			if (!o.is_object()) return "is not an object";
			for (const auto& [key, v] : o.items())
			{
				if (key == "archetype")
				{
					if (!v.is_string() || !ParseRockArchetype(v.get<std::string>(), f.Archetype)) return "'archetype' is not single, cluster, slab, spire, stack or ledge";
				}
				else if (key == "blocks") { if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 12) return "'blocks' is not a whole number from 0 to 12"; f.Blocks = v.get<int>(); }
				else if (key == "spread") { if (!v.is_number()) return "'spread' is not a number"; f.Spread = v.get<float>(); }
				else if (key == "variation") { if (!v.is_number()) return "'variation' is not a number"; f.Variation = v.get<float>(); }
				else if (key == "detail") { if (!v.is_number_integer() || v.get<int>() < -1 || v.get<int>() > 6) return "'detail' is not a whole number from -1 to 6"; f.Detail = v.get<int>(); }
				else if (key != "note") return "'" + key + "' is not a key of a rock formation";
			}
			return "";
		}
	}

	std::string LoadRockLibrary(const std::string& text, RockLibrary& out)
	{
		Json doc;
		try { doc = Json::parse(text); }
		catch (const std::exception& e) { return std::string("not valid JSON: ") + e.what(); }
		if (!doc.is_object()) return "the document is not an object";
		RockLibrary lib;
		for (const auto& [key, v] : doc.items())
			if (key != "recipes" && key != "formations" && key != "kits" && key != "note") return "'" + key + "' is not a section of a rocks document (recipes, formations, kits)";
		if (const auto it = doc.find("recipes"); it != doc.end())
		{
			if (!it->is_object()) return "'recipes' is not an object";
			for (const auto& [id, o] : it->items())
			{
				if (!o.is_object()) return "recipe '" + id + "' is not an object";
				RockParams p;
				for (const auto& [key, v] : o.items())
					if (std::string e = ReadRecipeKey(key, v, p); !e.empty()) return "recipe '" + id + "': '" + key + "' " + e;
				lib.Recipes[id] = p;
			}
		}
		if (const auto it = doc.find("formations"); it != doc.end())
		{
			if (!it->is_object()) return "'formations' is not an object";
			for (const auto& [id, o] : it->items())
			{
				RockFormation f;
				if (std::string e = ReadFormation(o, f); !e.empty()) return "formation '" + id + "': " + e;
				lib.Formations[id] = f;
			}
		}
		if (const auto it = doc.find("kits"); it != doc.end())
		{
			if (!it->is_object()) return "'kits' is not an object";
			for (const auto& [id, list] : it->items())
			{
				if (!list.is_array() || list.empty()) return "kit '" + id + "' is not a non-empty array";
				std::vector<RockVariantDef>& kit = lib.Kits[id];
				for (size_t i = 0; i < list.size(); ++i)
				{
					const Json& o = list[i];
					const std::string where = "kit '" + id + "', variant " + std::to_string(i);
					if (!o.is_object()) return where + " is not an object";
					const auto recipe = o.find("recipe");
					if (recipe == o.end() || !recipe->is_string()) return where + " has no 'recipe'";
					RockVariantDef def;
					def.Recipe = recipe->get<std::string>();
					const auto r = lib.Recipes.find(def.Recipe);
					if (r == lib.Recipes.end()) return where + ": recipe '" + def.Recipe + "' does not exist";
					def.Params = r->second;
					for (const auto& [key, v] : o.items())
					{
						if (key == "recipe") continue;
						if (key == "formation")
						{
							if (!v.is_string()) return where + ": 'formation' is not a string";
							def.Formation = v.get<std::string>();
							if (def.Formation != "single" && !lib.Formations.count(def.Formation)) return where + ": formation '" + def.Formation + "' does not exist";
							continue;
						}
						if (std::string e = ReadRecipeKey(key, v, def.Params); !e.empty()) return where + ": '" + key + "' " + e;
					}
					kit.push_back(std::move(def));
				}
			}
		}
		out = std::move(lib);
		return "";
	}

	std::string LoadRockLibraryFile(const std::string& path, RockLibrary& out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return "cannot open " + path;
		std::stringstream ss;
		ss << file.rdbuf();
		RockLibrary lib;
		if (std::string e = LoadRockLibrary(ss.str(), lib); !e.empty()) return path + ": " + e;
		lib.Source = path;
		out = std::move(lib);
		return "";
	}

	const RockLibrary& BuiltinRockLibrary()
	{
		// Exactly the recipes WorldGen.cpp made in code until 2026-11-08 (six per type of world, by index i): the sizes, stretches, roughness and
		// facets by index, and the colour and layers by type.
		static const RockLibrary lib = []
		{
			RockLibrary l;
			const float sizes[] = { 0.55f, 0.9f, 1.3f, 1.8f, 2.5f, 0.4f };
			const char* types[] = { "temperate", "alpine", "desert" };
			for (const char* type : types)
			{
				RockParams base;
				if (std::string(type) == "desert") { base.Sand = 0.8f; base.Color = { 0.55f, 0.40f, 0.28f }; }
				else if (std::string(type) == "alpine") { base.Moss = 0.12f; base.Color = { 0.36f, 0.37f, 0.40f }; }
				else { base.Moss = 0.75f; base.Color = { 0.40f, 0.39f, 0.36f }; }
				const std::string recipe = std::string("builtin_") + type;
				l.Recipes[recipe] = base;
				std::vector<RockVariantDef>& kit = l.Kits[type];
				for (uint32_t i = 0; i < 6; ++i)
				{
					RockVariantDef v;
					v.Recipe = recipe;
					v.Params = base;
					v.Params.Size = sizes[i];
					v.Params.Stretch = { 1.0f + 0.15f * static_cast<float>(i % 3), 0.65f + 0.1f * static_cast<float>(i % 4), 1.0f + 0.1f * static_cast<float>((i + 1) % 3) };
					v.Params.Roughness = 0.3f + 0.03f * static_cast<float>(i);
					v.Params.Facets = 0.5f + 0.08f * static_cast<float>(i);
					kit.push_back(v);
				}
			}
			return l;
		}();
		return lib;
	}

	namespace
	{
		struct Shipped
		{
			std::mutex Lock;
			std::string Dir;
			std::shared_ptr<const RockLibrary> Library;
			std::filesystem::file_time_type Stamp{};
			bool Loaded = false, Missing = false;
		};
		Shipped& ShippedState() { static Shipped s; return s; }
	}

	void SetRockLibraryDir(const std::string& dir)
	{
		Shipped& s = ShippedState();
		std::lock_guard<std::mutex> lock(s.Lock);
		s.Dir = dir;
		s.Loaded = false;
		s.Library.reset();
	}

	std::shared_ptr<const RockLibrary> ShippedRockLibrary()
	{
		Shipped& s = ShippedState();
		std::lock_guard<std::mutex> lock(s.Lock);
		std::string dir = s.Dir;
		if (dir.empty()) dir = std::filesystem::exists(GB_CONTENT_DIR) ? std::string(GB_CONTENT_DIR) : std::string("assets/data");
		const std::string path = dir + "/rocks.json";
		std::error_code ec;
		const auto stamp = std::filesystem::last_write_time(path, ec);
		if (ec)
		{
			if (!s.Missing) GB_CORE_WARN("rocks: {} not found; the world's rocks use the built-in recipes", path);
			s.Missing = true;
			s.Loaded = false;
			return std::shared_ptr<const RockLibrary>(std::shared_ptr<const RockLibrary>(), &BuiltinRockLibrary());
		}
		s.Missing = false;
		if (!s.Loaded || stamp != s.Stamp)
		{
			s.Loaded = true;
			s.Stamp = stamp;
			auto lib = std::make_shared<RockLibrary>();
			if (std::string e = LoadRockLibraryFile(path, *lib); !e.empty())
			{
				GB_CORE_ERROR("rocks: {}; the world's rocks use the built-in recipes", e);
				s.Library.reset();
			}
			else
			{
				if (s.Library) GB_CORE_INFO("rocks: {} reloaded ({} recipes, {} formations, {} kits)", path, lib->Recipes.size(), lib->Formations.size(), lib->Kits.size());
				s.Library = std::move(lib);
			}
		}
		if (!s.Library) return std::shared_ptr<const RockLibrary>(std::shared_ptr<const RockLibrary>(), &BuiltinRockLibrary());
		return s.Library;
	}
}
