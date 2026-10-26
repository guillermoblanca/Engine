#include "Terrain.h"
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		float Hash(int x, int y, uint32_t seed)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u + seed * 2246822519u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
		}

		float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

		// Value noise in [0,1]. `period` > 0 makes it tile every `period` cells (for seamless textures).
		float ValueNoise(float x, float y, uint32_t seed, int period = 0)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float fx = Smooth(x - static_cast<float>(ix)), fy = Smooth(y - static_cast<float>(iy));
			auto wrap = [&](int v) { return period > 0 ? ((v % period) + period) % period : v; };
			const float a = Hash(wrap(ix), wrap(iy), seed), b = Hash(wrap(ix + 1), wrap(iy), seed);
			const float c = Hash(wrap(ix), wrap(iy + 1), seed), d = Hash(wrap(ix + 1), wrap(iy + 1), seed);
			return glm::mix(glm::mix(a, b, fx), glm::mix(c, d, fx), fy);
		}

		float Fbm(float x, float y, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			for (int o = 0; o < octaves; ++o, amp *= 0.5f, x *= 2.0f, y *= 2.0f)
			{
				sum += amp * ValueNoise(x, y, seed + static_cast<uint32_t>(o) * 101u);
				norm += amp;
			}
			return sum / norm;
		}

		// Tileable fractal noise for textures: `cells` base cells per tile, octaves double the frequency.
		float TileFbm(float u, float v, int cells, int octaves, uint32_t seed)
		{
			float sum = 0.0f, amp = 0.5f, norm = 0.0f;
			int period = cells;
			for (int o = 0; o < octaves; ++o, amp *= 0.5f, period *= 2)
			{
				sum += amp * ValueNoise(u * static_cast<float>(period), v * static_cast<float>(period), seed + static_cast<uint32_t>(o) * 31u, period);
				norm += amp;
			}
			return sum / norm;
		}

		// Dune cross-section: phase 0..1 along the wind; 0 = trough, `crest` = crest, back to 0 on the steep lee side.
		float DuneProfile(float phase, float crest, float sharpness)
		{
			float cosine, triangle;
			if (phase < crest)
			{
				const float t = phase / crest;
				cosine = 0.5f - 0.5f * std::cos(kPi * t);
				triangle = t;
			}
			else
			{
				const float t = (phase - crest) / (1.0f - crest);
				cosine = 0.5f + 0.5f * std::cos(kPi * t);
				triangle = 1.0f - t;
			}
			return glm::mix(cosine, triangle, sharpness);
		}
	}

	// ------------------------------------------------------------------ Heightmap
	Heightmap::Heightmap(uint32_t resolution, float size, const glm::vec2& center)
		: m_Resolution(std::max(resolution, 2u)), m_Size(size), m_Center(center),
		  m_Heights(static_cast<size_t>(m_Resolution) * m_Resolution, 0.0f)
	{
	}

	bool Heightmap::Contains(float x, float z) const
	{
		const glm::vec2 lo = GetMin(), hi = GetMax();
		return x >= lo.x && x <= hi.x && z >= lo.y && z <= hi.y;
	}

	glm::vec2 Heightmap::SamplePosition(uint32_t i, uint32_t j) const
	{
		return GetMin() + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * GetSpacing();
	}

	float Heightmap::GetHeight(float x, float z) const
	{
		const glm::vec2 lo = GetMin();
		const float maxIndex = static_cast<float>(m_Resolution - 1);
		const float fx = glm::clamp((x - lo.x) / GetSpacing(), 0.0f, maxIndex);
		const float fz = glm::clamp((z - lo.y) / GetSpacing(), 0.0f, maxIndex);
		const uint32_t i0 = std::min(static_cast<uint32_t>(fx), m_Resolution - 2), j0 = std::min(static_cast<uint32_t>(fz), m_Resolution - 2);
		const float tx = fx - static_cast<float>(i0), tz = fz - static_cast<float>(j0);
		return glm::mix(glm::mix(At(i0, j0), At(i0 + 1, j0), tx), glm::mix(At(i0, j0 + 1), At(i0 + 1, j0 + 1), tx), tz);
	}

	glm::vec3 Heightmap::GetNormal(float x, float z) const
	{
		const float e = GetSpacing();
		const float dx = GetHeight(x + e, z) - GetHeight(x - e, z);
		const float dz = GetHeight(x, z + e) - GetHeight(x, z - e);
		return glm::normalize(glm::vec3(-dx, 2.0f * e, -dz));
	}

	float Heightmap::GetMinHeight() const { return *std::min_element(m_Heights.begin(), m_Heights.end()); }
	float Heightmap::GetMaxHeight() const { return *std::max_element(m_Heights.begin(), m_Heights.end()); }

	// ------------------------------------------------------------------ generators
	std::unique_ptr<Terrain> Terrain::CreateDunes(const DuneParams& p, float size, uint32_t resolution, TerrainStyle style)
	{
		std::unique_ptr<Terrain> t(new Terrain(resolution, size));
		t->m_WindAngle = glm::radians(p.WindAngleDegrees);
		const glm::vec2 wind(std::cos(t->m_WindAngle), std::sin(t->m_WindAngle));
		Heightmap& hm = t->m_Heightmap;

		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				// Crest lines meander: shift the along-wind coordinate by low-frequency noise.
				const float warp = (Fbm(pos.x / p.WarpScale, pos.y / p.WarpScale, 3, p.Seed) - 0.5f) * 2.0f * p.Warp;
				const float s = glm::dot(pos, wind) / p.Wavelength + warp;
				const float phase = s - std::floor(s);
				// Neighbouring dune ridges differ in size.
				const float cell = std::floor(s);
				const float across = glm::dot(pos, glm::vec2(-wind.y, wind.x));
				const float size01 = ValueNoise(cell * 3.7f, across / 160.0f, p.Seed + 7);
				const float amplitude = p.Height * (1.0f + p.SizeVariation * (size01 - 0.5f) * 2.0f);
				const float hills = (Fbm(pos.x / 170.0f, pos.y / 170.0f, 3, p.Seed + 3) - 0.5f) * 2.0f * p.BaseHills;
				hm.At(i, j) = amplitude * DuneProfile(phase, p.CrestPosition, p.CrestSharpness) + hills;
			}
		}

		// Crest points: local maxima along the wind direction.
		const float step = std::max(p.Wavelength * 0.08f, hm.GetSpacing() * 2.0f);
		const float minHeight = hm.GetMinHeight() + 0.55f * (hm.GetMaxHeight() - hm.GetMinHeight());
		for (uint32_t j = 2; j + 2 < hm.GetResolution(); j += 3)
		{
			for (uint32_t i = 2; i + 2 < hm.GetResolution(); i += 3)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				const float h = hm.At(i, j);
				if (h < minHeight)
					continue;
				const float ahead = hm.GetHeight(pos.x + wind.x * step, pos.y + wind.y * step);
				const float behind = hm.GetHeight(pos.x - wind.x * step, pos.y - wind.y * step);
				if (h >= ahead && h >= behind)
					t->m_Crests.push_back({ pos.x, h, pos.y });
			}
		}
		t->Finish(style, p.Wavelength);
		return t;
	}

	std::unique_ptr<Terrain> Terrain::CreateHills(const HillParams& p, float size, uint32_t resolution, TerrainStyle style)
	{
		std::unique_ptr<Terrain> t(new Terrain(resolution, size));
		Heightmap& hm = t->m_Heightmap;
		for (uint32_t j = 0; j < hm.GetResolution(); ++j)
		{
			for (uint32_t i = 0; i < hm.GetResolution(); ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				float n = Fbm(pos.x / p.Scale, pos.y / p.Scale, p.Octaves, p.Seed);
				n = glm::clamp((n - 0.25f) / 0.6f, 0.0f, 1.0f);
				n = glm::mix(n, n * n * (3.0f - 2.0f * n), p.Flatten + 0.35f);   // rounded hilltops, flatter valleys
				hm.At(i, j) = n * p.Height;
			}
		}
		t->Finish(style, 8.0f);
		return t;
	}

	// ------------------------------------------------------------------ mesh + material
	void Terrain::Finish(TerrainStyle style, float wavelengthMeters)
	{
		m_Style = style;
		const Heightmap& hm = m_Heightmap;
		const uint32_t n = hm.GetResolution();
		const float tileMeters = 4.0f;   // one detail texture tile

		std::vector<Vertex> vertices(static_cast<size_t>(n) * n);
		for (uint32_t j = 0; j < n; ++j)
		{
			for (uint32_t i = 0; i < n; ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(i, j);
				Vertex& v = vertices[static_cast<size_t>(j) * n + i];
				v.Position = { pos.x, hm.At(i, j), pos.y };
				v.Normal = hm.GetNormal(pos.x, pos.y);
				v.UV = { pos.x / tileMeters, pos.y / tileMeters };
				v.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
			}
		}
		std::vector<uint32_t> indices;
		indices.reserve(static_cast<size_t>(n - 1) * (n - 1) * 6);
		for (uint32_t j = 0; j + 1 < n; ++j)
		{
			for (uint32_t i = 0; i + 1 < n; ++i)
			{
				const uint32_t i0 = j * n + i, i1 = i0 + 1, i2 = i0 + n, i3 = i2 + 1;
				indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });   // counter-clockwise seen from above (+Y)
			}
		}
		m_Mesh = std::make_unique<Mesh>(vertices, indices);

		// ---- palette
		glm::vec3 sunlit, shade, crest;
		float grainContrast = 0.22f, rippleDepth = 1.0f, roughness = 0.9f;
		switch (style)
		{
		case TerrainStyle::Desert: sunlit = { 0.80f, 0.62f, 0.38f }; shade = { 0.58f, 0.38f, 0.22f }; crest = { 0.88f, 0.72f, 0.48f }; break;
		case TerrainStyle::Snow: sunlit = { 0.78f, 0.82f, 0.88f }; shade = { 0.50f, 0.58f, 0.72f }; crest = { 0.86f, 0.89f, 0.94f }; rippleDepth = 0.35f; roughness = 0.75f; grainContrast = 0.08f; break;
		default: sunlit = { 0.30f, 0.45f, 0.16f }; shade = { 0.16f, 0.26f, 0.09f }; crest = { 0.42f, 0.52f, 0.20f }; rippleDepth = 0.0f; grainContrast = 0.3f; roughness = 0.95f; break;
		}

		// ---- detail grain (grayscale, tileable)
		{
			constexpr uint32_t R = 256;
			Image grain = Image::Solid(R, R, 255, 255, 255);
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					const float u = static_cast<float>(x) / R, v = static_cast<float>(y) / R;
					const float f = 0.55f * TileFbm(u, v, 24, 3, 11) + 0.45f * ValueNoise(u * 128.0f, v * 128.0f, 29, 128);
					const float g = glm::clamp(1.0f - grainContrast + grainContrast * 2.0f * f, 0.0f, 1.0f);
					uint8_t* p = &grain.Pixels[(static_cast<size_t>(y) * R + x) * 4];
					p[0] = p[1] = p[2] = static_cast<uint8_t>(g * 255.0f + 0.5f);
				}
			m_Grain = std::make_unique<Texture2D>(grain, TextureEncoding::SRGB);
		}

		// ---- wind ripples (tangent-space normal map, tileable). Wave vector along the wind.
		{
			constexpr uint32_t R = 256;
			constexpr int kWaves = 14;                         // ripples per tile
			const int kx = static_cast<int>(std::lround(std::cos(m_WindAngle) * kWaves));
			const int kz = static_cast<int>(std::lround(std::sin(m_WindAngle) * kWaves));
			std::vector<float> h(static_cast<size_t>(R) * R);
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					const float u = static_cast<float>(x) / R, v = static_cast<float>(y) / R;
					const float wobble = (TileFbm(u, v, 6, 3, 41) - 0.5f) * 1.2f;
					const float phase = 2.0f * kPi * (static_cast<float>(kx) * u + static_cast<float>(kz) * v) + wobble * 6.0f;
					// Slightly asymmetric ripple: gentle windward side, steeper lee side.
					const float ripple = std::sin(phase) + 0.25f * std::sin(2.0f * phase + 0.8f);
					h[static_cast<size_t>(y) * R + x] = ripple * 0.35f + (TileFbm(u, v, 40, 2, 53) - 0.5f) * 0.35f;
				}
			Image normal = Image::Solid(R, R, 128, 128, 255);
			const float strength = 2.2f * rippleDepth;
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					auto at = [&](int xx, int yy) { return h[static_cast<size_t>((yy + static_cast<int>(R)) % R) * R + ((xx + static_cast<int>(R)) % R)]; };
					const float dhdu = (at(static_cast<int>(x) + 1, static_cast<int>(y)) - at(static_cast<int>(x) - 1, static_cast<int>(y))) * 0.5f * strength;
					const float dhdv = (at(static_cast<int>(x), static_cast<int>(y) + 1) - at(static_cast<int>(x), static_cast<int>(y) - 1)) * 0.5f * strength;
					// Tangent = +X (u), bitangent = cross(N, T) = -Z, i.e. against +v: green carries +dh/dv.
					glm::vec3 nrm = glm::normalize(glm::vec3(-dhdu, dhdv, 1.0f));
					uint8_t* p = &normal.Pixels[(static_cast<size_t>(y) * R + x) * 4];
					p[0] = static_cast<uint8_t>((nrm.x * 0.5f + 0.5f) * 255.0f + 0.5f);
					p[1] = static_cast<uint8_t>((nrm.y * 0.5f + 0.5f) * 255.0f + 0.5f);
					p[2] = static_cast<uint8_t>((nrm.z * 0.5f + 0.5f) * 255.0f + 0.5f);
				}
			m_Ripples = std::make_unique<Texture2D>(normal, TextureEncoding::Linear);
		}

		// ---- macro color over the whole terrain: slope and altitude pick between sunlit, shaded and crest colors.
		{
			constexpr uint32_t R = 1024;
			Image macro = Image::Solid(R, R, 255, 255, 255);
			const float lo = hm.GetMinHeight(), hi = std::max(hm.GetMaxHeight(), lo + 1e-3f);
			const glm::vec2 minCorner = hm.GetMin();
			for (uint32_t y = 0; y < R; ++y)
				for (uint32_t x = 0; x < R; ++x)
				{
					const float u = (static_cast<float>(x) + 0.5f) / R, v = (static_cast<float>(y) + 0.5f) / R;
					const float wx = minCorner.x + u * hm.GetSize(), wz = minCorner.y + v * hm.GetSize();
					const glm::vec3 nrm = hm.GetNormal(wx, wz);
					const float slope = 1.0f - nrm.y;
					const float altitude = (hm.GetHeight(wx, wz) - lo) / (hi - lo);
					const float patch = Fbm(wx / 38.0f, wz / 38.0f, 4, 71);
					glm::vec3 c = glm::mix(sunlit, shade, glm::clamp(slope * 4.5f, 0.0f, 1.0f));
					c = glm::mix(c, crest, glm::clamp((altitude - 0.55f) * 2.4f, 0.0f, 1.0f) * (1.0f - glm::clamp(slope * 6.0f, 0.0f, 1.0f)));
					c *= 0.86f + 0.28f * patch;
					uint8_t* p = &macro.Pixels[(static_cast<size_t>(y) * R + x) * 4];
					p[0] = static_cast<uint8_t>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
					p[1] = static_cast<uint8_t>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
					p[2] = static_cast<uint8_t>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			m_Macro = std::make_unique<Texture2D>(macro, TextureEncoding::SRGB);
		}

		m_Material = {};
		m_Material.Albedo = glm::vec3(1.0f);
		m_Material.Roughness = roughness;
		m_Material.AlbedoMap = m_Grain.get();
		m_Material.NormalMap = rippleDepth > 0.0f ? m_Ripples.get() : nullptr;
		m_Material.UVScale = 1.0f;
		m_Material.HeightScale = 0.0f;
		m_Material.CavityStrength = 0.0f;
		// Ground marks: sand holds shallow, compacted prints; snow takes deep, blue-shaded ones.
		switch (style)
		{
		case TerrainStyle::Desert: m_Material.TrailDepth = 0.07f; m_Material.TrailTint = { 0.60f, 0.52f, 0.44f }; m_Material.TrailNormalBoost = 3.5f; break;
		case TerrainStyle::Snow: m_Material.TrailDepth = 0.14f; m_Material.TrailTint = { 0.70f, 0.80f, 0.96f }; m_Material.TrailNormalBoost = 3.0f; break;
		default: m_Material.TrailDepth = 0.05f; m_Material.TrailTint = { 0.62f, 0.70f, 0.50f }; m_Material.TrailNormalBoost = 3.0f; break;
		}
		m_Material.MacroMap = m_Macro.get();
		m_Material.MacroRect = { hm.GetMin().x, hm.GetMin().y, hm.GetSize(), hm.GetSize() };
		(void)wavelengthMeters;
	}

	std::function<float(float, float)> Terrain::MakeGroundFunction() const
	{
		const Heightmap* hm = &m_Heightmap;
		return [hm](float x, float z) { return hm->GetHeight(x, z); };
	}

	void Terrain::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		RenderObject obj;
		obj.MeshPtr = m_Mesh.get();
		obj.Mat = m_Material;
		obj.Transform = root;
		scene.Objects.push_back(std::move(obj));
	}
}
