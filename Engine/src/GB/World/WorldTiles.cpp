#include "GB/World/WorldTiles.h"
#include "GB/World/Terrain.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	WorldTiles::WorldTiles(const glm::vec2& min, float tileSize, int tiles, int samplesPerTile)
		: m_Min(min), m_Size(tileSize), m_Tiles(std::max(tiles, 1)), m_Samples(std::max(samplesPerTile, 2))
	{
		m_Resident.assign(static_cast<size_t>(Count()), false);
	}

	int WorldTiles::TileAt(const glm::vec2& p) const
	{
		if (m_Tiles <= 0) return -1;
		const glm::vec2 local = (p - m_Min) / m_Size;
		const int i = std::clamp(static_cast<int>(std::floor(local.x)), 0, m_Tiles - 1), j = std::clamp(static_cast<int>(std::floor(local.y)), 0, m_Tiles - 1);
		return j * m_Tiles + i;
	}

	void WorldTiles::Samples(int tile, int& i0, int& j0, int& i1, int& j1) const
	{
		const int step = m_Samples - 1;
		i0 = (tile % m_Tiles) * step;
		j0 = (tile / m_Tiles) * step;
		i1 = i0 + step;
		j1 = j0 + step;
	}

	float WorldTiles::Distance(int tile, const glm::vec2& p) const
	{
		const glm::vec2 lo = Min(tile), hi = Max(tile);
		const glm::vec2 d = glm::max(glm::max(lo - p, p - hi), glm::vec2(0.0f));
		return glm::length(d);
	}

	WorldTiles::Work WorldTiles::Update(const glm::vec2& viewer, const TileStreamConfig& config)
	{
		Work work;
		std::vector<std::pair<float, int>> wanted;   // (distance, tile): sorted, the nearest first and ties by tile index
		for (int t = 0; t < Count(); ++t)
		{
			const float d = Distance(t, viewer);
			if (m_Resident[static_cast<size_t>(t)] && d > config.DropRadius) { m_Resident[static_cast<size_t>(t)] = false; work.Drop.push_back(t); }
			else if (!m_Resident[static_cast<size_t>(t)] && d <= config.LoadRadius) wanted.emplace_back(d, t);
		}
		std::sort(wanted.begin(), wanted.end());
		for (size_t k = 0; k < wanted.size() && static_cast<int>(work.Build.size()) < std::max(config.MaxBuildsPerFrame, 1); ++k)
		{
			m_Resident[static_cast<size_t>(wanted[k].second)] = true;
			work.Build.push_back(wanted[k].second);
		}
		return work;
	}

	int WorldTiles::ResidentCount() const
	{
		return static_cast<int>(std::count(m_Resident.begin(), m_Resident.end(), true));
	}

	void BuildTileMesh(const Terrain& terrain, const WorldTiles& tiles, int tile, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
	{
		vertices.clear();
		indices.clear();
		const Heightmap& hm = terrain.GetHeightmap();
		int i0, j0, i1, j1;
		tiles.Samples(tile, i0, j0, i1, j1);
		const int last = static_cast<int>(hm.GetResolution()) - 1;
		i1 = std::min(i1, last);
		j1 = std::min(j1, last);
		if (i1 <= i0 || j1 <= j0) return;
		const uint32_t w = static_cast<uint32_t>(i1 - i0 + 1);
		const float tileMeters = 4.0f;   // one detail texture tile (as Terrain::BuildGpu)
		vertices.reserve(static_cast<size_t>(w) * static_cast<size_t>(j1 - j0 + 1));
		for (int j = j0; j <= j1; ++j)
			for (int i = i0; i <= i1; ++i)
			{
				const glm::vec2 pos = hm.SamplePosition(static_cast<uint32_t>(i), static_cast<uint32_t>(j));
				Vertex v;
				v.Position = { pos.x, hm.At(static_cast<uint32_t>(i), static_cast<uint32_t>(j)), pos.y };
				v.Normal = terrain.GetSmoothNormal(pos.x, pos.y);
				v.UV = { pos.x / tileMeters, pos.y / tileMeters };
				v.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
				vertices.push_back(v);
			}
		const uint32_t h = static_cast<uint32_t>(j1 - j0 + 1);
		indices.reserve(static_cast<size_t>(w - 1) * (h - 1) * 6);
		for (uint32_t j = 0; j + 1 < h; ++j)
			for (uint32_t i = 0; i + 1 < w; ++i)
			{
				const uint32_t a = j * w + i, b = a + 1, c = a + w, d = c + 1;
				indices.insert(indices.end(), { a, c, b, b, c, d });   // counter-clockwise seen from above (+Y), as the whole terrain
			}
	}

	int TerrainTileSet::Stream(const Terrain& terrain, WorldTiles& tiles, const glm::vec2& viewer, const TileStreamConfig& config)
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

	void TerrainTileSet::AddToScene(const Terrain& terrain, Scene& scene) const
	{
		for (const std::unique_ptr<Mesh>& m : m_Meshes)
		{
			if (!m) continue;
			RenderObject obj;
			obj.MeshPtr = m.get();
			obj.Mat = terrain.GetMaterial();
			scene.Objects.push_back(std::move(obj));
		}
	}

	size_t TerrainTileSet::Meshes() const
	{
		return static_cast<size_t>(std::count_if(m_Meshes.begin(), m_Meshes.end(), [](const std::unique_ptr<Mesh>& m) { return m != nullptr; }));
	}
}
