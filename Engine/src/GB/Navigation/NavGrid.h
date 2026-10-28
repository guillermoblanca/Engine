#pragma once
#include "GB/Game/CharacterController.h"
#include <cstdint>
#include <functional>
#include <vector>
#include <glm/glm.hpp>

namespace GB
{
	// Navigation over the terrain (H5): a grid of cells that keeps what the ground is like (slope, depth of the water, vegetation) and which cells a static
	// obstacle blocks, and finds paths over it with A*. What a cell *costs* is decided at the time of the query by a NavProfile, so a hurried scout, a
	// porter with a heavy load and a swimmer-averse villager all use the same grid. Long distances go first through a coarse graph of chunks and the fine
	// search stays inside that corridor. CPU only, no window, deterministic (ties are broken by cell index).
	//
	// The grid is built from a CharacterWorld (its ground, water, bounds and *static* obstacles), so what blocks the character blocks the path. When the world
	// changes (a tree is felled, a building goes up, a door shuts) only the cells around the change are redone: see SyncObstacles and UpdateRegion.
	// Queries use scratch memory owned by the grid: one thread at a time.

	struct NavConfig
	{
		float CellSize = 0.5f;         // m; trunks are 0.3 m, so a cell must be smaller than the gaps worth finding
		float AgentRadius = 0.35f;     // obstacles are made this much bigger, so a path that goes through free cells clears the character
		float Margin = 0.1f;           // and this much more
		int ChunkCells = 16;           // cells per side of a region (the unit of the coarse graph and of the change log)
		glm::vec2 AreaMin{ 1e30f }, AreaMax{ -1e30f };   // the part of the world to cover; unset: the bounds of the world
		std::function<float(float, float)> Vegetation;   // 0..1 density of undergrowth at x,z (optional)
		int MaxExpansions = 120000;    // cells a single search may open before it gives up
		int MaxPlansPerFrame = 2;      // searches AllowPlan lets through between two NewFrame calls: the rest wait for the next frame (a crowd that all sets off at once)
		int CoarseFrom = 48;           // searches longer than this many cells (in a straight line) go through the coarse graph first
	};

	// How one walker values the terrain. Costs are time-like multipliers (1 = walking on level dry ground).
	struct NavProfile
	{
		float MaxSlope = 0.85f;        // rise over run above which a cell is a wall (the controller's MaxSlope)
		float MaxWadeDepth = 1.0f;     // water deeper than this is a wall (the controller's MaxWadeDepth)
		float SlopeWeight = 2.0f;      // extra cost per unit of slope
		float WaterWeight = 4.0f;      // extra cost of wading, per MaxWadeDepth of depth (there is also a fixed wet penalty)
		float VegetationWeight = 1.5f; // extra cost of full undergrowth
		float Load = 0.0f;             // 0..1 how loaded the walker is: slopes and water weigh more
		float LoadFactor = 1.5f;       // ... by (1 + Load * LoadFactor)

		static NavProfile Carrying(float load) { NavProfile p; p.Load = load; return p; }
		bool operator==(const NavProfile&) const = default;   // (a role's profile is data: see TribeRole)
	};

	struct NavPath
	{
		std::vector<glm::vec2> Points;   // smoothed waypoints from the start to the goal (the exact goal last when Found)
		float Cost = 0.0f;               // cost-weighted length
		bool Found = false;
		bool Partial = false;            // the goal cannot be reached: Points go to the nearest cell that can
		int Expanded = 0;                // cells opened by the search (what the CPU cost depends on)
	};

	struct NavStats
	{
		long Builds = 0, Paths = 0, Failed = 0, Replans = 0, Updates = 0;
		long Expanded = 0;
		long CellsUpdated = 0;
		double PathSeconds = 0.0, BuildSeconds = 0.0, UpdateSeconds = 0.0;
	};

	class NavGrid
	{
	public:
		// False (and `error`) when the world has no finite bounds and the config no area, or the area is too big.
		bool Build(const CharacterWorld& world, const NavConfig& config = NavConfig(), std::string* error = nullptr);
		void Clear();
		bool Ready() const { return m_Width > 0; }
		// Whether this grid was built for a world like `world` (same area and ground): false means "build again".
		bool Matches(const CharacterWorld& world) const;

		int Width() const { return m_Width; }
		int Height() const { return m_Height; }
		float CellSize() const { return m_Config.CellSize; }
		const NavConfig& Config() const { return m_Config; }
		glm::vec2 Min() const { return m_Min; }
		glm::vec2 Max() const { return m_Min + glm::vec2(static_cast<float>(m_Width), static_cast<float>(m_Height)) * m_Config.CellSize; }
		glm::vec2 CellCenter(int i, int j) const { return m_Min + (glm::vec2(static_cast<float>(i), static_cast<float>(j)) + 0.5f) * m_Config.CellSize; }
		bool CellAt(const glm::vec2& p, int& i, int& j) const;
		size_t Obstacles() const { return m_Obstacles.size(); }
		// Bytes the grid holds: the cells (`regions`: the part of it that is the coarse graph of regions) and the scratch memory of the search.
		size_t MemoryBytes(size_t* regions = nullptr) const;

		// ---- what a cell is
		// The cost of walking onto the cell (i, j) with `profile`, or < 0 when it is a wall for that walker.
		float CellCost(int i, int j, const NavProfile& profile) const { return CostAt(Index(i, j), profile); }
		bool Blocked(int i, int j) const { return m_Flags[Index(i, j)] != 0; }   // by an obstacle or the edge, for everyone
		bool Walkable(const glm::vec2& p, const NavProfile& profile = NavProfile()) const;
		float Depth(int i, int j) const { return static_cast<float>(m_Depth[Index(i, j)]) * 0.01f; }

