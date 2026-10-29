#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	class Terrain;

	// A bigger world cut in tiles (H16, groundwork for the streaming). The world of WorldDesc::Tiles > 1 is generated on the CPU as one seamless
	// heightmap; what is cut by tile is what costs memory and draw time: the GPU meshes of the terrain (built near the viewer, dropped far away), and
	// later the objects and the agents of each tile. Everything here but TerrainTileSet is CPU only and deterministic (tests/worlds_tiles in
	// scale_test.cpp).
	struct TileStreamConfig
	{
		float LoadRadius = 260.0f;    // m from the viewer to the nearest point of a tile: closer, its mesh is wanted
		float DropRadius = 340.0f;    // farther than this, it goes (the gap between the two keeps a tile at the border from coming and going)
		int MaxBuildsPerFrame = 1;    // meshes built in one frame (the nearest first): moving fast never stalls a frame with several
	};

	class WorldTiles
	{
	public:
		WorldTiles() = default;
		// `min`: the world's corner (x, z); `tileSize` m; `tiles` per side. The heightmap has (samplesPerTile - 1) x tiles + 1 samples per side.
		WorldTiles(const glm::vec2& min, float tileSize, int tiles, int samplesPerTile);
		int PerSide() const { return m_Tiles; }
		int Count() const { return m_Tiles * m_Tiles; }
		float TileSize() const { return m_Size; }
		glm::vec2 Min(int tile) const { return m_Min + glm::vec2(static_cast<float>(tile % m_Tiles), static_cast<float>(tile / m_Tiles)) * m_Size; }
		glm::vec2 Max(int tile) const { return Min(tile) + glm::vec2(m_Size); }
		// The tile under a point (clamped to the world), and the heightmap samples [i0, i1] x [j0, j1] it spans (borders shared with its neighbours).
		int TileAt(const glm::vec2& p) const;
		void Samples(int tile, int& i0, int& j0, int& i1, int& j1) const;
		// Metres from a point to the nearest point of a tile (0 inside).
		float Distance(int tile, const glm::vec2& p) const;

		// ---- streaming: which tiles have their mesh. Update marks what should be resident from the viewer and returns this frame's work: the tiles to
		// build (nearest first, at most MaxBuildsPerFrame) and those to drop. The caller builds and drops them (TerrainTileSet does).
		struct Work { std::vector<int> Build, Drop; };
		Work Update(const glm::vec2& viewer, const TileStreamConfig& config = TileStreamConfig());
		bool Resident(int tile) const { return tile >= 0 && tile < Count() && m_Resident[static_cast<size_t>(tile)]; }
		int ResidentCount() const;

	private:
		glm::vec2 m_Min{ 0.0f };
		float m_Size = 0.0f;
		int m_Tiles = 0, m_Samples = 0;
		std::vector<bool> m_Resident;
	};

	// The vertices and triangles of one tile of a terrain: its heightmap samples, with the normals and UVs the whole terrain's mesh would give them,
	// so the edges of two neighbouring tiles are the same vertices (no seam, no crack). CPU only.
	void BuildTileMesh(const Terrain& terrain, const WorldTiles& tiles, int tile, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

	// The GPU side: a mesh per resident tile, drawn with the terrain's material (needs an OpenGL context to build; the terrain's BuildGpu must have
	// made its material). Not wired into the editor yet: the editor still draws the whole terrain (see docs/mundo.md).
	class TerrainTileSet
	{
	public:
		// Builds and drops what WorldTiles::Update says; returns the tiles built this frame.
		int Stream(const Terrain& terrain, WorldTiles& tiles, const glm::vec2& viewer, const TileStreamConfig& config = TileStreamConfig());
		void AddToScene(const Terrain& terrain, Scene& scene) const;
		size_t Meshes() const;
	private:
		std::vector<std::unique_ptr<Mesh>> m_Meshes;   // by tile
	};
}
