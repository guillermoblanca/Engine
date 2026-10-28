#pragma once
#include "GB/World/WorldGen.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The regions of a generated world (H12, the first step of H16): the map cut into a dozen or so named areas ("North Woods", "Central Meadows",
	// "Lake"), each with a summary the AI and the panels read: its area, its dominant biome, how much water it has and how near, the wild resources
	// that grow in it and how dangerous it is. A clan knows the world region by region (GB/Game/Exploration.h): it sends expeditions to the regions it
	// does not know and moves its camp to a better one when its own runs dry.
	//
	// Pure CPU and deterministic: built from the baked biome map of the world (GeneratedWorld::Maps) and the wild nodes the seed plans
	// (PlanWorldResources), so nothing of it is saved: the seed, the generator version and the density of the wild resources give it back.
	//
	// How it cuts (RegionMap::Build):
	//  1. The map is split into square tiles of `Tile` metres. Each tile takes the biome most of its samples have (Water when more than `WaterShare`
	//     of them are water).
	//  2. Neighbouring tiles (4-connected) of the same biome in the same block of `BlockTiles` x `BlockTiles` tiles are one cluster: biome clusters,
	//     never larger than a block, so a forest that covers half the map is several regions, not one (step 3 may add a few tiles past it).
	//  3. A cluster of fewer than `MinTiles` tiles joins the neighbour it shares the longest border with (ties: the lowest id), until none is left.
	//  4. The regions are numbered by their first tile, row by row; named by where they lie from the middle of the map and by their dominant biome.

	// The rules of the cut and of the danger (the "regions" part of the "exploration" content section, see GB/Game/Exploration.h).
	struct RegionRules
	{
		float Tile = 30.0f;            // metres per tile side
		int BlockTiles = 4;            // a biome cluster never spans more than this many tiles across (in x or z) before small ones join it
		int MinTiles = 3;              // a smaller cluster joins a neighbour
		float WaterShare = 0.5f;       // a tile with more water than this is a water tile
		float ScoutCell = 10.0f;       // metres per side of the cells a clan marks as seen (the explored share of a region is of these cells)
		float NearWater = 12.0f;       // land this close to the water counts as "by the water" (RegionSummary::NearWater)
		std::map<std::string, float> Danger;   // biome name -> its danger, 0..1 (the region's is the mean over its ground)
		float PredatorDanger = 0.15f;  // what each predator living in the region adds to its danger
		std::map<std::string, std::string> Names;   // biome name -> what a region of it is called ("Forest" -> "Woods")
		bool operator==(const RegionRules&) const = default;
	};
	RegionRules DefaultRegionRules();   // assets/data/era1.json says the same (the "regions" part of its "exploration" section)

	// Something placed in the world that a region counts: a wild resource node by its type ("clay_bank").
	struct RegionThing
	{
		std::string Kind;
		glm::vec2 Pos{ 0.0f };
	};

	struct RegionSummary
	{
		int Id = -1;
		std::string Name;                       // "North Woods", "Lake", "Central Meadows 2"
		Biome Dominant = Biome::Meadow;         // the biome most of its ground has
		bool Water = false;                     // a lake (or a broad river): not a place to go to
		int Tiles = 0;
		float Area = 0.0f;                      // square metres inside the map
		glm::vec2 Min{ 0.0f }, Max{ 0.0f };     // bounds (x, z)
		glm::vec2 Centroid{ 0.0f };             // mean of its samples (may fall outside it on a crescent)
		glm::vec2 Anchor{ 0.0f };               // its dry ground nearest to the centroid: where a party heads for
		std::array<float, 7> Biomes{};          // share of its samples of each biome (indexed by Biome)
		float WaterShare = 0.0f;                // share of its samples that are water
		float NearWater = 0.0f;                 // share of its dry ground within RegionRules::NearWater of the water
		float WaterDistance = 1e9f;             // metres from its anchor to the water
		std::map<std::string, int> Resources;   // wild node type -> how many grow in it
		float BaseDanger = 0.0f;                // from its biomes (RegionRules::Danger)
		int Predators = 0;                      // predators living in it now (SetPredators)
		float Danger = 0.0f;                    // 0..1: BaseDanger plus the predators
		std::vector<int> Neighbours;            // ids, in order
	};

	class RegionMap
	{
	public:
		// Cuts a baked biome map into regions and counts `things` (wild nodes) into them. Empty map -> no regions.
		static RegionMap Build(const BiomeMap& maps, const RegionRules& rules, const std::vector<RegionThing>& things = {});
		// The same for a generated world (its baked maps).
		static RegionMap Build(const GeneratedWorld& world, const RegionRules& rules, const std::vector<RegionThing>& things = {});

		bool Empty() const { return m_Regions.empty(); }
		const RegionRules& Rules() const { return m_Rules; }
		const std::vector<RegionSummary>& Regions() const { return m_Regions; }
		const RegionSummary* Find(int id) const { return id >= 0 && id < static_cast<int>(m_Regions.size()) ? &m_Regions[static_cast<size_t>(id)] : nullptr; }
		// By id ("3") or by name, ignoring case ("north woods"); nullptr when none.
		const RegionSummary* Find(const std::string& idOrName) const;
		// The region a point is in (clamped to the map); -1 on an empty map.
		int RegionAt(const glm::vec2& p) const;

		// The tiles: TilesX x TilesZ, row by row (z); the region of each.
		int TilesX() const { return m_TilesX; }
		int TilesZ() const { return m_TilesZ; }
		int TileAt(const glm::vec2& p) const;
		int TileRegion(int tile) const { return tile >= 0 && tile < static_cast<int>(m_TileRegion.size()) ? m_TileRegion[static_cast<size_t>(tile)] : -1; }
		glm::vec2 TileCenter(int tile) const;
		// The dry ground of a tile nearest to its centre, in the tile's region (the centre itself when the tile is all water).
		glm::vec2 TileAnchor(int tile) const { return m_TileAnchor[static_cast<size_t>(tile)]; }
		std::vector<int> TilesOf(int region) const;

		// The scout cells (RegionRules::ScoutCell): what a clan marks as seen. CellsX x CellsZ, row by row; each in the region of its centre.
		int CellsX() const { return m_CellsX; }
		int CellsZ() const { return m_CellsZ; }
		int CellCount() const { return m_CellsX * m_CellsZ; }
		int CellAt(const glm::vec2& p) const;
		glm::vec2 CellCenter(int cell) const;
		int CellRegion(int cell) const { return cell >= 0 && cell < static_cast<int>(m_CellRegion.size()) ? m_CellRegion[static_cast<size_t>(cell)] : -1; }
		int CellsIn(int region) const { return region >= 0 && region < static_cast<int>(m_CellsIn.size()) ? m_CellsIn[static_cast<size_t>(region)] : 0; }
		// The cells within `radius` of a point (what a member standing there sees), in order.
		void CellsNear(const glm::vec2& p, float radius, std::vector<int>& out) const;
		// Metres to the water near a point: the least of the scout cell it falls in (coarse, for choosing a camp).
		float WaterDistanceAt(const glm::vec2& p) const;

		// The predators living in the world now (their positions): the danger of each region follows them.
		void SetPredators(const std::vector<glm::vec2>& positions);

		glm::vec2 WorldMin() const { return m_Min; }
		glm::vec2 WorldMax() const { return m_Max; }
		// A hash of the cut (the tiles' regions, the names and the resources): the same world always gives the same number.
		uint64_t Fingerprint() const;
		// For people: one line per region / the whole summary of one.
		std::string List() const;
		std::string Describe(int id) const;

	private:
		RegionRules m_Rules;
		glm::vec2 m_Min{ 0.0f }, m_Max{ 0.0f };
		int m_TilesX = 0, m_TilesZ = 0, m_CellsX = 0, m_CellsZ = 0;
		std::vector<int> m_TileRegion;
		std::vector<glm::vec2> m_TileAnchor;
		std::vector<int> m_CellRegion, m_CellsIn;
		std::vector<float> m_CellWater;
		std::vector<RegionSummary> m_Regions;
	};

	// The things of a list of wild resource spots, for RegionMap::Build (GB/Gameplay/WorldResources.h plans them).
	template <typename Spots>
	std::vector<RegionThing> RegionThingsOf(const Spots& spots)
	{
		std::vector<RegionThing> out;
		out.reserve(spots.size());
		for (const auto& s : spots) out.push_back({ s.Node, { s.Position.x, s.Position.z } });
		return out;
	}
}
