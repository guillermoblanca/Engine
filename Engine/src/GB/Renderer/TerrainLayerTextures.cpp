#include "GB/Renderer/TerrainLayerTextures.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>

namespace GB::TerrainLayerTextures
{
	namespace
	{
		using glm::vec2;
		using glm::vec3;

		// ------------------------------------------------------------------ periodic noise (tile units: u, v in [0, 1) and wrap)
		uint32_t HashU(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
			h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
			return h;
		}
		float Rand(int x, int y, uint32_t seed) { return static_cast<float>(HashU(x, y, seed) & 0xFFFFFFu) / 16777216.0f; }
		float RandId(uint32_t id, uint32_t salt) { return Rand(static_cast<int>(id & 0x7FFFFFFFu), static_cast<int>(salt), 977u); }
		int Wrap(int v, int period) { return ((v % period) + period) % period; }
		float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
		float Smoothstep(float a, float b, float x) { return Smooth(std::clamp((x - a) / (b - a), 0.0f, 1.0f)); }

		// Value noise with `cx` x `cy` lattice cells a tile (it tiles in u and v), 0..1.
		float Noise(float u, float v, int cx, int cy, uint32_t seed)
		{
			const float x = u * static_cast<float>(cx), y = v * static_cast<float>(cy);
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float fx = Smooth(x - static_cast<float>(ix)), fy = Smooth(y - static_cast<float>(iy));
			const float a = Rand(Wrap(ix, cx), Wrap(iy, cy), seed), b = Rand(Wrap(ix + 1, cx), Wrap(iy, cy), seed);
			const float c = Rand(Wrap(ix, cx), Wrap(iy + 1, cy), seed), d = Rand(Wrap(ix + 1, cx), Wrap(iy + 1, cy), seed);
			return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
		}
		float Noise(float u, float v, int cells, uint32_t seed) { return Noise(u, v, cells, cells, seed); }

		// Fractal value noise: `cells` lattice cells a tile at the first octave, doubled each octave. Each octave is shifted by a fraction of
		// its cell (the lattices of the octaves do not line up, so no grid shows; the shift keeps the tiling).
		float Fbm(float u, float v, int cells, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			for (int o = 0; o < octaves; ++o, amp *= 0.5f, cells *= 2)
			{
				const float shift = (0.31f + 0.17f * static_cast<float>(o)) / static_cast<float>(cells);
				sum += amp * Noise(u + shift, v + shift * 0.7f, cells, seed + static_cast<uint32_t>(o) * 131u);
				norm += amp;
			}
			return sum / norm;
		}

		// Cellular noise: `cells` x `cells` jittered points a tile. F1 and F2 in cell units; Id of the nearest cell; ToNearest from the
		// sample to the nearest point (cell units).
		struct Cell { float F1 = 1e9f, F2 = 1e9f; uint32_t Id = 0; vec2 ToNearest{ 0.0f }; };
		Cell Worley(float u, float v, int cells, uint32_t seed, float jitter = 0.9f)
		{
			const float x = u * static_cast<float>(cells), y = v * static_cast<float>(cells);
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			Cell c;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int cx = ix + dx, cy = iy + dy, wx = Wrap(cx, cells), wy = Wrap(cy, cells);
					const vec2 p(static_cast<float>(cx) + 0.5f + (Rand(wx, wy, seed) - 0.5f) * jitter, static_cast<float>(cy) + 0.5f + (Rand(wx, wy, seed + 7u) - 0.5f) * jitter);
					const vec2 to = p - vec2(x, y);
					const float d = glm::length(to);
					if (d < c.F1) { c.F2 = c.F1; c.F1 = d; c.Id = HashU(wx, wy, seed + 13u); c.ToNearest = to; }
					else if (d < c.F2) c.F2 = d;
				}
			return c;
		}

		// ------------------------------------------------------------------ the patterns
		struct Texel { vec3 Color{ 0.5f }; float Height = 0.5f, Rough = 0.9f, AO = 1.0f; };
		using Pattern = Texel (*)(float u, float v, uint32_t seed);

