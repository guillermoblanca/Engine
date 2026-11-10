#include "GB/Visual/ProcTexture.h"
#include "GB/Visual/ProcMesh.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/glm.hpp>

namespace GB
{
	namespace
	{
		constexpr float kTau = 6.28318530718f;

		// Periodic value noise: the lattice wraps every `period` cells, so a texture sampled over [0, 1) with an integer frequency tiles.
		float Cell(int x, int y, int period, uint32_t seed)
		{
			x = ((x % period) + period) % period;
			y = ((y % period) + period) % period;
			return HashUnit(VisualHash(static_cast<uint32_t>(x), static_cast<uint32_t>(y) * 2654435761u, seed));
		}

		float Noise(float u, float v, int period, uint32_t seed)
		{
			const float x = u * static_cast<float>(period), y = v * static_cast<float>(period);
			const float fx = std::floor(x), fy = std::floor(y);
			const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
			float tx = x - fx, ty = y - fy;
			tx = tx * tx * (3.0f - 2.0f * tx);
			ty = ty * ty * (3.0f - 2.0f * ty);
			const float a = Cell(ix, iy, period, seed), b = Cell(ix + 1, iy, period, seed), c = Cell(ix, iy + 1, period, seed), d = Cell(ix + 1, iy + 1, period, seed);
			return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
		}

		// Fractal sum of octaves (each twice the frequency, half the weight), 0..1.
		float Fbm(float u, float v, int period, int octaves, uint32_t seed)
		{
			float sum = 0.0f, weight = 0.5f, total = 0.0f;
			for (int o = 0; o < octaves; ++o)
			{
				sum += Noise(u, v, period << o, seed + static_cast<uint32_t>(o) * 1013u) * weight;
				total += weight;
				weight *= 0.5f;
			}
			return sum / total;
		}

		// Worley-like distance to the nearest of one jittered point per cell (periodic): cobbles, flakes, cells of hide.
		float Cells(float u, float v, int period, uint32_t seed, float* second = nullptr)
		{
			const float x = u * static_cast<float>(period), y = v * static_cast<float>(period);
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float best = 9.0f, next = 9.0f;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int cx = ix + dx, cy = iy + dy;
					const float px = static_cast<float>(cx) + Cell(cx, cy, period, seed), py = static_cast<float>(cy) + Cell(cx, cy, period, seed ^ 0xA5A5A5A5u);
					const float d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
					if (d < best) { next = best; best = d; } else if (d < next) next = d;
				}
			if (second) *second = next;
			return best;
		}

