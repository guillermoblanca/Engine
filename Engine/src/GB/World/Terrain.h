#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "GB/World/RiverData.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace GB
{
	// Regular grid of heights covering a square of the XZ plane, centered on `Center`.
	class Heightmap
	{
	public:
		Heightmap(uint32_t resolution, float size, const glm::vec2& center = { 0.0f, 0.0f });

		uint32_t GetResolution() const { return m_Resolution; }   // samples per side
		float GetSize() const { return m_Size; }                  // meters per side
		float GetSpacing() const { return m_Size / static_cast<float>(m_Resolution - 1); }
		glm::vec2 GetMin() const { return m_Center - m_Size * 0.5f; }
		glm::vec2 GetMax() const { return m_Center + m_Size * 0.5f; }
		bool Contains(float x, float z) const;

		float& At(uint32_t i, uint32_t j) { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		float At(uint32_t i, uint32_t j) const { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		glm::vec2 SamplePosition(uint32_t i, uint32_t j) const;   // world x,z of a grid point

		// Bilinear height at a world position; clamps to the border outside the map.
		float GetHeight(float x, float z) const;
		// Smooth surface normal (central differences) at a world position.
		glm::vec3 GetNormal(float x, float z) const;
		float GetMinHeight() const;
		float GetMaxHeight() const;

	private:
		uint32_t m_Resolution;
		float m_Size;
		glm::vec2 m_Center;
		std::vector<float> m_Heights;
	};

	enum class TerrainStyle { Desert, Snow, Meadow };

	struct DuneParams
	{
		uint32_t Seed = 1;
		float WindAngleDegrees = 30.0f;   // direction the wind blows toward, measured from +X toward +Z
		float Wavelength = 45.0f;         // meters between crests along the wind
		float Height = 6.0f;              // typical crest height above the trough
		float CrestPosition = 0.78f;      // fraction of the wavelength taken by the gentle windward slope
		float CrestSharpness = 0.55f;     // 0 = rounded cosine crest, 1 = knife edge
		float Warp = 0.5f;                // how much the crest lines meander (in wavelengths)
		float WarpScale = 110.0f;         // meters; size of the meanders
		float SizeVariation = 0.45f;      // some dunes are taller than others
		float BaseHills = 2.5f;           // meters of gentle large-scale undulation underneath
	};

	struct HillParams
	{
		uint32_t Seed = 1;
		float Height = 14.0f;
		float Scale = 140.0f;             // meters of the largest features
		int Octaves = 5;
		float Flatten = 0.25f;            // pushes valleys flatter (0 = none)
		// A lake basin carved into the hills (LakeRadius 0 = none). The shore is raised to stay above WaterLevel.
		glm::vec2 LakeCenter{ 0.0f, 0.0f };
		float LakeRadius = 0.0f;          // meters of the water edge
		float LakeDepth = 2.5f;           // meters below WaterLevel at the center
		float WaterLevel = 1.0f;
		// A river carved along these control points (xz, smoothed with a Catmull-Rom spline); it always flows
		// downhill: the water level only descends along the path. Empty = none.
		std::vector<glm::vec2> RiverPath;
		float RiverWidth = 5.0f;          // meters of water
		float RiverDepth = 1.3f;          // meters below the water level
		// Mesh refinement where the ground meets the water: cells along the lake shore and the river banks are split
		// ShoreSubdivision x ShoreSubdivision times (smooth cubic surface, 0/1 = off) so the waterline stops looking stepped.
		int ShoreSubdivision = 4;
		int ShoreMarginCells = 2;         // extra ring of refined cells around the shore, so the detail has room to fade out
	};


	// A drawable heightfield with a matching material (grain, wind ripples, slope-based coloring).
	class Terrain
	{
	public:
		static std::unique_ptr<Terrain> CreateDunes(const DuneParams& dunes = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Desert);
		static std::unique_ptr<Terrain> CreateHills(const HillParams& hills = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Meadow);

		const Heightmap& GetHeightmap() const { return m_Heightmap; }
		Heightmap& GetEditableHeightmap() { return m_Heightmap; }   // call RefreshMesh after editing
		// Re-uploads the vertices of the texels [i0, i1] x [j0, j1] (heights and normals; one extra texel is updated around the box for the normals).
		void RefreshMesh(int i0 = 0, int j0 = 0, int i1 = 1 << 30, int j1 = 1 << 30);
		float GetHeight(float x, float z) const { return m_Heightmap.GetHeight(x, z); }
		const Material& GetMaterial() const { return m_Material; }
		Material& GetMaterial() { return m_Material; }
		const Mesh& GetMesh() const { return *m_Mesh; }
		TerrainStyle GetStyle() const { return m_Style; }
		const RiverData* GetRiver() const { return m_River.Points.empty() ? nullptr : &m_River; }
		// Water velocity (m/s, xz) of the river at a point; zero away from it. Usable as PhysicsWorld::SetCurrent.
		glm::vec2 GetRiverFlow(float x, float z) const;
		// True where the surface of the river is above the ground (inside the water).
		float GetRiverLevelAt(float x, float z) const;   // water level, or -1e9 outside the water
		float GetRiverSurfaceLevel(float x, float z) const;   // water level at the nearest river point (even on land); -1e9 without a river
		float DistanceToRiver(float x, float z) const;   // meters to the river axis; 1e9 when there is none
		bool HasLake() const { return m_HasLake; }
		float GetWaterLevel() const { return m_WaterLevel; }
		glm::vec2 GetLakeCenter() const { return m_LakeCenter; }
		float GetLakeRadius() const { return m_LakeRadius; }
		float GetWindAngle() const { return m_WindAngle; }   // radians, dune terrains only

		// A function usable as ParticleEmitterDesc::GroundHeightFn. The Terrain must outlive it.
		std::function<float(float, float)> MakeGroundFunction(bool waterIsGround = false) const;   // waterIsGround: lakes count as ground (rain splashes on them)

		// Points along the dune crests (local maxima across the wind), for effects such as blowing sand.
		const std::vector<glm::vec3>& GetCrestPoints() const { return m_Crests; }

		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

	private:
		Terrain(uint32_t resolution, float size) : m_Heightmap(resolution, size) {}
		void Finish(TerrainStyle style, float wavelengthMeters);
		void CarveRiver(const HillParams& params);
		// Shore refinement (see HillParams::ShoreSubdivision): extra vertices appended after the grid ones.
		std::vector<uint8_t> FlagShoreCells(int marginCells) const;
		void BuildShoreRefinement(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		float SmoothHeight(float x, float z) const;          // Catmull-Rom surface through the heightmap, never beyond the cell's corner heights
		float FineHeight(int fi, int fj) const;              // height of a vertex of the refined lattice (fi, fj in units of spacing / N)
		bool IsCellRefined(int ci, int cj) const;
		Vertex MakeFineVertex(int fi, int fj) const;

		int m_ShoreSubdivision = 1, m_ShoreMargin = 2;
		uint32_t m_FineFirst = 0;                            // index of the first refined vertex (= grid vertex count)
		std::vector<uint8_t> m_RefinedCells;                 // (n-1)^2 flags
		std::unordered_map<uint64_t, uint32_t> m_FineIndex;  // refined lattice coordinate -> vertex index

		Heightmap m_Heightmap;
		TerrainStyle m_Style = TerrainStyle::Desert;
		float m_WindAngle = 0.0f;
		std::vector<Vertex> m_Vertices;
		RiverData m_River;
		bool m_HasLake = false;
		float m_WaterLevel = 0.0f, m_LakeRadius = 0.0f;
		glm::vec2 m_LakeCenter{ 0.0f };
		std::unique_ptr<Mesh> m_Mesh;
		std::unique_ptr<Texture2D> m_Grain, m_Ripples, m_Macro;
		Material m_Material;
		std::vector<glm::vec3> m_Crests;
	};
}