		// Bare granite, fractured: broken faces, each a plane tilted its own way, that meet in steps (not in dark lines), smaller fractures
		// inside some of them, a rough fbm over everything and a few short open cracks.
		Texel RockPattern(float u, float v, uint32_t s)
		{
			const float n = Fbm(u, v, 4, 5, s);
			const float wu = u + (Fbm(u, v, 3, 2, s + 7u) - 0.5f) * 0.08f, wv = v + (Fbm(u, v, 3, 2, s + 8u) - 0.5f) * 0.08f;
			const Cell big = Worley(wu, wv, 3, s + 1u, 1.0f);
			const Cell small = Worley(wu, wv, 8, s + 4u, 1.0f);
			auto Face = [](const Cell& c, float amp) {
				const vec2 tilt((RandId(c.Id, 2) - 0.5f) * 2.0f, (RandId(c.Id, 3) - 0.5f) * 2.0f);
				return (glm::dot(tilt, -c.ToNearest) + (RandId(c.Id, 1) - 0.5f) * 0.6f) * amp;
			};
			const float split = Smoothstep(0.45f, 0.6f, Fbm(u, v, 5, 2, s + 5u));   // where the small fractures break the big faces
			// short open cracks: on some edges of the small cells, cut into lengths by a noise
			const float crack = (1.0f - Smoothstep(0.0f, 0.03f, small.F2 - small.F1)) * Smoothstep(0.62f, 0.7f, Noise(u, v, 14, s + 3u)) * split;
			const float grain = Noise(u, v, 96, s + 2u);
			const float lichen = Smoothstep(0.62f, 0.75f, Fbm(u, v, 9, 3, s + 6u));
			Texel t;
			t.Height = 0.45f * n + Face(big, 0.35f) + Face(small, 0.12f) * split + 0.06f * grain - 0.3f * crack;
			const float tone = std::clamp(n * 0.7f + RandId(big.Id, 4) * 0.3f + Face(big, 0.4f) - 0.05f, 0.0f, 1.0f);
			vec3 col = glm::mix(vec3(0.33f, 0.28f, 0.24f), vec3(0.62f, 0.54f, 0.46f), tone) * (0.9f + 0.2f * grain);
			col = glm::mix(col, vec3(0.42f, 0.42f, 0.33f), lichen * 0.35f);
			t.Color = col * (1.0f - 0.45f * crack);
			t.Rough = 0.85f;
			t.AO = 1.0f - 0.45f * crack;
			return t;
		}

		// Old snow: wind-carved ridges (sastrugi) over soft swells, bluish in the hollows.
		Texel SnowPattern(float u, float v, uint32_t s)
		{
			const float n = Fbm(u, v, 3, 4, s);
			const float warp = Fbm(u, v, 2, 3, s + 1u) - 0.5f;
			const float ridge = 0.5f + 0.5f * std::sin(6.2831853f * (v * 5.0f + u * 1.0f + warp * 1.2f));
			Texel t;
			t.Height = 0.65f * n + 0.25f * ridge * ridge + 0.1f * Noise(u, v, 64, s + 2u);
			t.Color = glm::mix(vec3(0.80f, 0.86f, 0.96f), vec3(0.97f, 0.98f, 1.0f), Smoothstep(0.25f, 0.75f, t.Height));
			t.Rough = 0.62f + 0.2f * (1.0f - t.Height);
			return t;
		}

		// Ground with low plants: soil showing between tufts of blades (the 3D grass stands on top of it).
		Texel Meadow(float u, float v, uint32_t s, const vec3& soil, const vec3& low, const vec3& high, float cover, float rough)
		{
			const float soilTone = Fbm(u, v, 8, 3, s);
			const float patches = Fbm(u, v, 5, 3, s + 1u);
			const float blades = Noise(u, v, 48, 160, s + 2u) * 0.6f + Noise(u, v, 128, 40, s + 3u) * 0.4f;
			const float plant = Smoothstep(1.0f - cover - 0.15f, 1.0f - cover + 0.15f, patches * 0.6f + blades * 0.55f);
			Texel t;
			const vec3 green = glm::mix(low, high, std::clamp(blades * 1.2f - 0.1f, 0.0f, 1.0f));
			t.Color = glm::mix(soil * (0.8f + 0.4f * soilTone), green, plant);
			t.Height = 0.25f * soilTone + plant * (0.45f + 0.3f * blades);
			t.Rough = rough;
			t.AO = 0.7f + 0.3f * plant;
			return t;
		}
		Texel GrassPattern(float u, float v, uint32_t s) { return Meadow(u, v, s, vec3(0.32f, 0.25f, 0.17f), vec3(0.22f, 0.36f, 0.10f), vec3(0.42f, 0.54f, 0.20f), 0.65f, 0.9f); }
		Texel GrassWetPattern(float u, float v, uint32_t s) { return Meadow(u, v, s, vec3(0.20f, 0.16f, 0.11f), vec3(0.12f, 0.26f, 0.08f), vec3(0.22f, 0.38f, 0.13f), 0.75f, 0.7f); }
		Texel DryPattern(float u, float v, uint32_t s) { return Meadow(u, v, s, vec3(0.44f, 0.36f, 0.24f), vec3(0.42f, 0.42f, 0.20f), vec3(0.68f, 0.60f, 0.35f), 0.6f, 0.95f); }

