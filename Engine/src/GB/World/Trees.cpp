#include "Trees.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		struct Rng
		{
			uint64_t s;
			explicit Rng(uint64_t seed) : s(0x9E3779B97F4A7C15ull ^ (seed * 0xBF58476D1CE4E5B9ull)) { next(); next(); }
			float next()
			{
				s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
				return static_cast<float>(((s * 0x2545F4914F6CDD1Dull) >> 40) & 0xFFFFFF) / 16777216.0f;
			}
			float range(float a, float b) { return a + (b - a) * next(); }
		};

		glm::vec3 AnyPerpendicular(const glm::vec3& d)
		{
			const glm::vec3 h = std::fabs(d.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
			return glm::normalize(glm::cross(h, d));
		}

		float Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
		}

		float Noise(float x, float y, uint32_t seed, int period)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
			fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
			auto w = [&](int v) { return ((v % period) + period) % period; };
			const float a = Hash(w(ix), w(iy), seed), b = Hash(w(ix + 1), w(iy), seed);
			const float c = Hash(w(ix), w(iy + 1), seed), d = Hash(w(ix + 1), w(iy + 1), seed);
			return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
		}

		struct MeshBuilder
		{
			std::vector<Vertex> v;
			std::vector<uint32_t> i;

			uint32_t Add(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, const glm::vec4& t)
			{
				v.push_back({ p, n, uv, t });
				return static_cast<uint32_t>(v.size() - 1);
			}

			// Tapered tube from a to b; v coordinates run v0..v1 along it, u repeats `uRepeat` times around.
			void Tube(const glm::vec3& a, const glm::vec3& b, float ra, float rb, int sides, float v0, float v1, float uRepeat)
			{
				const glm::vec3 axis = b - a;
				const float len = glm::length(axis);
				if (len < 1e-5f)
					return;
				const glm::vec3 dir = axis / len;
				const glm::vec3 right = AnyPerpendicular(dir), up = glm::cross(dir, right);
				const uint32_t base = static_cast<uint32_t>(v.size());
				for (int s = 0; s <= sides; ++s)
				{
					const float u = static_cast<float>(s) / static_cast<float>(sides);
					const float ang = u * 2.0f * kPi;
					const glm::vec3 n = right * std::cos(ang) + up * std::sin(ang);
					const glm::vec4 tangent(glm::cross(n, dir), 1.0f);
					Add(a + n * ra, n, { u * uRepeat, v0 }, tangent);
					Add(b + n * rb, n, { u * uRepeat, v1 }, tangent);
				}
				for (int s = 0; s < sides; ++s)
				{
					const uint32_t i0 = base + static_cast<uint32_t>(s) * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
					i.insert(i.end(), { i0, i2, i1, i1, i2, i3 });
				}
			}

			// A quad centered on `c` spanned by the unit vectors r (width) and u (height), with a given vertex normal.
			void Card(const glm::vec3& c, const glm::vec3& r, const glm::vec3& u, float halfW, float halfH, const glm::vec3& normal)
			{
				const uint32_t base = static_cast<uint32_t>(v.size());
				const glm::vec4 tangent(r, 1.0f);
				Add(c - r * halfW - u * halfH, normal, { 0, 1 }, tangent);
				Add(c + r * halfW - u * halfH, normal, { 1, 1 }, tangent);
				Add(c + r * halfW + u * halfH, normal, { 1, 0 }, tangent);
				Add(c - r * halfW + u * halfH, normal, { 0, 0 }, tangent);
				i.insert(i.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			}
		};

		// ------------------------------------------------------------ procedural textures
		Image MakeBark(bool rings)
		{
			constexpr uint32_t W = 128, H = 256;
			Image img = Image::Solid(W, H, 80, 60, 45);
			for (uint32_t y = 0; y < H; ++y)
				for (uint32_t x = 0; x < W; ++x)
				{
					const float u = static_cast<float>(x) / W, v = static_cast<float>(y) / H;
					float n;
					if (rings)   // palm: horizontal ridges
						n = 0.55f + 0.35f * std::sin(v * 2.0f * kPi * 18.0f + 2.0f * Noise(u * 8.0f, v * 8.0f, 3, 8)) + 0.2f * (Noise(u * 16.0f, v * 64.0f, 5, 16) - 0.5f);
					else         // vertical furrows
						n = 0.45f * Noise(u * 12.0f, v * 3.0f, 7, 12) + 0.35f * Noise(u * 24.0f, v * 6.0f, 9, 24) + 0.3f * Noise(u * 48.0f, v * 12.0f, 11, 48);
					const float f = glm::clamp(0.35f + 0.9f * n, 0.0f, 1.3f);
					const glm::vec3 c = rings ? glm::vec3(0.50f, 0.40f, 0.30f) * f : glm::vec3(0.36f, 0.27f, 0.20f) * f;
					uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * W + x) * 4];
					p[0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
					p[1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
					p[2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
				}
			return img;
		}

		// Alpha-masked cards, drawn at 3x3 samples per texel so the leaf edges carry real partial coverage (the renderer
		// turns that into alpha-to-coverage, and the shadow pass cuts the same shapes out of the depth map of the sun).
		// kind 0: oak leaves (pointed, serrated), 1: needle tuft on a twig, 2: palm frond, 3: round leaves (birch, lime),
		// 4: five-lobed maple leaves. Card space: u, v in -1..1, v = -1 at the top.
		struct FoliageSample
		{
			bool Hit = false;
			glm::vec3 Color{ 0.0f };
		};

		template <typename Fn>
		Image RasterFoliage(uint32_t W, uint32_t H, Fn&& sample)
		{
			Image img = Image::Solid(W, H, 60, 110, 30, 0);
			const glm::vec3 bleed(0.14f, 0.30f, 0.07f);   // colour of the fully transparent texels, so filtering never fringes dark
			constexpr int kSub = 3;
			for (uint32_t y = 0; y < H; ++y)
				for (uint32_t x = 0; x < W; ++x)
				{
					int hits = 0;
					glm::vec3 sum(0.0f);
					for (int sy = 0; sy < kSub; ++sy)
						for (int sx = 0; sx < kSub; ++sx)
						{
							const float u = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSub) / static_cast<float>(W) * 2.0f - 1.0f;
							const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSub) / static_cast<float>(H) * 2.0f - 1.0f;
							const FoliageSample s = sample(u, v);
							if (s.Hit) { ++hits; sum += s.Color; }
						}
					const glm::vec3 c = hits > 0 ? sum / static_cast<float>(hits) * 1.7f : bleed;
					uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * W + x) * 4];
					p[0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
					p[1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
					p[2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
					p[3] = static_cast<uint8_t>(static_cast<float>(hits) / (kSub * kSub) * 255.0f + 0.5f);
				}
			return img;
		}

		// A leaf lying along `Dir` from `Base`: `along` 0..1 from the stem to the tip, `across` in leaf lengths from the midrib.
		struct LeafPlacement
		{
			glm::vec2 Base{ 0.0f };
			glm::vec2 Dir{ 0.0f, -1.0f };
			float Length = 1.0f;
			float Tone = 0.5f;   // per-leaf colour variation, 0..1
		};

		FoliageSample LeafSample(const LeafPlacement& l, const glm::vec2& p, bool round, float noise)
		{
			FoliageSample out;
			const glm::vec2 d = p - l.Base;
			const float along = glm::dot(d, l.Dir) / l.Length;
			const float across = glm::dot(d, glm::vec2(-l.Dir.y, l.Dir.x)) / l.Length;
			if (along <= 0.0f || along >= 1.0f)
				return out;
			float half;
			if (round)   // egg shape, blunt, with a short drawn-out tip
				half = 0.36f * std::pow(std::sin(kPi * std::pow(along, 0.72f)), 0.85f) * (1.0f - 0.25f * std::pow(along, 6.0f));
			else         // lance shape with a serrated margin
				half = 0.27f * std::sin(kPi * std::pow(along, 0.8f)) * (1.0f + 0.07f * std::sin(along * 64.0f + l.Tone * 9.0f));
			const float a = std::fabs(across);
			if (a >= half)
				return out;
			out.Hit = true;
			const float rim = glm::clamp((half - a) / std::max(half, 1e-3f), 0.0f, 1.0f);   // 0 at the margin, 1 at the midrib
			const bool midrib = a < 0.010f + 0.012f * (1.0f - along);
			const float lateral = std::fabs(std::fmod(along * 8.0f - a * 2.4f + 4.0f, 1.0f) - 0.5f);
			const bool vein = lateral < 0.045f && a < half * 0.92f;
			glm::vec3 c = glm::mix(glm::vec3(0.08f, 0.22f, 0.05f), glm::vec3(0.20f, 0.40f, 0.08f), l.Tone);
			c *= 0.75f + 0.45f * noise;
			c *= 0.72f + 0.38f * rim;                 // darker toward the margin
			c = glm::mix(c, c * 1.25f + glm::vec3(0.03f, 0.05f, 0.0f), glm::smoothstep(0.4f, 1.0f, along));   // lighter young tip
			if (midrib) c = c * 1.5f + glm::vec3(0.05f, 0.06f, 0.01f);
			else if (vein) c *= 1.2f;
			out.Color = c;
			return out;
		}

		Image MakeFoliage(int kind)
		{
			const uint32_t W = kind == 2 ? 128 : 256, H = kind == 2 ? 512 : 256;
			auto grain = [](float u, float v) { return 0.5f + 0.5f * Noise((u + 1.0f) * 3.0f, (v + 1.0f) * 3.0f, 21, 6); };

			if (kind == 0 || kind == 3)
			{
				// Leaves arranged around the card center, each with its own angle, length and tone.
				const int count = kind == 0 ? 5 : 6;
				std::vector<LeafPlacement> leaves;
				Rng rng(kind == 0 ? 41 : 43);
				for (int k = 0; k < count; ++k)
				{
					const float ang = (static_cast<float>(k) + rng.range(-0.15f, 0.15f)) / static_cast<float>(count) * 2.0f * kPi + 0.4f;
					LeafPlacement l;
					l.Dir = { std::cos(ang), std::sin(ang) };
					l.Length = (kind == 0 ? 0.92f : 0.80f) * rng.range(0.85f, 1.0f);
					l.Base = -l.Dir * 0.04f + glm::vec2(rng.range(-0.04f, 0.04f), rng.range(-0.04f, 0.04f));
					l.Tone = rng.next();
					leaves.push_back(l);
				}
				return RasterFoliage(W, H, [&](float u, float v)
				{
					FoliageSample best;
					const float g = grain(u, v);
					for (const LeafPlacement& l : leaves)
					{
						const FoliageSample s = LeafSample(l, { u, v }, kind == 3, g);
						if (s.Hit) best = s;   // later leaves lie on top
					}
					return best;
				});
			}
			if (kind == 4)
			{
				// Three five-lobed leaves: radius as a function of the angle around the leaf center, veins along the lobes.
				struct Maple { glm::vec2 Center; float Radius, Turn, Tone; };
				const Maple maples[] = { { { -0.30f, 0.20f }, 0.52f, 0.5f, 0.2f }, { { 0.36f, 0.12f }, 0.48f, -0.4f, 0.6f }, { { 0.0f, -0.42f }, 0.46f, 0.1f, 0.9f } };
				return RasterFoliage(W, H, [&](float u, float v)
				{
					FoliageSample best;
					const float g = grain(u, v);
					for (const Maple& m : maples)
					{
						const glm::vec2 d = glm::vec2(u, v) - m.Center;
						const float r = glm::length(d);
						if (r > m.Radius) continue;
						const float theta = std::atan2(d.y, d.x) - m.Turn;
						const float lobe = std::fabs(std::cos(2.5f * theta));
						const float edge = m.Radius * (0.30f + 0.70f * std::pow(lobe, 1.7f));
						const bool stem = std::fabs(std::sin(theta - kPi)) * r < 0.012f && std::cos(theta - kPi) > 0.0f;
						if (r > edge && !stem) continue;
						glm::vec3 c = glm::mix(glm::vec3(0.12f, 0.26f, 0.05f), glm::vec3(0.24f, 0.40f, 0.07f), m.Tone) * (0.75f + 0.45f * g);
						c *= 0.7f + 0.4f * glm::clamp(1.0f - r / std::max(edge, 1e-3f), 0.0f, 1.0f);
						if (std::fabs(std::sin(2.5f * theta)) * r < 0.014f || stem) c = c * 1.5f + glm::vec3(0.04f, 0.05f, 0.0f);
						best.Hit = true;
						best.Color = c;
					}
					return best;
				});
			}
			if (kind == 1)
			{
				// A twig with needles fanning from it; each needle is a tapered segment (distance test), dark at the base.
				struct Needle { glm::vec2 A, B; float Tone; };
				std::vector<Needle> needles;
				Rng rng(47);
				const int count = 96;
				for (int k = 0; k < count; ++k)
				{
					const float t = static_cast<float>(k) / static_cast<float>(count - 1);
					const glm::vec2 from(0.0f, 0.95f - 1.55f * t);   // along the twig, bottom to top
					const float side = (k & 1) ? 1.0f : -1.0f;
					const float ang = side * rng.range(0.55f, 1.15f);
					const float len = rng.range(0.55f, 0.85f) * (1.0f - 0.3f * t);
					needles.push_back({ from, from + glm::vec2(std::sin(ang), -std::cos(ang)) * len + glm::vec2(0.0f, -0.12f), rng.next() });
				}
				return RasterFoliage(W, H, [&](float u, float v)
				{
					FoliageSample best;
					const glm::vec2 p(u, v);
					if (std::fabs(u) < 0.03f && v > -0.75f && v < 0.98f)
					{
						best.Hit = true;
						best.Color = glm::vec3(0.16f, 0.10f, 0.06f);
					}
					for (const Needle& n : needles)
					{
						const glm::vec2 ab = n.B - n.A;
						const float t = glm::clamp(glm::dot(p - n.A, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
						const float dist = glm::length(p - (n.A + ab * t));
						if (dist < 0.052f * (1.0f - 0.55f * t))
						{
							best.Hit = true;
							best.Color = glm::mix(glm::vec3(0.07f, 0.20f, 0.08f), glm::vec3(0.17f, 0.38f, 0.15f), t) * (0.8f + 0.4f * n.Tone);
						}
					}
					return best;
				});
			}
			// kind 2: palm frond. Central rib with narrow leaflets angled toward the tip, shorter near the tip (v = -1) and the base.
			return RasterFoliage(W, H, [&](float u, float v)
			{
				FoliageSample best;
				const glm::vec2 p(u, v);
				if (std::fabs(u) < 0.03f && v > -0.98f)
				{
					best.Hit = true;
					best.Color = glm::vec3(0.18f, 0.26f, 0.07f);
				}
				const int pairs = 26;
				for (int k = 0; k < pairs; ++k)
				{
					const float t = (static_cast<float>(k) + 0.5f) / pairs;            // 0 base .. 1 tip
					const float y = 0.92f - 1.84f * t;
					const float len = 0.95f * std::sin(kPi * std::pow(std::clamp(1.0f - 0.9f * t, 0.0f, 1.0f), 0.75f)) * 0.85f + 0.05f;
					for (int side = -1; side <= 1; side += 2)
					{
						const glm::vec2 a(0.0f, y), b(static_cast<float>(side) * len, y - 0.30f * len);
						const glm::vec2 ab = b - a;
						const float s = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
						if (glm::length(p - (a + ab * s)) < 0.030f * (1.0f - 0.65f * s))
						{
							best.Hit = true;
							best.Color = glm::mix(glm::vec3(0.12f, 0.28f, 0.06f), glm::vec3(0.22f, 0.40f, 0.09f), s) * (0.85f + 0.3f * Noise(static_cast<float>(k) * 1.7f, static_cast<float>(side + 2), 31, 64));
						}
					}
				}
				return best;
			});
		}
	}

	TreeFactory::TreeFactory()
	{
		m_Bark = std::make_unique<Texture2D>(MakeBark(false), TextureEncoding::SRGB);
		m_BarkPalm = std::make_unique<Texture2D>(MakeBark(true), TextureEncoding::SRGB);
		m_Leaf = std::make_unique<Texture2D>(MakeFoliage(0), TextureEncoding::SRGB);
		m_LeafRound = std::make_unique<Texture2D>(MakeFoliage(3), TextureEncoding::SRGB);
		m_LeafMaple = std::make_unique<Texture2D>(MakeFoliage(4), TextureEncoding::SRGB);
		m_Needle = std::make_unique<Texture2D>(MakeFoliage(1), TextureEncoding::SRGB);
		m_Frond = std::make_unique<Texture2D>(MakeFoliage(2), TextureEncoding::SRGB);
	}

	TreeFactory::~TreeFactory() = default;

	std::unique_ptr<Tree> TreeFactory::Create(const TreeParams& p) const
	{
		auto tree = std::make_unique<Tree>();
		Rng rng(static_cast<uint64_t>(p.Seed) * 2654435761u + static_cast<uint64_t>(p.Species) * 97u + 13u);
		MeshBuilder wood, leaves;
		glm::vec3 crownCenter(0.0f, p.Height * 0.62f, 0.0f);
		float crownRadius = p.Height * 0.38f;
		const float trunkR = p.TrunkRadius > 0.0f ? p.TrunkRadius : p.Height * (p.Species == TreeSpecies::Palm ? 0.022f : 0.032f);

		const float leafSize = p.Species == TreeSpecies::Broadleaf ? 1.25f : 0.7f;
		auto addLeafCluster = [&](const glm::vec3& at, int count, float spread, float size, bool needles)
		{
			for (int k = 0; k < count; ++k)
			{
				const glm::vec3 c = at + glm::vec3(rng.range(-1, 1), rng.range(-0.7f, 0.9f), rng.range(-1, 1)) * spread;
				glm::vec3 outward = c - crownCenter;
				outward = glm::length(outward) > 1e-3f ? glm::normalize(outward) : glm::vec3(0, 1, 0);
				// Random orientation of the card; the vertex normal points out of the crown (soft, round shading).
				glm::vec3 cardN = glm::normalize(glm::vec3(rng.range(-1, 1), rng.range(-0.3f, 1.0f), rng.range(-1, 1)));
				const glm::vec3 r = AnyPerpendicular(cardN), u = glm::cross(cardN, r);
				const float ang = rng.range(0.0f, 2.0f * kPi);
				const glm::vec3 rr = r * std::cos(ang) + u * std::sin(ang), uu = -r * std::sin(ang) + u * std::cos(ang);
				const glm::vec3 shadeN = glm::normalize(glm::mix(outward, glm::vec3(0, 1, 0), needles ? 0.35f : 0.2f));
				const float s = size * rng.range(0.8f, 1.25f);
				leaves.Card(c, rr, uu, s * 0.5f, s * 0.5f, shadeN);
			}
		};

		// Recursive branch growth.
		struct Branch { glm::vec3 start, dir; float length, radius; int level; };
		std::vector<Branch> stack;

		switch (p.Species)
		{
		case TreeSpecies::Broadleaf:
		case TreeSpecies::Bare:
		{
			const bool bare = p.Species == TreeSpecies::Bare;
			const float trunkLen = p.Height * (bare ? 0.45f : 0.38f);
			stack.push_back({ glm::vec3(0.0f), glm::vec3(rng.range(-0.05f, 0.05f), 1.0f, rng.range(-0.05f, 0.05f)), trunkLen, trunkR, 0 });
			const int maxLevel = bare ? 3 : 3;
			while (!stack.empty())
			{
				Branch b = stack.back();
				stack.pop_back();
				const int segments = std::max(3, static_cast<int>(b.length * (b.level == 0 ? 2.0f : 3.0f)));
				glm::vec3 pos = b.start, dir = glm::normalize(b.dir);
				const float segLen = b.length / static_cast<float>(segments);
				for (int sIdx = 0; sIdx < segments; ++sIdx)
				{
					const float t0 = static_cast<float>(sIdx) / segments, t1 = static_cast<float>(sIdx + 1) / segments;
					const float r0 = b.radius * (1.0f - 0.65f * t0), r1 = b.radius * (1.0f - 0.65f * t1);
					dir = glm::normalize(dir + glm::vec3(rng.range(-0.12f, 0.12f), rng.range(-0.04f, 0.1f) + (b.level >= 2 ? -0.02f : 0.04f), rng.range(-0.12f, 0.12f)));
					const glm::vec3 next = pos + dir * segLen;
					wood.Tube(pos, next, r0, r1, b.level == 0 ? 9 : (b.level == 1 ? 6 : 4), t0 * b.length * 0.4f, t1 * b.length * 0.4f, b.level == 0 ? 2.0f : 1.0f);
					// children along the second half of the branch
					if (b.level < maxLevel && t0 > 0.3f)
					{
						const int kids = b.level == 0 ? 2 : (rng.next() < 0.7f ? 1 : 2);
						for (int k = 0; k < kids; ++k)
						{
							if (rng.next() > 0.8f)
								continue;
							const float ang = rng.range(35.0f, 65.0f) * kPi / 180.0f;
							const glm::vec3 side = AnyPerpendicular(dir);
							const float az = rng.range(0.0f, 2.0f * kPi);
							const glm::vec3 axis = glm::normalize(side * std::cos(az) + glm::cross(dir, side) * std::sin(az));
							glm::vec3 cd = glm::normalize(dir * std::cos(ang) + axis * std::sin(ang));
							if (b.level == 0) cd.y = std::max(cd.y, 0.1f);
							stack.push_back({ next, cd, b.length * rng.range(0.55f, 0.8f) * (b.level == 0 ? 1.15f : 1.0f), b.radius * 0.5f, b.level + 1 });
						}
					}
					pos = next;
				}
				// leaf clusters at the tip and along the last part of outer branches
				if (!bare && b.level >= 2)
				{
					const int count = static_cast<int>(std::ceil(11.0f * p.LeafDensity));
					addLeafCluster(pos, count, leafSize * 1.1f, leafSize * 1.3f, false);
					addLeafCluster(pos - dir * b.length * 0.3f, std::max(2, count / 2), leafSize, leafSize * 1.2f, false);
				}
				else if (!bare && b.level == 1 && b.length < p.Height * 0.2f)
					addLeafCluster(pos, static_cast<int>(std::ceil(8.0f * p.LeafDensity)), leafSize, leafSize * 1.3f, false);
			}
			crownCenter = { 0.0f, p.Height * 0.66f, 0.0f };
			crownRadius = p.Height * 0.42f;
			break;
		}
		case TreeSpecies::Conifer:
		{
			const float trunkLen = p.Height;
			const int segs = 10;
			for (int s = 0; s < segs; ++s)
			{
				const float t0 = static_cast<float>(s) / segs, t1 = static_cast<float>(s + 1) / segs;
				wood.Tube({ 0, t0 * trunkLen, 0 }, { 0, t1 * trunkLen, 0 }, trunkR * (1.0f - 0.85f * t0), trunkR * (1.0f - 0.85f * t1), 8, t0 * trunkLen * 0.4f, t1 * trunkLen * 0.4f, 2.0f);
			}
			crownCenter = { 0.0f, p.Height * 0.55f, 0.0f };
			crownRadius = p.Height * 0.3f;
			const int whorls = static_cast<int>(p.Height * 1.4f);
			for (int w = 0; w < whorls; ++w)
			{
				const float t = 0.18f + 0.82f * static_cast<float>(w) / whorls;
				const float y = t * trunkLen;
				const float reach = (1.0f - t) * p.Height * 0.26f + 0.25f;
				const int arms = 6;
				const float phase = rng.range(0.0f, 2.0f * kPi);
				for (int a = 0; a < arms; ++a)
				{
					const float ang = phase + static_cast<float>(a) / arms * 2.0f * kPi + rng.range(-0.2f, 0.2f);
					const glm::vec3 dir(std::cos(ang), -0.22f, std::sin(ang));
					const glm::vec3 tip = glm::vec3(0, y, 0) + dir * reach;
					wood.Tube({ 0, y, 0 }, tip, trunkR * 0.18f * (1.0f - t * 0.7f) + 0.01f, 0.006f, 4, 0.0f, reach * 0.4f, 1.0f);
					const int cards = std::max(2, static_cast<int>(std::ceil(reach * 3.0f * p.LeafDensity)));
					for (int k = 0; k < cards; ++k)
					{
						const float f = (static_cast<float>(k) + 0.5f) / cards;
						const glm::vec3 c = glm::mix(glm::vec3(0, y, 0), tip, 0.25f + 0.75f * f) + glm::vec3(rng.range(-0.1f, 0.1f), rng.range(-0.05f, 0.15f), rng.range(-0.1f, 0.1f));
						const glm::vec3 side = glm::normalize(glm::vec3(-dir.z, 0, dir.x));
						glm::vec3 outward = c - crownCenter;
						outward = glm::normalize(glm::mix(outward, glm::vec3(0, 1, 0), 0.4f));
						const glm::vec3 u = glm::normalize(glm::cross(side, glm::normalize(dir)));
						const float s = 0.75f * (1.0f - 0.3f * t) + 0.25f;
						leaves.Card(c, side, glm::normalize(glm::mix(u, glm::vec3(0, 1, 0), 0.35f)), s * 0.5f, s * 0.55f, outward);
					}
				}
			}
			break;
		}
		case TreeSpecies::Palm:
		{
			// Slightly curved trunk.
			const int segs = 12;
			const float lean = rng.range(0.6f, 1.6f);
			glm::vec3 prev(0.0f);
			glm::vec3 top(0.0f);
			for (int s = 1; s <= segs; ++s)
			{
				const float t = static_cast<float>(s) / segs;
				const glm::vec3 pt(lean * t * t, t * p.Height, 0.25f * lean * t * t);
				wood.Tube(prev, pt, trunkR * (1.0f - 0.35f * (t - 1.0f / segs)), trunkR * (1.0f - 0.35f * t), 8, (t - 1.0f / segs) * p.Height * 0.5f, t * p.Height * 0.5f, 1.0f);
				prev = pt;
				top = pt;
			}
			crownCenter = top;
			crownRadius = p.Height * 0.4f;
			const int fronds = static_cast<int>(std::ceil(11.0f * std::max(p.LeafDensity, 0.4f)));
			for (int f = 0; f < fronds; ++f)
			{
				const float ang = static_cast<float>(f) / fronds * 2.0f * kPi + rng.range(-0.2f, 0.2f);
				const float len = p.Height * rng.range(0.34f, 0.42f);
				const float up = rng.range(0.35f, 0.95f);
				const glm::vec3 horiz(std::cos(ang), 0.0f, std::sin(ang));
				// Ribbon along an arc that rises then droops.
				const int rs = 7;
				const float halfW = len * 0.17f;
				const glm::vec3 side(-horiz.z, 0.0f, horiz.x);
				const uint32_t base = static_cast<uint32_t>(leaves.v.size());
				for (int k = 0; k <= rs; ++k)
				{
					const float t = static_cast<float>(k) / rs;
					const glm::vec3 c = top + horiz * (len * t) + glm::vec3(0, len * (up * t - 0.55f * t * t), 0);
					const glm::vec3 ahead = top + horiz * (len * (t + 0.01f)) + glm::vec3(0, len * (up * (t + 0.01f) - 0.55f * (t + 0.01f) * (t + 0.01f)), 0);
					const glm::vec3 tang = glm::normalize(ahead - c);
					const glm::vec3 n = glm::normalize(glm::cross(side, tang));
					const glm::vec3 shadeN = glm::normalize(glm::mix(n.y < 0 ? -n : n, glm::vec3(0, 1, 0), 0.4f));
					// v = 1 at the base, -> 0 at the tip (matches the frond texture: tip at the top)
					leaves.Add(c - side * halfW, shadeN, { 0.0f, 1.0f - t }, glm::vec4(side, 1.0f));
					leaves.Add(c + side * halfW, shadeN, { 1.0f, 1.0f - t }, glm::vec4(side, 1.0f));
				}
				for (int k = 0; k < rs; ++k)
				{
					const uint32_t i0 = base + static_cast<uint32_t>(k) * 2;
					leaves.i.insert(leaves.i.end(), { i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2 });
				}
			}
			break;
		}
		}

		// ---- GPU meshes and materials
		tree->Height = p.Height;
		tree->CrownRadius = crownRadius;
		tree->Trunk = std::make_unique<Mesh>(wood.v, wood.i);
		tree->TriangleCount = wood.i.size() / 3;
		Material trunk;
		trunk.Albedo = p.Species == TreeSpecies::Bare ? glm::vec3(0.8f, 0.75f, 0.7f) : glm::vec3(1.0f);
		trunk.AlbedoMap = p.Species == TreeSpecies::Palm ? m_BarkPalm.get() : m_Bark.get();
		trunk.Roughness = 0.95f;
		trunk.HeightScale = 0.0f;
		trunk.CavityStrength = 0.0f;
		trunk.WindSway = p.Species == TreeSpecies::Palm ? 0.9f : 0.5f;
		trunk.WindHeight = p.Height;
		tree->TrunkMaterial = trunk;

		if (!leaves.v.empty())
		{
			tree->Foliage = std::make_unique<Mesh>(leaves.v, leaves.i);
			tree->TriangleCount += leaves.i.size() / 3;
			tree->LeafCardCount = leaves.i.size() / 6;
			Material foliage;
			foliage.Albedo = p.LeafTint;
			const Texture2D* broadleaf[] = { m_Leaf.get(), m_LeafRound.get(), m_LeafMaple.get() };   // the leaf shape varies with the seed
			foliage.AlbedoMap = p.Species == TreeSpecies::Conifer ? m_Needle.get() : p.Species == TreeSpecies::Palm ? m_Frond.get() : broadleaf[p.Seed % 3];
			foliage.Roughness = 0.7f;
			foliage.Alpha = AlphaMode::Mask;
			foliage.AlphaCutoff = 0.4f;
			foliage.DoubleSided = true;
			foliage.FoliageNormals = true;
			foliage.HeightScale = 0.0f;
			foliage.CavityStrength = 0.0f;
			foliage.WindSway = p.Species == TreeSpecies::Conifer ? 0.45f : (p.Species == TreeSpecies::Palm ? 1.0f : 0.8f);
			foliage.WindHeight = p.Height;
			foliage.WindFlutter = p.Species == TreeSpecies::Conifer ? 0.03f : 0.09f;
			tree->FoliageMaterial = foliage;
		}
		return tree;
	}

	void Tree::AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale, float maxDistance) const
	{
		const glm::mat4 transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		const glm::vec3 boundsCenter(0.0f, Height * 0.5f, 0.0f);
		const float boundsRadius = 1.1f * glm::length(glm::vec2(Height * 0.5f, CrownRadius + 0.5f));
		RenderObject t;
		t.BoundingCenter = boundsCenter; t.BoundingRadius = boundsRadius; t.MaxDistance = maxDistance;
		t.MeshPtr = Trunk.get();
		t.Mat = TrunkMaterial;
		t.Transform = transform;
		scene.Objects.push_back(t);
		if (Foliage)
		{
			RenderObject f;
			f.BoundingCenter = boundsCenter; f.BoundingRadius = boundsRadius; f.MaxDistance = maxDistance;
			f.MeshPtr = Foliage.get();
			f.Mat = FoliageMaterial;
			f.Transform = transform;
			scene.Objects.push_back(f);
		}
	}

	std::vector<TreePlacement> ScatterTrees(const Terrain& terrain, int count, int variants, float minSpacing, uint32_t seed,
		float minHeight, float maxHeight, float maxSlopeDegrees, const std::function<bool(float, float)>& allow)
	{
		std::vector<TreePlacement> out;
		Rng rng(static_cast<uint64_t>(seed) * 40503u + 7u);
		const Heightmap& hm = terrain.GetHeightmap();
		const glm::vec2 lo = hm.GetMin(), hi = hm.GetMax();
		const float cell = std::max(minSpacing, 0.5f);
		std::unordered_map<int64_t, std::vector<glm::vec2>> grid;
		auto key = [&](int cx, int cz) { return static_cast<int64_t>(cx) * 73856093ll ^ static_cast<int64_t>(cz) * 19349663ll; };
		const float cosSlope = std::cos(glm::radians(maxSlopeDegrees));

		for (int attempt = 0; attempt < count * 40 && static_cast<int>(out.size()) < count; ++attempt)
		{
			const float x = rng.range(lo.x + 4.0f, hi.x - 4.0f), z = rng.range(lo.y + 4.0f, hi.y - 4.0f);
			const float y = hm.GetHeight(x, z);
			if (y < minHeight || y > maxHeight || hm.GetNormal(x, z).y < cosSlope || (allow && !allow(x, z)))
				continue;
			const int cx = static_cast<int>(std::floor(x / cell)), cz = static_cast<int>(std::floor(z / cell));
			bool free = true;
			for (int dz = -1; dz <= 1 && free; ++dz)
				for (int dx = -1; dx <= 1 && free; ++dx)
				{
					const auto it = grid.find(key(cx + dx, cz + dz));
					if (it == grid.end())
						continue;
					for (const glm::vec2& q : it->second)
						if (glm::length(q - glm::vec2(x, z)) < minSpacing) { free = false; break; }
				}
			if (!free)
				continue;
			grid[key(cx, cz)].push_back({ x, z });
			TreePlacement tp;
			tp.Variant = variants > 0 ? static_cast<int>(rng.next() * static_cast<float>(variants)) % variants : 0;
			tp.Position = { x, y - 0.05f, z };
			tp.Yaw = rng.range(0.0f, 2.0f * kPi);
			tp.Scale = rng.range(0.8f, 1.25f);
			out.push_back(tp);
		}
		return out;
	}
}
