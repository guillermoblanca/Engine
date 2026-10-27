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

			// A card folded along its vertical midline (a V or a roof): the two halves are tilted by `fold` radians toward
			// `bendDir`, so the card keeps some width seen from any side instead of turning into a sliver.
			void FoldCard(const glm::vec3& c, const glm::vec3& r, const glm::vec3& u, float halfW, float halfH, float fold, const glm::vec3& bendDir, const glm::vec3& normal)
			{
				const uint32_t base = static_cast<uint32_t>(v.size());
				const glm::vec4 tangent(r, 1.0f);
				const glm::vec3 w = r * (halfW * std::cos(fold)), b = bendDir * (halfW * std::sin(fold));
				Add(c - w + b - u * halfH, normal, { 0.0f, 1.0f }, tangent);
				Add(c - u * halfH, normal, { 0.5f, 1.0f }, tangent);
				Add(c + w + b - u * halfH, normal, { 1.0f, 1.0f }, tangent);
				Add(c - w + b + u * halfH, normal, { 0.0f, 0.0f }, tangent);
				Add(c + u * halfH, normal, { 0.5f, 0.0f }, tangent);
				Add(c + w + b + u * halfH, normal, { 1.0f, 0.0f }, tangent);
				i.insert(i.end(), { base, base + 1, base + 4, base, base + 4, base + 3, base + 1, base + 2, base + 5, base + 1, base + 5, base + 4 });
			}

			// A small low-poly sphere (fruit).
			void Sphere(const glm::vec3& c, float radius, int seg)
			{
				const int rings = std::max(3, seg - 1);
				const uint32_t base = static_cast<uint32_t>(v.size());
				for (int y = 0; y <= rings; ++y)
				{
					const float phi = static_cast<float>(y) / static_cast<float>(rings) * kPi;
					for (int x = 0; x <= seg; ++x)
					{
						const float th = static_cast<float>(x) / static_cast<float>(seg) * 2.0f * kPi;
						const glm::vec3 n(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
						Add(c + n * radius, n, { static_cast<float>(x) / static_cast<float>(seg), static_cast<float>(y) / static_cast<float>(rings) * 0.2f }, glm::vec4(-std::sin(th), 0.0f, std::cos(th), 1.0f));
					}
				}
				for (int y = 0; y < rings; ++y)
					for (int x = 0; x < seg; ++x)
					{
						const uint32_t a = base + static_cast<uint32_t>(y * (seg + 1) + x), b = a + static_cast<uint32_t>(seg + 1);
						i.insert(i.end(), { a, b, a + 1, a + 1, b, b + 1 });
					}
			}
		};

		glm::vec3 RotateAround(const glm::vec3& v, const glm::vec3& axis, float a)
		{
			const float c = std::cos(a), s = std::sin(a);
			return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0f - c);
		}

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
				const int pairs = 44;
				for (int k = 0; k < pairs; ++k)
				{
					const float t = (static_cast<float>(k) + 0.5f) / pairs;            // 0 base .. 1 tip
					const float y = 0.92f - 1.84f * t;
					for (int side = -1; side <= 1; side += 2)
					{
						const float jitter = 0.88f + 0.24f * Noise(static_cast<float>(k) * 2.3f, static_cast<float>(side + 2), 33, 64);   // leaflets differ in length
						const float len = (0.95f * std::sin(kPi * std::pow(std::clamp(1.0f - 0.9f * t, 0.0f, 1.0f), 0.75f)) * 0.85f + 0.05f) * jitter;
						const glm::vec2 a(0.0f, y), b(static_cast<float>(side) * len, y - (0.22f + 0.14f * t) * len);
						const glm::vec2 ab = b - a;
						const float s = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
						if (glm::length(p - (a + ab * s)) < 0.024f * (1.0f - 0.6f * s))
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
		const float H = p.Height;
		const int detail = std::clamp(p.Detail, 0, 2);
		const float lod = detail == 0 ? 0.55f : (detail == 1 ? 1.0f : 1.3f);   // foliage density multiplier per level of detail
		const int sideCut = detail == 0 ? 2 : 0;                                // fewer sides on the tubes of the low level
		const float density = std::max(p.LeafDensity, 0.05f) * lod;
		const float trunkR = p.TrunkRadius > 0.0f ? p.TrunkRadius : H * (p.Species == TreeSpecies::Palm ? 0.022f : 0.032f);
		const int style = p.Style >= 0 ? p.Style : static_cast<int>(p.Seed % 3u);
		const glm::vec3 up(0, 1, 0);
		auto uniformDir = [&]() { return glm::normalize(glm::vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)) + glm::vec3(1e-4f)); };

		float crownRadius = H * 0.4f;

		// ---------------------------------------------------------------- broadleaf / bare
		if (p.Species == TreeSpecies::Broadleaf || p.Species == TreeSpecies::Bare)
		{
			const bool bare = p.Species == TreeSpecies::Bare;
			// Habit by style: 0 spreading (oak), 1 ovate (lime, birch), 2 columnar (poplar).
			struct Habit { float TrunkFrac, AngLo, AngHi, LenLo, LenHi, Up; int EndKids, TrunkKids; };
			static const Habit habits[3] = {
				{ 0.34f, 42.0f, 72.0f, 0.60f, 0.85f, 0.05f, 3, 4 },
				{ 0.42f, 32.0f, 58.0f, 0.50f, 0.75f, 0.12f, 2, 6 },
				{ 0.30f, 16.0f, 34.0f, 0.45f, 0.70f, 0.22f, 1, 7 } };
			const Habit hb = habits[style % 3];
			const float trunkLen = H * (hb.TrunkFrac + (bare ? 0.06f : 0.0f)) * rng.range(0.92f, 1.08f);
			const int maxLevel = bare ? 4 : 3;
			const float leafSize = (style == 0 ? 1.45f : 1.25f) * (bare ? 1.0f : 1.0f);

			struct Branch { glm::vec3 start, dir; float length, radius; int level; float az; bool trunkSide; };
			struct Anchor { glm::vec3 pos, dir; int level; };
			std::vector<Branch> stack;
			std::vector<Anchor> anchors;
						glm::vec3 trunkDir = glm::normalize(glm::vec3(rng.range(-0.06f, 0.06f), 1.0f, rng.range(-0.06f, 0.06f)));
			stack.push_back({ glm::vec3(0.0f), trunkDir, trunkLen, trunkR, 0, rng.range(0.0f, 2.0f * kPi), false });
			std::vector<glm::vec3> woodPoints;   // where branches (level >= 1) pass, to hang leaves on

			while (!stack.empty())
			{
				Branch b = stack.back();
				stack.pop_back();
				const int segments = b.level >= 2 ? std::max(3, static_cast<int>(b.length * 2.2f)) : std::max(b.level == 0 ? 4 : 3, static_cast<int>(b.length * (b.level == 0 ? 2.5f : 2.2f)));
				const int sides = std::max(3, (b.level == 0 ? 10 : (b.level == 1 ? 5 : (b.level == 2 ? 4 : 3))) - sideCut);
				glm::vec3 pos = b.start, dir = glm::normalize(b.dir);
				glm::vec3 wander = uniformDir();
				glm::vec3 midPos = b.start, midDir = dir;
				const float segLen = b.length / static_cast<float>(segments);
				const float taper = b.level == 0 ? 0.6f : 0.78f;
				auto radiusAt = [&](float t, const glm::vec3& at)
				{
					float r = std::max(b.radius * (1.0f - taper * std::pow(t, b.level == 0 ? 1.3f : 0.9f)), H * 0.0016f);
					if (b.level == 0)
						r *= 1.0f + 0.6f * std::exp(-at.y / (trunkR * 3.5f));   // widened base
					return r;
				};
				// Where along the branch the side branches leave (golden-angle azimuths, fewer and later on thin twigs).
				std::vector<int> kidsAt(static_cast<size_t>(segments), 0);
				int kidTotal = b.level == 0 ? hb.TrunkKids : (b.level == 1 ? 3 + static_cast<int>(rng.next() * 3.0f) : (b.level == 2 ? 2 + static_cast<int>(rng.next() * 2.0f) : (bare ? 1 : 0)));
				if (b.trunkSide) kidTotal = (kidTotal + 1) / 2;
				if (b.level == 0 && !bare && style != 2) kidTotal = std::max(2, kidTotal - 1);
				const float kidStart = b.level == 0 ? 0.45f : 0.3f;
				for (int k = 0; k < kidTotal; ++k)
				{
					const float tk = glm::mix(kidStart, 0.95f, (static_cast<float>(k) + rng.range(0.1f, 0.9f)) / static_cast<float>(kidTotal));
					++kidsAt[static_cast<size_t>(std::min(segments - 1, static_cast<int>(tk * static_cast<float>(segments))))];
				}
				auto spawn = [&](const glm::vec3& at, const glm::vec3& d, float radiusHere, float tAlong, bool end)
				{
					const glm::vec3 side = AnyPerpendicular(d);
					b.az += 2.399963f + rng.range(-0.35f, 0.35f);
					const glm::vec3 axis = glm::normalize(side * std::cos(b.az) + glm::cross(d, side) * std::sin(b.az));
					float angDeg = rng.range(hb.AngLo, hb.AngHi);
					if (end) angDeg = b.level == 0 ? rng.range(14.0f, 34.0f) * (style == 2 ? 0.4f : 1.0f) : angDeg * 0.55f;
					const float ang = angDeg * kPi / 180.0f;
					glm::vec3 cd = glm::normalize(d * std::cos(ang) + axis * std::sin(ang));
					cd = glm::normalize(cd + up * hb.Up * (b.level == 0 ? 1.0f : 0.5f) + glm::vec3(0.0f, b.level >= 2 ? -0.05f : 0.0f, 0.0f));
					if (b.level == 0) cd.y = std::max(cd.y, 0.15f);
					float len;
					if (end && b.level == 0)
						len = std::max((H * 0.97f - at.y) / std::max(cd.y, 0.45f), trunkLen * 0.5f) * rng.range(0.75f, 0.92f);   // the limbs finish the crown
					else
						len = b.length * rng.range(hb.LenLo, hb.LenHi) * (b.level == 1 ? 1.12f - 0.45f * tAlong : 1.0f) * (end ? 0.8f : 1.0f) * (b.level == 0 ? 0.6f : 1.0f);
					stack.push_back({ at, cd, len, radiusHere * rng.range(0.58f, 0.74f), b.level + 1, b.az, b.trunkSide || (b.level == 0 && !end) });
				};

				for (int s = 0; s < segments; ++s)
				{
					const float t0 = static_cast<float>(s) / static_cast<float>(segments), t1 = static_cast<float>(s + 1) / static_cast<float>(segments);
					wander = glm::normalize(wander + uniformDir() * 0.5f);
					dir = glm::normalize(dir + wander * (b.level == 0 ? 0.05f : 0.10f) + up * (b.level == 0 ? 0.0f : hb.Up * 0.06f) - up * (b.level >= 2 ? 0.035f : 0.0f));
					const glm::vec3 next = pos + dir * segLen;
					const float r0 = radiusAt(t0, pos), r1 = radiusAt(t1, next);
					wood.Tube(pos, next, r0, r1, sides, t0 * b.length * 0.4f, t1 * b.length * 0.4f, b.level == 0 ? 2.0f : 1.0f);
					if (s == segments * 3 / 5) { midPos = next; midDir = dir; }
					if (b.level >= 1 && !b.trunkSide) woodPoints.push_back(next);
					if (b.level < (b.trunkSide ? std::min(maxLevel, 2) : maxLevel))
						for (int k = 0; k < kidsAt[static_cast<size_t>(s)]; ++k)
							spawn(next, dir, r1, t1, false);
					pos = next;
				}
				// the end of the branch: the limbs of the crown (level 0), or the branch just carries on as the leaves' anchor
				if (b.level == 0)
				{
					for (int k = 0; k < hb.EndKids; ++k)
						spawn(pos, dir, radiusAt(1.0f, pos), 1.0f, true);
				}
				else if (b.level < maxLevel && b.level <= 2 && rng.next() < 0.5f)
					spawn(pos, dir, radiusAt(1.0f, pos), 1.0f, true);
				if (!bare && b.level >= 1 && b.level <= 2)
					anchors.push_back({ midPos, midDir, b.trunkSide ? 0 : 1 });   // leaves along the branch, not only at its tip
				if (!bare && (b.level >= 2 || (b.level == 1 && b.length < H * 0.22f)))
					anchors.push_back({ pos, dir, b.trunkSide ? 0 : b.level });
			}

			// Pruned-branch knots on the trunk and roots spreading from the base.
			if (!bare || true)
			{
				const int knots = bare ? 3 : 2 + static_cast<int>(rng.next() * 3.0f);
				for (int k = 0; k < knots; ++k)
				{
					const float y = trunkLen * rng.range(0.12f, 0.8f);
					const float r = trunkR * (1.0f - 0.6f * y / trunkLen) * 1.0f;
					const float ang = rng.range(0.0f, 2.0f * kPi);
					const glm::vec3 out(std::cos(ang), 0.25f, std::sin(ang));
					const glm::vec3 o = glm::normalize(out);
					wood.Tube(glm::vec3(0, y, 0) + o * r * 0.6f, glm::vec3(0, y, 0) + o * (r + trunkR * 0.28f), trunkR * 0.26f, trunkR * 0.20f, std::max(3, 5 - sideCut), 0.0f, 0.15f, 1.0f);
				}
				const int roots = (style == 2 ? 4 : 5) + static_cast<int>(rng.next() * 3.0f);
				for (int k = 0; k < roots; ++k)
				{
					const float ang = (static_cast<float>(k) + rng.range(-0.3f, 0.3f)) / static_cast<float>(roots) * 2.0f * kPi;
					const glm::vec3 o(std::cos(ang), 0.0f, std::sin(ang));
					const float reach = trunkR * rng.range(2.2f, 3.4f);
					const float r = trunkR * 1.35f;
					const glm::vec3 a = o * r * 0.55f + glm::vec3(0, trunkR * 1.9f, 0), m = o * (r + reach * 0.45f) + glm::vec3(0, trunkR * 0.55f, 0), e = o * (r + reach) + glm::vec3(0, -trunkR * 0.25f, 0);
					wood.Tube(a, m, trunkR * 0.62f, trunkR * 0.36f, std::max(3, 6 - sideCut), 0.0f, 0.4f, 1.0f);
					wood.Tube(m, e, trunkR * 0.36f, trunkR * 0.10f, std::max(3, 5 - sideCut), 0.4f, 0.8f, 1.0f);
				}
			}

			// Crown shape from where the leaves are, so the vertex normals shade it like one rounded volume.
			glm::vec3 centre(0.0f, H * 0.65f, 0.0f), ext(H * 0.35f);
			if (!anchors.empty())
			{
				glm::vec3 lo(1e9f), hi(-1e9f), sum(0.0f);
				for (const Anchor& a : anchors) { lo = glm::min(lo, a.pos); hi = glm::max(hi, a.pos); sum += a.pos; }
				centre = sum / static_cast<float>(anchors.size());
				ext = glm::max((hi - lo) * 0.5f + glm::vec3(leafSize), glm::vec3(1.0f));
				centre.y = (lo.y + hi.y) * 0.5f;
			}
			std::vector<glm::vec3> cardCentres;
			auto cluster = [&](const glm::vec3& at, const glm::vec3& dir, int level, int count)
			{
				const float spread = leafSize * (level >= 2 ? 1.0f : 0.8f);
				for (int k = 0; k < count; ++k)
				{
					glm::vec3 off = uniformDir() * std::cbrt(rng.next()) * spread;
					off.y *= 0.8f;
					const glm::vec3 c = at + off - dir * (level >= 2 ? spread * 0.25f : 0.0f);
					cardCentres.push_back(c);
					const glm::vec3 d = (c - centre) / (ext * ext);
					const glm::vec3 g = glm::length(d) > 1e-5f ? glm::normalize(d) : up;
					const glm::vec3 lobe = glm::length(off) > 1e-4f ? glm::normalize(off) : up;
					const glm::vec3 shadeN = glm::normalize(glm::mix(glm::normalize(g * 0.75f + lobe * 0.35f), up, 0.18f));
					const glm::vec3 cardN = glm::normalize(g * 0.6f + uniformDir() * 0.9f);
					const glm::vec3 r0 = AnyPerpendicular(cardN), u0 = glm::cross(cardN, r0);
					const float rot = rng.range(0.0f, 2.0f * kPi);
					const glm::vec3 rr = r0 * std::cos(rot) + u0 * std::sin(rot), uu = -r0 * std::sin(rot) + u0 * std::cos(rot);
					const float sz = leafSize * rng.range(0.75f, 1.3f);
					const glm::vec3 fold = glm::cross(rr, uu) * (rng.next() < 0.5f ? 1.0f : -1.0f);
					if (rng.next() < 0.35f)
						leaves.Card(c, rr, uu, sz * 0.5f, sz * 0.5f, shadeN);   // some flat cards: cheaper, and it varies the surface
					else
						leaves.FoldCard(c, rr, uu, sz * 0.5f, sz * 0.5f, rng.range(0.45f, 0.85f), fold, shadeN);
				}
			};
			for (const Anchor& a : anchors)
				cluster(a.pos, a.dir, a.level, std::max(2, static_cast<int>(std::ceil((a.level >= 2 ? 9.0f : (a.level == 1 ? 6.0f : 3.0f)) * density))));
			// fill the holes: best-candidate sampling along the branches, away from the leaves already placed
			if (!woodPoints.empty() && !cardCentres.empty())
			{
				const int fillers = static_cast<int>(static_cast<float>(anchors.size()) * 0.9f * lod);
				for (int k = 0; k < fillers; ++k)
				{
					glm::vec3 best = woodPoints[0];
					float bestD = -1.0f;
					for (int c = 0; c < 8; ++c)
					{
						const glm::vec3& cand = woodPoints[static_cast<size_t>(rng.next() * static_cast<float>(woodPoints.size())) % woodPoints.size()];
						float nearest = 1e9f;
						for (const glm::vec3& cc : cardCentres) nearest = std::min(nearest, glm::distance(cc, cand));
						if (nearest > bestD) { bestD = nearest; best = cand; }
					}
					if (bestD < leafSize * 0.5f) break;   // the crown is already covered
					cluster(best, up, 1, std::max(2, static_cast<int>(std::ceil(4.0f * density))));
				}
			}
			crownRadius = std::max(ext.x, ext.z);
		}
		// ---------------------------------------------------------------- conifer
		else if (p.Species == TreeSpecies::Conifer)
		{
			const bool pine = (style % 2) == 1;   // pine: bare lower trunk and tiers of upturned arms; spruce: a dense drooping cone
			const float bareFrac = pine ? rng.range(0.28f, 0.38f) : rng.range(0.10f, 0.18f);
			const float maxReach = H * (pine ? 0.25f : 0.20f) * rng.range(0.9f, 1.1f);
			const int segs = 12;
			glm::vec3 prev(0.0f);
			const float phase = rng.range(0.0f, 2.0f * kPi);
			auto trunkPoint = [&](float t) { return glm::vec3(H * 0.012f * std::sin(t * 5.0f + phase), t * H, H * 0.012f * std::sin(t * 4.0f + phase * 1.7f)); };
			for (int s = 1; s <= segs; ++s)
			{
				const float t0 = static_cast<float>(s - 1) / segs, t1 = static_cast<float>(s) / segs;
				const glm::vec3 pt = trunkPoint(t1);
				auto radius = [&](float t) { return trunkR * (1.0f - 0.9f * std::pow(t, 0.85f)) * (1.0f + 0.5f * std::exp(-t * H / (trunkR * 3.0f))); };
				wood.Tube(prev, pt, radius(t0), radius(t1), std::max(3, 8 - sideCut), t0 * H * 0.4f, t1 * H * 0.4f, 2.0f);
				prev = pt;
			}
			const glm::vec3 centre(0.0f, H * (bareFrac + 1.0f) * 0.5f, 0.0f);
			const glm::vec3 ext(maxReach, H * (1.0f - bareFrac) * 0.5f, maxReach);
			const float spacing = H * (pine ? 0.075f : 0.062f);
			float y = H * bareFrac;
			while (y < H * 0.97f)
			{
				const float u = glm::clamp((y - H * bareFrac) / (H * (0.97f - bareFrac)), 0.0f, 1.0f);   // 0 at the lowest whorl, 1 at the top
				const float profile = pine ? std::pow(1.0f - u, 0.62f) * (0.7f + 0.5f * Hash(static_cast<int>(y * 13.0f), 5, p.Seed)) : std::pow(1.0f - u, 0.92f);
				const float reachBase = maxReach * profile + 0.22f;
				const int arms = std::max(3, static_cast<int>(((pine ? 4 : 5) + static_cast<int>(rng.next() * 3.0f)) * (detail == 0 ? 0.7f : 1.0f)));
				const float ph = rng.range(0.0f, 2.0f * kPi);
				const glm::vec3 axis0 = trunkPoint(y / H);
				for (int a = 0; a < arms; ++a)
				{
					if (pine && rng.next() < 0.18f)
						continue;   // gaps between the tiers
					const float ang = ph + (static_cast<float>(a) + rng.range(-0.25f, 0.25f)) / arms * 2.0f * kPi;
					const float reach = reachBase * rng.range(0.78f, 1.12f);
					const glm::vec3 out(std::cos(ang), 0.0f, std::sin(ang));
					const float d0 = pine ? rng.range(-0.05f, 0.25f) : rng.range(-0.42f, -0.18f);   // initial slope
					const float d1 = pine ? rng.range(0.25f, 0.6f) : rng.range(0.0f, 0.35f);        // slope at the tip: tips curl up
					auto armPoint = [&](float s) { return axis0 + out * (reach * s) + glm::vec3(0.0f, reach * (d0 * s + (d1 - d0) * s * s), 0.0f); };
					auto armTangent = [&](float s) { return glm::normalize(out + glm::vec3(0.0f, d0 + 2.0f * (d1 - d0) * s, 0.0f)); };
					const float r0 = trunkR * 0.20f * (1.0f - 0.6f * u) + 0.012f;
					const int armSegs = detail == 0 ? 2 : 3;
					for (int k = 0; k < armSegs; ++k)
					{
						const float s0 = static_cast<float>(k) / static_cast<float>(armSegs), s1 = static_cast<float>(k + 1) / static_cast<float>(armSegs);
						wood.Tube(armPoint(s0), armPoint(s1), glm::mix(r0, 0.005f, s0), glm::mix(r0, 0.005f, s1), 4, s0 * reach * 0.4f, s1 * reach * 0.4f, 1.0f);
					}
					const float size = (pine ? 1.0f : 0.9f) * (1.0f - 0.42f * u) + 0.32f;
					const int n = std::max(2, static_cast<int>(std::ceil((reach * 1.45f + 0.5f) * density)));
					for (int k = 0; k < n; ++k)
					{
						const float s = glm::mix(0.12f, 1.0f, (static_cast<float>(k) + rng.range(0.3f, 0.9f)) / static_cast<float>(n));
						const int copies = (k % 2 == 1 && s > 0.3f) ? 3 : 1;   // every other position also sprays to both sides
						for (int c = 0; c < copies; ++c)
						{
							glm::vec3 tang = armTangent(s);
							if (c > 0) tang = RotateAround(tang, up, (c == 1 ? 1.0f : -1.0f) * rng.range(0.55f, 0.9f));
							const glm::vec3 side = glm::normalize(glm::cross(up, tang));
							const float roll = rng.range(-0.3f, 0.3f);
							const glm::vec3 rr = RotateAround(side, tang, roll);
							const float h = size * rng.range(0.85f, 1.2f) * (c > 0 ? 0.85f : 1.0f);
							const glm::vec3 pc = armPoint(s) + tang * (h * 0.42f);
							const glm::vec3 g = glm::normalize((pc - centre) / (ext * ext));
							const glm::vec3 shadeN = glm::normalize(glm::mix(g, up, 0.4f));
							// the spray lies along the arm; the fold droops its edges, so it is never edge-on like a fence of slats
							const glm::vec3 fold = glm::cross(rr, tang) * (glm::dot(glm::cross(rr, tang), up) > 0.0f ? -1.0f : 1.0f);
							leaves.FoldCard(pc, rr, tang, h * 0.46f, h * 0.55f, rng.range(0.5f, 0.8f), fold, shadeN);
						}
					}
				}
				y += spacing * rng.range(0.85f, 1.2f);
			}
			// the leader: a star of vertical sprays
			for (int k = 0; k < 3; ++k)
			{
				const float ang = static_cast<float>(k) / 3.0f * kPi + phase;
				const glm::vec3 rr(std::cos(ang), 0.0f, std::sin(ang));
				const glm::vec3 tip = trunkPoint(1.0f);
				leaves.Card(tip - up * 0.5f * 0.7f * 0.0f + up * 0.45f * lod, rr, up, 0.32f, 0.62f, glm::normalize(rr * 0.2f + up));
			}
			crownRadius = maxReach * 1.05f;
		}
		// ---------------------------------------------------------------- palm
		else
		{
			const bool date = (style % 2) == 1;   // 0: leaning coconut palm with long drooping fronds; 1: straighter, upright date-like palm
			const float lean = (date ? rng.range(0.15f, 0.5f) : rng.range(0.7f, 1.7f)) * H / 8.0f;
			const float bendAz = rng.range(0.0f, 2.0f * kPi);
			const glm::vec3 bendDir(std::cos(bendAz), 0.0f, std::sin(bendAz));
			const float wavePh = rng.range(0.0f, 2.0f * kPi);
			auto trunkPoint = [&](float t)
			{
				const float lat = lean * std::pow(t, 1.7f) + 0.05f * H * std::sin(t * 5.0f + wavePh) * t * (1.0f - 0.4f * t);
				const glm::vec3 side(-bendDir.z, 0.0f, bendDir.x);
				return bendDir * lat + side * (lean * 0.12f * std::sin(t * 3.0f + wavePh)) * t + glm::vec3(0.0f, t * H * (1.0f - 0.06f * t), 0.0f);
			};
			const int rings = std::max(12, static_cast<int>(H * (detail == 0 ? 1.8f : 3.2f)));
			glm::vec3 prev = trunkPoint(0.0f);
			glm::vec3 top = prev;
			for (int s = 1; s <= rings; ++s)
			{
				const float t0 = static_cast<float>(s - 1) / rings, t1 = static_cast<float>(s) / rings;
				auto radius = [&](float t) { return trunkR * (1.0f + 0.75f * std::exp(-t * 16.0f)) * (1.0f - 0.32f * t); };
				const glm::vec3 pt = trunkPoint(t1);
				// each ring flares slightly toward its upper edge: the scars of the fallen fronds
				wood.Tube(prev, pt, radius(t0) * 0.96f, radius(t1) * 1.05f, std::max(5, 8 - sideCut), t0 * H * 0.5f, t1 * H * 0.5f, 1.0f);
				prev = pt;
				top = pt;
			}
			// a small bulge where the fronds spring from
			wood.Tube(top - glm::vec3(0, 0.18f, 0) * H / 8.0f, top + glm::vec3(0, 0.12f, 0) * H / 8.0f, trunkR * 1.25f, trunkR * 0.8f, 6, 0.9f, 1.0f, 1.0f);
			crownRadius = H * 0.4f;

			const int fronds = std::max(6, static_cast<int>(std::ceil((date ? 20.0f : 17.0f) * std::max(p.LeafDensity, 0.4f) * (detail == 0 ? 0.6f : (detail == 1 ? 1.0f : 1.25f)))));
			for (int f = 0; f < fronds; ++f)
			{
				// Fronds on a golden-angle spiral: the oldest hang from the outside, the youngest stand near the middle.
				const float age = static_cast<float>(f) / static_cast<float>(fronds);   // 0 oldest .. 1 youngest
				const float ang = static_cast<float>(f) * 2.399963f + rng.range(-0.15f, 0.15f);
				const float len = H * rng.range(0.36f, 0.46f) * (date ? 0.88f : 1.0f) * (1.0f - 0.25f * age);
				const float upv = glm::mix(date ? 0.35f : 0.1f, date ? 1.4f : 1.15f, age) + rng.range(-0.1f, 0.1f);
				const float droop = glm::mix(date ? 0.75f : 1.05f, 0.35f, age);
				const glm::vec3 horiz(std::cos(ang), 0.0f, std::sin(ang));
				const glm::vec3 side0(-horiz.z, 0.0f, horiz.x);
				const int rs = detail == 0 ? 5 : (detail == 1 ? 9 : 12);
				const float halfW = len * 0.19f;
				const float twist = rng.range(-0.35f, 0.35f);
				const uint32_t base = static_cast<uint32_t>(leaves.v.size());
				auto arc = [&](float t) { return top + horiz * (len * t * (1.0f - 0.18f * age)) + glm::vec3(0, len * (upv * t - droop * t * t), 0); };
				for (int k = 0; k <= rs; ++k)
				{
					const float t = static_cast<float>(k) / rs;
					const glm::vec3 c = arc(t);
					const glm::vec3 tang = glm::normalize(arc(std::min(1.0f, t + 0.02f)) - arc(std::max(0.0f, t - 0.02f)));
					const glm::vec3 side = RotateAround(side0, tang, twist * t);
					glm::vec3 n = glm::normalize(glm::cross(side, tang));
					if (n.y < 0.0f) n = -n;
					const glm::vec3 shadeN = glm::normalize(glm::mix(n, up, 0.35f));
					// the leaflets hang down on both sides of the rib, more toward the tip
					const float sag = halfW * (0.25f + 0.35f * t);
					const float v = 1.0f - t;
					leaves.Add(c - side * halfW * (1.0f - 0.1f * t) - n * sag, shadeN, { 0.0f, v }, glm::vec4(side, 1.0f));
					leaves.Add(c, shadeN, { 0.5f, v }, glm::vec4(side, 1.0f));
					leaves.Add(c + side * halfW * (1.0f - 0.1f * t) - n * sag, shadeN, { 1.0f, v }, glm::vec4(side, 1.0f));
				}
				for (int k = 0; k < rs; ++k)
				{
					const uint32_t i0 = base + static_cast<uint32_t>(k) * 3;
					leaves.i.insert(leaves.i.end(), { i0, i0 + 1, i0 + 4, i0, i0 + 4, i0 + 3, i0 + 1, i0 + 2, i0 + 5, i0 + 1, i0 + 5, i0 + 4 });
				}
			}
			// fruit: coconuts / dates hanging in a bunch under the crown
			if (p.Fruit > 0.0f)
			{
				const int count = static_cast<int>(std::ceil(3.0f + 7.0f * glm::clamp(p.Fruit, 0.0f, 1.0f)));
				const float fr = H * (date ? 0.011f : 0.017f);
				const glm::vec3 centre = top - up * (H * 0.03f);
				for (int k = 0; k < count; ++k)
				{
					const float a = rng.range(0.0f, 2.0f * kPi), rad = trunkR * rng.range(1.4f, 2.4f);
					wood.Sphere(centre + glm::vec3(std::cos(a) * rad, -rng.range(0.0f, 0.18f) * H * 0.5f, std::sin(a) * rad), fr * rng.range(0.85f, 1.15f), detail == 0 ? 4 : 6);
				}
			}
		}

		// ---- fit the requested height, measure the real extent
		{
			float maxY = 0.0f;
			for (const Vertex& v : wood.v) maxY = std::max(maxY, v.Position.y);
			for (const Vertex& v : leaves.v) maxY = std::max(maxY, v.Position.y);
			const float k = maxY > 1e-3f ? glm::clamp(H / maxY, 0.6f, 1.6f) : 1.0f;
			float reach = 0.0f, top = 0.0f;
			for (auto* mb : { &wood, &leaves })
				for (Vertex& v : mb->v)
				{
					v.Position *= k;
					reach = std::max(reach, glm::length(glm::vec2(v.Position.x, v.Position.z)));
					top = std::max(top, v.Position.y);
				}
			crownRadius = reach;
			tree->BoundsHeight = std::max(top, H);
		}

		// ---- GPU meshes and materials
		tree->Height = H;
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
		trunk.WindHeight = H;
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
			foliage.WindHeight = H;
			foliage.WindFlutter = p.Species == TreeSpecies::Conifer ? 0.03f : 0.09f;
			tree->FoliageMaterial = foliage;
		}
		return tree;
	}

	void Tree::AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale, float maxDistance) const
	{
		const glm::mat4 transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		const float top = std::max(Height, BoundsHeight);
		const glm::vec3 boundsCenter(0.0f, top * 0.5f, 0.0f);
		const float boundsRadius = 1.1f * glm::length(glm::vec2(top * 0.5f, CrownRadius + 0.5f));
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
