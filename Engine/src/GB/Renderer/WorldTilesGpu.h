#pragma once
#include "GB/Renderer/Material.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include "GB/World/WorldTiles.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	class Terrain;

	// The GPU side of GB::WorldTiles (GB/World/WorldTiles.h, pure CPU): a mesh per resident tile (BuildTileMesh's vertices), drawn with the
	// terrain's material (needs an OpenGL context to build; AddToScene takes the material of the terrain's TerrainGpu). The tiles do not know it
	// exists (docs/ARQUITECTURA_REVISION.md, H5). Not wired into the editor yet: the editor still draws the whole terrain (see docs/mundo.md).
	class WorldTilesGpu
	{
	public:
		// Builds and drops what WorldTiles::Update says; returns the tiles built this frame.
		int Stream(const Terrain& terrain, WorldTiles& tiles, const glm::vec2& viewer, const TileStreamConfig& config = TileStreamConfig());
		void AddToScene(const Material& material, Scene& scene) const;   // material: the terrain's (TerrainGpu::GetMaterial)
		size_t Meshes() const;
	private:
		std::vector<std::unique_ptr<Mesh>> m_Meshes;   // by tile
	};
}
