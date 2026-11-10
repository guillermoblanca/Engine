#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// The recipes of the built-in grounds (Terrain::CreateHills/CreateDunes) and the palette a terrain is drawn with: plain data, so the water
	// planner (GB/World/Water/WaterPlanner.h), which writes the lake and the river into a HillParams, does not need the rest of GB::Terrain
	// (GB_WorldGen, docs/GRAFO_TERRENO.md section 6). Terrain.h includes it.
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
		float LakeIrregularity = 0.0f;    // 0 = a circular shore; up to ~0.25 lobes it, differently for each Seed (GB/World/Water/LakeOutline.h)
		bool SurfaceMaps = true;          // build the normal and macro color maps (off for a throw-away terrain used only for its heights)
		// A river carved along these control points (xz, smoothed with a Catmull-Rom spline); it always flows
		// downhill: the water level only descends along the path. Empty = none.
		std::vector<glm::vec2> RiverPath;
		float RiverWidth = 5.0f;          // meters of water
		float RiverDepth = 1.3f;          // meters below the water level
		std::vector<float> RiverDischarge;   // relative discharge (0..1) at each RiverPath point: a river that widens and deepens downstream (GB/World/Water/RiverHydraulics.h); empty = uniform
		// Mesh refinement where the ground meets the water: cells along the lake shore and the river banks are split
		// ShoreSubdivision x ShoreSubdivision times (smooth cubic surface, 0/1 = off) so the waterline stops looking stepped.
		int ShoreSubdivision = 4;
		int ShoreMarginCells = 2;         // extra ring of refined cells around the shore, so the detail has room to fade out
		// The same refinement, driven by curvature instead of water: a cell is split when its flat bilinear quad would sit more than
		// this many metres away from the smooth cubic surface through its corners (ridges, gullies, the channels erosion carves --
		// not the flat ground most of the map actually is). <= 0 = off (shore-only, as before).
		float DetailThreshold = 0.08f;
	};
}
