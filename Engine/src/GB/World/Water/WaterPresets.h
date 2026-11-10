#pragma once
#include "GB/World/Water/WaterField.h"
#include "GB/World/Water.h"
#include <memory>
#include <vector>

namespace GB
{
	// The numbers that used to be literals scattered through WorldGen.cpp and the editor's WorldInstance: how big a
	// generated lake and river are, and how the water that draws them looks. Collected here, by name, so a
	// generator or a scene picks them from one place and a test can vary them.

	// What the world generator digs: the size of the lake and the river it routes to it.
	struct WaterGenParams
	{
		float LakeRadius = 22.0f;           // meters of the water edge
		float LakeDepth = 3.0f;             // meters below the surface at the centre
		float LakeLevelAboveBasin = 1.0f;   // the surface stands this far above the lowest point the basin was dug at
		float RiverWidth = 6.5f;            // meters of water, at the mouth
		float LakeIrregularity = 0.0f;      // how far the shore departs from a circle (GB/World/Water/LakeOutline.h)
	};
	// `organic` is generator 3: a lake with a lobed shore. Generators 1 and 2 keep their circle.
	WaterGenParams DefaultWaterGen(bool alpine, bool organic = false);

	// How the water of a generated world is drawn: the wave and colour settings of each surface and how the lake disc
	// is sized against the lake it covers.
	struct WaterLook
	{
		WaterParams Lake;
		WaterParams River;
		float LakeSurfaceScale = 2.6f;      // the lake grid spans this many lake radii ...
		uint32_t LakeSubdivisions = 160;    // ... in this many cells ...
		float LakeMaskScale = 1.2f;         // ... and only a disc of this many radii is drawn (the foam fades out at its edge)
	};
	WaterLook DefaultWaterLook();

	// The renderable surfaces of a WaterField. Null when the field has no such body.
	std::unique_ptr<WaterSurface> MakeLakeSurface(const WaterField& field, const WaterLook& look = DefaultWaterLook());
	// One lake's (the main one is MakeLakeSurface(field)): a disc lake as above; a lake with a mask (lake_fill) on the circle that holds it.
	std::unique_ptr<WaterSurface> MakeLakeSurface(const LakeBody& lake, const WaterLook& look = DefaultWaterLook());
	// The surfaces of WaterField::MoreLakes(), in order (empty for a world with only its main lake).
	std::vector<std::unique_ptr<WaterSurface>> MakeMoreLakeSurfaces(const WaterField& field, const WaterLook& look = DefaultWaterLook());
	std::unique_ptr<WaterSurface> MakeRiverSurface(const WaterField& field, const WaterLook& look = DefaultWaterLook());
}