		// Forest floor: fallen leaves of browns and ochres over dark humus, a few twigs.
		Texel ForestPattern(float u, float v, uint32_t s)
		{
			static const vec3 kLeaves[5] = { { 0.36f, 0.23f, 0.12f }, { 0.50f, 0.34f, 0.15f }, { 0.22f, 0.15f, 0.09f }, { 0.32f, 0.31f, 0.15f }, { 0.44f, 0.27f, 0.13f } };
			const float humus = Fbm(u, v, 6, 3, s);
			Texel t;
			t.Color = vec3(0.14f, 0.11f, 0.08f) * (0.8f + 0.4f * humus);
			t.Height = 0.15f * humus;
			t.Rough = 0.9f;
			t.AO = 0.75f;
			for (int layer = 0; layer < 2; ++layer)
			{
				const Cell c = Worley(u, v, layer == 0 ? 22 : 35, s + 1u + static_cast<uint32_t>(layer) * 5u);
				const float size = 0.34f + 0.12f * RandId(c.Id, 2);
				// a leaf: an ellipse along a random direction
				const float angle = RandId(c.Id, 3) * 6.2831853f;
				const vec2 d = c.ToNearest;
				const vec2 r(d.x * std::cos(angle) + d.y * std::sin(angle), -d.x * std::sin(angle) + d.y * std::cos(angle));
				const float e = std::sqrt(r.x * r.x / (size * size) + r.y * r.y / (size * size * 0.3f));
				if (e < 1.0f && RandId(c.Id, 4) < 0.85f)
				{
					const float h = 0.45f + 0.15f * static_cast<float>(layer) + 0.2f * (1.0f - e);
					if (h > t.Height)
					{
						t.Height = h;
						t.Color = kLeaves[c.Id % 5u] * (0.85f + 0.3f * Noise(u, v, 80, s + 9u)) * (0.8f + 0.2f * (1.0f - e));
						t.AO = 0.85f + 0.15f * (1.0f - e);
						t.Rough = 0.8f;
					}
				}
			}
			const float twig = 1.0f - Smoothstep(0.0f, 0.012f, std::abs(Noise(u, v, 7, 3, s + 11u) - 0.5f));
			if (twig > 0.5f && Noise(u, v, 5, s + 12u) > 0.55f)
			{
				t.Color = vec3(0.26f, 0.18f, 0.11f);
				t.Height = std::max(t.Height, 0.7f);
			}
			return t;
		}

		// Sand: a fine grain over wind ripples.
		Texel SandPattern(float u, float v, uint32_t s)
		{
			const float grain = Noise(u, v, 160, s);
			const float warp = Fbm(u, v, 3, 2, s + 1u) - 0.5f;
			const float ripple = 0.5f + 0.5f * std::sin(6.2831853f * (v * 9.0f + warp * 1.5f));
			Texel t;
			t.Height = 0.75f * ripple + 0.25f * grain;
			t.Color = vec3(0.80f, 0.68f, 0.48f) * (0.88f + 0.16f * grain + 0.06f * ripple);
			t.Rough = 0.95f;
			return t;
		}

