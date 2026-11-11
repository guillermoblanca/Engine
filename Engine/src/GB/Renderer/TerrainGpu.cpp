#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/TerrainLayerTextures.h"
#include "GB/World/Terrain.h"
#include "GB/Core/Log.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

namespace GB
{
	TerrainGpu::TerrainGpu(Terrain& terrain) : m_Terrain(&terrain)
	{
		terrain.PrepareRenderData();
		const std::unique_ptr<TerrainRenderData> data = terrain.TakeRenderData();
		const Heightmap& hm = terrain.GetHeightmap();
		const TerrainStyle style = terrain.GetStyle();
		m_Revision = terrain.GetEditRevision();
		terrain.TakeEdits();   // (nothing to upload: the buffers are made from the vertices and the normal map as they are now)
		m_Mesh = std::make_unique<Mesh>(terrain.GetVertices(), data->Indices);
		m_Grain = std::make_unique<Texture2D>(data->Grain, TextureEncoding::SRGB);
		m_Ripples = std::make_unique<Texture2D>(data->Ripples, TextureEncoding::Linear);
		if (terrain.HasSurfaceMaps())
		{
			m_Macro = std::make_unique<Texture2D>(data->Macro, TextureEncoding::SRGB);
			m_NormalMap = std::make_unique<Texture2D>(terrain.GetNormalImage(), TextureEncoding::Linear);
		}
		const float roughness = data->Roughness, rippleDepth = data->RippleDepth;

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
		m_Material.SurfaceNormalMap = m_NormalMap.get();
		m_Material.MacroRect = { hm.GetMin().x, hm.GetMin().y, hm.GetSize(), hm.GetSize() };

		if (terrain.SurfaceLayersArePartition() && m_Macro && m_NormalMap)
			MakeLayerMaterials(terrain);
	}

	namespace
	{
		// T112: a separable Gaussian blur of a field of n x n samples (clamped at the edges).
		std::vector<float> Blur(const std::vector<float>& field, int n, float sigma)
		{
			const int radius = static_cast<int>(std::ceil(sigma * 2.5f));
			std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
			float total = 0.0f;
			for (int k = -radius; k <= radius; ++k) total += kernel[static_cast<size_t>(k + radius)] = std::exp(-0.5f * static_cast<float>(k * k) / (sigma * sigma));
			for (float& k : kernel) k /= total;
			std::vector<float> row(field.size()), out(field.size());
			auto at = [n](int i, int j) { return static_cast<size_t>(std::clamp(j, 0, n - 1)) * static_cast<size_t>(n) + static_cast<size_t>(std::clamp(i, 0, n - 1)); };
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					float s = 0.0f;
					for (int k = -radius; k <= radius; ++k) s += field[at(i + k, j)] * kernel[static_cast<size_t>(k + radius)];
					row[at(i, j)] = s;
				}
			for (int j = 0; j < n; ++j)
				for (int i = 0; i < n; ++i)
				{
					float s = 0.0f;
					for (int k = -radius; k <= radius; ++k) s += row[at(i, j + k)] * kernel[static_cast<size_t>(k + radius)];
					out[at(i, j)] = s;
				}
			return out;
		}

