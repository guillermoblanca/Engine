#pragma once
#include "GB/Water/LakeOutline.h"
#include "GB/World/RiverData.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <utility>
#include <vector>

namespace GB
{
	// The water of a world as data, with the questions the rest of the engine asks of it: where it is, how high its
	// surface stands, how far a point is from it and which way it flows. It knows nothing about the terrain it was
	// carved into, the mesh that draws it or the shader that shades it (SRP): Terrain keeps one, WorldGen and the
	// editor ask it instead of repeating its arithmetic, and a renderer builds geometry from the same data.
	// A world has at most one lake (a disc) and one river (a polyline) today; the questions are written so that
	// more bodies (an ocean, a pond) extend this class rather than its callers.

	inline constexpr float kNoWaterLevel = -1e9f;   // "no water here" for a level; also the level of a missing river
	inline constexpr float kNoWaterDistance = 1e9f; // "no water at all" for a distance

	struct LakeBody
	{
		bool Present = false;
		glm::vec2 Center{ 0.0f };
		float Radius = 0.0f;   // meters of the water edge
		float Level = 0.0f;    // height of the surface
		LakeOutline Outline;   // how far the shore departs from the circle of `Radius` (a circle by default)
	};

	// The nearest point of a river axis to a place: how far it is, how high the water stands there and the segment
	// it lies on (so a caller can read the direction and the current of that segment).
	struct RiverSample
	{
		float Distance = kNoWaterDistance;
		float Level = kNoWaterLevel;
		size_t Segment = 0;
		float T = 0.0f;                 // 0..1 along the segment
		float HalfWidth = 0.0f;         // half-width of the river at that point (it can widen downstream)
		glm::vec2 Direction{ 0.0f };    // the segment's xz vector (not normalised)
		bool Found = false;
	};

	// Distance from (x, z) to segment k of the river and the water level at the nearest point of it. The one
	// implementation of that geometry: every query below, and the biome baking of WorldGen, go through it.
	RiverSample ProbeRiverSegment(const RiverData& river, size_t k, float x, float z);
	// The first segment that is strictly nearest to (x, z); an empty `Found == false` sample for a river with no segment.
	RiverSample NearestRiverPoint(const RiverData& river, float x, float z);

	class WaterField
	{
	public:
		void SetLake(const LakeBody& lake) { m_Lake = lake; }
		void SetRiver(RiverData river) { m_River = std::move(river); BuildRiverIndex(); }

		bool HasLake() const { return m_Lake.Present; }
		const LakeBody& Lake() const { return m_Lake; }
		float LakeLevel() const { return m_Lake.Level; }
		glm::vec2 LakeCenter() const { return m_Lake.Center; }
		float LakeRadius() const { return m_Lake.Radius; }

		const RiverData* River() const { return m_River.Points.empty() ? nullptr : &m_River; }

		// River queries (what Terrain::GetRiver*/DistanceToRiver used to compute).
		RiverSample NearestRiver(float x, float z) const { return NearestRiverPoint(m_River, x, z); }
		// The sample counts as "in the water band" of the river: within 1.2 half-widths of its axis. Whether the ground is under the
		// surface there is the caller's to check, since the ground belongs to the terrain.
		bool InRiverBand(const RiverSample& s) const { return s.Found && s.Distance <= s.HalfWidth * 1.2f; }
		// NearestRiver for a caller that only wants the river where the point is in its band: the same sample when the nearest point of the
		// axis is within 1.2 of the widest half-width, and Found == false otherwise (then the point cannot be in the band). It looks only at the
		// segments filed near the point instead of the whole river: the navigation grid of a 3 km map asks it tens of millions of times.
		RiverSample NearestRiverInBand(float x, float z) const;
		float RiverSurfaceLevel(float x, float z) const { return NearestRiver(x, z).Level; }   // level at the nearest point (even on land); kNoWaterLevel without a river
		float DistanceToRiver(float x, float z) const { return NearestRiver(x, z).Distance; }   // kNoWaterDistance without a river
		// Water velocity (m/s, xz) of the river at a point; zero away from it.
		glm::vec2 RiverFlow(float x, float z) const;

		// The level of the water that wets things around (x, z): the lake's when the point is within `lakeReach` lake radii of its
		// centre, the river's when within `riverReach` river half-widths of its axis (the higher one if both). kNoWaterLevel when neither.
		// Rocks darken at this level (WorldInstance); the reaches are the caller's look.
		float LevelNear(float x, float z, float lakeReach, float riverReach) const;

	private:
		void BuildRiverIndex();

		LakeBody m_Lake;
		RiverData m_River;
		// The river's segments filed by square cells: each in every cell within m_IndexReach (1.2 of the widest half-width) of it, in order.
		float m_IndexCell = 16.0f, m_IndexReach = 0.0f;
		glm::vec2 m_IndexMin{ 0.0f };
		int m_IndexW = 0, m_IndexH = 0;
		std::vector<std::vector<unsigned>> m_IndexCells;
	};
}
