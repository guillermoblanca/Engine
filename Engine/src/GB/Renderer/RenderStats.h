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
		// T78: the culling is shared now (RenderList, at the start of Render): CpuObjectsMs also holds building the object list and culling it
		// for the camera and for the shadow map (CpuCullMs), and CpuShadowMs is only the shadow pass's draws and batches.
		RHI::FrameCounters Submit, ObjectSubmit;
		float CpuObjectsMs = 0.0f;
		float CpuObjectDrawMs = 0.0f;   // ... of it, inside the draws of the objects (uniforms, binds, the draw call); the rest is culling and batching
		float CpuCullMs = 0.0f;         // ... of it, building the list and culling it for both passes (T78), and preparing the materials of the objects drawn alone (T36)
		float CpuShadowMs = 0.0f;       // the CPU time of the shadow pass (its draws and batches), for the rest of "sending"
		RHI::FrameCounters ShadowSubmit;
		// T36 (render M4): the materials by id (MaterialBuffer). The objects drawn alone are prepared before the passes, and each object draw,
		// shadow or scene, resolves its material's id. The first time in a frame an object packs its material and compares it with the entry it
		// had; only a changed or unknown material is hashed, and only a new one is added to the table and uploaded (256 B).
		uint32_t MaterialResolves = 0;   // draws of objects that set uMaterialId
		uint32_t MaterialsPacked = 0;    // objects that packed their material (once per object and frame, prepared or drawn)
		uint32_t MaterialsHashed = 0;    // ... of them, looked up in the table by hash
		uint32_t MaterialsAdded = 0;     // materials added to the table (uploaded this frame)
		uint32_t Materials = 0;          // materials in the table at the end of the frame
		// T34: resolves of the scene for the transmissive translucent objects (glass, ice): 1 in a frame that has one in view, else 0
		uint32_t TransmissionCopies = 0;
	};

	// Counts the draw calls as they are made (the mesh and the instanced draws call Add): the renderer reads it around each pass.
	struct DrawCounter
	{
		static inline uint32_t Calls = 0;
		static inline uint64_t Triangles = 0;
		static void Add(uint64_t triangles) { ++Calls; Triangles += triangles; }
	};
}
