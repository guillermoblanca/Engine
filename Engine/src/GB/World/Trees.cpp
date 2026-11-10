#include "GB/World/Trees.h"
#include "GB/Scene/Image.h"
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

		// Value noise that repeats every `px` cells in x and `py` in y (a texture tiles when x and y span whole periods over it).
		float Noise(float x, float y, uint32_t seed, int px, int py)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
			fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
			auto wx = [&](int v) { return ((v % px) + px) % px; };
			auto wy = [&](int v) { return ((v % py) + py) % py; };
			const float a = Hash(wx(ix), wy(iy), seed), b = Hash(wx(ix + 1), wy(iy), seed);
			const float c = Hash(wx(ix), wy(iy + 1), seed), d = Hash(wx(ix + 1), wy(iy + 1), seed);
			return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
		}
		float Noise(float x, float y, uint32_t seed, int period) { return Noise(x, y, seed, period, period); }

		// One cross-section of a branch: the centre of the ring and its radius (0 = the closed tip of the branch).
		struct Ring
		{
			glm::vec3 C{ 0.0f };
			float R = 0.0f;
		};

		struct MeshBuilder
		{
			std::vector<Vertex> v;
			std::vector<uint32_t> i;

			uint32_t Add(const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv, const glm::vec4& t)
			{
				v.push_back({ p, n, uv, t });
				return static_cast<uint32_t>(v.size() - 1);
			}

			// T110: a whole branch as one strip of rings (`sides[k]` vertices round ring k; neighbouring rings with a different count are
			// stitched by angle). The rings share their vertices with both segments, and their frame is carried along the branch by
			// parallel transport (the projection of the previous ring's frame), so consecutive rings line up: no notch nor slit at a
			// joint, and u runs on across it. With `tip` the last ring is the closed point of the branch (its radius is ignored).
			// - Normals: the central differences of the ring grid (round the ring and from the ring before to the ring after), so they
			//   are smooth along the whole branch, lean with the taper and with a collar, and point out (the triangles turn
			//   counter-clockwise seen from outside).
			// - UV in metres: u = u0 + (angle - pi) * radius of the ring (the lines converge toward the front line as the branch tapers,
			//   without a shear; the seam is at angle 0), v = v0 + the length along the axis. The tangents come from MeshData::ComputeTangents
			//   once the tree is scaled to its height. `frame` (optional) is the first ring's right vector in, the last one's out.
			void Strip(const std::vector<Ring>& rings, const std::vector<int>& sides, bool tip, float u0, float v0, glm::vec3* frame = nullptr)
			{
				const size_t n = rings.size();
				if (n < 2 || sides.size() != n)
					return;
				std::vector<glm::vec3> dir(n), right(n), up(n);
				for (size_t k = 0; k < n; ++k)
				{
					const glm::vec3 d = rings[std::min(k + 1, n - 1)].C - rings[k == 0 ? 0 : k - 1].C;
					dir[k] = glm::length(d) > 1e-7f ? glm::normalize(d) : (k > 0 ? dir[k - 1] : glm::vec3(0, 1, 0));
				}
				for (size_t k = 0; k < n; ++k)
				{
					glm::vec3 r = k == 0 ? (frame && glm::length(*frame) > 0.5f ? *frame : AnyPerpendicular(dir[0])) : right[k - 1];
					r -= dir[k] * glm::dot(r, dir[k]);
					right[k] = glm::length(r) > 1e-4f ? glm::normalize(r) : AnyPerpendicular(dir[k]);
					up[k] = glm::cross(dir[k], right[k]);
				}
				if (frame)
					*frame = right[n - 1];
				const size_t rimmed = tip ? n - 1 : n;   // rings with a rim of vertices
				auto at = [&](size_t k, float ang)
				{
					if (k >= rimmed)
						return rings[k].C;
					return rings[k].C + (right[k] * std::cos(ang) + up[k] * std::sin(ang)) * rings[k].R;
				};
				std::vector<float> vAlong(n, v0);
				for (size_t k = 1; k < n; ++k)
					vAlong[k] = vAlong[k - 1] + glm::length(rings[k].C - rings[k - 1].C);
				std::vector<uint32_t> base(n, 0);
				for (size_t k = 0; k < rimmed; ++k)
				{
					base[k] = static_cast<uint32_t>(v.size());
					const int m = std::max(3, sides[k]);
					const float step = 2.0f * kPi / static_cast<float>(m);
					for (int s = 0; s <= m; ++s)
					{
						const int sm = s % m;   // the seam's second column repeats the first one bit for bit
						const float ang = step * static_cast<float>(sm);
						const glm::vec3 p = at(k, ang);
						const glm::vec3 around = at(k, ang + step) - at(k, ang - step);
						const glm::vec3 along = at(std::min(k + 1, n - 1), ang) - at(k == 0 ? 0 : k - 1, ang);
						glm::vec3 nrm = glm::cross(around, along);
						const glm::vec3 radial = right[k] * std::cos(ang) + up[k] * std::sin(ang);
						nrm = glm::length(nrm) > 1e-9f ? glm::normalize(nrm) : radial;
						if (glm::dot(nrm, radial) < 0.0f) nrm = radial;   // a ring folded over its neighbour (never for sane input)
						Add(p, nrm, { u0 + (step * static_cast<float>(s) - kPi) * rings[k].R, vAlong[k] }, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
					}
				}
				uint32_t apex = 0;
				if (tip)
					apex = Add(rings[n - 1].C, dir[n - 1], { u0, vAlong[n - 1] }, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
				for (size_t k = 0; k + 1 < n; ++k)
				{
					const uint32_t na = static_cast<uint32_t>(std::max(3, sides[k]));
					if (k + 1 >= rimmed)
					{
						for (uint32_t s = 0; s < na; ++s)
							i.insert(i.end(), { base[k] + s, base[k] + s + 1, apex });
						continue;
					}
					const uint32_t nb = static_cast<uint32_t>(std::max(3, sides[k + 1]));
					// walk both rims by angle: advance the one whose next vertex comes first (a plain quad strip when na == nb)
					uint32_t a = 0, b = 0;
					while (a < na || b < nb)
					{
						if (b == nb || (a < na && (a + 1) * nb < (b + 1) * na))
						{
							i.insert(i.end(), { base[k] + a, base[k] + a + 1, base[k + 1] + b });
							++a;
						}
						else
						{
							i.insert(i.end(), { base[k + 1] + b, base[k] + a, base[k + 1] + b + 1 });
							++b;
						}
					}
				}
			}

			// T110: a leaf card, `cols` (1 flat, 2 folded along its midline) by `rows` along its height, centred on `c`, spanned by the unit
			// vectors r (width) and u (height, from the stem to the tip), with the crown normal on every vertex.
			// - fold: the two halves tilt by `fold` radians toward `bendDir` (a V or a roof: it keeps some width seen from any side).
			// - curl: the rows bend toward `bendDir` along the height (a parabola, `curl` x half the height at the ends).
			// - twist: the rows turn round the height axis, -twist at the stem to +twist at the tip (a warped card: non-planar halves).
			// - jitter: every vertex moved by up to `jitter` x half the width, from its own hash (`seed`).
			// UV: u 0..1 across, v 0 at the stem .. 1 at the tip (the texture's stem at v = 0, as ProcTexture's cards).
			void LeafCard(const glm::vec3& c, const glm::vec3& r, const glm::vec3& u, float halfW, float halfH, int cols, int rows,
			              float fold, float curl, float twist, float jitter, const glm::vec3& bendDir, uint32_t seed, const glm::vec3& normal)
			{
				const uint32_t base = static_cast<uint32_t>(v.size());
				const glm::vec3 pn = glm::normalize(glm::cross(r, u));
				const glm::vec3 bend = glm::dot(bendDir, pn) < 0.0f ? -pn : pn;
				for (int j = 0; j <= rows; ++j)
				{
					const float t = static_cast<float>(j) / static_cast<float>(rows), y = 2.0f * t - 1.0f;
					const float phi = twist * y;
					const glm::vec3 rr = r * std::cos(phi) + pn * std::sin(phi);
					const glm::vec3 rowC = c + u * (y * halfH) + bend * (curl * halfH * (y * y - 0.33f));
					for (int x = 0; x <= cols; ++x)
					{
						const float s = static_cast<float>(x) / static_cast<float>(cols) * 2.0f - 1.0f;   // -1 .. 1 across
						glm::vec3 p = rowC + rr * (s * halfW * (cols == 2 ? std::cos(fold) : 1.0f)) + bend * (std::fabs(s) * halfW * (cols == 2 ? std::sin(fold) : 0.0f));
						const int key = j * 8 + x;
						p += glm::vec3(Hash(key, 1, seed) - 0.5f, Hash(key, 2, seed) - 0.5f, Hash(key, 3, seed) - 0.5f) * (2.0f * jitter * halfW);
						Add(p, normal, { 0.5f * (s + 1.0f), t }, glm::vec4(rr, 1.0f));
					}
				}
				const uint32_t w = static_cast<uint32_t>(cols) + 1u;
				for (uint32_t j = 0; j < static_cast<uint32_t>(rows); ++j)
					for (uint32_t x = 0; x < static_cast<uint32_t>(cols); ++x)
					{
						const uint32_t a = base + j * w + x, b = a + 1, cc = a + w, d = cc + 1;
						i.insert(i.end(), { a, b, d, a, d, cc });
					}
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
						n = 0.55f + 0.35f * std::sin(v * 2.0f * kPi * 9.0f + 2.0f * Noise(u * 8.0f, v * 4.0f, 3, 8, 4)) + 0.2f * (Noise(u * 16.0f, v * 32.0f, 5, 16, 32) - 0.5f);   // T110: 9 ridges a tile (UV in metres, UVScale 1)
					else         // vertical furrows
						n = 0.45f * Noise(u * 12.0f, v * 1.0f, 7, 12, 1) + 0.35f * Noise(u * 24.0f, v * 2.0f, 9, 24, 2) + 0.3f * Noise(u * 48.0f, v * 4.0f, 11, 48, 4);   // T110: a square tile in metres (UVScale 1.25: 0.8 m), furrows ~0.8 m long
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
					uint8_t* p = &img.Pixels[(static_cast<size_t>(H - 1 - y) * W + x) * 4];   // T110: rows flipped, so the stem (v = +1 here) lands at the texture's v = 0
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


		// T110: the leaves at their real size. A card of a broadleaf is 1.0-1.9 m and a conifer's spray 0.6-1.6 m, so the texture is a whole
		// sprig: a twig from the stem (v = +1 here) with side twigs, the leaves (or needles) along them, alternating, each 10-20 cm (a
		// needle 4-6 cm), instead of five leaves of half the card. The shapes go into an 8 x 8 grid of buckets, so a sample only tests
		// the few that can cover it.
		struct Twig
		{
			glm::vec2 A{ 0.0f }, B{ 0.0f };
			float Width = 0.01f;
		};

		struct Sprig
		{
			std::vector<Twig> Twigs;
			std::vector<LeafPlacement> Leaves;
		};

		// A sprig: the main twig up the middle of the card, `sides` side twigs alternating left and right (longer toward the base),
		// leaves every `spacing` along every twig, `leafAngle` radians off it, and one at the end of each twig. Leaves that would leave
		// the card are shortened, or dropped below 45 % of their length.
		Sprig MakeSprig(uint64_t seed, int sides, float sideLo, float sideHi, float spacing, float leafLen, float leafAngle, float twigWidth)
		{
			Sprig out;
			Rng rng(seed);
			auto rot = [](const glm::vec2& d, float a) { return glm::vec2(d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)); };
			auto addLeaves = [&](const std::vector<glm::vec2>& path, float start)
			{
				float walked = 0.0f, next = start;
				int side = rng.next() < 0.5f ? 1 : -1;
				for (size_t k = 0; k + 1 < path.size(); ++k)
				{
					const glm::vec2 seg = path[k + 1] - path[k];
					const float len = glm::length(seg);
					if (len < 1e-5f) continue;
					const glm::vec2 d = seg / len;
					while (next <= walked + len)
					{
						LeafPlacement l;
						l.Base = path[k] + d * (next - walked);
						l.Dir = rot(d, static_cast<float>(side) * (leafAngle + rng.range(-0.2f, 0.2f)));
						l.Length = leafLen * rng.range(0.8f, 1.15f);
						l.Tone = rng.next();
						out.Leaves.push_back(l);
						side = -side;
						next += spacing * rng.range(0.85f, 1.15f);
					}
					walked += len;
				}
				// the twig's own end leaf, along it
				const glm::vec2 d = glm::normalize(path.back() - path[path.size() - 2]);
				LeafPlacement l;
				l.Base = path.back() - d * 0.01f;
				l.Dir = rot(d, rng.range(-0.15f, 0.15f));
				l.Length = leafLen * rng.range(0.95f, 1.2f);
				l.Tone = rng.next();
				out.Leaves.push_back(l);
			};
			auto twig = [&](glm::vec2 from, glm::vec2 dir, float length, float bendUp, float width)
			{
				std::vector<glm::vec2> path = { from };
				const int segs = 4;
				for (int s = 0; s < segs; ++s)
				{
					dir = glm::normalize(dir + glm::vec2(0.0f, -bendUp) + glm::vec2(rng.range(-0.12f, 0.12f), rng.range(-0.12f, 0.12f)));
					const glm::vec2 to = path.back() + dir * (length / segs);
					out.Twigs.push_back({ path.back(), to, width * (1.0f - 0.15f * static_cast<float>(s)) });
					path.push_back(to);
				}
				return path;
			};
			const std::vector<glm::vec2> main = twig({ rng.range(-0.05f, 0.05f), 0.99f }, { 0.0f, -1.0f }, 1.7f, 0.0f, twigWidth * 1.5f);
			std::vector<std::vector<glm::vec2>> paths = { main };
			for (int k = 0; k < sides; ++k)
			{
				const float f = 0.12f + 0.78f * (static_cast<float>(k) + rng.range(0.2f, 0.8f)) / static_cast<float>(sides);   // along the main twig
				const float pos = f * static_cast<float>(main.size() - 1);
				const size_t seg = std::min(static_cast<size_t>(pos), main.size() - 2);
				const glm::vec2 at = glm::mix(main[seg], main[seg + 1], pos - static_cast<float>(seg));
				const glm::vec2 md = glm::normalize(main[seg + 1] - main[seg]);
				const float side = (k & 1) ? 1.0f : -1.0f;
				const glm::vec2 d = rot(md, side * rng.range(0.7f, 1.1f));
				paths.push_back(twig(at, d, glm::mix(sideHi, sideLo, f) * rng.range(0.85f, 1.1f), 0.12f, twigWidth));
			}
			for (const std::vector<glm::vec2>& path : paths)
				addLeaves(path, spacing * 0.6f);
			// keep every leaf on the card
			std::vector<LeafPlacement> kept;
			for (LeafPlacement l : out.Leaves)
			{
				const float full = l.Length;
				auto inside = [](const glm::vec2& p) { return std::fabs(p.x) < 0.97f && std::fabs(p.y) < 0.97f; };
				while (l.Length > 0.45f * full && !inside(l.Base + l.Dir * l.Length)) l.Length *= 0.9f;
				if (inside(l.Base) && inside(l.Base + l.Dir * l.Length)) kept.push_back(l);
			}
			out.Leaves = std::move(kept);
			return out;
		}

		// Shape indices bucketed over the card ([-1, 1] in u and v) by their bounds.
		struct Buckets
		{
			static constexpr int N = 8;
			std::vector<int> Cells[N * N];
			static int Cell(float x) { return std::clamp(static_cast<int>((x + 1.0f) * 0.5f * N), 0, N - 1); }
			void Insert(int id, const glm::vec2& lo, const glm::vec2& hi)
			{
				for (int y = Cell(lo.y); y <= Cell(hi.y); ++y)
					for (int x = Cell(lo.x); x <= Cell(hi.x); ++x)
						Cells[y * N + x].push_back(id);
			}
			const std::vector<int>& At(float u, float v) const { return Cells[Cell(v) * N + Cell(u)]; }
		};

		float SegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, float* t = nullptr)
		{
			const glm::vec2 ab = b - a;
			const float s = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
			if (t) *t = s;
			return glm::length(p - (a + ab * s));
		}

		Image MakeFoliage(int kind)
		{
			const uint32_t W = kind == 2 ? 128 : 256, H = kind == 2 ? 512 : 256;
			auto grain = [](float u, float v) { return 0.5f + 0.5f * Noise((u + 1.0f) * 3.0f, (v + 1.0f) * 3.0f, 21, 6); };
			const glm::vec3 bark(0.16f, 0.10f, 0.06f);

			if (kind == 0 || kind == 3 || kind == 4)
			{
				// 0: oak-like lance leaves (~0.22 of the card: 15 cm on a 1.4 m card), 3: round leaves (~0.19), 4: five-lobed maple leaves
				// (radius ~0.12) on short stalks; shapes later in the list lie on top
				const Sprig sp = kind == 0 ? MakeSprig(41, 7, 0.45f, 0.85f, 0.090f, 0.22f, 0.85f, 0.010f)
				               : kind == 3 ? MakeSprig(43, 7, 0.45f, 0.85f, 0.078f, 0.19f, 0.80f, 0.009f)
				                           : MakeSprig(45, 6, 0.45f, 0.85f, 0.130f, 0.20f, 0.75f, 0.010f);
				Buckets twigs, leaves;
				for (size_t k = 0; k < sp.Twigs.size(); ++k)
				{
					const Twig& t = sp.Twigs[k];
					twigs.Insert(static_cast<int>(k), glm::min(t.A, t.B) - t.Width, glm::max(t.A, t.B) + t.Width);
				}
				for (size_t k = 0; k < sp.Leaves.size(); ++k)
				{
					const LeafPlacement& l = sp.Leaves[k];
					const glm::vec2 e = l.Base + l.Dir * l.Length;
					const float pad = l.Length * 0.4f;
					leaves.Insert(static_cast<int>(k), glm::min(l.Base, e) - pad, glm::max(l.Base, e) + pad);
				}
				return RasterFoliage(W, H, [&](float u, float v)
				{
					FoliageSample best;
					const glm::vec2 p(u, v);
					for (int k : twigs.At(u, v))
						if (SegmentDistance(p, sp.Twigs[static_cast<size_t>(k)].A, sp.Twigs[static_cast<size_t>(k)].B) < sp.Twigs[static_cast<size_t>(k)].Width)
						{
							best.Hit = true;
							best.Color = bark;
						}
					const float g = grain(u, v);
					int top = -1;
					FoliageSample hit;
					for (int k : leaves.At(u, v))
					{
						if (k < top) continue;   // a later leaf already covers the sample
						const LeafPlacement& l = sp.Leaves[static_cast<size_t>(k)];
						FoliageSample s;
						if (kind == 4)
						{
							// the blade round a centre one radius out along the stalk; veins along the lobes, the stalk back to the twig
							const float radius = l.Length * 0.6f;
							const glm::vec2 centre = l.Base + l.Dir * radius;
							const glm::vec2 d = p - centre;
							const float r = glm::length(d);
							const float theta = std::atan2(d.y, d.x) - std::atan2(l.Dir.y, l.Dir.x);
							const float lobe = std::fabs(std::cos(2.5f * theta));
							const float edge = radius * (0.30f + 0.70f * std::pow(lobe, 1.7f));
							const bool stalk = SegmentDistance(p, l.Base, centre) < 0.006f;
							if (r > edge && !stalk) continue;
							glm::vec3 c = glm::mix(glm::vec3(0.12f, 0.26f, 0.05f), glm::vec3(0.24f, 0.40f, 0.07f), l.Tone) * (0.75f + 0.45f * g);
							c *= 0.7f + 0.4f * glm::clamp(1.0f - r / std::max(edge, 1e-3f), 0.0f, 1.0f);
							if (std::fabs(std::sin(2.5f * theta)) * r < 0.006f || stalk) c = c * 1.5f + glm::vec3(0.04f, 0.05f, 0.0f);
							s.Hit = true;
							s.Color = c;
						}
						else
							s = LeafSample(l, p, kind == 3, g);
						if (s.Hit) { hit = s; top = k; }
					}
					return top >= 0 ? hit : best;
				});
			}
			if (kind == 1)
			{
				// A conifer spray: a twig with side twiglets, the needles (5-8 cm: ~0.15 of a 1 m spray) in pairs along all of them, angled
				// toward the tip; each needle a tapered segment (distance test), darker at its base.
				struct Needle { glm::vec2 A, B; float Tone; };
				const Sprig sp = MakeSprig(47, 16, 0.40f, 0.80f, 0.016f, 0.15f, 0.70f, 0.009f);
				std::vector<Needle> needles;
				for (const LeafPlacement& l : sp.Leaves)
					needles.push_back({ l.Base, l.Base + l.Dir * l.Length, l.Tone });
				Buckets twigs, buckets;
				for (size_t k = 0; k < sp.Twigs.size(); ++k)
				{
					const Twig& t = sp.Twigs[k];
					twigs.Insert(static_cast<int>(k), glm::min(t.A, t.B) - t.Width, glm::max(t.A, t.B) + t.Width);
				}
				for (size_t k = 0; k < needles.size(); ++k)
					buckets.Insert(static_cast<int>(k), glm::min(needles[k].A, needles[k].B) - 0.02f, glm::max(needles[k].A, needles[k].B) + 0.02f);
				return RasterFoliage(W, H, [&](float u, float v)
				{
					FoliageSample best;
					const glm::vec2 p(u, v);
					for (int k : twigs.At(u, v))
						if (SegmentDistance(p, sp.Twigs[static_cast<size_t>(k)].A, sp.Twigs[static_cast<size_t>(k)].B) < sp.Twigs[static_cast<size_t>(k)].Width)
						{
							best.Hit = true;
							best.Color = bark;
						}
					for (int k : buckets.At(u, v))
					{
						const Needle& n = needles[static_cast<size_t>(k)];
						float t = 0.0f;
						if (SegmentDistance(p, n.A, n.B, &t) < 0.017f * (1.0f - 0.5f * t))
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

	// The bark and leaf images depend on nothing of the world: painted once in the process (seconds of work), on whichever thread asks first
	// (the static is initialised once even when two threads ask together).
	const TreeImages& TreeFactory::Textures()
	{
		static const TreeImages images = []
		{
			TreeImages t;
			t.Images[static_cast<size_t>(TreeTexture::Bark)] = MakeBark(false);
			t.Images[static_cast<size_t>(TreeTexture::BarkPalm)] = MakeBark(true);
			t.Images[static_cast<size_t>(TreeTexture::Leaf)] = MakeFoliage(0);
			t.Images[static_cast<size_t>(TreeTexture::Needle)] = MakeFoliage(1);
			t.Images[static_cast<size_t>(TreeTexture::Frond)] = MakeFoliage(2);
			t.Images[static_cast<size_t>(TreeTexture::LeafRound)] = MakeFoliage(3);
			t.Images[static_cast<size_t>(TreeTexture::LeafMaple)] = MakeFoliage(4);
			return t;
		}();
		return images;
	}

	void TreeFactory::PrepareTextures() { (void)Textures(); }

	TreeTexture TreeFactory::TrunkTexture(const TreeParams& p) { return p.Species == TreeSpecies::Palm ? TreeTexture::BarkPalm : TreeTexture::Bark; }

	TreeTexture TreeFactory::FoliageTexture(const TreeParams& p)
	{
		const TreeTexture broadleaf[] = { TreeTexture::Leaf, TreeTexture::LeafRound, TreeTexture::LeafMaple };   // the leaf shape varies with the seed
		return p.Species == TreeSpecies::Conifer ? TreeTexture::Needle : p.Species == TreeSpecies::Palm ? TreeTexture::Frond : broadleaf[p.Seed % 3];
	}

	std::unique_ptr<Tree> TreeFactory::Create(const TreeParams& p)
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
		// T110: the cards' extra turn and shape draw from their own stream, so the tree's (branches, card places) is the one before
		Rng look(static_cast<uint64_t>(p.Seed) * 0x9E3779B1u + static_cast<uint64_t>(p.Species) * 131u + 7u);
		uint32_t cardSeed = 0;

		float crownRadius = H * 0.4f;

		// T111: roots of every count, angle, length and thickness: each sunk in the trunk's foot (its first ring inside the trunk), running
		// out and curving down into the ground, where it ends closed below the surface. The gaps between them are uneven, the thicker ones
		// reach further, each veers a little to one side.
		auto addRoots = [&](const auto& trunkAtY, float R, int count, float reachLo, float reachHi)
		{
			const int rootSides = std::max(3, 5 - sideCut);
			std::vector<float> steps(static_cast<size_t>(count));
			float total = 0.0f;
			for (float& st : steps) { st = rng.range(0.45f, 1.55f); total += st; }
			float az = rng.range(0.0f, 2.0f * kPi);
			const glm::vec3 foot = trunkAtY(0.0f).C;
			for (int k = 0; k < count; ++k)
			{
				az += steps[static_cast<size_t>(k)] / total * 2.0f * kPi;
				const float thick = R * rng.range(0.3f, 0.55f);
				const float reach = R * rng.range(reachLo, reachHi) * (0.55f + thick / R);
				const float h0 = std::max(std::min(R * rng.range(1.2f, 2.2f), 0.55f * reach), thick * 1.4f);
				const float depth = R * rng.range(0.25f, 0.5f);
				const float veer = rng.range(-0.35f, 0.35f);
				const Ring at0 = trunkAtY(h0);
				std::vector<Ring> rings = { { at0.C + glm::vec3(std::cos(az), 0.0f, std::sin(az)) * (at0.R * 0.4f), std::min(thick, 0.55f * at0.R) } };
				const int n = detail == 0 ? 3 : 6;
				for (int j = 1; j <= n; ++j)
				{
					const float s = static_cast<float>(j) / static_cast<float>(n);
					const float a = az + veer * s;
					const glm::vec3 o(std::cos(a), 0.0f, std::sin(a));
					const glm::vec3 c = foot + o * (R * 0.8f + reach * s) + up * (h0 * std::pow(1.0f - s, 2.2f) + thick * 0.2f - (thick * 0.2f + depth) * glm::smoothstep(0.45f, 1.0f, s));
					rings.push_back({ c, j == n ? 0.0f : thick * (1.0f - 0.8f * std::pow(s, 1.1f)) });
				}
				wood.Strip(rings, std::vector<int>(rings.size(), rootSides), true, Hash(k, 15, p.Seed), Hash(k, 16, p.Seed));
			}
		};

		// ---------------------------------------------------------------- broadleaf / bare
		if (p.Species == TreeSpecies::Broadleaf || p.Species == TreeSpecies::Bare)
		{
			const bool bare = p.Species == TreeSpecies::Bare;
			// T111: habit by style: 0 spreading (oak), 1 ovate (lime, birch), 2 columnar (poplar), 3 sapling (only when asked for: the style
			// derived from the seed stays 0..2, so the game's recipes keep their habits). How a tree grows:
			// - the radii follow the pipe model (Leonardo's rule): a side branch takes its cross-section off its parent's, so the parent
			//   narrows past every fork and the area of a mother is about the sum of her daughters';
			// - the trunk divides over a transition zone at its top: the limbs leave it one by one (ForkKids), each with its share of the
			//   area, and the trunk carries on as the leader with what is left (no ring that every limb leaves from);
			// - the side branches sag with their weight (Gravity, more the longer and the more level they are) and their ends turn up to
			//   the light (Light); the crown leans toward a light side (longer branches there, fewer on the shaded one);
			// - the trunk leans and bends gently (a dead tree more, and wobbling), and stands on a flared foot with roots of every size.
			struct Habit { float TrunkFrac, AngLo, AngHi, LenLo, LenHi, Up, Gravity, Light, LeafSize, RadiusK, Leaves; int ForkKids, TrunkKids, LeaderKids; };
			static const Habit habits[4] = {
				{ 0.34f, 42.0f, 70.0f, 0.55f, 0.80f, 0.07f, 0.06f, 0.12f, 1.40f, 1.00f, 7.6f, 3, 2, 3 },
				{ 0.40f, 32.0f, 58.0f, 0.50f, 0.75f, 0.12f, 0.05f, 0.12f, 1.30f, 1.00f, 5.8f, 2, 4, 4 },
				{ 0.30f, 16.0f, 34.0f, 0.45f, 0.70f, 0.22f, 0.03f, 0.08f, 1.30f, 1.00f, 4.6f, 1, 7, 5 },
				{ 0.46f, 38.0f, 64.0f, 0.45f, 0.70f, 0.14f, 0.05f, 0.14f, 1.05f, 0.42f, 1.7f, 0, 4, 5 } };
			const int habit = p.Style == 3 ? 3 : style % 3;
			const Habit hb = habits[habit];
			const bool sapling = habit == 3;
			const float baseR = p.TrunkRadius > 0.0f ? trunkR : trunkR * hb.RadiusK;
			const float trunkLen = H * (hb.TrunkFrac + (bare ? 0.06f : 0.0f)) * rng.range(0.92f, 1.08f);
			const int maxLevel = (bare ? 5 : (sapling ? 2 : 3)) - (detail == 0 && !sapling ? (bare ? 2 : 1) : 0);   // far trees: no finest twigs
			const float leafSize = hb.LeafSize;
			const float rMin = H * (bare ? 0.0009f : 0.0011f) * (sapling ? 0.8f : 1.0f);   // the thinnest twig
			const float flare = sapling ? 0.35f : (bare ? 0.9f : 0.75f);
			const float lightAz = rng.range(0.0f, 2.0f * kPi);
			const glm::vec3 lightDir(std::cos(lightAz), 0.0f, std::sin(lightAz));
			const float leanAz = rng.range(0.0f, 2.0f * kPi), lean = bare ? rng.range(0.10f, 0.22f) : rng.range(0.03f, 0.11f);
			const float bendAz = rng.range(0.0f, 2.0f * kPi), bendFreq = rng.range(1.6f, 3.2f), bendPh = rng.range(0.0f, 2.0f * kPi);
			const glm::vec3 bendDir(std::cos(bendAz), 0.0f, std::sin(bendAz));
			const float bendAmp = bare ? 0.10f : (sapling ? 0.035f : 0.05f);
			const float wobble = bare ? 0.16f : 0.06f;   // the dead trunk's twist: its axis winds round a small helix (fractions of its radius)
			const float wobblePitch = rng.range(1.2f, 2.0f), wobblePh = rng.range(0.0f, 2.0f * kPi);

			struct Branch
			{
				glm::vec3 start, dir; float length, radius; int level; float az; bool trunkSide;
				int parent = -1;          // the strip of the parent branch
				bool cont = false;        // carries the parent's strip on
				glm::vec3 pdir{ 0.0f };   // the parent's direction where this one leaves it
				float pr = 0.0f;          // the parent's radius there
				int psides = 3;           // and its sides
				bool stunted = false;     // stopped growing early: no side branches
				bool leader = false;      // the trunk carried on above the fork
			};
			struct StripDef
			{
				std::vector<Ring> rings;
				std::vector<int> sides;
				int parent = -1;
				bool cont = false;
				bool tipKids = false;     // branches leave from its last ring
			};
			struct Site { glm::vec3 pos, dir; float scale; };   // where a cluster of leaves hangs
			std::vector<Branch> stack;
			std::vector<StripDef> strips;
			std::vector<Site> sites;
			const glm::vec3 trunkDir = glm::normalize(up + glm::vec3(std::cos(leanAz), 0.0f, std::sin(leanAz)) * lean);
			stack.push_back({ glm::vec3(0.0f), trunkDir, trunkLen, baseR, 0, rng.range(0.0f, 2.0f * kPi), false });
			constexpr float kCollar = 1.25f;

			// The finest twigs of a dead tree (level 4) are grown in a second pass: when there are more than its budget (by height; the rest of
			// the bark is bounded by the branch counts), an evenly spread subset of them is kept, so the cost stays bounded whatever the seed
			// and no side of the crown is left bare.
			std::vector<Branch> fine;
			const size_t twigBudget = static_cast<size_t>(150.0f * H / 6.5f);
			for (int pass = 0; pass < 2; ++pass)
			{
			if (pass == 1)
			{
				const size_t keep = std::min(fine.size(), twigBudget);
				for (size_t k = 0; k < keep; ++k)
					stack.push_back(fine[k * fine.size() / keep]);
			}
			while (!stack.empty())
			{
				Branch b = stack.back();
				stack.pop_back();
				const int strip = static_cast<int>(strips.size());
				strips.push_back({});
				const float perMetre = (b.level == 0 ? 2.5f : (b.level == 1 ? 2.2f : (b.level == 2 ? 1.8f : 1.5f))) * (detail == 0 ? 0.7f : 1.0f);
				const int segments = std::max(b.level == 0 ? 4 : (b.level >= 4 ? 1 : (b.level == 3 ? 2 : 3)), static_cast<int>(b.length * perMetre));
				const int sides = std::max(3, (b.level == 0 ? 10 : (b.level == 1 ? 5 : (b.level == 2 && !bare ? 4 : 3))) - sideCut);
				glm::vec3 pos = b.start, dir = glm::normalize(b.dir);
				glm::vec3 wander = uniformDir();
				std::vector<std::pair<glm::vec3, glm::vec3>> along;   // every ring's centre and direction (the leaves' sites)
				const float segLen = b.length / static_cast<float>(segments);

				// where along the branch the side branches leave (and, on the trunk, the limbs of the fork)
				std::vector<int> kidsAt(static_cast<size_t>(segments), 0), forkAt(static_cast<size_t>(segments), 0);
				int kidTotal = b.level == 0 ? hb.TrunkKids
				             : b.leader ? hb.LeaderKids + static_cast<int>(rng.next() * 2.0f)
				             : b.level == 1 ? 2 + static_cast<int>(rng.next() * 3.0f)
				             : b.level == 2 ? (bare ? 2 + static_cast<int>(rng.next() * 2.5f) : 2 + static_cast<int>(rng.next() * 2.0f))
				             : b.level == 3 ? (bare ? 1 + static_cast<int>(rng.next() * 2.0f) : 0)
				                            : (bare ? 2 + static_cast<int>(rng.next() * 2.0f) : 0);
				if (b.trunkSide) kidTotal = (kidTotal + 1) / 2;
				if (b.stunted || b.level >= (b.trunkSide ? std::min(maxLevel, 2) : maxLevel)) kidTotal = 0;
				const float kidStart = b.level == 0 ? (sapling ? 0.3f : 0.4f) : 0.22f, kidEnd = b.level == 0 && hb.ForkKids > 0 ? 0.68f : 0.95f;
				for (int k = 0; k < kidTotal; ++k)
				{
					const float tk = glm::mix(kidStart, kidEnd, (static_cast<float>(k) + rng.range(0.0f, 1.0f)) / static_cast<float>(kidTotal));
					++kidsAt[static_cast<size_t>(std::min(segments - 1, static_cast<int>(tk * static_cast<float>(segments))))];
				}
				if (b.level == 0)
					for (int k = 0; k < hb.ForkKids; ++k)
					{
						const float tk = glm::mix(0.72f, 0.98f, (static_cast<float>(k) + rng.range(0.2f, 0.8f)) / static_cast<float>(hb.ForkKids));
						++forkAt[static_cast<size_t>(std::min(segments - 2, static_cast<int>(tk * static_cast<float>(segments))))];
					}

				// the radius: the cross-section left after the side branches (pipe model), a gentle taper on top of it, the trunk's foot flared
				float area = b.radius * b.radius;
				const float taper = b.level == 0 ? 0.15f : (kidTotal == 0 ? 0.42f : 0.30f);
				auto radiusAt = [&](float t, const glm::vec3& at)
				{
					float r = std::max(std::sqrt(area) * (1.0f - taper * t), rMin);
					if (b.level == 0)
						r *= 1.0f + flare * std::exp(-std::max(at.y, 0.0f) / (baseR * flare / 0.22f));   // its slope kept under ~0.22 (the bark's u, in metres round every ring, shears with it)
					return r;
				};
				// kind 0: a side branch, 1: a limb of the fork, 2: the shoot that carries the branch on (the leader on the trunk)
				auto spawn = [&](const glm::vec3& at, const glm::vec3& d, float radiusHere, float tAlong, int kind, bool cont)
				{
					const glm::vec3 side = AnyPerpendicular(d);
					b.az += 2.399963f + rng.range(-0.6f, 0.6f);
					const glm::vec3 axis = glm::normalize(side * std::cos(b.az) + glm::cross(d, side) * std::sin(b.az));
					float angDeg;
					if (kind == 1) angDeg = habit == 0 ? rng.range(18.0f, 36.0f) : (habit == 1 ? rng.range(18.0f, 34.0f) : rng.range(10.0f, 22.0f));
					else if (kind == 2) angDeg = b.level == 0 ? (habit == 0 ? rng.range(8.0f, 26.0f) : rng.range(2.0f, 10.0f)) : rng.range(hb.AngLo, hb.AngHi) * 0.45f;
					else angDeg = rng.range(hb.AngLo, hb.AngHi);
					const float ang = angDeg * kPi / 180.0f;
					glm::vec3 cd = glm::normalize(d * std::cos(ang) + axis * std::sin(ang));
					cd = glm::normalize(cd + up * hb.Up * (b.level == 0 ? 1.0f : 0.5f));
					if (b.level == 0 && cd.y < 0.15f) { cd.y = 0.15f; cd = glm::normalize(cd); }
					const glm::vec3 flat(cd.x, 0.0f, cd.z);
					const float toLight = glm::length(flat) > 1e-3f ? glm::dot(glm::normalize(flat), lightDir) : 0.0f;
					if (kind == 0 && b.level <= 1 && rng.next() < 0.22f * (0.5f - 0.5f * toLight))
						return;   // a side branch in the shade that died
					float len;
					if (kind != 0 && b.level == 0)   // the limbs and the leader finish the crown
						len = std::max((H * 0.97f - at.y) / std::max(cd.y, 0.62f), trunkLen * 0.5f) * rng.range(0.68f, 0.88f);
					else
						len = b.length * rng.range(hb.LenLo, hb.LenHi) * (b.level == 1 ? 1.12f - 0.45f * tAlong : 1.0f) * (kind == 2 ? 0.8f : 1.0f) * (b.level == 0 ? 0.6f : 1.0f);
					len *= (1.0f + 0.22f * toLight) * (b.level >= 3 ? 0.55f : 1.0f);   // (the finest twigs short: a dense fringe, not wires)
					bool stunted = false;
					if (kind == 0 && b.level >= 1 && rng.next() < 0.12f) { len *= 0.45f; stunted = true; }
					const float ratio = kind == 1 ? std::sqrt(rng.range(0.28f, 0.40f)) : kind == 2 ? 1.0f : (b.level == 0 ? rng.range(0.30f, 0.45f) : rng.range(0.50f, 0.70f));
					const float rk = std::max(radiusHere * ratio, rMin);
					if (kind != 2)
						area = std::max(area - rk * rk * 0.85f, 0.09f * b.radius * b.radius);
					Branch kid{ at, cd, len, rk, b.level + 1, b.az, b.trunkSide || (b.level == 0 && kind == 0) };
					kid.parent = strip; kid.cont = cont; kid.pdir = d; kid.pr = radiusHere; kid.psides = sides;
					kid.stunted = stunted; kid.leader = b.level == 0 && kind == 2;
					(bare && kid.level == 4 ? fine : stack).push_back(kid);   // the finest twigs wait for the second pass
				};

				StripDef sd;
				sd.parent = b.parent;
				sd.cont = b.cont;
				const bool sunk = b.parent >= 0 && !b.cont;
				float r0 = radiusAt(0.0f, b.start);
				if (sunk) r0 = std::min(r0, 0.85f * b.pr * std::cos(kPi / static_cast<float>(b.psides)));   // inside the parent's polygon
				sd.rings.push_back({ b.start, r0 });
				sd.sides.push_back(sides);
				// the trunk's axis winds round a small helix (a twisted dead trunk); the rings sit on it, the growth follows the plain axis
				auto wound = [&](const glm::vec3& c)
				{
					if (b.level != 0) return c;
					const float a = wobblePh + c.y / wobblePitch * 2.0f * kPi, fade = glm::smoothstep(0.0f, 1.6f, c.y) * (1.0f - glm::smoothstep(0.8f * trunkLen, trunkLen, c.y));
					return c + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * (wobble * baseR * fade);
				};
				if (b.level == 0)   // the flare: two more rings low on the trunk, so its foot widens over several of them
					for (const float f : { 0.3f, 0.65f })
					{
						const glm::vec3 c = wound(pos + dir * (segLen * f));
						sd.rings.push_back({ c, radiusAt(f / static_cast<float>(segments), c) });
						sd.sides.push_back(sides);
					}
				// (the sag grows with the branch's length and falls with its thickness: a thick limb hardly bends, a long thin branch arches)
				const float grav = b.level == 0 ? 0.0f : hb.Gravity * (bare ? 1.2f : 1.0f) * (b.level >= 4 ? 0.5f : 1.0f) * std::min(b.length / (H * 0.25f), 2.0f)
				                                        * glm::clamp(0.3f * baseR / std::max(b.radius, 1e-4f), 0.25f, 1.5f);
				const float light = b.level == 0 ? 0.0f : hb.Light * (bare ? 1.1f : 1.0f);
				for (int s = 0; s < segments; ++s)
				{
					const float t1 = static_cast<float>(s + 1) / static_cast<float>(segments), tm = (static_cast<float>(s) + 0.5f) / static_cast<float>(segments);
					wander = glm::normalize(wander + uniformDir() * 0.5f);
					glm::vec3 steer = wander * (b.level == 0 ? 0.03f : b.leader ? 0.035f : (b.level >= 3 ? 0.18f : 0.10f));
					if (b.leader) steer += up * 0.05f;
					if (b.level == 0)
						steer += bendDir * (bendAmp * std::sin(bendPh + bendFreq * tm * kPi)) + up * 0.02f;   // a gentle S, righting itself
					else
					{
						const float level = 1.0f - std::fabs(dir.y);   // a level branch sags the most
						steer += up * (hb.Up * 0.06f + light * tm * tm) - up * (grav * level * (1.0f - 0.5f * tm));
					}
					dir = glm::normalize(dir + steer);
					const glm::vec3 next = pos + dir * segLen;
					const glm::vec3 ringAt = wound(next);
					if (s == 0 && sunk && r0 > 0.015f)   // (a twig under 1.5 cm gets none: it would not show, and it saves triangles)
					{
						// the collar: just out of the parent's surface (a steep branch leaves it soon, a shallow one runs along it)
						const float sinT = glm::length(glm::cross(dir, glm::normalize(b.pdir)));
						const float exitAt = b.pr / std::max(sinT, 0.2f);
						const float at = exitAt + 0.3f * r0;
						if (at < 0.7f * segLen)
						{
							const glm::vec3 c = pos + dir * at;
							sd.rings.push_back({ c, radiusAt(at / b.length, c) * kCollar });
							sd.sides.push_back(sides);
						}
					}
					const float r1 = radiusAt(t1, ringAt);
					sd.rings.push_back({ ringAt, r1 });
					sd.sides.push_back(sides);
					along.push_back({ ringAt, dir });
					for (int k = 0; k < kidsAt[static_cast<size_t>(s)]; ++k)
					{
						spawn(ringAt, dir, r1, t1, 0, false);
						if (s == segments - 1) sd.tipKids = true;
					}
					if (b.level == 0)
						for (int k = 0; k < forkAt[static_cast<size_t>(s)]; ++k)
							spawn(ringAt, dir, r1, t1, 1, false);
					pos = next;
				}
				// the end: the trunk carries on as the leader; a branch may carry on as its own end shoot
				bool shoot = false;
				if (b.level == 0)
				{
					spawn(wound(pos), dir, radiusAt(1.0f, wound(pos)), 1.0f, 2, true);
					shoot = true;
				}
				else if (b.level < maxLevel && b.level <= 2 && !b.stunted && rng.next() < 0.5f)
				{
					spawn(pos, dir, radiusAt(1.0f, pos), 1.0f, 2, true);
					shoot = true;
				}
				// the leaves: a cluster at every branch end that does not carry on, and one along the longer branches
				if (!bare && b.level >= 1)
				{
					if (!shoot && (b.level >= 2 || kidTotal == 0 || b.length < H * 0.22f || sapling))
						sites.push_back({ pos, dir, b.trunkSide ? 0.75f : 1.0f });
					// and along the branch from about a third of its length, every ~0.7 of a leaf card (the crowding test spaces them)
					for (size_t k = static_cast<size_t>(along.size() / 3); k + 1 < along.size(); ++k)
						sites.push_back({ along[k].first, along[k].second, b.trunkSide ? 0.7f : 0.85f });
				}
				strips[static_cast<size_t>(strip)] = std::move(sd);
			}
			}

			// The trunk's centre and radius at height y (from its rings, before the leader carries it on), for the knots and roots.
			const std::vector<Ring> trunkRings = strips.front().rings;
			auto trunkAt = [&](float y)
			{
				for (size_t k = 0; k + 1 < trunkRings.size(); ++k)
					if (y <= trunkRings[k + 1].C.y || k + 2 == trunkRings.size())
					{
						const float t = glm::clamp((y - trunkRings[k].C.y) / std::max(trunkRings[k + 1].C.y - trunkRings[k].C.y, 1e-4f), 0.0f, 1.0f);
						return Ring{ glm::mix(trunkRings[k].C, trunkRings[k + 1].C, t), glm::mix(trunkRings[k].R, trunkRings[k + 1].R, t) };
					}
				return trunkRings.back();
			};

			// Chains: a branch that carries its parent on joins the parent's strip at the parent's last ring. Then every chain is one strip,
			// closed at its end by a short rounded cap (T111: never a long spike).
			{
				std::vector<int> root(strips.size(), -1);
				for (size_t k = 0; k < strips.size(); ++k)
				{
					StripDef& s = strips[k];
					root[k] = static_cast<int>(k);
					if (!s.cont || s.parent < 0) continue;
					const int r = root[static_cast<size_t>(s.parent)];
					StripDef& chain = strips[static_cast<size_t>(r)];
					chain.rings.insert(chain.rings.end(), s.rings.begin() + 1, s.rings.end());
					chain.sides.insert(chain.sides.end(), s.sides.begin() + 1, s.sides.end());
					chain.tipKids = s.tipKids;
					root[k] = r;
					s.rings.clear();
				}
				for (size_t k = 0; k < strips.size(); ++k)
				{
					StripDef& s = strips[k];
					if (s.rings.size() < 2) continue;
					const Ring last = s.rings.back();
					const glm::vec3 d = glm::normalize(last.C - s.rings[s.rings.size() - 2].C + glm::vec3(1e-6f));
					// (a thin twig ends in the apex alone; when branches leave from the last ring, the cap is longer, so it covers their mouths)
					const float reach = s.tipKids ? 1.6f : 1.0f;
					const bool capRing = detail > 0 && (last.R >= 0.02f || s.tipKids);
					if (capRing)
					{
						s.rings.push_back({ last.C + d * (last.R * 0.55f * reach), last.R * 0.75f });
						s.sides.push_back(s.sides.back());
					}
					s.rings.push_back({ last.C + d * (last.R * (capRing ? 0.85f : 0.6f) * reach), 0.0f });
					s.sides.push_back(s.sides.back());
					wood.Strip(s.rings, s.sides, true, Hash(static_cast<int>(k), 11, p.Seed), Hash(static_cast<int>(k), 12, p.Seed) * 2.0f);
				}
			}

			// Pruned-branch knots on the trunk, and the roots.
			{
				const int knotSides = std::max(3, 5 - sideCut);
				const int knots = sapling ? 1 : (bare ? 3 : 2 + static_cast<int>(rng.next() * 3.0f));
				for (int k = 0; k < knots; ++k)
				{
					const float y = trunkLen * rng.range(0.15f, 0.7f);
					const float ang = rng.range(0.0f, 2.0f * kPi);
					const glm::vec3 o = glm::normalize(glm::vec3(std::cos(ang), 0.25f, std::sin(ang)));
					const Ring t = trunkAt(y);
					const std::vector<Ring> rings = { { t.C + o * (t.R * 0.45f), std::min(baseR * 0.26f, 0.5f * t.R) }, { t.C + o * (t.R + baseR * 0.28f), baseR * 0.20f },
					                                  { t.C + o * (t.R + baseR * 0.36f), 0.0f } };
					wood.Strip(rings, std::vector<int>(rings.size(), knotSides), true, Hash(k, 13, p.Seed), Hash(k, 14, p.Seed));
				}
				const int roots = sapling ? 3 + static_cast<int>(rng.next() * 2.0f) : 4 + static_cast<int>(rng.next() * 4.0f);
				addRoots(trunkAt, baseR, roots, sapling ? 1.4f : 1.8f, sapling ? 2.6f : 3.8f);
			}

			// T111: the crown in clusters. Every site (a branch end, the middle of a long branch) carries a cluster: its cards spread round
			// its centre on a Fibonacci spiral (every card a different direction: never two nearly coplanar cards on top of each other),
			// lying across that direction like the scales of a ball, the outward half of the ball fuller. A cluster too close to one already
			// made is dropped, so the crown keeps holes between its clusters where the branches show. The shading normal mixes the crown's
			// ellipsoid with the cluster's own ball, so every cluster reads as a volume and the crown as one mass.
			glm::vec3 centre(0.0f, H * 0.65f, 0.0f), ext(H * 0.35f);
			if (!sites.empty())
			{
				glm::vec3 lo(1e9f), hi(-1e9f), sum(0.0f);
				for (const Site& st : sites) { lo = glm::min(lo, st.pos); hi = glm::max(hi, st.pos); sum += st.pos; }
				centre = sum / static_cast<float>(sites.size());
				ext = glm::max((hi - lo) * 0.5f + glm::vec3(leafSize * 0.8f), glm::vec3(1.0f));
				centre.y = (lo.y + hi.y) * 0.5f;
			}
			const float clusterR = leafSize * 0.6f;
			struct Cluster { glm::vec3 c0; float R; Site st; };
			std::vector<Cluster> clusters;
			float weight = 0.0f;
			for (const Site& st : sites)
			{
				const float R = clusterR * st.scale * rng.range(0.85f, 1.15f);
				const glm::vec3 c0 = st.pos + st.dir * (R * 0.35f) + up * (R * 0.1f);
				bool crowded = false;
				for (const Cluster& q : clusters) crowded = crowded || glm::distance(q.c0, c0) < R * 1.1f;
				if (crowded) continue;
				clusters.push_back({ c0, R, st });
				weight += st.scale;
			}
			// the crown's cards: a budget by habit and height (about what the crowns had before T111), shared out over the clusters
			const float cardBudget = hb.Leaves * 82.0f * (H / 7.5f) * density;
			const float perWeight = weight > 0.0f ? cardBudget / weight : 0.0f;
			for (const Cluster& cl : clusters)
			{
				const Site& st = cl.st;
				const float R = cl.R;
				const glm::vec3 c0 = cl.c0;
				const int count = std::clamp(static_cast<int>(std::lround(perWeight * st.scale)), 3, 14);
				const glm::vec3 away = c0 - centre;
				const glm::vec3 outward = glm::normalize(glm::mix(glm::length(away) > 1e-3f ? glm::normalize(away) : up, up, 0.3f));
				const float spin = rng.range(0.0f, 2.0f * kPi);
				for (int k = 0; k < count; ++k)
				{
					const float zf = 1.0f - 2.0f * (static_cast<float>(k) + 0.5f) / static_cast<float>(count);
					const float ring = std::sqrt(std::max(0.0f, 1.0f - zf * zf)), th = static_cast<float>(k) * 2.399963f + spin;
					const glm::vec3 o = glm::normalize(glm::vec3(ring * std::cos(th), zf, ring * std::sin(th)) + outward * 0.55f);
					const glm::vec3 c = c0 + o * (R * rng.range(0.3f, 0.6f));
					const glm::vec3 cardN = glm::normalize(o + uniformDir() * 0.3f);
					glm::vec3 uRef = glm::normalize(st.dir + up * 0.3f);
					uRef -= cardN * glm::dot(uRef, cardN);
					glm::vec3 uu = glm::length(uRef) > 1e-3f ? glm::normalize(uRef) : AnyPerpendicular(cardN);
					uu = RotateAround(uu, cardN, rng.range(-0.7f, 0.7f));
					const glm::vec3 rr = glm::normalize(glm::cross(uu, cardN));
					const glm::vec3 d = (c - centre) / (ext * ext);
					const glm::vec3 g = glm::length(d) > 1e-5f ? glm::normalize(d) : up;
					const glm::vec3 shadeN = glm::normalize(glm::mix(glm::normalize(g * 0.55f + o * 0.6f), up, 0.15f));
					const float sz = leafSize * rng.range(0.8f, 1.15f);
					const glm::vec3 fold = cardN * (rng.next() < 0.5f ? 1.0f : -1.0f);
					const bool flat = rng.next() < 0.35f;
					const float foldAngle = flat ? 0.0f : rng.range(0.45f, 0.85f);
					if (flat)
						leaves.LeafCard(c, rr, uu, sz * 0.5f, sz * 0.5f, 1, 2, 0.0f, look.range(0.12f, 0.32f), look.range(-0.35f, 0.35f), 0.05f, fold, ++cardSeed + p.Seed * 7919u, shadeN);
					else
						leaves.LeafCard(c, rr, uu, sz * 0.5f, sz * 0.5f, 2, 1, foldAngle, 0.0f, look.range(-0.4f, 0.4f), 0.05f, fold, ++cardSeed + p.Seed * 7919u, shadeN);
				}
			}
			crownRadius = std::max(ext.x, ext.z);
		}
		// ---------------------------------------------------------------- conifer
		else if (p.Species == TreeSpecies::Conifer)
		{
			// pine: bare lower trunk and tiers of upturned arms; spruce: a dense drooping cone; T111: cypress (only when asked for, Style 2:
			// the style derived from the seed stays spruce or pine as before), a narrow column of short arms climbing steeply, widest at about
			// a third of its height, closing to a point
			const int habit = p.Style == 2 ? 2 : (style % 2);
			const bool pine = habit == 1, cypress = habit == 2;
			const float bareFrac = pine ? rng.range(0.28f, 0.38f) : cypress ? rng.range(0.03f, 0.07f) : rng.range(0.10f, 0.18f);
			const float maxReach = H * (pine ? 0.25f : cypress ? 0.085f : 0.20f) * rng.range(0.9f, 1.1f);
			const int segs = 12;
			const float phase = rng.range(0.0f, 2.0f * kPi);
			// T111: the trunk leans a little and bends back up toward the top
			const float leanAz = rng.range(0.0f, 2.0f * kPi), lean = H * rng.range(0.008f, 0.025f);
			const glm::vec3 leanDir(std::cos(leanAz), 0.0f, std::sin(leanAz));
			auto trunkPoint = [&](float t)
			{
				return glm::vec3(H * 0.012f * std::sin(t * 5.0f + phase), t * H, H * 0.012f * std::sin(t * 4.0f + phase * 1.7f)) + leanDir * (lean * std::sin(t * kPi * 0.8f));
			};
			auto trunkRadius = [&](float t) { return trunkR * (1.0f - 0.9f * std::pow(t, 0.85f)) * (1.0f + 0.7f * std::exp(-t * H / (trunkR * 3.0f))); };
			const int trunkSides = std::max(3, 8 - sideCut);
			{
				// T110: one strip, closed at the top (the leader's sprays hide the point); T111: three more rings at the foot for its flare
				std::vector<Ring> rings;
				std::vector<float> ts = { 0.0f };
				for (const float f : { 0.25f, 0.6f, 1.2f })
					if (f * trunkR / H < 0.8f / segs) ts.push_back(f * trunkR / H);
				for (int s = 1; s <= segs; ++s)
					ts.push_back(static_cast<float>(s) / segs);
				for (size_t s = 0; s < ts.size(); ++s)
					rings.push_back({ trunkPoint(ts[s]), s + 1 == ts.size() ? 0.0f : trunkRadius(ts[s]) });
				wood.Strip(rings, std::vector<int>(rings.size(), trunkSides), true, Hash(0, 11, p.Seed), Hash(0, 12, p.Seed) * 2.0f);
			}
			addRoots([&](float y) { return Ring{ trunkPoint(y / H), trunkRadius(y / H) }; }, trunkR, 4 + static_cast<int>(rng.next() * 3.0f), cypress ? 1.5f : 1.6f, cypress ? 2.6f : 3.0f);
			int armIndex = 0;
			const glm::vec3 centre(0.0f, H * (bareFrac + 1.0f) * 0.5f, 0.0f);
			const glm::vec3 ext(maxReach, H * (1.0f - bareFrac) * 0.5f, maxReach);
			const float spacing = H * (pine ? 0.075f : cypress ? 0.05f : 0.062f);
			float y = H * bareFrac;
			while (y < H * 0.97f)
			{
				const float u = glm::clamp((y - H * bareFrac) / (H * (0.97f - bareFrac)), 0.0f, 1.0f);   // 0 at the lowest whorl, 1 at the top
				const float profile = pine ? std::pow(1.0f - u, 0.62f) * (0.7f + 0.5f * Hash(static_cast<int>(y * 13.0f), 5, p.Seed))
				                    : cypress ? (0.7f + 0.3f * std::sin(std::min(u / 0.3f, 1.0f) * 0.5f * kPi)) * std::pow(1.0f - u, 0.75f) * (0.85f + 0.3f * Hash(static_cast<int>(y * 13.0f), 5, p.Seed))
				                              : std::pow(1.0f - u, 0.92f);
				const float reachBase = maxReach * profile + (cypress ? 0.10f : 0.22f);
				const int arms = std::max(3, static_cast<int>(((pine || cypress ? 4 : 5) + static_cast<int>(rng.next() * (cypress ? 2.0f : 3.0f))) * (detail == 0 ? 0.7f : 1.0f)));
				const float ph = rng.range(0.0f, 2.0f * kPi);
				const glm::vec3 axis0 = trunkPoint(y / H);
				for (int a = 0; a < arms; ++a)
				{
					if (pine && rng.next() < 0.18f)
						continue;   // gaps between the tiers
					const float ang = ph + (static_cast<float>(a) + rng.range(-0.25f, 0.25f)) / arms * 2.0f * kPi;
					const float reach = reachBase * rng.range(0.78f, 1.12f);
					const glm::vec3 out(std::cos(ang), 0.0f, std::sin(ang));
					const float d0 = pine ? rng.range(-0.05f, 0.25f) : cypress ? rng.range(1.2f, 1.8f) : rng.range(-0.42f, -0.18f);   // initial slope
					const float d1 = pine ? rng.range(0.25f, 0.6f) : cypress ? rng.range(1.0f, 1.6f) : rng.range(0.0f, 0.35f);        // slope at the tip: tips curl up
					auto armPoint = [&](float s) { return axis0 + out * (reach * s) + glm::vec3(0.0f, reach * (d0 * s + (d1 - d0) * s * s), 0.0f); };
					auto armTangent = [&](float s) { return glm::normalize(out + glm::vec3(0.0f, d0 + 2.0f * (d1 - d0) * s, 0.0f)); };
					const float r0 = trunkR * (cypress ? 0.12f : 0.20f) * (1.0f - 0.6f * u) + (cypress ? 0.008f : 0.012f);
					const int armSegs = detail == 0 || cypress ? 2 : 3;
					const int armSides = cypress ? 3 : 4;
					{
						// T110: one strip sunk in the trunk (its first ring inside the trunk's polygon), a collar where it leaves the
						// trunk's surface, closed at its tip
						auto surface = [&](float t) { const float last = static_cast<float>(segs - 1) / segs; return t > last ? trunkRadius(last) * (1.0f - t) * segs : trunkRadius(t); };
						const float rt = std::min(surface(y / H), surface(std::min(1.0f, (y + 2.0f * r0) / H)));
						std::vector<Ring> rings = { { armPoint(0.0f), std::min(r0, 0.85f * rt * std::cos(kPi / static_cast<float>(trunkSides))) } };
						const float sinT = glm::length(glm::cross(armTangent(0.0f), up));
						const float collar = (rt / std::max(sinT, 0.2f) + 0.5f * r0) / std::max(reach, 1e-3f);   // as a fraction of the arm
						if (collar < 0.7f / static_cast<float>(armSegs))
							rings.push_back({ armPoint(collar), glm::mix(r0, 0.005f, collar) * 1.25f });
						for (int k = 1; k <= armSegs; ++k)
						{
							const float s1 = static_cast<float>(k) / static_cast<float>(armSegs);
							rings.push_back({ armPoint(s1), k == armSegs ? 0.0f : glm::mix(r0, 0.005f, s1) });
						}
						++armIndex;
						wood.Strip(rings, std::vector<int>(rings.size(), armSides), true, Hash(armIndex, 11, p.Seed), Hash(armIndex, 12, p.Seed) * 2.0f);
					}
					const float size = cypress ? 0.6f * (1.0f - 0.35f * u) + 0.3f : (pine ? 1.0f : 0.9f) * (1.0f - 0.42f * u) + 0.32f;
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
							const float foldAngle = rng.range(0.5f, 0.8f);
							// T110: a little turn of its own (look stream); a quarter of the sprays arch along their length (two rows), the
							// rest are warped; all jittered
							const glm::vec3 pn = glm::cross(rr, tang);
							const float yaw = look.range(-0.2f, 0.2f), pitch = look.range(-0.25f, 0.25f);
							const glm::vec3 r2 = RotateAround(rr, pn, yaw);
							const glm::vec3 u2 = RotateAround(RotateAround(tang, pn, yaw), r2, pitch);
							if (look.next() < 0.25f)
								leaves.LeafCard(pc, r2, u2, h * 0.46f, h * 0.55f, 2, 2, foldAngle, look.range(0.15f, 0.3f), look.range(-0.25f, 0.25f), 0.04f, fold, ++cardSeed + p.Seed * 7919u, shadeN);
							else
								leaves.LeafCard(pc, r2, u2, h * 0.46f, h * 0.55f, 2, 1, foldAngle, 0.0f, look.range(-0.3f, 0.3f), 0.04f, fold, ++cardSeed + p.Seed * 7919u, shadeN);
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
				const glm::vec3 n = glm::normalize(rr * 0.2f + up);
				leaves.LeafCard(tip + up * 0.35f * lod, rr, up, 0.22f, 0.5f, 1, 2, 0.0f, 0.12f, look.range(-0.3f, 0.3f), 0.03f, glm::cross(rr, up), ++cardSeed + p.Seed * 7919u, n);
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
			glm::vec3 frame(0.0f);   // T110: carried from ring to ring, so u lines up across the scars
			float along = 0.0f;
			const int ringSides = std::max(5, 8 - sideCut);
			for (int s = 1; s <= rings; ++s)
			{
				const float t0 = static_cast<float>(s - 1) / rings, t1 = static_cast<float>(s) / rings;
				auto radius = [&](float t) { return trunkR * (1.0f + 0.75f * std::exp(-t * 16.0f)) * (1.0f - 0.32f * t); };
				const glm::vec3 pt = trunkPoint(t1);
				// each ring flares slightly toward its upper edge: the scars of the fallen fronds (a ring of their own, on purpose)
				wood.Strip({ { prev, radius(t0) * 0.96f }, { pt, radius(t1) * 1.05f } }, { ringSides, ringSides }, false, 0.0f, along, &frame);
				along += glm::length(pt - prev);
				prev = pt;
				top = pt;
			}
			// a small bulge where the fronds spring from, closed on top
			wood.Strip({ { top - glm::vec3(0, 0.18f, 0) * H / 8.0f, trunkR * 1.25f }, { top + glm::vec3(0, 0.12f, 0) * H / 8.0f, trunkR * 0.8f }, { top + glm::vec3(0, 0.2f, 0) * H / 8.0f, 0.0f } },
			           { 6, 6, 6 }, true, 0.0f, along, &frame);
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
					const float v = t;   // T110: the stalk at v = 0 (the frond image is flipped to match)
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
			for (Vertex& v : wood.v)
				v.UV *= k;   // T110: the bark's UVs are metres: they scale with the tree
			crownRadius = reach;
			tree->BoundsHeight = std::max(top, H);
		}
		// T110: tangents from the UVs (dP/du, with the bitangent's sign in w), once the tree has its final size
		{
			MeshData m;
			m.Vertices = std::move(wood.v); m.Indices = std::move(wood.i);
			m.ComputeTangents();
			wood.v = std::move(m.Vertices); wood.i = std::move(m.Indices);
			m.Vertices = std::move(leaves.v); m.Indices = std::move(leaves.i);
			m.ComputeTangents();
			leaves.v = std::move(m.Vertices); leaves.i = std::move(m.Indices);
		}

		// ---- the meshes (TreesGpu uploads them and derives the materials from the params)
		tree->Params = p;
		tree->Height = H;
		tree->CrownRadius = crownRadius;
		tree->TriangleCount = wood.i.size() / 3;
		tree->Trunk.Vertices = std::move(wood.v);
		tree->Trunk.Indices = std::move(wood.i);
		if (!leaves.v.empty())
		{
			tree->TriangleCount += leaves.i.size() / 3;
			tree->LeafCardCount = p.Species == TreeSpecies::Palm ? leaves.i.size() / 6 : cardSeed;   // T110: the cards have 2 to 8 triangles
			tree->Foliage.Vertices = std::move(leaves.v);
			tree->Foliage.Indices = std::move(leaves.i);
		}
		return tree;
	}
}