		float Linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
	}

	void TerrainGpu::MakeLayerMaterials(const Terrain& terrain)
	{
		constexpr int kKinds = TerrainLayerMaps::kKinds;
		const Heightmap& hm = terrain.GetHeightmap();
		const int n = static_cast<int>(hm.GetResolution());
		const size_t count = static_cast<size_t>(n) * static_cast<size_t>(n);

		const auto start = std::chrono::steady_clock::now();
		auto msSince = [](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count(); };

		// The weights by kind, read in place (a kind the terrain does not have weighs 0 everywhere).
		std::array<const std::vector<float>*, kKinds> weights{};
		for (const SurfaceLayer& layer : terrain.GetSurfaceLayers())
		{
			const int kind = static_cast<int>(layer.Kind);
			if (kind < 0 || kind >= kKinds || layer.Weights.size() != count) continue;
			weights[static_cast<size_t>(kind)] = &layer.Weights;
		}
		// T112: the snow of a terrain graph (snow_accumulate) has samples that stand alone against their neighbours (its slope, hollow and
		// flow terms are measured on the grid), and a lone sample read bilinearly is a diamond: the partial snow came out as rows of rhombi.
		// The snow is low-passed over ~1.2 samples (a round blob, not a diamond) and the other layers share what it gave up or took in their
		// own proportions (the partition still sums to 1); where there was only snow, they share it as their own weights blurred the same way
		// around that sample (only there, at the snow's edge: under deep snow they get nothing either way).
		const size_t snow = static_cast<size_t>(SurfaceLayerKind::Snow);
		constexpr float kSnowSigma = 1.2f;
		std::vector<float> softSnow;
		if (weights[snow]) softSnow = Blur(*weights[snow], n, kSnowSigma);
		const int radius = static_cast<int>(std::ceil(kSnowSigma * 2.5f));
		std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
		for (int k = -radius; k <= radius; ++k) kernel[static_cast<size_t>(k + radius)] = std::exp(-0.5f * static_cast<float>(k * k) / (kSnowSigma * kSnowSigma));

		std::vector<Image> splat(4);
		for (Image& image : splat)
		{
			image.Width = image.Height = static_cast<uint32_t>(n);
			image.Pixels.assign(count * 4, 0);
		}
		auto byte = [](float w) { return static_cast<uint8_t>(std::clamp(w, 0.0f, 1.0f) * 255.0f + 0.5f); };
		// (rows in parallel: each writes its own samples)
		auto fillRows = [&](int j0, int j1) {
			std::array<float, kKinds> w{};
			for (int j = j0; j < j1; ++j)
				for (int i = 0; i < n; ++i)
				{
					const size_t c = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(i);
					for (size_t k = 0; k < kKinds; ++k) w[k] = weights[k] ? (*weights[k])[c] : 0.0f;
					if (!softSnow.empty())
					{
						const float rest = 1.0f - std::clamp(w[snow], 0.0f, 1.0f), restSoft = 1.0f - std::clamp(softSnow[c], 0.0f, 1.0f);
						if (rest > 0.05f)
						{
							for (size_t k = 0; k < kKinds; ++k)
								if (k != snow) w[k] *= restSoft / rest;
						}
						else
						{
							std::array<float, kKinds> local{};
							float sum = 0.0f;
							if (restSoft > 0.5f / 255.0f)
								for (int dj = -radius; dj <= radius; ++dj)
									for (int di = -radius; di <= radius; ++di)
									{
										const size_t at = static_cast<size_t>(std::clamp(j + dj, 0, n - 1)) * static_cast<size_t>(n) + static_cast<size_t>(std::clamp(i + di, 0, n - 1));
										const float g = kernel[static_cast<size_t>(di + radius)] * kernel[static_cast<size_t>(dj + radius)];
										for (size_t k = 0; k < kKinds; ++k)
											if (k != snow && weights[k]) { local[k] += (*weights[k])[at] * g; sum += (*weights[k])[at] * g; }
									}
							for (size_t k = 0; k < kKinds; ++k)
								if (k != snow) w[k] = sum > 1e-6f ? local[k] / sum * restSoft : 0.0f;
						}
						w[snow] = softSnow[c];
					}
					for (size_t k = 0; k < kKinds; ++k) splat[k / 4].Pixels[c * 4 + k % 4] = byte(w[k]);
				}
		};
		{
			const int workers = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
			std::vector<std::thread> threads;
			for (int t = 1; t < workers; ++t) threads.emplace_back(fillRows, n * t / workers, n * (t + 1) / workers);
			fillRows(0, n / workers);
			for (std::thread& t : threads) t.join();
		}
		// (channel 13, the rock hardness, stays 0: the terrain does not carry the graph's splat_hardness)
		const double splatMs = msSince(start);
		m_Splat = std::make_unique<TextureArray>(std::vector<const Image*>{ &splat[0], &splat[1], &splat[2], &splat[3] }, TextureEncoding::Linear, false);

		const auto mapsStart = std::chrono::steady_clock::now();
		const std::shared_ptr<const TerrainLayerTextures::Maps> maps = TerrainLayerTextures::Get(TerrainLayerTextures::Size(false));
		const double mapsMs = msSince(mapsStart);
		std::vector<const Image*> albedo, surface;
		for (int k = 0; k < kKinds; ++k)
		{
			albedo.push_back(&maps->Albedo[static_cast<size_t>(k)]);
			surface.push_back(&maps->Surface[static_cast<size_t>(k)]);
		}
		m_LayerAlbedo = std::make_unique<TextureArray>(albedo, TextureEncoding::SRGB, true);
		m_LayerSurface = std::make_unique<TextureArray>(surface, TextureEncoding::Linear, true);
		if (!m_Splat->IsValid() || !m_LayerAlbedo->IsValid() || !m_LayerSurface->IsValid()) return;

		m_LayerMaps = {};
		m_LayerMaps.Splat = m_Splat.get();
		m_LayerMaps.Albedo = m_LayerAlbedo.get();
		m_LayerMaps.Surface = m_LayerSurface.get();
		m_LayerMaps.SplatRect = { hm.GetMin().x, hm.GetMin().y, hm.GetSpacing(), static_cast<float>(n) };
		for (int k = 0; k < kKinds; ++k)
		{
			const TerrainLayerTextures::Look& look = TerrainLayerTextures::Looks()[static_cast<size_t>(k)];
			m_LayerMaps.Params[k] = { 1.0f / look.TileMetres, look.Triplanar ? 1.0f : 0.0f, look.Contrast, 0.0f };
			m_LayerMaps.MacroColor[k] = { Linear(look.MacroColor.r), Linear(look.MacroColor.g), Linear(look.MacroColor.b), 0.0f };
		}
		// The layers carry the colour, the relief and the roughness: the grain and the ripples of the style are not drawn under them.
		m_Material.AlbedoMap = nullptr;
		m_Material.NormalMap = nullptr;
		m_Material.Roughness = 1.0f;
		m_Material.TerrainLayers = &m_LayerMaps;
		GB_CORE_INFO("terrain layers: splat {}^2 in {:.0f} ms, layer maps ready in {:.0f} ms, {:.0f} ms in all", n, splatMs, mapsMs, msSince(start));
	}

	void TerrainGpu::Sync()
	{
		if (m_Terrain->GetEditRevision() == m_Revision) return;
		m_Revision = m_Terrain->GetEditRevision();
		const TerrainEdits edits = m_Terrain->TakeEdits();
		const std::vector<Vertex>& vertices = m_Terrain->GetVertices();
		if (edits.FineLast >= edits.FineFirst && edits.FineLast < vertices.size())   // the refined vertices over the edited texels
			m_Mesh->UpdateVertices(edits.FineFirst, &vertices[edits.FineFirst], edits.FineLast - edits.FineFirst + 1);
		const Image& normals = m_Terrain->GetNormalImage();
		if (m_NormalMap && edits.NormalX1 >= edits.NormalX0 && edits.NormalY1 >= edits.NormalY0)   // that area of the normal map
		{
			const uint32_t w = static_cast<uint32_t>(edits.NormalX1 - edits.NormalX0 + 1), h = static_cast<uint32_t>(edits.NormalY1 - edits.NormalY0 + 1);
			std::vector<uint8_t> patch(static_cast<size_t>(w) * h * 4);
			for (uint32_t y = 0; y < h; ++y)
				std::copy_n(&normals.Pixels[(static_cast<size_t>(edits.NormalY0 + static_cast<int>(y)) * normals.Width + static_cast<size_t>(edits.NormalX0)) * 4],
					static_cast<size_t>(w) * 4, &patch[static_cast<size_t>(y) * w * 4]);
			m_NormalMap->Update(static_cast<uint32_t>(edits.NormalX0), static_cast<uint32_t>(edits.NormalY0), w, h, patch.data());
		}
		const int n = static_cast<int>(m_Terrain->GetHeightmap().GetResolution());
		for (int j = edits.RowJ0; j <= edits.RowJ1; ++j)   // the grid vertices, a row at a time
		{
			const size_t first = static_cast<size_t>(j) * static_cast<size_t>(n) + static_cast<size_t>(edits.RowI0);
			m_Mesh->UpdateVertices(static_cast<uint32_t>(first), &vertices[first], static_cast<uint32_t>(edits.RowI1 - edits.RowI0 + 1));
		}
	}

	void TerrainGpu::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		const std::vector<TerrainTile>& tiles = m_Terrain->GetTiles();
		if (tiles.size() <= 1)
		{
			RenderObject obj;
			obj.MeshPtr = m_Mesh.get();
			obj.Mat = m_Material;
			obj.Transform = root;
			scene.Objects.push_back(std::move(obj));
			return;
		}
		for (const TerrainTile& tile : tiles)   // one object per tile, culled by its bounds (Terrain::SortIntoTiles)
		{
			RenderObject obj;
			obj.MeshPtr = m_Mesh.get();
			obj.Mat = m_Material;
			obj.Transform = root;
			obj.IndexFirst = tile.First;
			obj.IndexCount = tile.Count;
			obj.BoundingCenter = tile.Center;
			obj.BoundingRadius = tile.Radius;
			scene.Objects.push_back(std::move(obj));
		}
	}
}
