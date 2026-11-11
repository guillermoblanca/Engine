#pragma once
#include "GB/Scene/Image.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	// T115 (render.md, "Terreno por capas de material"): the materials of the 13 kinds of ground layer of a partition terrain
	// (GB::SurfaceLayerKind, GB/World/Terrain.h, ids fixed: Rock 0 ... Path 12) as tiling procedural maps, made on the CPU (no GL) and uploaded
	// by TerrainGpu into two texture arrays the GB_TERRAIN_LAYERS program blends by the splat weights.
	//
	// Why here and not in GB/Visual/ProcTexture: the renderer cannot include GB/Visual (Visual uses the renderer: the include would close a
	// cycle), and whoever builds a TerrainGpu (the editor's WorldInstance, the tests) only hands it the Terrain. So the patterns the terrain
	// needs are made in the render module itself, from the same kind of periodic integer-hashed noise as ProcTexture (deterministic on every
	// machine). Mud and dry mud are this module's own versions, not ProcTexture's "mud" / "mud_dry".
	namespace TerrainLayerTextures
	{
		inline constexpr int kKinds = 13;

		// What each kind is drawn with. The table is code for now (TerrainLayerTextures.cpp, Looks()); when the looks move to data they would go
		// to a terrain section of assets/data/visuals.json, keyed by the layer names of the graph's layer_normalize ("rock", "snow", ...).
		struct Look
		{
			const char* Name;         // the layer's name in the terrain graph (splat_<name>)
			const char* Pattern;      // the pattern its maps are generated from (TerrainLayerTextures.cpp)
			float TileMetres;         // the size of one tile of its maps on the ground
			bool Triplanar;           // projected along the three axes (walls: cliff, rock), else from above
			float Contrast;           // how hard its edges are in the height blend (0 soft .. 1 hard): hard rock cuts its edge
			float ReliefMetres;       // the relief of its normal map: metres between the lowest and the highest point of its height
			glm::vec3 MeanColor;      // the mean albedo (sRGB 0..1) its map is normalised to
			// The flat colour (sRGB 0..1) the CPU paints for it into the macro map (Terrain::PaintPartition): the shader divides the macro map by
			// the weighted sum of these, so what it keeps of the macro map is only what the CPU added beyond the flat colours (its patches,
			// the rock's banding, a trail tinted in by the editor), not the layer colours a second time. Equal to MeanColor except where the
			// near look is not the far one (the forest floor under the canopy the macro map paints).
			glm::vec3 MacroColor;
		};
		const std::array<Look, kKinds>& Looks();

		// The maps of every kind at one size, in kind order:
		// - Albedo: rgb the albedo (sRGB), a the height (0..1, linear: the height blend reads it);
		// - Surface: rg the tangent-space normal's x and y (0.5 = flat; +x along u, +y along v), b the roughness, a the occlusion.
		struct Maps
		{
			uint32_t Size = 0;
			std::vector<Image> Albedo, Surface;
			std::array<glm::vec3, kKinds> MeanLinear{};   // the mean of each albedo map in linear colour
		};

		// Generates the maps (one thread per kind). `size` is a power of two from 16 to 1024. Deterministic.
		Maps Generate(uint32_t size);
		// The maps at `size`, made once per process and size and kept for the process (a new world reuses them). Waits if they are still
		// being made.
		std::shared_ptr<const Maps> Get(uint32_t size);
		// Starts making the maps at `size` on a worker thread, if nobody has asked for them yet, and returns at once: the renderer calls it when
		// it is created, so they are ready before the first partition terrain wants them (and no world load waits for them).
		void Prefetch(uint32_t size);
		// The size the terrain's maps are made at: 512, or 256 with `lowRes` (the editor's `quality minimum`).
		inline uint32_t Size(bool lowRes) { return lowRes ? 256u : 512u; }
	}
}