		// A cliff face: strata of rock of different hardness and tone (the soft ones weathered back), split by vertical joints. The strata run
		// along u: the walls' projections put v along the world's height.
		Texel CliffPattern(float u, float v, uint32_t s)
		{
			constexpr int kStrata = 9;
			constexpr float kTau = 6.2831853f;
			// beds of uneven thickness (the sine keeps the mapping monotonic and the tile wrapping) that wander a little
			const float vv = v + (Fbm(u, v, 3, 3, s) - 0.5f) * 0.07f + 0.02f * std::sin(kTau * (2.0f * u + v));
			const float layerT = vv * static_cast<float>(kStrata) + 0.6f * std::sin(kTau * 2.0f * vv + 1.3f);
			const int index = Wrap(static_cast<int>(std::floor(layerT)), kStrata);
			const float within = layerT - std::floor(layerT);
			const float hard = Rand(index, 0, s + 1u), tone = Rand(index, 1, s + 1u);
			const float detail = Fbm(u, v, 8, 4, s + 2u);
			// the parting between two beds, not everywhere (some beds weld)
			const float groove = (1.0f - Smoothstep(0.0f, 0.05f, within)) * (Rand(index, 5, s + 1u) > 0.25f ? 1.0f : 0.3f);
			// broken faces: facets tilted their own way, some of their edges open cracks
			const Cell c = Worley(u, v, 7, s + 5u, 1.0f);
			const vec2 tilt((RandId(c.Id, 2) - 0.5f) * 1.4f, (RandId(c.Id, 3) - 0.5f) * 1.4f);
			const float facet = glm::dot(tilt, -c.ToNearest) * 0.22f + (RandId(c.Id, 1) - 0.5f) * 0.12f;
			const float edge = (1.0f - Smoothstep(0.02f, 0.07f, c.F2 - c.F1)) * (RandId(c.Id, 4) > 0.4f ? 1.0f : 0.0f);
			// joints: meandering cracks across the beds, in some beds only
			const float jn = Noise(u, v, 9, 2, s + 3u) * 0.7f + Noise(u, v, 18, 4, s + 6u) * 0.3f;
			const float joint = (1.0f - Smoothstep(0.0f, 0.025f, std::abs(jn - 0.5f))) * (Rand(index, 4, s + 1u) > 0.4f ? 1.0f : 0.0f);
			const float dark = std::max({ groove * 0.9f, joint * 0.8f, edge * 0.6f });
			Texel t;
			t.Height = 0.3f + 0.35f * hard + 0.2f * detail + facet - 0.3f * dark;
			vec3 col = glm::mix(vec3(0.30f, 0.29f, 0.28f), vec3(0.56f, 0.54f, 0.51f), std::clamp(tone * 0.6f + detail * 0.5f + facet - 0.05f, 0.0f, 1.0f));
			if (Rand(index, 3, s + 1u) > 0.65f) col = glm::mix(col, vec3(0.52f, 0.43f, 0.35f), 0.55f);   // a warmer, iron-stained bed
			t.Color = col * (1.0f - 0.45f * dark) * (0.9f + 0.2f * Noise(u, v, 90, s + 4u));
			t.Rough = 0.85f;
			t.AO = 1.0f - 0.5f * dark;
			return t;
		}