		glm::vec3 Mix(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }
		float Smooth(float a, float b, float x) { const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

		using Shader = std::function<glm::vec3(float u, float v, uint32_t seed)>;

		glm::vec3 Bark(float u, float v, uint32_t s)
		{
			// vertical plates split by dark furrows that wander: noise stretched along v
			const float plates = Fbm(u, v * 0.25f, 8, 4, s);
			const float furrow = Smooth(0.42f, 0.5f, std::fabs(Noise(u + 0.13f * Fbm(u, v, 4, 2, s + 7), v * 0.25f, 12, s + 3) - 0.5f) * 2.0f);
			const glm::vec3 light(0.78f, 0.7f, 0.6f), dark(0.32f, 0.26f, 0.21f);
			return Mix(dark, light, 0.35f + 0.65f * furrow) * (0.8f + 0.4f * plates);
		}

		glm::vec3 Planks(float u, float v, uint32_t s)
		{
			// four boards across v, each with its own tint, grain lines along u and a dark seam between boards
			const float boards = 4.0f;
			const float b = std::floor(v * boards);
			const float within = v * boards - b;
			const uint32_t bs = s + static_cast<uint32_t>(b) * 97u;
			const float tint = 0.85f + 0.3f * Cell(static_cast<int>(b), 0, 4, s);
			const float grain = 0.5f + 0.5f * std::sin((within * 9.0f + Fbm(u, v, 4, 3, bs) * 6.0f) * kTau * 0.5f);
			const float seam = Smooth(0.0f, 0.06f, within) * Smooth(0.0f, 0.06f, 1.0f - within);
			const float knot = Smooth(0.08f, 0.0f, Cells(u, v, 4, bs));
			glm::vec3 c = Mix(glm::vec3(0.62f, 0.47f, 0.31f), glm::vec3(0.84f, 0.68f, 0.48f), grain * 0.6f + Fbm(u, v * 4.0f, 8, 2, bs) * 0.4f) * tint;
			c = Mix(c, glm::vec3(0.36f, 0.25f, 0.16f), knot);
			return c * (0.35f + 0.65f * seam);
		}

		glm::vec3 Stone(float u, float v, uint32_t s)
		{
			const float base = Fbm(u, v, 4, 5, s);
			const float crack = Smooth(0.035f, 0.0f, std::fabs(Fbm(u, v, 3, 3, s + 11) - 0.5f));
			const float speck = Cell(static_cast<int>(u * 128.0f), static_cast<int>(v * 128.0f), 128, s + 5) > 0.93f ? 1.0f : 0.0f;
			glm::vec3 c = Mix(glm::vec3(0.5f, 0.5f, 0.49f), glm::vec3(0.82f, 0.81f, 0.78f), base);
			c = Mix(c, glm::vec3(0.25f, 0.25f, 0.25f), crack * 0.8f);
			return Mix(c, glm::vec3(0.95f), speck * 0.4f);
		}

		glm::vec3 Flint(float u, float v, uint32_t s)
		{
			// dark glassy nodules with the rings of conchoidal fracture and a pale cortex between them
			float second = 0.0f;
			const float d = Cells(u, v, 5, s, &second);
			const float rings = 0.5f + 0.5f * std::sin(d * 38.0f);
			const float cortex = Smooth(0.08f, 0.0f, second - d);
			glm::vec3 c = Mix(glm::vec3(0.16f, 0.17f, 0.2f), glm::vec3(0.36f, 0.37f, 0.42f), rings * 0.6f + Fbm(u, v, 8, 2, s) * 0.4f);
			return Mix(c, glm::vec3(0.78f, 0.74f, 0.66f), cortex);
		}

		glm::vec3 Copper(float u, float v, uint32_t s)
		{
			// a grey rock with green streaks of oxidised ore along a ridged noise, and a few rusty-orange spots
			glm::vec3 c = Stone(u, v, s) * 0.92f;
			const float ridge = 1.0f - std::fabs(Fbm(u * 1.0f, v, 3, 4, s + 23) * 2.0f - 1.0f);
			const float vein = Smooth(0.82f, 0.92f, ridge);
			c = Mix(c, Mix(glm::vec3(0.18f, 0.55f, 0.42f), glm::vec3(0.35f, 0.75f, 0.55f), Fbm(u, v, 16, 2, s)), vein);
			const float rust = Smooth(0.9f, 0.97f, Fbm(u, v, 6, 2, s + 41));
			return Mix(c, glm::vec3(0.72f, 0.42f, 0.2f), rust * 0.8f);
		}

		glm::vec3 Thatch(float u, float v, uint32_t s)
		{
			// rows of straw: thin vertical strands with their own shade, each row a little darker at its lower edge
			const float rows = 6.0f;
			const float r = std::floor(v * rows);
			const float within = v * rows - r;
			const float strand = Cell(static_cast<int>(u * 96.0f + r * 31.0f), static_cast<int>(r), 96, s);
			glm::vec3 c = Mix(glm::vec3(0.6f, 0.48f, 0.26f), glm::vec3(0.9f, 0.78f, 0.48f), strand);
			c *= 0.65f + 0.35f * Smooth(0.0f, 0.5f, within);
			return c * (0.85f + 0.3f * Fbm(u, v, 8, 2, s + 9));
		}

		glm::vec3 Soil(float u, float v, uint32_t s)
		{
			// tilled earth: furrows across v (dark troughs, lighter ridges) and clods
			const float furrows = 0.5f + 0.5f * std::sin(v * 8.0f * kTau);
			const float clods = Smooth(0.2f, 0.0f, Cells(u, v, 16, s));
			glm::vec3 c = Mix(glm::vec3(0.26f, 0.18f, 0.12f), glm::vec3(0.52f, 0.38f, 0.26f), furrows * 0.7f + Fbm(u, v, 8, 4, s) * 0.3f);
			return Mix(c, glm::vec3(0.6f, 0.46f, 0.32f), clods * 0.5f);
		}

		glm::vec3 Hide(float u, float v, uint32_t s)
		{
			const float mottle = Fbm(u, v, 4, 5, s);
			float second = 0.0f;
			const float d = Cells(u, v, 10, s + 3, &second);
			const float creases = Smooth(0.06f, 0.0f, second - d);
			glm::vec3 c = Mix(glm::vec3(0.55f, 0.4f, 0.27f), glm::vec3(0.8f, 0.66f, 0.48f), mottle);
			return c * (1.0f - 0.25f * creases);
		}

		glm::vec3 Leaves(float u, float v, uint32_t s)
		{
			// overlapping leaves: cells with a lighter middle and a dark rim, varied in shade (multiplied by the season's colour)
			float second = 0.0f;
			const float d = Cells(u, v, 12, s, &second);
			const float shade = Cell(static_cast<int>(u * 12.0f), static_cast<int>(v * 12.0f), 12, s + 1);
			const float edge = Smooth(0.0f, 0.15f, second - d);
			return glm::vec3(0.75f + 0.25f * shade) * (0.55f + 0.45f * edge) * (0.85f + 0.3f * Fbm(u, v, 4, 2, s + 2));
		}

		glm::vec3 Clay(float u, float v, uint32_t s)
		{
			const float base = Fbm(u, v, 4, 4, s);
			const float cracks = Smooth(0.04f, 0.0f, std::fabs(Fbm(u, v, 6, 2, s + 4) - 0.5f));   // the dried surface cracks along a noise contour
			glm::vec3 c = Mix(glm::vec3(0.62f, 0.4f, 0.27f), glm::vec3(0.82f, 0.58f, 0.42f), base);
			return Mix(c, glm::vec3(0.4f, 0.26f, 0.18f), cracks);
		}

		glm::vec3 Rope(float u, float v, uint32_t s)
		{
			// twisted strands: diagonal stripes
			const float twist = 0.5f + 0.5f * std::sin((u * 6.0f + v * 12.0f) * kTau);
			return Mix(glm::vec3(0.55f, 0.46f, 0.3f), glm::vec3(0.82f, 0.72f, 0.5f), twist) * (0.9f + 0.2f * Fbm(u, v, 16, 2, s));
		}

		glm::vec3 Wicker(float u, float v, uint32_t s)
		{
			// a basket weave: horizontal strips over and under vertical stakes
			const float rows = 10.0f, cols = 8.0f;
			const float r = std::floor(v * rows), c = std::floor(u * cols);
			const bool over = (static_cast<int>(r) + static_cast<int>(c)) % 2 == 0;
			const float wv = v * rows - r, wu = u * cols - c;
			const float bulge = over ? std::sin(wu * 3.14159f) : std::sin(wv * 3.14159f);
			glm::vec3 col = Mix(glm::vec3(0.5f, 0.36f, 0.2f), glm::vec3(0.85f, 0.7f, 0.45f), 0.4f + 0.6f * bulge);
			return col * (0.85f + 0.3f * Fbm(u, v, 8, 2, s));
		}

		glm::vec3 Grass(float u, float v, uint32_t s)
		{
			const float blades = Cell(static_cast<int>(u * 64.0f), static_cast<int>(v * 16.0f), 64, s);
			return glm::vec3(0.75f + 0.25f * blades) * (0.8f + 0.4f * Fbm(u, v, 8, 3, s + 1));
		}

		glm::vec3 Fur(float u, float v, uint32_t s)
		{
			// short hairs along v
			const float hair = Fbm(u * 1.0f, v * 0.2f, 32, 2, s);
			return glm::vec3(0.8f + 0.2f * hair) * (0.85f + 0.3f * Fbm(u, v, 4, 3, s + 1));
		}

		const std::vector<std::pair<std::string, Shader>>& Table()
		{
			static const std::vector<std::pair<std::string, Shader>> table = {
				{ "bark", Bark }, { "planks", Planks }, { "stone", Stone }, { "flint", Flint }, { "copper_ore", Copper }, { "thatch", Thatch },
				{ "tilled_soil", Soil }, { "hide", Hide }, { "leaves", Leaves }, { "clay", Clay }, { "rope", Rope }, { "wicker", Wicker },
				{ "grass", Grass }, { "fur", Fur } };
			return table;
		}

		// ---- T91: kinds written as a surface (albedo, relief, roughness), so they also give a normal and a roughness map. The albedo follows the
		// art bible: physical ranges, the painted light that does not move (occlusion in joints and cracks, R1) but no directional shadow nor
		// highlight. Every period is a whole number of cells over the tile (rows of bricks, ripples, strata), so they tile like the rest.
		struct Surface
		{
			glm::vec3 Albedo{ 0.5f };
			float Height = 0.5f;   // 0..1, scaled by the kind's depth for the normal map
			float Rough = 1.0f;    // 0..1, the factor on the material's roughness
			float Alpha = 1.0f;    // T103 cards: 1 inside the silhouette, 0 outside (the surfaces that tile leave it at 1)
		};
		using SurfaceShader = Surface (*)(float u, float v, uint32_t seed);

		int Wrap(int a, int period) { return ((a % period) + period) % period; }
		float Hash01(int a, int b, uint32_t seed) { return HashUnit(VisualHash(static_cast<uint32_t>(a), static_cast<uint32_t>(b) * 2654435761u, seed)); }

		// Periodic value noise with its own period along u and along v (fibres, streaks): `pu` cells across, `pv` along.
		float NoiseXY(float u, float v, int pu, int pv, uint32_t seed)
		{
			const float x = u * static_cast<float>(pu), y = v * static_cast<float>(pv);
			const float fx = std::floor(x), fy = std::floor(y);
			const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
			float tx = x - fx, ty = y - fy;
			tx = tx * tx * (3.0f - 2.0f * tx);
			ty = ty * ty * (3.0f - 2.0f * ty);
			const auto at = [&](int cx, int cy) { return Hash01(Wrap(cx, pu), Wrap(cy, pv), seed); };
			const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
			return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
		}

		Surface Sand(float u, float v, uint32_t s)
		{
			// wind ripples across v (seven a tile, about 14 cm at UVScale 1) that wander and fade, over a fine grain of light and dark sand
			const float warp = Fbm(u, v, 3, 3, s + 1) - 0.5f;
			const float phase = 7.0f * v + 1.3f * warp + 0.35f * (Noise(u, v, 2, s + 2) - 0.5f);
			const float w = phase * kTau;
			const float ripple = 0.5f + 0.5f * std::sin(w + 0.7f * std::sin(w));   // a steep lee side and a long gentle stoss side
			const float strength = 0.35f + 0.65f * Fbm(u, v, 2, 2, s + 3);
			const float grain = Noise(u, v, 128, s + 5) * 0.6f + Noise(u, v, 64, s + 6) * 0.4f;
			const float speck = Cell(static_cast<int>(u * 256.0f), static_cast<int>(v * 256.0f), 256, s + 9);
			Surface o;
			o.Albedo = Mix(glm::vec3(0.72f, 0.64f, 0.50f), glm::vec3(0.86f, 0.79f, 0.64f), 0.6f * Fbm(u, v, 4, 4, s + 7) + 0.4f * grain);
			o.Albedo = Mix(o.Albedo, glm::vec3(0.44f, 0.38f, 0.32f), speck > 0.9f ? 0.55f : 0.0f);      // dark mineral grains
			o.Albedo = Mix(o.Albedo, glm::vec3(0.95f, 0.93f, 0.88f), speck > 0.985f ? 0.6f : 0.0f);     // bright quartz
			o.Albedo *= 0.9f + 0.1f * (1.0f + (ripple - 1.0f) * strength);          // the troughs keep the heavy dark grains
			o.Height = 0.62f * ripple * strength + 0.38f * grain;
			o.Rough = 0.88f + 0.12f * grain;
			return o;
		}

		// Bricks or cut stone laid in courses: `rows` courses (even, so the half-offset bond tiles) of `cols` blocks a tile, joints of mortar
		// `mortar` wide (tile units, half on each side), bevelled over `bevel`; `wear` chips the arrises, pits the faces and stains the wall.
		struct Masonry
		{
			int Rows, Cols;
			float Mortar, Bevel, Wear;
			bool Stone;
		};

		Surface Lay(float u, float v, uint32_t s, const Masonry& p)
		{
			const float fr = v * static_cast<float>(p.Rows);
			const int row = static_cast<int>(std::floor(fr));
			const float fv = fr - static_cast<float>(row);
			const float fc = u * static_cast<float>(p.Cols) + ((Wrap(row, p.Rows) & 1) ? 0.5f : 0.0f);
			const int col = static_cast<int>(std::floor(fc));
			const float fu = fc - static_cast<float>(col);
			const int bc = Wrap(col, p.Cols), br = Wrap(row, p.Rows);
			const float tone = Hash01(bc, br, s + 31), odd = Hash01(bc, br, s + 37), tilt = Hash01(bc, br, s + 41);
			// distance to the block's edge in tile units, the corners rounded, the arris broken by noise where it is worn
			const float ex = std::min(fu, 1.0f - fu) / static_cast<float>(p.Cols), ey = std::min(fv, 1.0f - fv) / static_cast<float>(p.Rows);
			const float round = p.Mortar + p.Bevel;
			float edge = (ex < round && ey < round) ? round - std::sqrt((round - ex) * (round - ex) + (round - ey) * (round - ey)) : std::min(ex, ey);
			const float chip = Fbm(u, v, 24, 3, s + 43);
			edge -= p.Wear * 0.012f * Smooth(0.45f, 0.75f, chip);
			const float face = Smooth(p.Mortar, p.Mortar + p.Bevel, edge);           // 0 in the joint, 1 on the face
			const float mortarMask = 1.0f - Smooth(p.Mortar - 0.0015f, p.Mortar + 0.0015f, edge);
			const float mottle = Fbm(u, v, 8, 4, s + 51);
			float second = 0.0f;
			const float pit = p.Wear * Smooth(0.16f, 0.06f, Cells(u, v, 40, s + 61, &second)) * Smooth(0.5f, 0.7f, Noise(u, v, 6, s + 63));
			Surface o;
			glm::vec3 block;
			if (p.Stone)
			{
				// cut stone: pale limestone blocks, each its own tint, with the fine diagonal strokes of the dressing chisel
				block = Mix(glm::vec3(0.62f, 0.60f, 0.56f), glm::vec3(0.80f, 0.77f, 0.71f), tone) * (0.9f + 0.2f * mottle);
				const float strokes = 0.5f + 0.5f * std::sin((24.0f * (u + v) + 0.6f * Fbm(u, v, 4, 2, s + 71)) * kTau);
				block *= 0.97f + 0.03f * strokes;
				o.Height = 0.8f + 0.03f * strokes + 0.08f * (mottle - 0.5f);
			}
			else
			{
				// fired clay: red to orange by the brick, a few over-burnt dark ones, a mottled face
				block = Mix(glm::vec3(0.60f, 0.31f, 0.21f), glm::vec3(0.80f, 0.50f, 0.35f), tone);
				block = Mix(block, glm::vec3(0.38f, 0.23f, 0.19f), odd > 0.84f ? 0.65f : 0.0f);
				block *= 0.86f + 0.28f * mottle;
				o.Height = 0.82f + 0.1f * (mottle - 0.5f) + 0.05f * (tilt - 0.5f);
			}
			if (p.Wear > 0.0f)
			{
				// weathering: the faces fade and take white salt bloom in patches, the pits are dark
				const float bloom = Smooth(0.6f, 0.78f, Fbm(u, v, 3, 4, s + 81)) * p.Wear;
				block = Mix(block, block * 0.75f + glm::vec3(0.2f), 0.35f * p.Wear);
				block = Mix(block, glm::vec3(0.84f, 0.82f, 0.78f), bloom * 0.55f);
				block *= 1.0f - 0.35f * pit;
				o.Height -= 0.3f * pit;
			}
			block *= 0.8f + 0.2f * Smooth(p.Mortar, p.Mortar + 2.0f * p.Bevel, edge);   // painted occlusion along the joints (R1)
			const float m = Noise(u, v, 64, s + 91);
			const glm::vec3 mortar = Mix(glm::vec3(0.70f, 0.67f, 0.62f), glm::vec3(0.52f, 0.50f, 0.46f), p.Wear * 0.6f) * (0.78f + 0.2f * m);
			o.Albedo = Mix(block, mortar, mortarMask);
			const float mortarHeight = 0.18f + 0.08f * m - 0.1f * p.Wear;
			o.Height = mortarHeight + (o.Height - mortarHeight) * face;
			o.Rough = Mix(glm::vec3(0.82f + 0.12f * mottle + 0.06f * p.Wear), glm::vec3(1.0f), mortarMask).x;
			return o;
		}

		Surface Brick(float u, float v, uint32_t s) { return Lay(u, v, s, { 12, 4, 0.006f, 0.005f, 0.0f, false }); }
		Surface BrickWorn(float u, float v, uint32_t s) { return Lay(u, v, s, { 12, 4, 0.007f, 0.012f, 1.0f, false }); }
		Surface Ashlar(float u, float v, uint32_t s) { return Lay(u, v, s, { 4, 2, 0.004f, 0.008f, 0.25f, true }); }

		Surface Rock(float u, float v, uint32_t s)
		{
			// a large layered rock: six strata a tile that wander, each its own tone and hardness (the soft ones weathered back), split by
			// joints that run across them, mottled and speckled
			const float sv = 6.0f * v + 1.2f * (Fbm(u, v, 2, 3, s + 1) - 0.5f) + 0.3f * (Fbm(u, v, 8, 2, s + 2) - 0.5f);
			const int band = static_cast<int>(std::floor(sv));
			const float fb = sv - static_cast<float>(band);
			const int bw = Wrap(band, 6);
			const float tone = Hash01(bw, 13, s + 5), warm = Hash01(bw, 17, s + 5), hard = Hash01(bw, 19, s + 5);
			const float groove = Smooth(0.0f, 0.1f, fb) * Smooth(0.0f, 0.1f, 1.0f - fb);
			float second = 0.0f;
			const float d = Cells(u * 2.0f, v, 3, s + 7, &second);
			const float joint = Smooth(0.07f, 0.0f, second - d) * Smooth(0.35f, 0.6f, Noise(u, v, 4, s + 9));
			const float fine = Fbm(u, v, 8, 5, s + 11);
			const float speck = Cell(static_cast<int>(u * 128.0f), static_cast<int>(v * 128.0f), 128, s + 13);
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.50f, 0.49f, 0.47f), glm::vec3(0.74f, 0.71f, 0.66f), tone);
			c = Mix(c, glm::vec3(0.70f, 0.60f, 0.49f), warm * 0.4f);
			c *= 0.84f + 0.32f * fine;
			c *= 0.72f + 0.28f * groove;
			c = Mix(c, glm::vec3(0.24f, 0.23f, 0.22f), joint * 0.55f);
			o.Albedo = Mix(c, glm::vec3(0.86f), speck > 0.975f ? 0.15f : 0.0f);
			o.Height = (0.35f + 0.25f * hard + 0.4f * fine) * (0.45f + 0.55f * groove) - 0.35f * joint;
			o.Rough = Mix(glm::vec3(0.82f + 0.15f * fine), glm::vec3(1.0f), 1.0f - groove).x;
			return o;
		}

		Surface Wood(float u, float v, uint32_t s)
		{
			// worked timber, the grain along v (a post or a beam standing): growth rings as fine dark lines that bend round the knots, fibre
			// streaks, a few knots and the long checks of drying
			float second = 0.0f;
			const float kd = Cells(u, v, 3, s + 3, &second);
			const float knotOn = Smooth(0.5f, 0.62f, Noise(u, v, 3, s + 4));
			const float knotCore = Smooth(0.09f, 0.03f, kd) * knotOn;
			const float g = 14.0f * u + 1.4f * (NoiseXY(u, v, 3, 1, s + 2) - 0.5f) + 0.9f * (Fbm(u, v, 3, 3, s + 1) - 0.5f) + 1.6f * Smooth(0.32f, 0.0f, kd) * knotOn;
			const float lines = 0.5f + 0.5f * std::sin(g * kTau);
			const float late = lines * lines * lines;
			const float fibre = NoiseXY(u, v, 128, 6, s + 5) * 0.6f + NoiseXY(u, v, 64, 3, s + 6) * 0.4f;
			const float check = Smooth(0.022f, 0.0f, std::fabs(NoiseXY(u, v, 20, 2, s + 9) - 0.5f)) * Smooth(0.55f, 0.7f, NoiseXY(u, v, 3, 3, s + 10));
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.80f, 0.66f, 0.48f), glm::vec3(0.52f, 0.38f, 0.25f), 0.55f * late + 0.35f * fibre);
			c *= 0.9f + 0.2f * Fbm(u, v, 4, 3, s + 7);
			c = Mix(c, glm::vec3(0.30f, 0.21f, 0.14f), knotCore);
			o.Albedo = Mix(c, glm::vec3(0.22f, 0.16f, 0.11f), check * 0.85f);
			o.Height = 0.5f + 0.28f * late + 0.18f * fibre - 0.6f * check + 0.1f * knotCore;
			o.Rough = 0.78f + 0.22f * fibre;
			return o;
		}

		Surface IceSurface(float u, float v, uint32_t s, float frost)
		{
			// clear ice, deeper and bluer where it is thick, crazed by fine white fractures, with strings of trapped bubbles; frost grows over
			// it in feathery patches (all of it on ice_frost). Very little saturation: it is white with a cold cast, not blue.
			const float depth = Fbm(u, v, 2, 4, s + 1);
			glm::vec3 c = Mix(glm::vec3(0.56f, 0.66f, 0.74f), glm::vec3(0.80f, 0.86f, 0.90f), depth);
			float second = 0.0f;
			const float d = Cells(u, v, 4, s + 3, &second);
			const float crackA = Smooth(0.02f, 0.0f, second - d) * Smooth(0.35f, 0.55f, Noise(u, v, 6, s + 4));
			const float crackB = Smooth(0.012f, 0.0f, std::fabs(Fbm(u, v, 3, 3, s + 5) - 0.5f));
			const float crack = std::max(crackA, crackB * 0.8f);
			c = Mix(c, glm::vec3(0.95f, 0.97f, 0.98f), crack * 0.8f);
			float bs = 0.0f;
			const float bd = Cells(u, v, 20, s + 7, &bs);
			const float radius = 0.06f + 0.12f * Noise(u, v, 16, s + 10);   // few bubbles, of every size, in loose clouds
			const float bubble = Smooth(radius, radius - 0.05f, bd) * Smooth(0.62f, 0.8f, Noise(u, v, 7, s + 8));
			c = Mix(c, glm::vec3(0.97f, 0.98f, 0.99f), bubble * 0.7f);
			const float cover = Fbm(u, v, 4, 5, s + 9);
			const float frostMask = Smooth(0.72f - 0.42f * frost, 0.84f - 0.32f * frost, cover);
			float fs = 0.0f;
			const float fd = Cells(u, v, 48, s + 11, &fs);
			const float crystals = Noise(u, v, 128, s + 12) * 0.6f + Smooth(0.0f, 0.2f, fs - fd) * 0.4f;
			const glm::vec3 rime = glm::vec3(0.88f, 0.91f, 0.93f) * (0.9f + 0.1f * crystals);
			Surface o;
			o.Albedo = Mix(c, rime, frostMask);
			o.Height = 0.45f + 0.05f * depth - 0.25f * crack + frostMask * (0.2f + 0.3f * crystals);
			o.Rough = Mix(glm::vec3(0.22f + 0.35f * crack + 0.2f * bubble), glm::vec3(0.85f + 0.15f * crystals), frostMask).x;
			return o;
		}

		Surface Ice(float u, float v, uint32_t s) { return IceSurface(u, v, s, 0.0f); }
		Surface IceFrost(float u, float v, uint32_t s) { return IceSurface(u, v, s, 1.0f); }

		Surface GlassFrosted(float u, float v, uint32_t s)
		{
			// sand-blasted glass: almost white, a fine even pitting that scatters the highlight
			const float n = Noise(u, v, 128, s + 1) * 0.6f + Noise(u, v, 64, s + 2) * 0.4f;
			Surface o;
			o.Albedo = glm::vec3(0.94f + 0.03f * n);
			o.Height = n;
			o.Rough = 0.82f + 0.18f * n;
			return o;
		}

		// ---- T102: faceted ice and wet glass. A Voronoi that also says which cell won (wrapped id, for its hash), where its point is (unwrapped,
		// so a plane through it is continuous inside the cell) and the true distance to the cell's border (bisector, two passes), so cracks
		// along the borders keep an even width.
		struct Voronoi
		{
			glm::vec2 X{ 0.0f };            // the sample, in cell units
			glm::vec2 P1{ 0.0f }, P2{ 0.0f };   // the nearest point and the one across the nearest border (cell units, unwrapped)
			int Id1 = 0, Id2 = 0;          // their wrapped cell ids
			float D1 = 0.0f;               // distance to P1 (cell units)
			float Edge = 0.0f;             // distance to the nearest border (cell units)
		};

		glm::vec2 VoronoiPoint(int cx, int cy, int period, uint32_t seed, float jitter)
		{
			const int wx = Wrap(cx, period), wy = Wrap(cy, period);
			return { static_cast<float>(cx) + 0.5f + jitter * (Hash01(wx, wy, seed) - 0.5f), static_cast<float>(cy) + 0.5f + jitter * (Hash01(wx, wy, seed ^ 0x5BD1E995u) - 0.5f) };
		}

		// `x`, `y` in cell units (u * period plus any periodic warp); the pattern repeats every `period` cells.
		Voronoi VoronoiAt(float x, float y, int period, uint32_t seed, float jitter)
		{
			Voronoi r;
			r.X = { x, y };
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float best = 1e9f;
			int bx = ix, by = iy;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const glm::vec2 p = VoronoiPoint(ix + dx, iy + dy, period, seed, jitter);
					const float d = glm::dot(p - r.X, p - r.X);
					if (d < best) { best = d; r.P1 = p; bx = ix + dx; by = iy + dy; }
				}
			r.D1 = std::sqrt(best);
			r.Id1 = Wrap(bx, period) + Wrap(by, period) * period;
			r.Edge = 1e9f;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					if (dx == 0 && dy == 0) continue;
					const glm::vec2 p = VoronoiPoint(bx + dx, by + dy, period, seed, jitter);
					const glm::vec2 n = glm::normalize(p - r.P1);
					const float e = glm::dot(0.5f * (r.P1 + p) - r.X, n);
					if (e < r.Edge) { r.Edge = e; r.P2 = p; r.Id2 = Wrap(bx + dx, period) + Wrap(by + dy, period) * period; }
				}
			r.Edge = std::max(r.Edge, 0.0f);
			return r;
		}

		float Lerp(float a, float b, float t) { return a + (b - a) * std::clamp(t, 0.0f, 1.0f); }

		// A tilted plane per cell (not a dome): its own height and slope (up to `tilt` per cell unit) through the cell's point.
		float FacetPlane(const glm::vec2& p, int id, const glm::vec2& x, uint32_t seed, float tilt)
		{
			const float a = Hash01(id, 1, seed) * kTau, m = tilt * (0.35f + 0.65f * Hash01(id, 2, seed));
			return 0.5f + 0.3f * (Hash01(id, 3, seed) - 0.5f) + m * (std::cos(a) * (x.x - p.x) + std::sin(a) * (x.y - p.y));
		}

		// The faceted relief of a Voronoi: every cell its plane, and a narrow crease `bevel` wide (cell units) where two planes meet, so the
		// height is continuous and the arris is sharp (no one-texel step for the normal map to turn into a spike).
		float Facets(const Voronoi& c, uint32_t seed, float tilt, float bevel)
		{
			const float h1 = FacetPlane(c.P1, c.Id1, c.X, seed, tilt), h2 = FacetPlane(c.P2, c.Id2, c.X, seed, tilt);
			return Lerp(0.5f * (h1 + h2), h1, Smooth(0.0f, bevel, c.Edge));
		}

		Surface IceFaceted(float u, float v, uint32_t s)
		{
			// chiselled ice: big facets (six a tile, ~17 cm at UVScale 1), each a tilted plane with a sharp arris whose line is chipped by a
			// warp; smaller facets in patches over them and conchoidal chips near the arrises. Fine white cracks run along some arrises and
			// across the faces, small dark inclusions, a little rime on the arrises. The albedo is almost white: the cyan is the absorption's.
			const float wu = 0.035f * (Fbm(u, v, 6, 3, s + 1) - 0.5f), wv = 0.035f * (Fbm(u, v, 6, 3, s + 2) - 0.5f);
			constexpr int kBig = 6, kSmall = 15;
			const Voronoi a = VoronoiAt((u + wu) * kBig, (v + wv) * kBig, kBig, s + 3, 0.85f);
			const Voronoi b = VoronoiAt((u + 0.6f * wu) * kSmall, (v + 0.6f * wv) * kSmall, kSmall, s + 5, 0.9f);
			const float big = Facets(a, s + 4, 0.9f, 0.05f);
			const float small = Facets(b, s + 6, 0.8f, 0.06f) - 0.5f;
			const float smallOn = Smooth(0.35f, 0.6f, Noise(u, v, 4, s + 7));
			const float nearArris = Smooth(0.3f, 0.04f, a.Edge);
			const float chipR = 0.25f + 0.12f * Hash01(b.Id1, 4, s + 8);
			const float chip = Hash01(b.Id1, 5, s + 8) > 0.97f - 0.12f * nearArris ? std::max(0.0f, 1.0f - (b.D1 * b.D1) / (chipR * chipR)) : 0.0f;
			// cracks: half the arrises, and two wandering lines across the faces
			const float edgeCrack = Smooth(0.03f, 0.0f, a.Edge) * Smooth(0.42f, 0.55f, Noise(u, v, 5, s + 10));
			const float crossA = Smooth(0.008f, 0.0f, std::fabs(Fbm(u + 0.5f * wu, v + 0.5f * wv, 3, 4, s + 11) - 0.5f));
			const float crossB = Smooth(0.006f, 0.0f, std::fabs(Fbm(u, v, 5, 3, s + 12) - 0.5f)) * Smooth(0.5f, 0.65f, Noise(u, v, 3, s + 13));
			const float crack = std::max({ edgeCrack, crossA * 0.9f, crossB * 0.7f });
			// inclusions: a few small dark flecks
			const Voronoi i = VoronoiAt(u * 40.0f, v * 40.0f, 40, s + 17, 0.8f);
			const float ir = 0.1f + 0.14f * Hash01(i.Id1, 6, s + 18);
			const float inclusion = Hash01(i.Id1, 7, s + 18) > 0.84f ? Smooth(ir, ir * 0.5f, i.D1) : 0.0f;
			// rime on the arrises, in patches
			const float crystals = Noise(u, v, 128, s + 15);
			const float frost = Smooth(0.14f, 0.02f, a.Edge) * Smooth(0.45f, 0.7f, Fbm(u, v, 4, 3, s + 14)) * (0.5f + 0.5f * crystals) * 0.8f;
			const float tone = Fbm(u, v, 2, 3, s + 16);
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.86f, 0.93f, 0.96f), glm::vec3(0.95f, 0.98f, 0.99f), tone) * (0.97f + 0.06f * Hash01(a.Id1, 8, s + 19));
			c = Mix(c, glm::vec3(0.97f, 0.98f, 0.99f), crack * 0.85f);
			c = Mix(c, glm::vec3(0.28f, 0.36f, 0.42f), inclusion * 0.75f);
			o.Albedo = Mix(c, glm::vec3(0.93f, 0.95f, 0.97f), frost);
			o.Height = 0.66f * big + 0.2f * smallOn * small - 0.22f * chip - 0.1f * crack + 0.06f * frost * crystals;
			float rough = 0.07f + 0.08f * Noise(u, v, 8, s + 20);   // the faces: x the material's 0.85 gives 0.06..0.13
			rough = std::max(rough, 0.3f * chip);
			rough = std::max(rough, crack);
			o.Rough = Lerp(rough, 0.9f + 0.1f * crystals, frost);
			return o;
		}

		// The inside of clear ice (GenerateInterior): rgb what is seen in the ice, a its density (straight alpha). Fracture planes on a coarser
		// and a finer Voronoi than any surface facet (3 and 8 a tile against 6 and 15), clouds of small bubbles and milky veils.
		glm::vec4 IceInterior(float u, float v, uint32_t s)
		{
			const float wu = 0.05f * (Fbm(u, v, 3, 3, s + 1) - 0.5f), wv = 0.05f * (Fbm(u, v, 3, 3, s + 2) - 0.5f);
			const Voronoi a = VoronoiAt((u + wu) * 3.0f, (v + wv) * 3.0f, 3, s + 3, 0.9f);
			const float aOn = Smooth(0.4f, 0.6f, Noise(u, v, 4, s + 4));
			const float planeA = (0.7f * Smooth(0.04f, 0.0f, a.Edge) + 0.3f * Smooth(0.22f, 0.0f, a.Edge)) * aOn;   // a bright core and a sheet seen aslant
			const Voronoi b = VoronoiAt((u - wv) * 8.0f, (v + wu) * 8.0f, 8, s + 5, 0.9f);
			const float planeB = 0.5f * Smooth(0.04f, 0.0f, b.Edge) * Smooth(0.55f, 0.7f, Noise(u, v, 6, s + 6));
			const Voronoi c = VoronoiAt(u * 56.0f, v * 56.0f, 56, s + 7, 0.8f);
			const float r = 0.12f + 0.2f * Hash01(c.Id1, 1, s + 8);
			const float cloud = Smooth(0.55f, 0.75f, Fbm(u, v, 4, 3, s + 9));
			const float bubble = Hash01(c.Id1, 2, s + 8) > 0.45f ? 0.8f * Smooth(r, r * 0.6f, c.D1) * cloud : 0.0f;
			const float veil = 0.3f * Smooth(0.5f, 0.85f, Fbm(u, v, 2, 4, s + 10));
			const glm::vec3 crackColour(0.93f, 0.96f, 0.98f), bubbleColour(1.0f), veilColour(0.88f, 0.93f, 0.96f);
			const float sum = planeA + planeB + bubble + veil;
			const glm::vec3 rgb = sum > 1e-5f ? (crackColour * (planeA + planeB) + bubbleColour * bubble + veilColour * veil) / sum : glm::vec3(1.0f);
			const float alpha = 1.0f - (1.0f - std::min(planeA, 1.0f)) * (1.0f - planeB) * (1.0f - bubble) * (1.0f - veil);
			return { rgb, alpha };
		}

		// Wet glass. Drops on stratified jittered grids (an approximation of a Poisson disc per size class): four classes, each twice as
		// many and half as big as the last (few big drops, many small); a drop is a spherical lens cap, a little taller than wide and
		// sagging at the bottom, and its height scales with its radius (the same contact angle for all). Gravity is -v (ProcMesh's v grows
		// up the side of a box or a ball): rivulets run down from their head drop towards lower v, leaving a thin beaded thread and a band
		// swept clean of fog above it. Bigger drops and the swept bands clear the smaller drops around them.
		constexpr float kDropBig = 0.36f / 8.0f;   // the biggest radius (tile units): Height 1

		struct WetSample
		{
			float Height = 0.0f, Drop = 0.0f, Clear = 0.0f;
		};

		void DropLayer(float u, float v, int period, float presence, float rMin, float rMax, float halo, uint32_t seed, WetSample& w)
		{
			const float x = u * static_cast<float>(period), y = v * static_cast<float>(period);
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float visible = 1.0f - w.Clear;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const int cx = Wrap(ix + dx, period), cy = Wrap(iy + dy, period);
					if (Hash01(cx, cy, seed) >= presence) continue;
					const float px = static_cast<float>(ix + dx) + 0.25f + 0.5f * Hash01(cx, cy, seed + 1);
					const float py = static_cast<float>(iy + dy) + 0.25f + 0.5f * Hash01(cx, cy, seed + 2);
					const float sz = Hash01(cx, cy, seed + 3);
					const float r = rMin + (rMax - rMin) * sz * sz;
					const float stretch = 1.0f + 0.5f * std::pow(Hash01(cx, cy, seed + 4), 3.0f);   // some run long
					float ly = (y - py) / stretch;
					if (ly < 0.0f) ly *= 0.85f;   // the lower half (toward -v) bulges
					const float q = std::sqrt((x - px) * (x - px) + ly * ly) / r;
					const float rTile = r / static_cast<float>(period);
					if (q < 1.0f) w.Height = std::max(w.Height, visible * rTile / kDropBig * std::sqrt(1.0f - q * q));
					w.Drop = std::max(w.Drop, visible * Smooth(1.0f, 0.85f, q));
					if (halo > 1.0f) w.Clear = std::max(w.Clear, Smooth(halo, 1.0f, q));   // the dry ring a drop leaves by gathering the fog around it
				}
		}

		Surface GlassWet(float u, float v, uint32_t s)
		{
			WetSample w;
			// rivulets: sixteen columns a tile, six in ten carry one, its head anywhere in v; the thread climbs L above the head (+v)
			constexpr int kCols = 16;
			const int col = static_cast<int>(std::floor(u * kCols));
			for (int dc = -1; dc <= 1; ++dc)
			{
				const int id = Wrap(col + dc, kCols);
				if (Hash01(id, 0, s + 30) > 0.6f) continue;
				const float x0 = (static_cast<float>(col + dc) + 0.2f + 0.6f * Hash01(id, 1, s + 30)) / kCols;
				const float head = Hash01(id, 2, s + 30), len = 0.15f + 0.45f * Hash01(id, 3, s + 30);
				const float rh = 0.012f + 0.01f * Hash01(id, 4, s + 30);
				const auto centre = [&](float at) { return x0 + 0.012f * (NoiseXY(0.0f, at, 1, 8, s + 31 + static_cast<uint32_t>(id)) - 0.5f); };
				float t = v - head;   // how far above the head, wrapped to [0, 1)
				t -= std::floor(t);
				float ddx = u - centre(v);
				ddx -= std::round(ddx);
				// the head: a drop sitting on the head line, its lower half fuller
				float hy = t - rh;
				hy -= std::round(hy);
				float hdx = u - centre(head + rh);
				hdx -= std::round(hdx);
				const float qh = std::sqrt(hdx * hdx + (hy < 0.0f ? hy * 0.85f : hy) * (hy < 0.0f ? hy * 0.85f : hy)) / rh;
				if (qh < 1.0f) w.Height = std::max(w.Height, rh / kDropBig * std::sqrt(1.0f - qh * qh));
				w.Drop = std::max(w.Drop, Smooth(1.0f, 0.85f, qh));
				w.Clear = std::max(w.Clear, Smooth(1.6f, 1.0f, qh));
				if (t < len)
				{
					// the thread, thinner toward its top, beaded; the band the drop swept clean is about as wide as the drop
					const float k = t / len;
					const float bead = std::pow(0.5f + 0.5f * std::sin(t * 260.0f + 6.0f * Hash01(id, 5, s + 30)), 6.0f);
					const float wt = rh * 0.45f * (1.0f - 0.6f * k) * (1.0f + 0.6f * bead);
					const float qt = std::fabs(ddx) / wt;
					if (t > rh && qt < 1.0f) w.Height = std::max(w.Height, wt / kDropBig * 0.8f * std::sqrt(1.0f - qt * qt));
					if (t > rh) w.Drop = std::max(w.Drop, Smooth(1.0f, 0.8f, qt));
					w.Clear = std::max(w.Clear, Smooth(1.3f, 0.9f, std::fabs(ddx) / rh) * Smooth(len, 0.75f * len, t));
				}
			}
			// the drops, from the big ones down
			DropLayer(u, v, 8, 0.35f, 0.2f, 0.36f, 1.5f, s + 40, w);
			DropLayer(u, v, 16, 0.45f, 0.18f, 0.34f, 1.4f, s + 41, w);
			DropLayer(u, v, 32, 0.55f, 0.16f, 0.32f, 1.25f, s + 42, w);
			DropLayer(u, v, 64, 0.6f, 0.15f, 0.3f, 1.0f, s + 43, w);   // the finest: no ring
			const float fog = Noise(u, v, 128, s + 50) * 0.6f + Noise(u, v, 64, s + 51) * 0.4f;
			const float fogged = 1.0f - w.Clear;
			Surface o;
			o.Albedo = Mix(Mix(glm::vec3(0.93f, 0.94f, 0.95f) * (0.98f + 0.02f * fog), glm::vec3(0.98f), w.Clear), glm::vec3(1.0f), w.Drop);
			o.Height = w.Height + 0.015f * fog * fogged;
			o.Rough = Lerp(Lerp(0.78f + 0.22f * fog, 0.25f, w.Clear), 0.07f, w.Drop);   // x the material's 0.8: fog 0.62..0.8, swept 0.2, drops 0.06
			return o;
		}

		// ---- T103: mud, better wood, and cards of leaves and grass (MATERIALES.md, "Materiales v3").

		// A Voronoi with its own period along u and along v (cells `1/px` wide and `1/py` tall: the long plates of bark). It is found in cell
		// space (square cells), so the regions come out stretched like the cells; D1 is in cell units, but Edge, the distance to the
		// nearest border, is measured in the tile (in units of a cell's width, 1/px), so a crack along a border keeps one width whichever
		// way the border runs.
		glm::vec2 VoronoiPointXY(int cx, int cy, int px, int py, uint32_t seed, float jitter)
		{
			const int wx = Wrap(cx, px), wy = Wrap(cy, py);
			return { static_cast<float>(cx) + 0.5f + jitter * (Hash01(wx, wy, seed) - 0.5f), static_cast<float>(cy) + 0.5f + jitter * (Hash01(wx, wy, seed ^ 0x5BD1E995u) - 0.5f) };
		}

		Voronoi VoronoiAtXY(float x, float y, int px, int py, uint32_t seed, float jitter)
		{
			const glm::vec2 scale(static_cast<float>(px), static_cast<float>(py));   // cell units per tile unit along u and v
			Voronoi r;
			r.X = { x, y };
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float best = 1e9f;
			int bx = ix, by = iy;
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx)
				{
					const glm::vec2 p = VoronoiPointXY(ix + dx, iy + dy, px, py, seed, jitter);
					const float d = glm::dot(p - r.X, p - r.X);
					if (d < best) { best = d; r.P1 = p; bx = ix + dx; by = iy + dy; }
				}
			r.D1 = std::sqrt(best);
			r.Id1 = Wrap(bx, px) + Wrap(by, py) * px;
			r.Edge = 1e9f;
			for (int dy = -2; dy <= 2; ++dy)
				for (int dx = -2; dx <= 2; ++dx)
				{
					if (dx == 0 && dy == 0) continue;
					const glm::vec2 p = VoronoiPointXY(bx + dx, by + dy, px, py, seed, jitter);
					const glm::vec2 n = glm::normalize(p - r.P1);
					// the distance to the bisector in cell space, then in the tile (the border's normal there is n scaled by px, py), in cell widths
					const float e = glm::dot(0.5f * (r.P1 + p) - r.X, n) * scale.x / glm::length(n * scale);
					if (e < r.Edge) { r.Edge = e; r.P2 = p; r.Id2 = Wrap(bx + dx, px) + Wrap(by + dy, py) * px; }
				}
			r.Edge = std::max(r.Edge, 0.0f);
			return r;
		}

		Surface Mud(float u, float v, uint32_t s)
		{
			// wet mud: a soft uneven ground of clods, a rut and a line of footprints along v, small stones; water stands in everything below
			// a level (shallow puddles: flat, dark, glossy) and the rim around them is wet
			const float wu = 0.03f * (Fbm(u, v, 4, 3, s + 1) - 0.5f), wv = 0.03f * (Fbm(u, v, 4, 3, s + 2) - 0.5f);
			const float broad = Fbm(u, v, 2, 4, s + 3);
			const float lumps = Fbm(u + wu, v + wv, 6, 3, s + 4);
			float second = 0.0f;
			const float cd = Cells(u + wu, v + wv, 12, s + 5, &second);
			const float clod = Smooth(0.5f, 0.05f, cd) * Smooth(0.45f, 0.65f, Noise(u, v, 5, s + 6));
			const float fine = Fbm(u, v, 32, 3, s + 7);
			// a soft rut along v, wandering (u ~ 0.15 and its neighbour across the tile edge)
			const float ru = u + 0.06f * (NoiseXY(u, v, 1, 3, s + 9) - 0.5f);
			const float rut = std::pow(std::max(0.0f, std::cos((ru - 0.15f) * kTau)), 24.0f);
			// footprints: one per row (six a tile, ~17 cm), left and right of u ~ 0.6, each an oval with a deeper heel and toe and a pushed-up rim
			const float fr = v * 6.0f;
			const int row = static_cast<int>(std::floor(fr));
			const int wr = Wrap(row, 6);
			const float side = (wr & 1) ? 1.0f : -1.0f;
			const glm::vec2 fc(0.6f + side * 0.05f + 0.02f * (Hash01(wr, 1, s + 11) - 0.5f), (static_cast<float>(row) + 0.5f + 0.2f * (Hash01(wr, 2, s + 11) - 0.5f)) / 6.0f);
			const glm::vec2 fd((u - fc.x) / 0.045f, (v - fc.y) / 0.115f);
			const float fq = glm::length(fd);
			const float print = Hash01(wr, 3, s + 11) < 0.8f ? Smooth(1.0f, 0.75f, fq) * (0.75f + 0.25f * Smooth(0.2f, 0.8f, std::fabs(fd.y))) : 0.0f;
			const float rim = Hash01(wr, 3, s + 11) < 0.8f ? Smooth(0.95f, 1.15f, fq) * Smooth(1.5f, 1.15f, fq) : 0.0f;
			// small stones
			const Voronoi st = VoronoiAt(u * 28.0f, v * 28.0f, 28, s + 13, 0.8f);
			const float sr = 0.18f + 0.18f * Hash01(st.Id1, 1, s + 14);
			const float stone = Hash01(st.Id1, 2, s + 14) > 0.93f ? std::sqrt(std::max(0.0f, 1.0f - (st.D1 * st.D1) / (sr * sr))) : 0.0f;
			float h = 0.42f + 0.3f * broad + 0.12f * lumps + 0.1f * clod + 0.04f * fine - 0.13f * rut - 0.12f * print + 0.05f * rim;
			h = std::max(h, h + 0.22f * stone);
			// water: a level a little above the low ground, so puddles gather in the rut, the prints and the hollows
			const float level = 0.555f + 0.03f * (Noise(u, v, 3, s + 15) - 0.5f);
			const float water = Smooth(0.0f, 0.012f, level - h);
			const float wet = Smooth(0.05f, 0.0f, h - level);   // the wet rim, darker and shinier
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.39f, 0.3f, 0.21f), glm::vec3(0.52f, 0.4f, 0.28f), 0.55f * broad + 0.45f * fine);
			c = Mix(c, glm::vec3(0.53f, 0.43f, 0.31f), clod * 0.5f);           // the clods' tops drier
			c *= 0.85f + 0.15f * Smooth(0.0f, 0.15f, h - level + 0.05f);    // painted occlusion in the hollows (R1)
			c = Mix(c, glm::vec3(0.42f, 0.40f, 0.37f) * (0.85f + 0.25f * Hash01(st.Id1, 3, s + 14)), stone > 0.0f ? 0.8f : 0.0f);
			c = Mix(c, c * 0.78f, wet);
			o.Albedo = Mix(c, c * 0.62f + glm::vec3(0.035f, 0.035f, 0.03f), water);   // standing water: darker, a little turbid (its sheen is the roughness)
			o.Height = std::max(h, level);   // the water's surface is flat
			const float roughMud = 0.72f + 0.28f * fine;
			o.Rough = Lerp(Lerp(stone > 0.0f ? 0.75f : roughMud, 0.4f, wet), 0.05f, water);   // x the material's 0.95: mud 0.68-0.95, rim 0.38, water 0.05
			return o;
		}

		Surface MudDry(float u, float v, uint32_t s)
		{
			// dried and cracked mud: polygonal plates (seven a tile, ~14 cm) that curl up at their edges, deep cracks between them, finer
			// cracks across some plates, pale dust on the curled rims and in patches
			const float wu = 0.03f * (Fbm(u, v, 5, 3, s + 1) - 0.5f), wv = 0.03f * (Fbm(u, v, 5, 3, s + 2) - 0.5f);
			const Voronoi a = VoronoiAt((u + wu) * 7.0f, (v + wv) * 7.0f, 7, s + 3, 0.9f);
			const Voronoi b = VoronoiAt((u + 0.5f * wu) * 19.0f, (v + 0.5f * wv) * 19.0f, 19, s + 5, 0.9f);
			const float width = 0.035f + 0.045f * Noise(u, v, 6, s + 4);
			const float crack = Smooth(width, width * 0.35f, a.Edge);
			const float curlAmount = 0.4f + 0.6f * Hash01(a.Id1, 1, s + 6);
			const float curl = Smooth(0.32f, 0.03f, a.Edge) * curlAmount;
			const float fineOn = Smooth(0.45f, 0.6f, Noise(u, v, 4, s + 7));
			const float crack2 = Smooth(0.03f, 0.008f, b.Edge) * fineOn;
			const float fine = Fbm(u, v, 32, 3, s + 8);
			const float plate = FacetPlane(a.P1, a.Id1, a.X, s + 9, 0.12f);
			Surface o;
			o.Height = (0.45f + 0.25f * plate + 0.22f * curl + 0.05f * fine - 0.12f * crack2) * (1.0f - crack) + 0.02f * crack;
			glm::vec3 c = Mix(glm::vec3(0.50f, 0.40f, 0.29f), glm::vec3(0.60f, 0.49f, 0.36f), Hash01(a.Id1, 2, s + 6)) * (0.9f + 0.2f * fine);
			const float dust = std::min(1.0f, 0.6f * curl + 0.5f * Smooth(0.55f, 0.75f, Fbm(u, v, 3, 4, s + 10)));
			c = Mix(c, glm::vec3(0.70f, 0.63f, 0.52f) * (0.95f + 0.1f * fine), dust * 0.7f);
			c *= 0.78f + 0.22f * Smooth(width, width * 3.0f, a.Edge);   // painted occlusion along the cracks (R1)
			c *= 1.0f - 0.25f * crack2;
			o.Albedo = Mix(c, glm::vec3(0.17f, 0.12f, 0.08f), crack);
			o.Rough = Lerp(0.88f + 0.12f * fine, 1.0f, crack);
			return o;
		}

		// Growth rings where a flat face cuts a trunk: the pith runs along the board at a depth that wanders (metres), so the rings show as
		// long lines that close into arches and ellipses where the face comes near the pith. `across` in metres from the board's middle,
		// `along` 0..1 down the piece; returns the ring count (its fraction is the place in the year's ring).
		float RingCount(float across, float along, uint32_t seed, float ringsPerMetre)
		{
			const float depth = 0.015f + 0.11f * NoiseXY(along, 0.0f, 3, 1, seed) * NoiseXY(along, 0.5f, 2, 1, seed + 1);
			const float centre = 0.05f * (NoiseXY(along, 0.25f, 2, 1, seed + 2) - 0.5f);
			const float r = std::sqrt((across - centre) * (across - centre) + depth * depth);
			return r * ringsPerMetre;
		}

		// The late wood of a ring: dark, narrow, the early wood eases into it and the next year starts sharply.
		float LateWood(float rings)
		{
			const float f = rings - std::floor(rings);
			return Smooth(0.5f, 0.9f, f) * Smooth(1.0f, 0.95f, f);
		}

		// Boards laid across v (five a tile, 20 cm at UVScale 1), the grain along u. Every board has one or two pieces a tile, butt-jointed,
		// each its own tone and its own run of rings; knots with a dark eye, the rings bending round them; sunk joints with a rounded,
		// worn arris. `weathered`: old boards gone silver grey in the weather, the soft early wood eroded so the late rings stand up, and
		// long drying checks.
		Surface PlanksSurface(float u, float v, uint32_t s, bool weathered)
		{
			constexpr int kBoards = 5;
			constexpr float kWidth = 1.0f / kBoards;   // metres at UVScale 1
			const float fr = v * kBoards;
			const int row = static_cast<int>(std::floor(fr));
			const float fv = fr - static_cast<float>(row);
			const int br = Wrap(row, kBoards);
			const int pieces = Hash01(br, 1, s) > 0.5f ? 2 : 1;
			const float fa = (u + Hash01(br, 2, s)) * static_cast<float>(pieces);
			const int pi = static_cast<int>(std::floor(fa));
			const float a = fa - static_cast<float>(pi);
			const int pid = br * 4 + Wrap(pi, pieces);
			const uint32_t ps = s + static_cast<uint32_t>(pid) * 131u;
			const float length = 1.0f / static_cast<float>(pieces);
			const float across = (fv - 0.5f) * kWidth;
			// knots: three pieces in four have one
			const bool hasKnot = Hash01(pid, 3, s) < 0.75f;
			const glm::vec2 kp(0.15f + 0.7f * Hash01(pid, 4, s), (Hash01(pid, 5, s) - 0.5f) * 0.6f * kWidth);
			const float kr = 0.007f + 0.008f * Hash01(pid, 6, s);
			const glm::vec2 kdv((a - kp.x) * length / 1.7f, across - kp.y);
			const float kd = hasKnot ? glm::length(kdv) : 1.0f;
			float rings = RingCount(across, a, ps, 170.0f) + 1.2f * (Fbm(u, v, 4, 3, ps + 3) - 0.5f);
			rings += 3.0f * Smooth(kr * 5.0f, kr * 0.8f, kd);   // the grain swirls round the knot
			const float late = LateWood(rings);
			const float fibre = NoiseXY(u, v, 6, 180, ps + 4) * 0.6f + NoiseXY(u, v, 24, 400, ps + 5) * 0.4f;
			const float knot = Smooth(kr, kr * 0.75f, kd);
			const float eye = Smooth(kr * 0.5f, kr * 0.25f, kd);
			const float knotRing = Smooth(kr * 0.25f, 0.0f, std::fabs(kd - kr));
			// the joints: long sides between boards, butt joints between pieces; a 2 mm gap and a rounded arris
			const float edge = std::min(std::min(fv, 1.0f - fv) * kWidth, std::min(a, 1.0f - a) * length);
			const float gap = Smooth(0.0022f, 0.0014f, edge);
			const float round = Smooth(0.0014f, 0.007f, edge);
			const float wear = Smooth(0.014f, 0.0f, edge) * Smooth(0.35f, 0.65f, Noise(u, v, 12, s + 7));
			const float tone = Hash01(pid, 7, s);
			Surface o;
			glm::vec3 early = Mix(glm::vec3(0.58f, 0.43f, 0.29f), glm::vec3(0.69f, 0.54f, 0.37f), tone);
			early *= 0.94f + 0.12f * Fbm(u, v, 3, 3, ps + 8);
			glm::vec3 c = Mix(early, early * glm::vec3(0.66f, 0.62f, 0.58f), 0.75f * late);
			c *= 0.92f + 0.14f * fibre;
			float check = 0.0f;
			if (weathered)
			{
				// silver: the colour washes out to a grey that keeps a little warmth in the late wood and in the checks
				const float grey = 0.5f + 0.08f * tone;
				c = Mix(glm::vec3(grey, grey * 0.98f, grey * 0.93f) * (0.9f + 0.18f * fibre), glm::vec3(0.36f, 0.33f, 0.29f), 0.6f * late);
				c = Mix(c, glm::vec3(0.40f, 0.37f, 0.30f), 0.5f * Smooth(0.6f, 0.8f, Fbm(u, v, 3, 3, s + 9)));   // dirt and old stains
				check = Smooth(0.035f, 0.0f, std::fabs(NoiseXY(u, v, 3, 22, ps + 10) - 0.5f)) * Smooth(0.5f, 0.7f, NoiseXY(u, v, 2, 6, ps + 11));
			}
			c = Mix(c, weathered ? glm::vec3(0.30f, 0.27f, 0.23f) : glm::vec3(0.34f, 0.22f, 0.13f), knot * 0.85f);
			c = Mix(c, glm::vec3(0.16f, 0.11f, 0.07f), std::max(eye, knotRing * 0.7f));
			c = Mix(c, Mix(c, glm::vec3(0.68f, 0.62f, 0.52f), 0.35f), wear);   // the worn arris paler
			c = Mix(c, glm::vec3(0.14f, 0.11f, 0.08f), check * 0.85f);
			c *= 0.84f + 0.16f * round;   // painted occlusion along the joints (R1)
			o.Albedo = Mix(c, glm::vec3(0.2f, 0.15f, 0.1f), gap);
			const float relief = weathered ? 0.16f * late + 0.06f * fibre : 0.035f * late + 0.025f * fibre;
			o.Height = (0.7f + relief + 0.04f * knot - 0.03f * eye - 0.5f * check) * round + 0.05f * (1.0f - round) - 0.03f * wear;
			o.Rough = weathered ? 0.9f + 0.1f * fibre : Lerp(Lerp(0.78f + 0.17f * fibre, 0.6f, wear), 0.7f, knot);
			return o;
		}

		Surface WoodPlanks2(float u, float v, uint32_t s) { return PlanksSurface(u, v, s, false); }
		Surface WoodOld(float u, float v, uint32_t s) { return PlanksSurface(u, v, s, true); }

		Surface WoodLog2(float u, float v, uint32_t s)
		{
			// a debarked log or a hewn beam, the grain along v (as `wood`): the rings of a log about 50 cm across, mirrored at its middle so
			// the tile repeats, wandering and closing into long arches; fibre at several scales, a faint grey patina, the shallow facets of
			// the drawknife, a few branch stubs and the long checks of drying, dark inside
			const float x = std::fabs((u * 2.0f - std::floor(u * 2.0f)) - 0.5f) * 0.5f;   // metres from the log's middle line, 0..0.25
			const float along = v;
			float rings = RingCount(x, along, s + 1, 120.0f) + 0.6f * (Fbm(u, v, 3, 3, s + 2) - 0.5f) + 0.3f * (NoiseXY(u, v, 6, 2, s + 3) - 0.5f);
			// branch stubs: few, small, the rings swirl round them
			const Voronoi k = VoronoiAt(u * 3.0f, v * 3.0f, 3, s + 4, 0.8f);
			const bool knotOn = Hash01(k.Id1, 1, s + 5) < 0.45f;
			const glm::vec2 kd((k.X.x - k.P1.x) / 3.0f, (k.X.y - k.P1.y) / 3.0f / 1.8f);
			const float kdist = knotOn ? glm::length(kd) : 1.0f;
			const float kr = 0.008f + 0.008f * Hash01(k.Id1, 2, s + 5);
			rings += 2.5f * Smooth(kr * 5.0f, kr, kdist);
			const float late = LateWood(rings);
			const float fibre = NoiseXY(u, v, 200, 6, s + 6) * 0.45f + NoiseXY(u, v, 90, 3, s + 7) * 0.35f + NoiseXY(u, v, 400, 12, s + 8) * 0.2f;
			const float facet = NoiseXY(u, v, 5, 14, s + 9);   // drawknife: shallow facets ~7 cm long
			const float check = Smooth(0.02f, 0.0f, std::fabs(NoiseXY(u, v, 14, 2, s + 10) - 0.5f)) * Smooth(0.55f, 0.72f, NoiseXY(u, v, 3, 3, s + 11));
			const float checkRim = Smooth(0.05f, 0.0f, std::fabs(NoiseXY(u, v, 14, 2, s + 10) - 0.5f)) * Smooth(0.55f, 0.72f, NoiseXY(u, v, 3, 3, s + 11));
			const float knot = Smooth(kr, kr * 0.7f, kdist), eye = Smooth(kr * 0.45f, kr * 0.2f, kdist);
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.60f, 0.47f, 0.33f), glm::vec3(0.70f, 0.57f, 0.41f), Fbm(u, v, 2, 3, s + 12));
			c = Mix(c, c * glm::vec3(0.72f, 0.68f, 0.64f), 0.6f * late);
			c *= 0.9f + 0.16f * fibre;
			c = Mix(c, glm::vec3(0.55f, 0.53f, 0.50f), 0.3f * Smooth(0.45f, 0.75f, Fbm(u, v, 3, 4, s + 13)));   // grey patina in patches
			c = Mix(c, glm::vec3(0.33f, 0.22f, 0.14f), knot * 0.8f);
			c = Mix(c, glm::vec3(0.16f, 0.11f, 0.07f), eye);
			c *= 1.0f - 0.18f * checkRim;   // painted occlusion beside the checks (R1)
			o.Albedo = Mix(c, glm::vec3(0.13f, 0.09f, 0.06f), check * 0.9f);
			o.Height = 0.55f + 0.05f * late + 0.08f * fibre + 0.18f * (facet - 0.5f) + 0.06f * knot - 0.6f * check;
			o.Rough = Lerp(0.8f + 0.2f * fibre, 1.0f, check);
			return o;
		}

		Surface Bark2(float u, float v, uint32_t s)
		{
			// a fissured bark that repeats in v as well (the old `bark` does not): long plates (seven across, three along a tile) split by
			// deep furrows of varying width; the plates are layered in thin flakes across v, greyer on top, and carry a little lichen
			const float wu = 0.04f * (Fbm(u, v, 4, 3, s + 1) - 0.5f), wv = 0.06f * (Fbm(u, v, 4, 3, s + 2) - 0.5f);
			const Voronoi p = VoronoiAtXY((u + wu) * 12.0f, (v + wv) * 2.0f, 12, 2, s + 3, 0.8f);
			const float width = 0.15f + 0.15f * Noise(u, v, 5, s + 4);   // in cell widths (8 cm): 1.2 to 2.4 cm
			const float furrow = Smooth(width, width * 0.3f, p.Edge);
			const float top = Smooth(width * 0.5f, 0.35f, p.Edge);
			const float flakes = NoiseXY(u, v, 7, 3, s + 5);
			const float layer = 30.0f * v + 0.6f * Hash01(p.Id1, 1, s + 6) + 1.5f * (flakes - 0.5f);
			const float step = layer - std::floor(layer);   // a sawtooth: each flake's lower edge stands out over the next
			const float fibre = NoiseXY(u, v, 80, 4, s + 7) * 0.6f + NoiseXY(u, v, 160, 9, s + 8) * 0.4f;
			const float fine = Fbm(u, v, 16, 3, s + 9);
			const float lichen = Smooth(0.72f, 0.8f, Fbm(u, v, 6, 3, s + 10)) * top;
			Surface o;
			glm::vec3 c = Mix(glm::vec3(0.45f, 0.38f, 0.31f), glm::vec3(0.60f, 0.53f, 0.45f), 0.5f * Hash01(p.Id1, 2, s + 6) + 0.5f * fine);
			c = Mix(c, glm::vec3(0.56f, 0.54f, 0.50f), 0.35f * top * Smooth(0.3f, 0.8f, fine));   // weathered grey plate tops
			c *= 0.9f + 0.12f * step;
			c = Mix(c, glm::vec3(0.52f, 0.55f, 0.44f), lichen * 0.6f);
			c *= 0.8f + 0.2f * Smooth(width, width * 2.5f, p.Edge);   // painted occlusion in the furrows (R1)
			o.Albedo = Mix(c, glm::vec3(0.26f, 0.19f, 0.14f) * (0.85f + 0.3f * fibre), furrow);
			o.Height = (0.25f + 0.55f * top + 0.1f * step + 0.04f * fine) * (1.0f - furrow) + (0.05f + 0.08f * fibre) * furrow;
			o.Rough = 0.88f + 0.12f * fine;
			return o;
		}

		// ---- T103 cards: leaves, twigs and grass on an alpha-masked card (u, v 0..1 over the whole card, v up: the stem or the grass's
		// base at v ~ 0). A card is a list of primitives in drawing order (capsules for twigs, needles, blades and stems; leaves; seed heads),
		// binned on a grid; a sample takes the topmost primitive under it. Cards do not tile: the silhouette stays inside the card's borders
		// and every texel outside it has the same bleed colour (so filtering never fringes dark, and the edge columns match).
		struct CardPrim
		{
			enum class Type : uint8_t { Stem, Needle, Leaf, Blade, Head };
			Type T = Type::Stem;
			glm::vec2 A{ 0.0f }, B{ 0.0f };   // from A to B (a leaf: stalk end to tip; a blade segment: lower to upper end)
			float R0 = 0.0f, R1 = 0.0f;       // capsule radii at A and B (card units); a leaf: R0 its half width over its length
			float T0 = 0.0f, T1 = 1.0f;       // a blade segment: where it lies along its blade (0 base, 1 tip)
			float Tone = 0.5f, Tone2 = 0.5f;  // colour variations
			float Age = 0.0f;                 // autumn of a leaf, dryness of a blade's tip, new growth of a needle
			float Back = 0.0f;                // 0 at the front of the tuft, 1 at the back: painted occlusion
			int Shape = 0;                    // a leaf: 0 broad (ovate, serrated, drawn-out tip), 1 small (elliptic)
			glm::vec2 Lo{ 0.0f }, Hi{ 0.0f };
		};

		struct CardLayout
		{
			static constexpr int kBins = 16;
			std::vector<CardPrim> Prims;
			std::vector<std::vector<uint32_t>> Bins;
			glm::vec3 Bleed{ 0.2f, 0.3f, 0.1f };
			float Dry = 0.0f;   // the whole card dried to straw (grass_dry)

			void Add(CardPrim p)
			{
				if (p.T == CardPrim::Type::Leaf)
				{
					const float l = glm::length(p.B - p.A), w = p.R0 * l + 0.01f;
					p.Lo = glm::min(p.A, p.B) - glm::vec2(w);
					p.Hi = glm::max(p.A, p.B) + glm::vec2(w);
				}
				else
				{
					const float r = std::max(p.R0, p.R1) + 0.002f;
					p.Lo = glm::min(p.A, p.B) - glm::vec2(r);
					p.Hi = glm::max(p.A, p.B) + glm::vec2(r);
				}
				Prims.push_back(p);
			}
			void Capsule(CardPrim::Type t, const glm::vec2& a, const glm::vec2& b, float r0, float r1, float tone, float age = 0.0f, float back = 0.0f)
			{
				CardPrim p;
				p.T = t; p.A = a; p.B = b; p.R0 = r0; p.R1 = r1; p.Tone = tone; p.Age = age; p.Back = back;
				Add(p);
			}
			void Finish()
			{
				Bins.assign(static_cast<size_t>(kBins) * kBins, {});
				for (uint32_t i = 0; i < Prims.size(); ++i)
				{
					const CardPrim& p = Prims[i];
					const int x0 = std::clamp(static_cast<int>(std::floor(p.Lo.x * kBins)), 0, kBins - 1), x1 = std::clamp(static_cast<int>(std::floor(p.Hi.x * kBins)), 0, kBins - 1);
					const int y0 = std::clamp(static_cast<int>(std::floor(p.Lo.y * kBins)), 0, kBins - 1), y1 = std::clamp(static_cast<int>(std::floor(p.Hi.y * kBins)), 0, kBins - 1);
					for (int y = y0; y <= y1; ++y)
						for (int x = x0; x <= x1; ++x) Bins[static_cast<size_t>(y) * kBins + static_cast<size_t>(x)].push_back(i);
				}
			}
		};

		glm::vec2 Rotate(const glm::vec2& d, float a) { return { d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a) }; }
		glm::vec2 Bezier(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, float t) { return (1.0f - t) * (1.0f - t) * p0 + 2.0f * (1.0f - t) * t * p1 + t * t * p2; }
		glm::vec2 BezierDir(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, float t) { return glm::normalize(2.0f * (1.0f - t) * (p1 - p0) + 2.0f * t * (p2 - p1)); }

		// Keeps a point inside the card's safe area (the borders stay transparent).
		glm::vec2 Inside(const glm::vec2& p, float margin) { return glm::clamp(p, glm::vec2(margin), glm::vec2(1.0f - margin)); }

		// A twig along a quadratic curve, as capsules tapering from `w0` to `w1`.
		void AddTwig(CardLayout& c, const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, float w0, float w1, float tone, CardPrim::Type type = CardPrim::Type::Stem, float back = 0.0f)
		{
			constexpr int kSegs = 10;
			for (int i = 0; i < kSegs; ++i)
			{
				const float t0 = static_cast<float>(i) / kSegs, t1 = static_cast<float>(i + 1) / kSegs;
				c.Capsule(type, Bezier(p0, p1, p2, t0), Bezier(p0, p1, p2, t1), Lerp(w0, w1, t0), Lerp(w0, w1, t1), tone, 0.0f, back);
			}
		}

		// A leaf on a short stalk from `at` toward `dir`, shortened so its tip stays inside the card.
		void AddLeaf(CardLayout& c, const glm::vec2& at, const glm::vec2& dir, float stalk, float length, float width, int shape, float tone, float tone2, float age, float back)
		{
			const glm::vec2 base = at + dir * stalk;
			glm::vec2 tip = base + dir * length;
			const glm::vec2 safe = Inside(tip, 0.035f + width * length);
			if (safe != tip)
			{
				// shrink along dir until the tip is in
				float k = 1.0f;
				for (int i = 0; i < 8 && Inside(base + dir * length * k, 0.035f + width * length * k) != base + dir * length * k; ++i) k *= 0.85f;
				tip = base + dir * length * k;
			}
			c.Capsule(CardPrim::Type::Stem, at, base, 0.0035f, 0.0025f, 0.8f, 0.0f, back);
			CardPrim p;
			p.T = CardPrim::Type::Leaf; p.A = base; p.B = tip; p.R0 = width; p.Tone = tone; p.Tone2 = tone2; p.Age = age; p.Back = back; p.Shape = shape;
			c.Add(p);
		}

		CardLayout LeafBroadCard(uint32_t s)
		{
			// a twig with seven broad leaves alternating up it and one at its tip; a few of them turning (by seed: how far autumn has come)
			CardLayout c;
			c.Bleed = { 0.22f, 0.32f, 0.10f };
			const glm::vec2 p0(0.5f, 0.03f), p1(0.5f + 0.12f * (Hash01(1, 0, s) - 0.5f), 0.4f), p2(0.5f + 0.1f * (Hash01(2, 0, s) - 0.5f), 0.66f);
			AddTwig(c, p0, p1, p2, 0.012f, 0.005f, 0.3f);
			const float autumn = Hash01(3, 0, s);
			for (int i = 0; i < 8; ++i)
			{
				const bool tipLeaf = i == 7;
				const float t = tipLeaf ? 1.0f : 0.14f + 0.12f * static_cast<float>(i);
				const float side = (i & 1) ? 1.0f : -1.0f;
				const glm::vec2 at = Bezier(p0, p1, p2, t), along = BezierDir(p0, p1, p2, t);
				const float angle = tipLeaf ? 0.08f * (Hash01(i, 1, s) - 0.5f) : side * (1.05f - 0.07f * static_cast<float>(i) + 0.15f * (Hash01(i, 1, s) - 0.5f));
				const float length = (tipLeaf ? 0.3f : 0.36f - 0.012f * static_cast<float>(i)) * (0.9f + 0.15f * Hash01(i, 2, s));
				const float age = Smooth(0.5f, 0.85f, 0.55f * Hash01(i, 3, s) + 0.45f * autumn + 0.08f);
				AddLeaf(c, at, Rotate(along, angle), 0.03f, length, 0.33f + 0.06f * Hash01(i, 4, s), 0, Hash01(i, 5, s), Hash01(i, 6, s), age, 0.0f);
			}
			c.Finish();
			return c;
		}

		CardLayout LeafSmallCard(uint32_t s)
		{
			// a spray of small leaves for bushes: twigs fanning from the base, a back row darker, leaves alternating up every twig
			CardLayout c;
			c.Bleed = { 0.18f, 0.28f, 0.09f };
			const glm::vec2 root(0.5f, 0.03f);
			struct Spray { float Angle, Length, Back; };
			const Spray sprays[] = { { -0.55f, 0.62f, 1.0f }, { 0.5f, 0.66f, 1.0f }, { 0.05f, 0.82f, 0.9f },
			                         { -0.8f, 0.5f, 0.0f }, { -0.32f, 0.8f, 0.0f }, { 0.28f, 0.84f, 0.0f }, { 0.78f, 0.52f, 0.0f } };
			int leaf = 0;
			for (int k = 0; k < 7; ++k)
			{
				const Spray& sp = sprays[k];
				const float a = sp.Angle + 0.12f * (Hash01(k, 10, s) - 0.5f);
				const glm::vec2 d = Rotate({ 0.0f, 1.0f }, -a);
				const glm::vec2 p2 = Inside(root + d * sp.Length, 0.08f);
				const glm::vec2 p1 = root + d * sp.Length * 0.5f + glm::vec2(0.05f * (Hash01(k, 11, s) - 0.5f), 0.0f);
				AddTwig(c, root, p1, p2, 0.008f, 0.003f, 0.35f, CardPrim::Type::Stem, sp.Back);
				for (float t = 0.22f; t <= 1.001f; t += 0.075f, ++leaf)
				{
					const bool end = t > 0.97f;
					const float side = (leaf & 1) ? 1.0f : -1.0f;
					const glm::vec2 at = Bezier(root, p1, p2, std::min(t, 1.0f)), along = BezierDir(root, p1, p2, std::min(t, 1.0f));
					const float angle = end ? 0.0f : side * (0.75f + 0.3f * (Hash01(leaf, 1, s) - 0.5f));
					const float length = (0.1f + 0.035f * Hash01(leaf, 2, s)) * (end ? 0.9f : 1.0f);
					AddLeaf(c, at, Rotate(along, angle), 0.012f, length, 0.4f + 0.06f * Hash01(leaf, 3, s), 1, Hash01(leaf, 4, s), Hash01(leaf, 5, s), 0.0f, sp.Back * 0.8f);
				}
			}
			c.Finish();
			return c;
		}

		CardLayout LeafNeedleCard(uint32_t s)
		{
			// a conifer twig: a main shoot, side shoots alternating up it, needles all along both sides angled forward; the tips' new growth paler
			CardLayout c;
			c.Bleed = { 0.12f, 0.2f, 0.08f };
			struct Shoot { glm::vec2 P0, P1, P2; float W0, W1; };
			std::vector<Shoot> shoots;
			const glm::vec2 m0(0.5f, 0.03f), m1(0.5f + 0.1f * (Hash01(1, 0, s) - 0.5f), 0.5f), m2(0.5f + 0.06f * (Hash01(2, 0, s) - 0.5f), 0.93f);
			shoots.push_back({ m0, m1, m2, 0.009f, 0.004f });
			for (int i = 0; i < 8; ++i)
			{
				const float t = 0.14f + 0.095f * static_cast<float>(i);
				const float side = (i & 1) ? 1.0f : -1.0f;
				const glm::vec2 at = Bezier(m0, m1, m2, t), along = BezierDir(m0, m1, m2, t);
				const glm::vec2 d = Rotate(along, side * (0.85f + 0.15f * (Hash01(i, 1, s) - 0.5f)));
				const float length = (0.4f - 0.3f * t) * (0.85f + 0.3f * Hash01(i, 2, s));
				const glm::vec2 end = Inside(at + d * length, 0.08f);
				shoots.push_back({ at, 0.5f * (at + end) + glm::vec2(0.0f, 0.03f), end, 0.006f, 0.003f });
			}
			int n = 0;
			for (size_t k = 0; k < shoots.size(); ++k)
			{
				const Shoot& sh = shoots[k];
				const float len = glm::length(sh.P2 - sh.P0);
				// needles first on the back side (darker), then the twig, then the front side
				for (int pass = 0; pass < 3; ++pass)
				{
					if (pass == 1) { AddTwig(c, sh.P0, sh.P1, sh.P2, sh.W0, sh.W1, 0.5f); continue; }
					const float side = pass == 0 ? -1.0f : 1.0f;
					const float stepT = 0.011f / std::max(len, 0.05f);
					for (float t = 0.04f; t <= 1.0f; t += stepT, ++n)
					{
						const glm::vec2 at = Bezier(sh.P0, sh.P1, sh.P2, t), along = BezierDir(sh.P0, sh.P1, sh.P2, t);
						const glm::vec2 d = Rotate(along, side * (0.95f + 0.25f * (Hash01(n, 1, s) - 0.5f)));
						const float nl = (0.05f - 0.015f * t) * (0.85f + 0.3f * Hash01(n, 2, s));
						const glm::vec2 b = Inside(at + d * nl, 0.03f);
						c.Capsule(CardPrim::Type::Needle, at, b, 0.0042f, 0.0016f, Hash01(n, 3, s), Smooth(0.72f, 1.0f, t), pass == 0 ? 0.6f : 0.0f);
					}
				}
			}
			c.Finish();
			return c;
		}

		struct GrassSpec
		{
			int Blades;
			float HMin, HMax;   // blade heights (card units)
			float Width;        // half width at the base (card units)
			float Spread;       // how far the base fans out from the middle (card units)
			float Lean;         // the blades' starting angle from the vertical, at most (radians)
			float Curl;         // how much each of the six segments turns away from the vertical (radians)
			int Heads;          // stems with a seed head
			float HeadLength;   // card units
			float HeadWidth;
			float Dry;          // 0 green, 1 dried to straw
		};

		CardLayout GrassCard(const GrassSpec& g, uint32_t s)
		{
			// a tuft: blades fanning from a base near the bottom middle, each bending away as it rises, darker at the base, paler toward the
			// tip and dry at the very tip; stems with seed heads. Blades are drawn back to front.
			CardLayout c;
			c.Dry = g.Dry;
			c.Bleed = g.Dry > 0.5f ? glm::vec3(0.5f, 0.44f, 0.27f) : glm::vec3(0.18f, 0.28f, 0.09f);
			struct Item { float Back; int Index; bool Head; };
			std::vector<Item> items;
			for (int i = 0; i < g.Blades; ++i) items.push_back({ Hash01(i, 1, s), i, false });
			for (int i = 0; i < g.Heads; ++i) items.push_back({ Hash01(i, 1, s + 7), i, true });
			std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.Back != b.Back ? a.Back > b.Back : a.Index < b.Index; });
			for (const Item& it : items)
			{
				const uint32_t hs = it.Head ? s + 7 : s;
				const int i = it.Index;
				const float offset = (Hash01(i, 2, hs) + Hash01(i, 3, hs) - 1.0f) * g.Spread;
				glm::vec2 p(0.5f + offset, 0.012f + 0.03f * Hash01(i, 4, hs));
				const float height = it.Head ? g.HMax * (0.92f + 0.1f * Hash01(i, 5, hs)) : Lerp(g.HMin, g.HMax, Hash01(i, 5, hs));
				float angle = std::clamp(offset * 1.4f + g.Lean * (Hash01(i, 6, hs) - 0.5f) * 2.0f, -g.Lean * 1.3f, g.Lean * 1.3f);
				const float curl = (angle >= 0.0f ? 1.0f : -1.0f) * g.Curl * (0.4f + 1.2f * Hash01(i, 7, hs)) * (it.Head ? 0.35f : 1.0f);
				const float w = (it.Head ? 0.3f : 1.0f) * g.Width * (0.75f + 0.5f * Hash01(i, 8, hs));
				const float tone = Hash01(i, 9, hs), age = Hash01(i, 10, hs) * Hash01(i, 11, hs);
				constexpr int kSegs = 6;
				const float seg = height / kSegs;
				for (int k = 0; k < kSegs; ++k)
				{
					const float t0 = static_cast<float>(k) / kSegs, t1 = static_cast<float>(k + 1) / kSegs;
					const glm::vec2 q = p + glm::vec2(std::sin(angle), std::cos(angle)) * seg;
					if (q.x < 0.035f || q.x > 0.965f || q.y > 0.975f) break;
					CardPrim b;
					b.T = CardPrim::Type::Blade; b.A = p; b.B = q; b.T0 = t0; b.T1 = t1; b.Tone = tone; b.Age = age; b.Back = it.Back;
					b.R0 = it.Head ? w : w * std::pow(1.0f - t0, 0.7f) + 0.0008f;
					b.R1 = it.Head ? w * 0.8f : w * std::pow(1.0f - t1, 0.7f) + 0.0008f;
					c.Add(b);
					if (it.Head && k == kSegs - 1)
					{
						// the seed head along the stem's last stretch, continuing its direction
						const glm::vec2 d(std::sin(angle), std::cos(angle));
						const glm::vec2 hb = Inside(q + d * g.HeadLength, 0.03f);
						CardPrim h;
						h.T = CardPrim::Type::Head; h.A = q - d * 0.01f; h.B = hb; h.R0 = g.HeadWidth * (0.8f + 0.4f * Hash01(i, 12, hs)); h.Tone = tone; h.Age = age; h.Back = it.Back;
						c.Add(h);
					}
					p = q;
					angle += curl;
				}
			}
			c.Finish();
			return c;
		}

		bool CapsuleAt(const CardPrim& p, const glm::vec2& q, float& t, float& across)
		{
			const glm::vec2 ab = p.B - p.A;
			const float l2 = glm::dot(ab, ab);
			t = l2 > 0.0f ? std::clamp(glm::dot(q - p.A, ab) / l2, 0.0f, 1.0f) : 0.0f;
			const glm::vec2 d = q - (p.A + ab * t);
			const float r = Lerp(p.R0, p.R1, t), dist = glm::length(d);
			if (r <= 0.0f || dist >= r) return false;
			across = dist / r * (ab.x * d.y - ab.y * d.x >= 0.0f ? 1.0f : -1.0f);
			return true;
		}

		// The colour, relief and shine of a primitive at q, or false if q is outside it.
		bool ShadePrim(const CardLayout& card, const CardPrim& p, const glm::vec2& q, uint32_t s, Surface& o)
		{
			float t = 0.0f, a = 0.0f;
			switch (p.T)
			{
			case CardPrim::Type::Stem:
			{
				if (!CapsuleAt(p, q, t, a)) return false;
				const float round = std::sqrt(std::max(0.0f, 1.0f - a * a));
				glm::vec3 c = Mix(glm::vec3(0.27f, 0.2f, 0.13f), glm::vec3(0.36f, 0.3f, 0.17f), p.Tone) * (0.75f + 0.25f * round);
				c *= 1.0f - 0.3f * p.Back;
				o.Albedo = c;
				o.Height = 0.5f + 0.3f * round;
				o.Rough = 0.9f;
				return true;
			}
			case CardPrim::Type::Needle:
			{
				if (!CapsuleAt(p, q, t, a)) return false;
				const float round = std::sqrt(std::max(0.0f, 1.0f - a * a));
				glm::vec3 c = Mix(glm::vec3(0.11f, 0.2f, 0.09f), glm::vec3(0.17f, 0.29f, 0.12f), p.Tone);
				c = Mix(c, glm::vec3(0.3f, 0.43f, 0.15f), p.Age * 0.8f);   // new growth at the shoots' tips
				c *= (0.85f + 0.15f * round) * (1.0f - 0.35f * p.Back) * (0.9f + 0.15f * t);
				o.Albedo = c;
				o.Height = 0.5f + 0.25f * round;
				o.Rough = 0.55f;
				return true;
			}
			case CardPrim::Type::Blade:
			{
				if (!CapsuleAt(p, q, t, a)) return false;
				const float gt = Lerp(p.T0, p.T1, t);
				const glm::vec3 base(0.1f, 0.17f, 0.06f), mid = Mix(glm::vec3(0.22f, 0.34f, 0.1f), glm::vec3(0.33f, 0.45f, 0.15f), p.Tone);
				glm::vec3 c = Mix(base, mid, Smooth(0.0f, 0.5f, gt));
				const float tipDry = Smooth(1.0f - 0.12f - 0.3f * p.Age, 1.0f, gt);
				c = Mix(c, glm::vec3(0.6f, 0.54f, 0.33f), tipDry * 0.85f);
				const glm::vec3 straw = Mix(glm::vec3(0.5f, 0.41f, 0.22f), glm::vec3(0.7f, 0.6f, 0.36f), p.Tone) * (0.72f + 0.28f * Smooth(0.0f, 0.5f, gt));
				c = Mix(c, straw, card.Dry * (0.8f + 0.2f * p.Age));
				const float rib = Smooth(0.3f, 0.0f, std::fabs(a));
				c *= 1.0f + 0.12f * rib;
				c *= (1.0f - 0.38f * p.Back * (1.0f - 0.5f * gt)) * (0.62f + 0.38f * Smooth(0.0f, 0.3f, gt));   // painted occlusion in the tuft and at its base (R1)
				c *= 0.94f + 0.12f * Noise(q.x, q.y, 64, s + 3);
				o.Albedo = c;
				o.Height = 0.5f + 0.2f * (1.0f - a * a) - 0.08f * rib;
				o.Rough = 0.72f + 0.25f * std::max(tipDry, card.Dry);
				return true;
			}
			case CardPrim::Type::Head:
			{
				const glm::vec2 ab = p.B - p.A;
				const float l2 = glm::dot(ab, ab);
				t = l2 > 0.0f ? std::clamp(glm::dot(q - p.A, ab) / l2, 0.0f, 1.0f) : 0.0f;
				const glm::vec2 d = q - (p.A + ab * t);
				const float side = ab.x * d.y - ab.y * d.x >= 0.0f ? 1.0f : -1.0f;
				const float grain = 0.5f + 0.5f * std::sin(t * kTau * 9.0f + side * 1.6f);
				const float r = p.R0 * std::pow(std::max(0.0f, std::sin(3.14159265f * t)), 0.45f) * (0.78f + 0.22f * grain);
				const float dist = glm::length(d);
				if (r <= 0.0f || dist >= r) return false;
				a = dist / r;
				glm::vec3 c = Mix(glm::vec3(0.42f, 0.45f, 0.22f), glm::vec3(0.62f, 0.56f, 0.33f), 0.4f + 0.6f * p.Age);
				c = Mix(c, glm::vec3(0.74f, 0.62f, 0.36f), card.Dry);
				c *= (0.8f + 0.2f * grain) * (0.85f + 0.15f * (1.0f - a)) * (1.0f - 0.3f * p.Back);
				o.Albedo = c;
				o.Height = 0.5f + 0.25f * (1.0f - a * a) + 0.1f * grain;
				o.Rough = 0.85f;
				return true;
			}
			case CardPrim::Type::Leaf:
			{
				const glm::vec2 ab = p.B - p.A;
				const float len = glm::length(ab);
				if (len <= 0.0f) return false;
				const glm::vec2 dir = ab / len, perp(-dir.y, dir.x);
				const glm::vec2 d = q - p.A;
				const float along = glm::dot(d, dir) / len;
				if (along <= 0.0f || along >= 1.0f) return false;
				float across = glm::dot(d, perp) / len;
				across -= 0.07f * along * along * (p.Tone2 > 0.5f ? 1.0f : -1.0f);   // the leaf bends a little
				float half;
				if (p.Shape == 0)
				{
					half = p.R0 * std::pow(std::sin(3.14159265f * std::pow(along, 0.62f)), 0.85f);
					half *= 1.0f - 0.07f * (along * 20.0f - std::floor(along * 20.0f)) * Smooth(0.08f, 0.2f, along);   // serrated margin
				}
				else half = p.R0 * std::pow(std::sin(3.14159265f * along), 0.7f);
				const float ax = std::fabs(across);
				if (ax >= half) return false;
				const float rel = ax / half;
				const float rib = Smooth(0.012f, 0.004f, ax) * (1.0f - 0.6f * along);
				const float lat = std::fabs((along * (p.Shape == 0 ? 7.0f : 4.0f) - ax * 2.2f + 0.3f) - std::floor(along * (p.Shape == 0 ? 7.0f : 4.0f) - ax * 2.2f + 0.3f) - 0.5f);
				const float vein = Smooth(0.06f, 0.02f, lat) * Smooth(0.95f, 0.7f, rel) * Smooth(0.0f, 0.15f, along) * (p.Shape == 0 ? 1.0f : 0.5f);
				glm::vec3 c = p.Shape == 0 ? Mix(glm::vec3(0.15f, 0.26f, 0.07f), glm::vec3(0.27f, 0.39f, 0.11f), p.Tone)
				                           : Mix(glm::vec3(0.13f, 0.23f, 0.06f), glm::vec3(0.25f, 0.37f, 0.1f), p.Tone);
				c *= 0.92f + 0.1f * (1.0f - rel);
				c *= 0.92f + 0.14f * Noise(q.x, q.y, 48, s + 5);
				// autumn: from the margin and the tip inward, yellow or rust by the leaf
				const float aut = p.Age * Smooth(0.0f, 0.6f, 0.4f + 0.7f * rel + 0.4f * along - 0.6f * (1.0f - p.Age));
				c = Mix(c, Mix(glm::vec3(0.66f, 0.5f, 0.14f), glm::vec3(0.58f, 0.25f, 0.09f), p.Tone2), aut);
				c = Mix(c, c * 1.35f + glm::vec3(0.04f, 0.05f, 0.0f), std::max(rib, vein * 0.55f));
				c *= 1.0f - 0.32f * p.Back;
				o.Albedo = c;
				o.Height = 0.5f + 0.22f * std::sqrt(1.0f - rel * rel) - 0.12f * rib - 0.06f * vein;
				o.Rough = 0.55f + 0.12f * vein + 0.2f * aut;
				return true;
			}
			}
			return false;
		}

		Surface SampleCard(const CardLayout& card, float u, float v, uint32_t s)
		{
			Surface o;
			o.Albedo = card.Bleed;
			o.Height = 0.5f;
			o.Rough = 1.0f;
			o.Alpha = 0.0f;
			const int bx = std::clamp(static_cast<int>(u * CardLayout::kBins), 0, CardLayout::kBins - 1), by = std::clamp(static_cast<int>(v * CardLayout::kBins), 0, CardLayout::kBins - 1);
			const std::vector<uint32_t>& bin = card.Bins[static_cast<size_t>(by) * CardLayout::kBins + static_cast<size_t>(bx)];
			for (auto it = bin.rbegin(); it != bin.rend(); ++it)
			{
				const CardPrim& p = card.Prims[*it];
				if (u < p.Lo.x || u > p.Hi.x || v < p.Lo.y || v > p.Hi.y) continue;
				if (ShadePrim(card, p, { u, v }, s, o)) { o.Alpha = 1.0f; return o; }
			}
			return o;
		}

		using CardBuilder = CardLayout (*)(uint32_t seed);

		CardLayout GrassShortCard(uint32_t s) { return GrassCard({ 46, 0.5f, 0.92f, 0.024f, 0.3f, 0.3f, 0.04f, 0, 0.0f, 0.0f, 0.0f }, s); }
		CardLayout GrassMediumCard(uint32_t s) { return GrassCard({ 34, 0.4f, 0.8f, 0.017f, 0.24f, 0.32f, 0.07f, 3, 0.13f, 0.022f, 0.0f }, s); }
		CardLayout GrassTallCard(uint32_t s) { return GrassCard({ 28, 0.55f, 0.9f, 0.013f, 0.2f, 0.25f, 0.12f, 5, 0.15f, 0.018f, 0.0f }, s); }
		CardLayout GrassDryCard(uint32_t s) { return GrassCard({ 30, 0.45f, 0.85f, 0.015f, 0.24f, 0.32f, 0.09f, 5, 0.13f, 0.02f, 1.0f }, s); }

		struct CardKind
		{
			const char* Name;
			CardBuilder Build;
			float Depth;   // the relief of Height 0..1, in card units
		};

		const std::vector<CardKind>& Cards()
		{
			static const std::vector<CardKind> table = {
				{ "leaf_broad", LeafBroadCard, 0.004f }, { "leaf_small", LeafSmallCard, 0.004f }, { "leaf_needle", LeafNeedleCard, 0.003f },
				{ "grass_short", GrassShortCard, 0.003f }, { "grass_medium", GrassMediumCard, 0.003f }, { "grass_tall", GrassTallCard, 0.003f },
				{ "grass_dry", GrassDryCard, 0.003f } };
			return table;
		}

		const CardKind* FindCard(const std::string& kind)
		{
			for (const CardKind& k : Cards()) if (kind == k.Name) return &k;
			return nullptr;
		}

		// A card texel: 3x3 samples, so the silhouette's edge carries partial coverage (alpha to coverage) and the colour, relief and shine
		// are the mean of the covered samples (the bleed colour where none is).
		Surface CardTexel(const CardLayout& card, uint32_t x, uint32_t y, uint32_t size, uint32_t s)
		{
			constexpr int kSub = 3;
			Surface sum;
			sum.Albedo = glm::vec3(0.0f);
			sum.Height = 0.0f;
			sum.Rough = 0.0f;
			int hits = 0;
			for (int sy = 0; sy < kSub; ++sy)
				for (int sx = 0; sx < kSub; ++sx)
				{
					const float u = (static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kSub) / static_cast<float>(size);
					const float v = (static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kSub) / static_cast<float>(size);
					const Surface o = SampleCard(card, u, v, s);
					if (o.Alpha <= 0.0f) continue;
					++hits;
					sum.Albedo += o.Albedo;
					sum.Height += o.Height;
					sum.Rough += o.Rough;
				}
			Surface o;
			if (hits == 0) { o.Albedo = card.Bleed; o.Height = 0.5f; o.Rough = 1.0f; o.Alpha = 0.0f; return o; }
			const float n = static_cast<float>(hits);
			o.Albedo = sum.Albedo / n;
			o.Height = sum.Height / n;
			o.Rough = sum.Rough / n;
			o.Alpha = n / (kSub * kSub);
			return o;
		}

		struct SurfaceKind
		{
			const char* Name;
			SurfaceShader Fn;
			float Depth;   // the relief of Height 0..1, in tile units (0.01 = 1 cm at UVScale 1)
		};

		const std::vector<SurfaceKind>& Surfaces()
		{
			static const std::vector<SurfaceKind> table = {
				{ "sand", Sand, 0.006f }, { "brick", Brick, 0.012f }, { "brick_worn", BrickWorn, 0.014f }, { "ashlar", Ashlar, 0.012f },
				{ "rock", Rock, 0.03f }, { "wood", Wood, 0.006f }, { "ice", Ice, 0.004f }, { "ice_frost", IceFrost, 0.006f },
				{ "glass_frosted", GlassFrosted, 0.0015f }, { "ice_faceted", IceFaceted, 0.15f }, { "glass_wet", GlassWet, 0.025f },
				{ "mud", Mud, 0.03f }, { "mud_dry", MudDry, 0.02f }, { "wood_planks2", WoodPlanks2, 0.006f }, { "wood_log2", WoodLog2, 0.006f },
				{ "bark2", Bark2, 0.03f }, { "wood_old", WoodOld, 0.008f } };
			return table;
		}

		const SurfaceKind* FindSurface(const std::string& kind)
		{
			for (const SurfaceKind& k : Surfaces()) if (kind == k.Name) return &k;
			return nullptr;
		}

		const Shader* FindShader(const std::string& kind)
		{
			for (const auto& [name, fn] : Table()) if (name == kind) return &fn;
			return nullptr;
		}

		// One value per texel at the texel centres (the same pattern at every size, so the low-res texture is the same look, only blurrier).
		template <typename F>
		void ForTexels(uint32_t size, F&& f)
		{
			for (uint32_t y = 0; y < size; ++y)
				for (uint32_t x = 0; x < size; ++x)
					f(x, y, (static_cast<float>(x) + 0.5f) / static_cast<float>(size), (static_cast<float>(y) + 0.5f) / static_cast<float>(size));
		}

		Image Blank(uint32_t size)
		{
			Image img;
			img.Width = img.Height = size;
			img.Pixels.resize(static_cast<size_t>(size) * size * 4);
			return img;
		}

		uint8_t Byte(float c) { return static_cast<uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); }

		// T103: every texel of a card, row by row (empty if `kind` is not a card).
		std::vector<Surface> CardTexels(const std::string& kind, uint32_t size, uint32_t seed)
		{
			const CardKind* card = FindCard(kind);
			if (!card) return {};
			const uint32_t s = VisualHash(kind.c_str(), seed);
			const CardLayout layout = card->Build(s);
			std::vector<Surface> out(static_cast<size_t>(size) * size);
			for (uint32_t y = 0; y < size; ++y)
				for (uint32_t x = 0; x < size; ++x) out[static_cast<size_t>(y) * size + x] = CardTexel(layout, x, y, size, s);
			return out;
		}
	}

	namespace ProcTexture
	{
		const std::vector<std::string>& Kinds()
		{
			static const std::vector<std::string> kinds = []
			{
				std::vector<std::string> k;
				for (const auto& [name, fn] : Table()) k.push_back(name);
				for (const SurfaceKind& s : Surfaces()) k.push_back(s.Name);
				for (const CardKind& c : Cards()) k.push_back(c.Name);
				return k;
			}();
			return kinds;
		}

		bool Known(const std::string& kind)
		{
			return FindShader(kind) != nullptr || FindSurface(kind) != nullptr || FindCard(kind) != nullptr;
		}

		bool IsCard(const std::string& kind)
		{
			return FindCard(kind) != nullptr;
		}

		Image Generate(const std::string& kind, uint32_t size, uint32_t seed)
		{
			size = std::clamp(size, 16u, 1024u);
			const Shader* shader = FindShader(kind);
			const SurfaceKind* surface = shader ? nullptr : FindSurface(kind);
			if (!shader && !surface && FindCard(kind))
			{
				// a card: RGBA, the alpha its coverage (straight, not premultiplied)
				const std::vector<Surface> texels = CardTexels(kind, size, seed);
				Image img = Blank(size);
				for (size_t i = 0; i < texels.size(); ++i)
				{
					uint8_t* p = &img.Pixels[i * 4];
					p[0] = Byte(texels[i].Albedo.r);
					p[1] = Byte(texels[i].Albedo.g);
					p[2] = Byte(texels[i].Albedo.b);
					p[3] = Byte(texels[i].Alpha);
				}
				return img;
			}
			if (!shader && !surface) return Image::Solid(size, size, 128, 128, 128);
			Image img = Blank(size);
			const uint32_t s = VisualHash(kind.c_str(), seed);
			ForTexels(size, [&](uint32_t x, uint32_t y, float u, float v)
			{
				const glm::vec3 c = shader ? (*shader)(u, v, s) : surface->Fn(u, v, s).Albedo;
				uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * size + x) * 4];
				p[0] = Byte(c.r);
				p[1] = Byte(c.g);
				p[2] = Byte(c.b);
				p[3] = 255;
			});
			return img;
		}

		Image GenerateNormal(const std::string& kind, uint32_t size, uint32_t seed)
		{
			size = std::clamp(size, 16u, 1024u);
			const Shader* shader = FindShader(kind);
			const SurfaceKind* surface = shader ? nullptr : FindSurface(kind);
			const CardKind* card = shader || surface ? nullptr : FindCard(kind);
			if (!shader && !surface && !card) return Image::Solid(size, size, 128, 128, 255);
			// the relief at every texel: the surface's own, or the older kinds' albedo luminance (darker is lower: joints, furrows, cracks)
			std::vector<float> height(static_cast<size_t>(size) * size);
			if (card)
			{
				const std::vector<Surface> texels = CardTexels(kind, size, seed);
				for (size_t i = 0; i < texels.size(); ++i) height[i] = texels[i].Height;
			}
			else
			{
				const uint32_t s = VisualHash(kind.c_str(), seed);
				ForTexels(size, [&](uint32_t x, uint32_t y, float u, float v)
				{
					float h = 0.0f;
					if (surface) h = surface->Fn(u, v, s).Height;
					else
					{
						const glm::vec3 c = glm::clamp((*shader)(u, v, s), glm::vec3(0.0f), glm::vec3(1.0f));
						h = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
					}
					height[static_cast<size_t>(y) * size + x] = h;
				});
			}
			const float depth = surface ? surface->Depth : card ? card->Depth : 0.004f;
			// slopes by central differences across the wrapped edges (the map tiles like the albedo), in tile units: dh/du = depth * dH * size / 2
			const float k = depth * static_cast<float>(size) * 0.5f;
			Image img = Blank(size);
			const auto at = [&](uint32_t x, uint32_t y) { return height[static_cast<size_t>(y % size) * size + x % size]; };
			ForTexels(size, [&](uint32_t x, uint32_t y, float, float)
			{
				const float gx = (at(x + 1, y) - at(x + size - 1, y)) * k, gy = (at(x, y + 1) - at(x, y + size - 1)) * k;
				const glm::vec3 n = glm::normalize(glm::vec3(-gx, -gy, 1.0f));
				uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * size + x) * 4];
				p[0] = Byte(0.5f + 0.5f * n.x);
				p[1] = Byte(0.5f + 0.5f * n.y);
				p[2] = Byte(0.5f + 0.5f * n.z);
				p[3] = 255;
			});
			return img;
		}

		Image GenerateRoughness(const std::string& kind, uint32_t size, uint32_t seed)
		{
			size = std::clamp(size, 16u, 1024u);
			const SurfaceKind* surface = FindShader(kind) ? nullptr : FindSurface(kind);
			if (!surface && FindCard(kind))
			{
				const std::vector<Surface> texels = CardTexels(kind, size, seed);
				Image img = Blank(size);
				for (size_t i = 0; i < texels.size(); ++i)
				{
					uint8_t* p = &img.Pixels[i * 4];
					p[0] = 255;
					p[1] = Byte(texels[i].Rough);
					p[2] = 255;
					p[3] = 255;
				}
				return img;
			}
			if (!surface) return Image::Solid(size, size, 255, 255, 255);
			Image img = Blank(size);
			const uint32_t s = VisualHash(kind.c_str(), seed);
			ForTexels(size, [&](uint32_t x, uint32_t y, float u, float v)
			{
				uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * size + x) * 4];
				p[0] = 255;
				p[1] = Byte(surface->Fn(u, v, s).Rough);
				p[2] = 255;
				p[3] = 255;
			});
			return img;
		}

		Image GenerateInterior(const std::string& kind, uint32_t size, uint32_t seed)
		{
			size = std::clamp(size, 16u, 1024u);
			Image img = Blank(size);   // all zero: transparent, clean ice
			if (kind != "ice" && kind != "ice_faceted") return img;
			const uint32_t s = VisualHash((kind + ".interior").c_str(), seed);
			ForTexels(size, [&](uint32_t x, uint32_t y, float u, float v)
			{
				const glm::vec4 c = IceInterior(u, v, s);
				uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * size + x) * 4];
				p[0] = Byte(c.r);
				p[1] = Byte(c.g);
				p[2] = Byte(c.b);
				p[3] = Byte(c.a);
			});
			return img;
		}

		uint64_t Hash(const Image& image)
		{
			uint64_t h = 1469598103934665603ull ^ (static_cast<uint64_t>(image.Width) << 32 | image.Height);
			for (uint8_t b : image.Pixels) { h ^= b; h *= 1099511628211ull; }
			return h;
		}
	}
}
