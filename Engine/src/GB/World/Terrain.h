#pragma once
#include "GB/Scene/Image.h"
#include "GB/Scene/Vertex.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "GB/Water/WaterField.h"
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
		// Curvature-driven mesh refinement (there is no shore to drive it here): see HillParams::ShoreSubdivision/DetailThreshold.
		int DetailSubdivision = 4;
		float DetailThreshold = 0.08f;
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
		// Steepest rise of the ground away from the shore (meters per meter, 0 = unlimited). Hills next to the basin are cut down to this
		// cone, so the lake gets a gentle bank instead of a cliff with a jagged edge.
		float LakeBankSlope = 0.3f;
		float LakeIrregularity = 0.0f;    // 0 = a circular shore; up to ~0.25 lobes it, differently for each Seed (GB/Water/LakeOutline.h)
		bool SurfaceMaps = true;          // build the normal and macro color maps (off for a throw-away terrain used only for its heights)
		// A river carved along these control points (xz, smoothed with a Catmull-Rom spline); it always flows
		// downhill: the water level only descends along the path. Empty = none.
		std::vector<glm::vec2> RiverPath;
		float RiverWidth = 5.0f;          // meters of water
		float RiverDepth = 1.3f;          // meters below the water level
		std::vector<float> RiverDischarge;   // relative discharge (0..1) at each RiverPath point: a river that widens and deepens downstream (GB/Water/RiverHydraulics.h); empty = uniform
		// Mesh refinement where the ground meets the water: cells along the lake shore and the river banks are split
		// ShoreSubdivision x ShoreSubdivision times (smooth cubic surface, 0/1 = off) so the waterline stops looking stepped.
		int ShoreSubdivision = 4;
		int ShoreMarginCells = 2;         // extra ring of refined cells around the shore, so the detail has room to fade out
		// The same refinement, driven by curvature instead of water: a cell is split when its flat bilinear quad would sit more than
		// this many metres away from the smooth cubic surface through its corners (ridges, gullies, the channels erosion carves --
		// not the flat ground most of the map actually is). <= 0 = off (shore-only, as before).
		float DetailThreshold = 0.08f;
	};


	// A layer of the ground's material with a 0..1 weight per sample of the heightmap (row major, resolution^2): what a terrain graph's
	// "splat_rock"/"splat_snow"/"splat_grass"/"splat_sand"/"splat_dry"/"splat_forest" outputs give (docs/GRAFO_TERRENO.md, 4b). The layers are
	// painted into the macro colour map over the style's palette, in order (a later layer covers an earlier one); rock also carves its fluting
	// into the surface normals. Dry is sun-cured grass (olive to straw); Forest is the canopy seen from afar (crowns and the dark between them).
	enum class SurfaceLayerKind { Rock, Snow, Grass, Sand, Dry, Forest };
	struct SurfaceLayer
	{
		SurfaceLayerKind Kind = SurfaceLayerKind::Rock;
		std::vector<float> Weights;
	};

	// The triangles of one square tile of a terrain's mesh (Terrain::GetTiles): a range of its sorted index buffer and its bounds, so the
	// shadows' box and the view draw, and cull, only the tiles they touch.
	struct TerrainTile { uint32_t First = 0, Count = 0; glm::vec3 Center{ 0.0f }; float Radius = 0.0f; };

	// What PrepareRenderData leaves for the renderer to upload once (GB/Renderer/TerrainGpu.h takes it): the triangles sorted into tiles and the
	// images of the detail textures and of the macro colour map, with the material's roughness and ripple depth. The vertices and the normal
	// map stay in the Terrain (GetVertices, GetNormalImage), because an edit (RefreshMesh) rewrites them.
	struct TerrainRenderData
	{
		std::vector<uint32_t> Indices;
		Image Grain, Ripples, Macro;   // Macro is empty without surface maps
		float Roughness = 0.9f, RippleDepth = 1.0f;
	};

	// What RefreshMesh rewrote since the renderer last read it (Terrain::TakeEdits): the grid rows and columns, the range of refined vertices and
	// the rectangle of normal-map texels, each the union of the edits. The CPU copies are exact, so uploading a little more than changed is harmless.
	struct TerrainEdits
	{
		int RowJ0 = 0, RowJ1 = -1, RowI0 = 0, RowI1 = -1;        // grid vertices: columns [RowI0, RowI1] of the rows [RowJ0, RowJ1]
		uint32_t FineFirst = 1, FineLast = 0;                    // refined vertices [FineFirst, FineLast]
		int NormalX0 = 0, NormalY0 = 0, NormalX1 = -1, NormalY1 = -1;   // normal-map texels, inclusive
		bool Any() const { return RowJ1 >= RowJ0 || FineLast >= FineFirst || NormalX1 >= NormalX0; }
	};

	// A heightfield and what the world asks of it: heights, smooth normals, the water carved into it, the material layers, the dune crests. Pure
	// CPU, no OpenGL: it also prepares, as plain data, the refined mesh and the images its drawing needs (PrepareRenderData), which
	// GB/Renderer/TerrainGpu.h uploads, keeps in step with the edits and draws (docs/ARQUITECTURA_REVISION.md, H5).
	class Terrain
	{
	public:
		// The heights and the queries only: PrepareRenderData (or a TerrainGpu, which calls it) adds the mesh and the images.
		static std::unique_ptr<Terrain> CreateDunes(const DuneParams& dunes = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Desert);
		static std::unique_ptr<Terrain> CreateHills(const HillParams& hills = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Meadow);
		// CreateHills without the hills' noise: `ground` (a terrain graph's height output, or any heightmap) is the ground the lake and river of
		// `water` are carved into; the rest of the recipe (refinement, surface maps) is read the same way. CreateHills is its noise + this.
		static std::unique_ptr<Terrain> CreateFromHeightmap(const Heightmap& ground, const HillParams& water = {},
			TerrainStyle style = TerrainStyle::Meadow);
		// A ground the water has already been carved into (a terrain graph's lake_carve/river_carve): `water` is taken as it is, nothing is dug.
		// Only the refinement and surface-map fields of `recipe` are read.
		static std::unique_ptr<Terrain> CreateFromHeightmap(const Heightmap& ground, const WaterField& water, const HillParams& recipe,
			TerrainStyle style = TerrainStyle::Meadow);

		// The CPU half of drawing the terrain (the refined mesh sorted into tiles, the images of the textures), on any thread and with no OpenGL:
		// a world generated in the background prepares it there, so the main thread only uploads it (a 3 km map froze the window for seconds).
		// Nothing when it is prepared already. A TerrainGpu calls it when nobody did.
		void PrepareRenderData();
		bool HasRenderData() const { return m_Prepared != nullptr || !m_Vertices.empty(); }   // prepared, or already handed to a renderer
		// For the renderer, once: the prepared data moved out (null when there is none). The vertices move to GetVertices, where the edits go.
		std::unique_ptr<TerrainRenderData> TakeRenderData();
		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }   // the mesh's vertices, once taken (TakeRenderData)
		const std::vector<TerrainTile>& GetTiles() const { return m_Tiles; }    // empty for a terrain of one tile or less (one draw)
		bool HasSurfaceMaps() const { return m_SurfaceMaps; }
		const Image& GetNormalImage() const { return m_NormalImage; }          // the high-resolution normal map (empty without surface maps)
		// The parts of GetVertices and GetNormalImage that RefreshMesh rewrote since the last call, which clears them. The revision goes up on
		// every edit, so a renderer can tell cheaply that there is something to take.
		TerrainEdits TakeEdits();
		uint64_t GetEditRevision() const { return m_EditRevision; }
		// The material layers (see SurfaceLayer); a layer whose weights are not resolution^2 long is ignored. Read when the surface maps are
		// built (PrepareRenderData): set them before it. Without layers the ground is coloured by its style alone, as always.
		void SetSurfaceLayers(std::vector<SurfaceLayer> layers);
		const std::vector<SurfaceLayer>& GetSurfaceLayers() const { return m_Layers; }
		float SurfaceLayerWeight(SurfaceLayerKind kind, float x, float z) const;   // bilinear; 0 without that layer

		const Heightmap& GetHeightmap() const { return m_Heightmap; }
		Heightmap& GetEditableHeightmap() { return m_Heightmap; }   // call RefreshMesh after editing
		// Rewrites the vertices of the texels [i0, i1] x [j0, j1] (heights and normals; one extra texel is updated around the box for the normals)
		// and that part of the normal map, and records it for the renderer (TakeEdits: TerrainGpu::Sync uploads it). Nothing before a renderer took
		// the mesh: it reads the heights as they are then.
		void RefreshMesh(int i0 = 0, int j0 = 0, int i1 = 1 << 30, int j1 = 1 << 30);
		float GetHeight(float x, float z) const { return m_Heightmap.GetHeight(x, z); }
		TerrainStyle GetStyle() const { return m_Style; }
		// The water carved into this terrain, as data (GB/Water/WaterField.h): the queries below forward to it.
		const WaterField& Water() const { return m_Water; }
		const RiverData* GetRiver() const { return m_Water.River(); }
		// Water velocity (m/s, xz) of the river at a point; zero away from it. Usable as PhysicsWorld::SetCurrent.
		glm::vec2 GetRiverFlow(float x, float z) const { return m_Water.RiverFlow(x, z); }
		// True where the surface of the river is above the ground (inside the water).
		float GetRiverLevelAt(float x, float z) const;   // water level, or -1e9 outside the water
		float GetRiverSurfaceLevel(float x, float z) const { return m_Water.RiverSurfaceLevel(x, z); }   // water level at the nearest river point (even on land); -1e9 without a river
		float DistanceToRiver(float x, float z) const { return m_Water.DistanceToRiver(x, z); }   // meters to the river axis; 1e9 when there is none
		bool HasLake() const { return m_Water.HasLake(); }
		float GetWaterLevel() const { return m_Water.LakeLevel(); }
		glm::vec2 GetLakeCenter() const { return m_Water.LakeCenter(); }
		float GetLakeRadius() const { return m_Water.LakeRadius(); }
		// Smooth ground normal (Catmull-Rom surface through the heightmap, like the refined shore and the normal map).
		glm::vec3 GetSmoothNormal(float x, float z) const;
		float GetSmoothHeight(float x, float z) const { return SmoothHeight(x, z); }
		float GetWindAngle() const { return m_WindAngle; }   // radians, dune terrains only
		// The wind of a dune field built some other way (a terrain graph's dunes): sets the angle and finds the crests as CreateDunes does.
		void SetWind(float angleRadians, float wavelength);

		// A function usable as ParticleEmitterDesc::GroundHeightFn. The Terrain must outlive it.
		std::function<float(float, float)> MakeGroundFunction(bool waterIsGround = false) const;   // waterIsGround: lakes count as ground (rain splashes on them)

		// Points along the dune crests (local maxima across the wind), for effects such as blowing sand.
		const std::vector<glm::vec3>& GetCrestPoints() const { return m_Crests; }

	private:
		Terrain(uint32_t resolution, float size) : m_Heightmap(resolution, size) {}
		void FindCrests(float wavelength);   // the local maxima across the wind (m_WindAngle), into m_Crests
		// Mesh refinement (see HillParams::ShoreSubdivision/DetailThreshold): extra vertices appended after the grid ones, for cells
		// flagged by either strategy below (SRP: each only decides *which* cells, never how a flagged cell is split or stitched).
		std::vector<uint8_t> FlagShoreCells(int marginCells) const;
		std::vector<uint8_t> FlagDetailCells(float threshold) const;
		void BuildMeshRefinement(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		float SmoothHeight(float x, float z) const;          // Catmull-Rom surface through the heightmap, never beyond the cell's corner heights
		float FineHeight(int fi, int fj) const;              // height of a vertex of the refined lattice (fi, fj in units of spacing / N)
		bool IsCellRefined(int ci, int cj) const;
		void BuildSurfaceMaps(TerrainStyle style, const glm::vec3& sunlit, const glm::vec3& shade, const glm::vec3& crest, Image& macro);
		std::unique_ptr<TerrainRenderData> m_Prepared;   // PrepareRenderData's work, waiting for a renderer (TakeRenderData)
		std::vector<Vertex> m_PreparedVertices;         // its vertices, moved to m_Vertices when a renderer takes the rest
		void WriteNormalTexels(int x0, int y0, int x1, int y1);   // recomputes the normal map texels in [x0, x1] x [y0, y1] (and records them as edited)
		// Metres the surface maps add to the smooth height at a point: the fluting of the rock layer (0 without one). Used by both the first
		// build of the normal map and its partial updates, so an edit never erases it.
		float SurfaceDetail(float x, float z) const;
		// The colour of the material layers at a point, painted over `base` (the style's colour there).
		glm::vec3 PaintLayers(const glm::vec3& base, float x, float z, float height, const glm::vec3& normal, float patch, float fine) const;
		Vertex MakeFineVertex(int fi, int fj) const;

		bool m_SurfaceMaps = true;
		int m_ShoreSubdivision = 1, m_ShoreMargin = 2;
		float m_DetailThreshold = 0.0f;   // 0 = off; set from HillParams::DetailThreshold (CreateDunes leaves it off)
		uint32_t m_FineFirst = 0;                            // index of the first refined vertex (= grid vertex count)
		std::vector<uint8_t> m_RefinedCells;                 // (n-1)^2 flags
		std::unordered_map<uint64_t, uint32_t> m_FineIndex;  // refined lattice coordinate -> vertex index

		Heightmap m_Heightmap;
		TerrainStyle m_Style = TerrainStyle::Desert;
		float m_WindAngle = 0.0f;
		std::vector<Vertex> m_Vertices;                      // the mesh's vertices once a renderer took it: the copy RefreshMesh edits
		WaterField m_Water;
		// The mesh's triangles sorted into square tiles of about kTileCells cells (the vertices stay one buffer): each tile is drawn, and culled,
		// on its own, so the shadows' box and the view draw only the tiles they touch.
		static constexpr int kTileCells = 64;
		std::vector<TerrainTile> m_Tiles;
		void SortIntoTiles(const std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
		Image m_NormalImage;                                 // CPU copy of the normal map, for partial updates
		uint32_t m_NormalRes = 0;
		TerrainEdits m_Edits;                                // what RefreshMesh rewrote since the renderer last took the edits
		uint64_t m_EditRevision = 0;
		std::vector<glm::vec3> m_Crests;
		std::vector<SurfaceLayer> m_Layers;
	};
}
