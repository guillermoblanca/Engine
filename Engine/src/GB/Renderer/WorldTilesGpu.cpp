#include "GB/Renderer/WorldTilesGpu.h"
#include "GB/World/Terrain.h"
#include <algorithm>

namespace GB
{
	int WorldTilesGpu::Stream(const Terrain& terrain, WorldTiles& tiles, const glm::vec2& viewer, const TileStreamConfig& config)
	{
		if (m_Meshes.size() != static_cast<size_t>(tiles.Count())) m_Meshes.resize(static_cast<size_t>(tiles.Count()));
		const WorldTiles::Work work = tiles.Update(viewer, config);
		for (int t : work.Drop) m_Meshes[static_cast<size_t>(t)].reset();
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		for (int t : work.Build)
		{
			BuildTileMesh(terrain, tiles, t, vertices, indices);
			if (!vertices.empty()) m_Meshes[static_cast<size_t>(t)] = std::make_unique<Mesh>(vertices, indices);
		}
		return static_cast<int>(work.Build.size());
	}

	void WorldTilesGpu::AddToScene(const Material& material, Scene& scene) const
	{
		for (const std::unique_ptr<Mesh>& m : m_Meshes)
		{
			if (!m) continue;
			RenderObject obj;
			obj.MeshPtr = m.get();
			obj.Mat = material;
			scene.Objects.push_back(std::move(obj));
		}
	}

	size_t WorldTilesGpu::Meshes() const
	{
		return static_cast<size_t>(std::count_if(m_Meshes.begin(), m_Meshes.end(), [](const std::unique_ptr<Mesh>& m) { return m != nullptr; }));
	}
}
