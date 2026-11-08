#pragma once
#include <cstdint>

namespace GB
{
	struct RenderStats
	{
		uint32_t ObjectsDrawn = 0;
		uint32_t ObjectsCulled = 0;
		uint32_t InstancedDraws = 0;   // calls that drew many copies of one object at once (RenderObject::Instanced)
		// The draw calls of the last frame, by pass, and the triangles they sent (DrawCounter: every Mesh draw, the grass, the particles)
		uint32_t DrawCalls = 0;        // all of them
		uint32_t ShadowCalls = 0;      // ... of the shadow map
		uint32_t SceneCalls = 0;       // ... of the scene's objects (opaque, masked, translucent)
		uint32_t GrassCalls = 0;       // ... of the grass chunks
		uint32_t WaterCalls = 0;       // ... of the water
		uint64_t Triangles = 0;
		// What the GPU spent on each pass, measured with timestamp queries (the frame before the last: the results are read without waiting)
		float GpuShadowMs = 0.0f, GpuSceneMs = 0.0f, GpuGrassMs = 0.0f, GpuWaterMs = 0.0f, GpuEffectsMs = 0.0f, GpuPostMs = 0.0f, GpuTotalMs = 0.0f;
	};

	// Counts the draw calls as they are made (the mesh and the instanced draws call Add): the renderer reads it around each pass.
	struct DrawCounter
	{
		static inline uint32_t Calls = 0;
		static inline uint64_t Triangles = 0;
		static void Add(uint64_t triangles) { ++Calls; Triangles += triangles; }
	};
}
