#pragma once

#include "Camera.h"
#include "Framebuffer.h"
#include "Scene.h"
#include "Shader.h"
#include "Mesh.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace GB
{
	// Renders translucent water after the opaque scene. The supplied framebuffers are
	// used to resolve the scene depth and color that water samples for depth tinting
	// and refraction.
	//
	// T138: the lakes of Scene::Lakes are drawn from their real mesh (one vertex and index buffer, one draw per lake with its level and
	// look), with the depth and the distance to the shore of every vertex; the lake surfaces of Scene::Water that stand for those lakes
	// are skipped. Without a lake mesh (or for a lake it has no triangles for) a lake surface is drawn as before: a grid masked to a disc.
	class WaterRenderer
	{
	public:
		WaterRenderer();
		~WaterRenderer();
		WaterRenderer(const WaterRenderer&) = delete;
		WaterRenderer& operator=(const WaterRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		// The camera, sun, sky, fog, point lights and time come from the frame block (FrameConstants), bound by Renderer::Render with the
		// environment map.
		void Render(const Scene& scene, const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth);

	private:
		// The GPU mesh of a surface, uploaded from its CPU geometry the first time it is drawn and kept while the geometry lives.
		const Mesh& MeshFor(const WaterSurface& surface);

		// The GPU copy of a LakeMesh (LakeVertex layout, stride 36), uploaded the first time it is drawn and kept while it lives.
		struct LakeGpu
		{
			std::weak_ptr<const LakeMesh> Source;
			const LakeMesh* Key = nullptr;
			RHI::BufferHandle Vertices, Indices;
			RHI::VertexLayoutHandle Layout;
			float Spacing = 1.0f;   // m between the samples of the grid the mesh lies on
		};
		const LakeGpu* LakesFor(const std::shared_ptr<const LakeMesh>& lakes);
		void ReleaseLakes();
		void SetLook(const WaterParams& p);

		struct CachedMesh
		{
			std::weak_ptr<const WaterGeometry> Source;   // tells a live geometry from a dead one whose address was reused
			std::unique_ptr<Mesh> Gpu;
		};

		Shader m_Shader;
		RHI::PipelineHandle m_Pipeline;   // depth test without writes, no culling, alpha blending
		RHI::PipelineHandle m_End;        // depth writes on and blending off again for the passes after
		std::unordered_map<const WaterGeometry*, CachedMesh> m_Meshes;
		LakeGpu m_Lakes;
	};
}
