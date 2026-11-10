#pragma once
#include "GB/Scene/Image.h"
#include "GB/Scene/Vertex.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "GB/World/Water/WaterField.h"
#include "GB/World/Heightmap.h"
#include "GB/World/TerrainParams.h"
#include "GB/World/RiverData.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace GB
{
	// A layer of the ground's material with a 0..1 weight per sample of the heightmap (row major, resolution^2): what a terrain graph's
	// "splat_rock"/"splat_snow"/"splat_grass"/"splat_sand"/"splat_dry"/"splat_forest" outputs give (docs/GRAFO_TERRENO.md, 4b). The layers are
	// painted into the macro colour map over the style's palette, in order (a later layer covers an earlier one); rock also carves its fluting
	// into the surface normals. Dry is sun-cured grass (olive to straw); Forest is the canopy seen from afar (crowns and the dark between them).
	//
	// The layers of the terrain of TERRENO_V5 come after those six, in this order, with the ids they have here (the renderer keys its materials
	// by them): Cliff (bare faces), Scree (broken rock at their foot), Gravel (river bed and channel banks), Mud (lake beds and shores), MudDry
	// (the shore the water left), GrassWet (damp meadow) and Path ("splat_cliff" ... "splat_path"; GRAFO_TERRENO.md, "Capas de material y
	// árboles del terreno nuevo"). A set of layers with any of them is a partition: the weights sum to 1 at every sample (the graph's
	// layer_normalize makes them so, snow and path on top), and they are painted as a weighted sum of flat colours, not one over another; the
	// rock fluting is carved on Rock and Cliff. A set with only the six of before is painted exactly as before.
	enum class SurfaceLayerKind { Rock, Snow, Grass, Sand, Dry, Forest, Cliff, Scree, Gravel, Mud, MudDry, GrassWet, Path };
	static_assert(static_cast<int>(SurfaceLayerKind::Forest) == 5 && static_cast<int>(SurfaceLayerKind::Cliff) == 6 && static_cast<int>(SurfaceLayerKind::Path) == 12,
		"the six kinds of before keep their ids; the new ones follow them");
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
		// True when the layers hold any kind after Forest: they are a partition (sum 1 at every sample) and are painted as one.
		bool SurfaceLayersArePartition() const { return m_LayersPartition; }
		float SurfaceLayerWeight(SurfaceLayerKind kind, float x, float z) const;   // bilinear; 0 without that layer

		const Heightmap& GetHeightmap() const { return m_Heightmap; }
		Heightmap& GetEditableHeightmap() { return m_Heightmap; }   // call RefreshMesh after editing
		// Rewrites the vertices of the texels [i0, i1] x [j0, j1] (heights and normals; one extra texel is updated around the box for the normals)
		// and that part of the normal map, and records it for the renderer (TakeEdits: TerrainGpu::Sync uploads it). Nothing before a renderer took
		// the mesh: it reads the heights as they are then.
		void RefreshMesh(int i0 = 0, int j0 = 0, int i1 = 1 << 30, int j1 = 1 << 30);
		float GetHeight(float x, float z) const { return m_Heightmap.GetHeight(x, z); }
		TerrainStyle GetStyle() const { return m_Style; }
		// The water carved into this terrain, as data (GB/World/Water/WaterField.h): the queries below forward to it.
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
		// The same for a partition (SurfaceLayersArePartition): the weighted sum of every layer's flat colour, a little sharpened.
		glm::vec3 PaintPartition(const glm::vec3& base, float x, float z, float height, const glm::vec3& normal, float patch, float fine) const;
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
		bool m_LayersPartition = false;
	};
}
