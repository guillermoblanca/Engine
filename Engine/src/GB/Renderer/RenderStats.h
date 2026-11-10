#pragma once
#include "GB/Renderer/RHI/Counters.h"
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
		// T40: the same frame split finer, for the budget of PLAN_RENDER §3 (each is a part of one of the times above, which keep their meaning):
		// the clear and the sky (of GpuSceneMs); the translucent objects, the particles and the MSAA resolve (of GpuEffectsMs); the bloom, the SSAO
		// (with its depth copy) and the exposure and composite (of GpuPostMs).
		float GpuSkyMs = 0.0f, GpuTranslucentMs = 0.0f, GpuParticlesMs = 0.0f, GpuResolveMs = 0.0f, GpuBloomMs = 0.0f, GpuSsaoMs = 0.0f, GpuCompositeMs = 0.0f;
		// T70 (render M0): what the frame asked of the RHI backend and what reached the API (uniforms, binds, state, bytes uploaded), for the
		// whole frame and for the object loop alone; and the CPU time of that loop (culling, the draws of the opaque and masked objects, the
		// instanced batches and the translucent objects), of this frame, without the grass, water and particle passes.
		RHI::FrameCounters Submit, ObjectSubmit;
		float CpuObjectsMs = 0.0f;
		float CpuObjectDrawMs = 0.0f;   // ... of it, inside the draws of the objects (uniforms, binds, the draw call); the rest is culling and batching
		float CpuShadowMs = 0.0f;       // the CPU time of the shadow pass (its own culling and draws), for the rest of "sending"
		RHI::FrameCounters ShadowSubmit;
	};

	// Counts the draw calls as they are made (the mesh and the instanced draws call Add): the renderer reads it around each pass.
	struct DrawCounter
	{
		static inline uint32_t Calls = 0;
		static inline uint64_t Triangles = 0;
		static void Add(uint64_t triangles) { ++Calls; Triangles += triangles; }
	};
}
