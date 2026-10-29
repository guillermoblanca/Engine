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
	}

	namespace ProcTexture
	{
		const std::vector<std::string>& Kinds()
		{
			static const std::vector<std::string> kinds = []
			{
				std::vector<std::string> k;
				for (const auto& [name, fn] : Table()) k.push_back(name);
				return k;
			}();
			return kinds;
		}

		bool Known(const std::string& kind)
		{
			for (const auto& [name, fn] : Table()) if (name == kind) return true;
			return false;
		}

		Image Generate(const std::string& kind, uint32_t size, uint32_t seed)
		{
			size = std::clamp(size, 16u, 1024u);
			const Shader* shader = nullptr;
			for (const auto& [name, fn] : Table()) if (name == kind) shader = &fn;
			if (!shader) return Image::Solid(size, size, 128, 128, 128);
			Image img;
			img.Width = img.Height = size;
			img.Pixels.resize(static_cast<size_t>(size) * size * 4);
			const uint32_t s = VisualHash(kind.c_str(), seed);
			for (uint32_t y = 0; y < size; ++y)
				for (uint32_t x = 0; x < size; ++x)
				{
					// (texel centres: the same pattern at every size, so the low-res texture is the same look, only blurrier)
					const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
					const glm::vec3 c = glm::clamp((*shader)(u, v, s), glm::vec3(0.0f), glm::vec3(1.0f));
					uint8_t* p = &img.Pixels[(static_cast<size_t>(y) * size + x) * 4];
					p[0] = static_cast<uint8_t>(std::lround(c.r * 255.0f));
					p[1] = static_cast<uint8_t>(std::lround(c.g * 255.0f));
					p[2] = static_cast<uint8_t>(std::lround(c.b * 255.0f));
					p[3] = 255;
				}
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
