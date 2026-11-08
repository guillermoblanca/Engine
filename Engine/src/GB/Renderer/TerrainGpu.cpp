#include "GB/Renderer/TerrainGpu.h"
#include "GB/World/Terrain.h"
#include <algorithm>
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
