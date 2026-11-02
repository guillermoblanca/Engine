#pragma once
#include "GB/Renderer/Vertex.h"
#include "GB/World/RiverData.h"
#include <cstdint>
#include <vector>

namespace GB
{
	// What a body of water is made of, as plain data: the CPU description of a surface the renderer uploads once.
	// A lake is a flat grid (only its size and subdivision matter: the shader displaces and masks it); a river is a
	// ribbon of vertices that follows the water level. Nothing here touches OpenGL, so it is built, measured and
	// tested without a context, and the GPU mesh is the renderer's business (Renderer/WaterRenderer.h).
	struct WaterGeometry
	{
		float GridSize = 0.0f;               // lake: meters per side of the flat grid
		uint32_t GridSubdivisions = 0;       // lake: cells per side
		std::vector<Vertex> RiverVertices;   // river: RiverColumns vertices per row, rows along the flow
		std::vector<uint32_t> RiverIndices;

		bool IsRiver() const { return !RiverVertices.empty(); }
	};

	inline constexpr int kRiverColumns = 25;      // vertices across a river ribbon
	inline constexpr float kRiverStep = 0.75f;    // meters between rows along it

	WaterGeometry MakeLakeGeometry(float size, uint32_t subdivisions);

	// A ribbon of flowing water along a river: the surface follows the water level, the shader advects the surface
	// detail along the current and adds foam in the rapids. The ribbon is resampled to kRiverStep meters along and
	// kRiverColumns across (waves need far more vertices than the carve path has). A river that ends in a lake
	// (RiverData::MouthLength > 0) is extended a few meters into it, flares open and fades out, so the two surfaces
	// overlap instead of meeting in a hard edge. A river with fewer than two points gives an empty geometry.
	WaterGeometry MakeRiverGeometry(const RiverData& river, float widthScale = 1.5f);
}