		// ---- paths
		// Cost-weighted shortest path from `from` to `to`. `extra` are temporary obstacles (other walkers in the way) that only this search sees.
		bool FindPath(const NavProfile& profile, const glm::vec2& from, const glm::vec2& to, NavPath& out, const std::vector<CircleObstacle>* extra = nullptr) const;
		// Whether the straight segment a-b crosses only walkable cells; `cost` gets its cost-weighted length.
		bool LineClear(const NavProfile& profile, const glm::vec2& a, const glm::vec2& b, float* cost = nullptr, const std::vector<CircleObstacle>* extra = nullptr) const;

		// ---- the world changes
		// Replaces the static obstacles with `now` (a tree is gone, a house is there) and redoes only the cells around the circles that differ.
		// Returns the number of cells redone.
		int SyncObstacles(const std::vector<CircleObstacle>& now);
		// Reads the ground, the water and the obstacles again inside the rectangle (terrain edited, a river moved).
		int UpdateRegion(const glm::vec2& min, const glm::vec2& max);
		// Bumped by every change. Walkers remember it with their path.
		uint32_t Revision() const { return m_Revision; }
		// Whether something changed since `revision` inside the box around a and b (and `pad` m around it). True when the log no longer goes back that far.
		bool ChangedSince(uint32_t revision, const glm::vec2& a, const glm::vec2& b, float pad = 2.0f) const;

		// ---- measuring and debugging
		const NavStats& Stats() const { return m_Stats; }
		void NoteReplan() const { ++m_Stats.Replans; }
		// Spreads the searches of a crowd over frames: the host calls NewFrame once per frame, and a walker that wants to plan asks AllowPlan first
		// (false: try again next frame). Hosts that never call NewFrame are never refused.
		void NewFrame() const { m_PlansThisFrame = 0; m_FrameAware = true; }
		bool AllowPlan() const { return !m_FrameAware || m_PlansThisFrame++ < m_Config.MaxPlansPerFrame; }
		void ResetStats() { m_Stats = NavStats(); }
		// With recording on, every path found is kept (the last 64) so the editor can draw them.
		void SetRecording(bool on) const { m_Recording = on; if (!on) m_Traces.clear(); }
		bool Recording() const { return m_Recording; }
		const std::vector<std::vector<glm::vec2>>& Traces() const { return m_Traces; }
		// Cells blocked / of cost above 1.5 for a default walker / water, counted, for the status line.
		void Count(long& blocked, long& costly, long& wet) const;

	private:
		struct Change { uint32_t Revision; glm::vec2 Min, Max; };
		struct Scratch
		{
			std::vector<float> G;
			std::vector<int> Parent;
			std::vector<uint32_t> Open, Closed;
			uint32_t Gen = 0;
		};

		int Index(int i, int j) const { return j * m_Width + i; }
		int ChunkIndex(int i, int j) const { return (j / m_Config.ChunkCells) * m_ChunkW + (i / m_Config.ChunkCells); }
		float CostAt(int index, const NavProfile& profile) const;
		float CostWith(int index, const NavProfile& profile, const std::vector<CircleObstacle>* extra) const;
		void ReadTerrain(int i0, int j0, int i1, int j1);          // [i0, i1) x [j0, j1)
		void Rasterize(int i0, int j0, int i1, int j1);            // the obstacle flags of the rectangle, from the obstacle list
		void RefreshChunks(int i0, int j0, int i1, int j1);
		void Log(int i0, int j0, int i1, int j1);
		bool Search(const NavProfile& profile, int start, int goal, const std::vector<uint8_t>* corridor, const std::vector<CircleObstacle>* extra,
			std::vector<int>& cells, std::vector<float>& costs, int& expanded, int& nearest) const;
		bool Corridor(int start, int goal, std::vector<uint8_t>& mask) const;
		int NearestWalkable(int i, int j, const NavProfile& profile, const std::vector<CircleObstacle>* extra, int rings) const;
		void Smooth(const NavProfile& profile, const std::vector<int>& cells, const std::vector<float>& costs, const glm::vec2& from, const glm::vec2& to,
			bool exactGoal, const std::vector<CircleObstacle>* extra, NavPath& out) const;

		NavConfig m_Config;
		int m_Width = 0, m_Height = 0, m_ChunkW = 0, m_ChunkH = 0;
		glm::vec2 m_Min{ 0.0f };
		glm::vec2 m_BoundsMin{ 0.0f }, m_BoundsMax{ 0.0f };
		std::function<float(float, float)> m_Ground, m_Water;
		float m_Signature[9] = {};
		std::vector<uint8_t> m_Slope;    // rise over run * 100, capped
		std::vector<uint8_t> m_Depth;    // water depth in cm, capped
		std::vector<uint8_t> m_Veg;      // 0..255
		std::vector<uint8_t> m_Flags;    // 1 an obstacle, 2 outside the walkable bounds
		std::vector<float> m_ChunkCost;  // mean cost of the open cells of a chunk for a default walker, divided by the open fraction; < 0 closed
		std::vector<CircleObstacle> m_Obstacles;
		std::vector<Change> m_Log;
		uint32_t m_Revision = 0;

		mutable Scratch m_Scratch;
		mutable NavStats m_Stats;
		mutable bool m_Recording = false;
		mutable bool m_FrameAware = false;
		mutable int m_PlansThisFrame = 0;
		mutable std::vector<std::vector<glm::vec2>> m_Traces;
	};
}