		// Scree: a pile of loose angular blocks, not a paving. Three scatters of blocks (big, middle, small), each block an irregular polygon
		// turned its own way with a top tilted its own way and steep faceted sides, standing in fines and earth; where blocks overlap the
		// higher one wins (a block lies on another). The fines darken next to the blocks (their contact shadow) and the blocks' sides darken
		// toward their foot, so each block reads by its own shading.
		Texel ScreePattern(float u, float v, uint32_t s)
		{
			struct Scatter { int Cells; float Chance, Base, Radius; };
			static constexpr Scatter kScatters[3] = { { 3, 0.65f, 0.62f, 0.46f }, { 5, 0.75f, 0.5f, 0.44f }, { 9, 0.7f, 0.36f, 0.42f } };
			const float edgeNoise = Noise(u, v, 40, s + 20u) - 0.5f;
			float best = -1.0f, bestE = 2.0f, nearest = 9.0f;
			uint32_t bestId = 0;
			for (int L = 0; L < 3; ++L)
			{
				const Scatter& sc = kScatters[L];
				const uint32_t seed = s + 31u * static_cast<uint32_t>(L);
				const float x = u * static_cast<float>(sc.Cells), y = v * static_cast<float>(sc.Cells);
				const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int cx = ix + dx, cy = iy + dy, wx = Wrap(cx, sc.Cells), wy = Wrap(cy, sc.Cells);
						const uint32_t id = HashU(wx, wy, seed + 13u);
						if (RandId(id, 1) > sc.Chance) continue;
						const vec2 centre(static_cast<float>(cx) + 0.5f + (Rand(wx, wy, seed) - 0.5f) * 0.85f, static_cast<float>(cy) + 0.5f + (Rand(wx, wy, seed + 7u) - 0.5f) * 0.85f);
						const vec2 d = vec2(x, y) - centre;
						const float angle = RandId(id, 2) * 6.2831853f, ca = std::cos(angle), sa = std::sin(angle);
						const vec2 r(d.x * ca + d.y * sa, -d.x * sa + d.y * ca);
						const float a = sc.Radius * (0.75f + 0.4f * RandId(id, 3)), b = a * (0.6f + 0.35f * RandId(id, 4));
						// an irregular polygon: a box with its corners cut at a random depth, its edges a little ragged
						const float box = std::max(std::abs(r.x) / a, std::abs(r.y) / b);
						const float diamond = (std::abs(r.x) / a + std::abs(r.y) / b) * (0.62f + 0.2f * RandId(id, 5));
						const float e = std::max(box, diamond) + edgeNoise * 0.12f;
						nearest = std::min(nearest, e);
						if (e >= 1.0f) continue;
						const vec2 tilt((RandId(id, 6) - 0.5f) * 1.6f, (RandId(id, 7) - 0.5f) * 1.6f);   // (each block's top lit its own way)
						// no flat top: a low roof from the middle out, then the steep sides (each a plane: the faceted look)
						const float h = sc.Base + 0.12f * RandId(id, 8) + glm::dot(tilt, r) * 0.5f - 0.22f * e - 0.6f * std::max(0.0f, e - 0.55f);
						if (h > best) { best = h; bestE = e; bestId = id; }
					}
			}
			const float grit = Noise(u, v, 70, s + 21u);
			const Cell pebbles = Worley(u, v, 29, s + 22u);
			const float pebble = std::max(0.0f, 1.0f - pebbles.F1 / 0.4f) * (RandId(pebbles.Id, 1) > 0.45f ? 1.0f : 0.0f);
			const float fines = 0.04f + 0.1f * pebble + 0.04f * grit;
			Texel t;
			if (best > fines)
			{
				const float tone = RandId(bestId, 9);
				const float side = Smoothstep(0.5f, 1.0f, bestE);   // 0 on the top, 1 at the foot
				vec3 col = glm::mix(vec3(0.36f, 0.34f, 0.31f), vec3(0.60f, 0.57f, 0.52f), tone);
				col = glm::mix(col, vec3(0.50f, 0.42f, 0.33f), RandId(bestId, 10) * 0.6f);   // some iron-stained
				t.Color = col * (0.88f + 0.24f * grit) * (1.0f - 0.25f * side);
				t.Height = best;
				t.Rough = 0.82f;
				t.AO = 1.0f - 0.6f * side * side;
			}
			else
			{
				// fines and earth, darkest right against a block
				const float contact = 1.0f - Smoothstep(1.0f, 1.5f, nearest);
				const vec3 earth = glm::mix(vec3(0.26f, 0.23f, 0.19f), vec3(0.35f, 0.32f, 0.27f), grit);
				t.Color = glm::mix(earth, vec3(0.44f, 0.41f, 0.37f) * (0.8f + 0.3f * RandId(pebbles.Id, 2)), Smoothstep(0.0f, 0.3f, pebble));
				t.Height = fines;
				t.Rough = 0.95f;
				t.AO = 1.0f - 0.45f * contact;
			}
			return t;
		}

		// River gravel: rounded pebbles of several stones and sizes, packed in sand.
		Texel GravelPattern(float u, float v, uint32_t s)
		{
			static const vec3 kStones[5] = { { 0.70f, 0.67f, 0.60f }, { 0.52f, 0.50f, 0.46f }, { 0.63f, 0.55f, 0.45f }, { 0.40f, 0.38f, 0.36f }, { 0.76f, 0.73f, 0.67f } };
			Texel t;
			t.Color = vec3(0.42f, 0.38f, 0.32f) * (0.85f + 0.3f * Noise(u, v, 120, s));
			t.Height = 0.05f;
			t.Rough = 0.92f;
			t.AO = 0.6f;
			const int sizes[2] = { 9, 16 };
			for (int k = 0; k < 2; ++k)
			{
				const Cell c = Worley(u, v, sizes[k], s + 1u + static_cast<uint32_t>(k) * 7u, 0.8f);
				const float radius = 0.38f + 0.12f * RandId(c.Id, 1);
				const float aspect = 0.7f + 0.3f * RandId(c.Id, 2);
				const float angle = RandId(c.Id, 3) * 6.2831853f;
				const vec2 d = c.ToNearest;
				const vec2 r(d.x * std::cos(angle) + d.y * std::sin(angle), (-d.x * std::sin(angle) + d.y * std::cos(angle)) / aspect);
				const float e = glm::length(r) / radius;
				if (e >= 1.0f) continue;
				const float h = (k == 0 ? 0.55f : 0.4f) * std::sqrt(1.0f - e * e) + 0.4f;
				if (h <= t.Height) continue;
				t.Height = h;
				t.Color = kStones[c.Id % 5u] * (0.9f + 0.2f * Noise(u, v, 90, s + 9u)) * (0.75f + 0.25f * std::sqrt(1.0f - e * e));
				t.Rough = 0.62f;
				t.AO = 0.7f + 0.3f * std::sqrt(1.0f - e * e);
			}
			return t;
		}

		// Wet mud: soft swells, darker glossy puddles in the low parts.
		Texel MudPattern(float u, float v, uint32_t s)
		{
			const float n = Fbm(u, v, 6, 4, s);
			const float tone = Fbm(u, v, 10, 3, s + 1u);
			const float puddle = Smoothstep(0.36f, 0.30f, n) * 0.7f;
			Texel t;
			t.Height = std::max(n, 0.32f) + 0.05f * Noise(u, v, 60, s + 2u);
			t.Color = glm::mix(glm::mix(vec3(0.31f, 0.25f, 0.18f), vec3(0.21f, 0.16f, 0.11f), tone), vec3(0.16f, 0.12f, 0.09f), puddle);
			// Matte: mud is mostly a lake's bed, and any gloss there reflects the sky up through the water, which turned the beds light grey
			// from the air (the macro-map terrain was 0.9 everywhere).
			t.Rough = glm::mix(0.92f, 0.8f, puddle);
			return t;
		}

		// Dried mud: plates curled up at their edges, split by cracks.
		Texel MudDryPattern(float u, float v, uint32_t s)
		{
			const Cell c = Worley(u, v, 7, s, 0.9f);
			const float edge = c.F2 - c.F1;
			const float crack = 1.0f - Smoothstep(0.02f, 0.07f, edge);
			const float curl = 1.0f - Smoothstep(0.05f, 0.35f, edge);
			const float n = Fbm(u, v, 12, 3, s + 1u);
			Texel t;
			t.Height = 0.45f + 0.25f * curl + 0.1f * n - 0.5f * crack;
			t.Color = glm::mix(vec3(0.50f, 0.43f, 0.33f), vec3(0.62f, 0.54f, 0.42f), RandId(c.Id, 1)) * (0.9f + 0.15f * n) * (1.0f - 0.55f * crack);
			t.Rough = 0.9f;
			t.AO = 1.0f - 0.6f * crack;
			return t;
		}

		// A path: trodden earth, compacted and smooth, with small stones pressed into it and shallow dents.
		Texel PathPattern(float u, float v, uint32_t s)
		{
			const float n = Fbm(u, v, 5, 4, s);
			const Cell stones = Worley(u, v, 26, s + 1u, 1.0f);
			const float stone = RandId(stones.Id, 1) > 0.88f ? std::max(0.0f, 1.0f - stones.F1 / (0.16f + 0.14f * RandId(stones.Id, 4))) : 0.0f;
			// dents: soft hollows of uneven size where feet and rain wore it, not a lattice
			const float dent = Smoothstep(0.55f, 0.8f, Fbm(u, v, 11, 2, s + 2u)) * 0.7f + Smoothstep(0.6f, 0.85f, Noise(u, v, 23, s + 3u)) * 0.3f;
			const float grit = Noise(u, v, 110, s + 5u);
			Texel t;
			t.Height = 0.35f + 0.2f * n + 0.4f * std::sqrt(stone) - 0.15f * dent + 0.05f * grit;
			t.Color = glm::mix(vec3(0.40f, 0.33f, 0.24f), vec3(0.56f, 0.47f, 0.35f), n) * (1.0f - 0.12f * dent) * (0.94f + 0.12f * grit);
			if (stone > 0.0f) t.Color = glm::mix(t.Color, vec3(0.47f, 0.44f, 0.40f) * (0.8f + 0.3f * RandId(stones.Id, 3)), Smoothstep(0.0f, 0.25f, stone));
			t.Rough = stone > 0.1f ? 0.6f : 0.82f;
			t.AO = 1.0f - 0.2f * dent;
			return t;
		}

		struct PatternEntry { const char* Name; Pattern Fn; };
		const PatternEntry kPatterns[] = {
			{ "rock", RockPattern }, { "snow", SnowPattern }, { "grass", GrassPattern }, { "sand", SandPattern }, { "dry_grass", DryPattern },
			{ "forest_floor", ForestPattern }, { "cliff_strata", CliffPattern }, { "scree_blocks", ScreePattern }, { "gravel_pebbles", GravelPattern },
			{ "mud", MudPattern }, { "mud_dry", MudDryPattern }, { "grass_wet", GrassWetPattern }, { "path_earth", PathPattern } };

		Pattern FindPattern(const char* name)
		{
			for (const PatternEntry& p : kPatterns)
				if (std::string_view(p.Name) == name) return p.Fn;
			return GrassPattern;
		}

		float ToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
		float ToSrgb(float c) { c = std::clamp(c, 0.0f, 1.0f); return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
		uint8_t Byte(float c) { return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); }

		// One kind's maps: the pattern at every texel centre, its colour scaled (in linear) to the kind's mean, its height stretched to 0..1,
		// and the normal from the height's slopes in metres.
		void MakeKind(int kind, uint32_t size, Image& albedo, Image& surface, vec3& meanLinear)
		{
			const Look& look = Looks()[static_cast<size_t>(kind)];
			const Pattern fn = FindPattern(look.Pattern);
			const uint32_t seed = 1000u + static_cast<uint32_t>(kind) * 7919u;
			const size_t count = static_cast<size_t>(size) * size;
			std::vector<Texel> texels(count);
			for (uint32_t y = 0; y < size; ++y)
				for (uint32_t x = 0; x < size; ++x)
					texels[static_cast<size_t>(y) * size + x] = fn((static_cast<float>(x) + 0.5f) / static_cast<float>(size), (static_cast<float>(y) + 0.5f) / static_cast<float>(size), seed);

			glm::dvec3 sum(0.0);
			float lo = 1e9f, hi = -1e9f;
			for (const Texel& t : texels)
			{
				sum += glm::dvec3(ToLinear(t.Color.r), ToLinear(t.Color.g), ToLinear(t.Color.b));
				lo = std::min(lo, t.Height);
				hi = std::max(hi, t.Height);
			}
			const vec3 mean = vec3(sum / static_cast<double>(count));
			const vec3 target(ToLinear(look.MeanColor.r), ToLinear(look.MeanColor.g), ToLinear(look.MeanColor.b));
			const vec3 scale = target / glm::max(mean, vec3(1e-4f));
			const float range = std::max(hi - lo, 1e-4f);

			albedo = {};
			albedo.Width = albedo.Height = size;
			albedo.Pixels.resize(count * 4);
			surface = albedo;
			std::vector<float> heights(count);
			glm::dvec3 scaled(0.0);
			for (size_t i = 0; i < count; ++i)
			{
				const Texel& t = texels[i];
				heights[i] = (t.Height - lo) / range;
				const vec3 linear = glm::min(vec3(ToLinear(t.Color.r), ToLinear(t.Color.g), ToLinear(t.Color.b)) * scale, vec3(1.0f));
				scaled += glm::dvec3(linear);
				albedo.Pixels[i * 4 + 0] = Byte(ToSrgb(linear.r));
				albedo.Pixels[i * 4 + 1] = Byte(ToSrgb(linear.g));
				albedo.Pixels[i * 4 + 2] = Byte(ToSrgb(linear.b));
				albedo.Pixels[i * 4 + 3] = Byte(heights[i]);
			}
			meanLinear = vec3(scaled / static_cast<double>(count));

			const float texel = look.TileMetres / static_cast<float>(size);
			const int S = static_cast<int>(size);
			auto H = [&](int x, int y) { return heights[static_cast<size_t>(Wrap(y, S)) * size + static_cast<size_t>(Wrap(x, S))] * look.ReliefMetres; };
			for (int y = 0; y < S; ++y)
				for (int x = 0; x < S; ++x)
				{
					const float dhdu = (H(x + 1, y) - H(x - 1, y)) / (2.0f * texel), dhdv = (H(x, y + 1) - H(x, y - 1)) / (2.0f * texel);
					const vec3 n = glm::normalize(vec3(-dhdu, -dhdv, 1.0f));
					const size_t i = static_cast<size_t>(y) * size + static_cast<size_t>(x);
					surface.Pixels[i * 4 + 0] = Byte(n.x * 0.5f + 0.5f);
					surface.Pixels[i * 4 + 1] = Byte(n.y * 0.5f + 0.5f);
					surface.Pixels[i * 4 + 2] = Byte(texels[i].Rough);
					surface.Pixels[i * 4 + 3] = Byte(texels[i].AO);
				}
		}
	}

	const std::array<Look, kKinds>& Looks()
	{
		// By SurfaceLayerKind. MacroColor: Terrain::PaintPartition's flat colours (their mean over its noise), for the macro map's modulation.
		static const std::array<Look, kKinds> table = { {
			{ "rock",      "rock",           6.0f, true,  0.70f, 0.30f, { 0.44f, 0.34f, 0.28f }, { 0.44f, 0.34f, 0.28f } },
			{ "snow",      "snow",          10.0f, false, 0.45f, 0.06f, { 0.92f, 0.94f, 0.98f }, { 0.92f, 0.94f, 0.98f } },
			{ "grass",     "grass",          4.0f, false, 0.20f, 0.02f, { 0.30f, 0.40f, 0.17f }, { 0.32f, 0.44f, 0.18f } },
			{ "sand",      "sand",           4.0f, false, 0.30f, 0.02f, { 0.80f, 0.66f, 0.45f }, { 0.80f, 0.66f, 0.45f } },
			{ "dry",       "dry_grass",      4.0f, false, 0.20f, 0.02f, { 0.47f, 0.47f, 0.23f }, { 0.47f, 0.47f, 0.23f } },
			{ "forest",    "forest_floor",   4.0f, false, 0.25f, 0.025f, { 0.24f, 0.20f, 0.12f }, { 0.12f, 0.17f, 0.07f } },
			{ "cliff",     "cliff_strata",  12.0f, true,  0.85f, 0.60f, { 0.38f, 0.36f, 0.34f }, { 0.37f, 0.36f, 0.34f } },
			{ "scree",     "scree_blocks",   6.0f, false, 0.65f, 0.85f, { 0.40f, 0.37f, 0.33f }, { 0.38f, 0.35f, 0.30f } },
			{ "gravel",    "gravel_pebbles", 1.6f, false, 0.55f, 0.05f, { 0.60f, 0.56f, 0.48f }, { 0.60f, 0.56f, 0.48f } },
			{ "mud",       "mud",            5.0f, false, 0.35f, 0.04f, { 0.22f, 0.17f, 0.13f }, { 0.26f, 0.21f, 0.16f } },
			{ "mud_dry",   "mud_dry",        4.0f, false, 0.50f, 0.03f, { 0.47f, 0.40f, 0.31f }, { 0.47f, 0.40f, 0.31f } },
			{ "grass_wet", "grass_wet",      4.0f, false, 0.25f, 0.02f, { 0.17f, 0.31f, 0.12f }, { 0.17f, 0.33f, 0.12f } },
			{ "path",      "path_earth",     5.0f, false, 0.45f, 0.03f, { 0.50f, 0.42f, 0.31f }, { 0.50f, 0.42f, 0.31f } },
		} };
		return table;
	}

	Maps Generate(uint32_t size)
	{
		size = std::clamp(size, 16u, 1024u);
		Maps maps;
		maps.Size = size;
		maps.Albedo.resize(kKinds);
		maps.Surface.resize(kKinds);
		std::vector<std::thread> threads;
		threads.reserve(kKinds);
		for (int kind = 0; kind < kKinds; ++kind)
			threads.emplace_back([&maps, kind, size] { MakeKind(kind, size, maps.Albedo[static_cast<size_t>(kind)], maps.Surface[static_cast<size_t>(kind)], maps.MeanLinear[static_cast<size_t>(kind)]); });
		for (std::thread& t : threads) t.join();
		return maps;
	}

	namespace
	{
		std::shared_future<std::shared_ptr<const Maps>> Request(uint32_t size)
		{
			(void)Looks();   // (its table outlives the cache: a worker still running at exit reads it)
			static std::mutex mutex;
			static std::map<uint32_t, std::shared_future<std::shared_ptr<const Maps>>> cache;
			const std::lock_guard<std::mutex> lock(mutex);
			std::shared_future<std::shared_ptr<const Maps>>& slot = cache[size];
			if (!slot.valid()) slot = std::async(std::launch::async, [size] { return std::make_shared<const Maps>(Generate(size)); }).share();
			return slot;
		}
	}

	void Prefetch(uint32_t size) { Request(size); }

	std::shared_ptr<const Maps> Get(uint32_t size) { return Request(size).get(); }
}
