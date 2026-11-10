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
	class WaterRenderer
	{
	public:
		static constexpr uint32_t MaxPointLights = 32;

		WaterRenderer();
		~WaterRenderer();
		WaterRenderer(const WaterRenderer&) = delete;
		WaterRenderer& operator=(const WaterRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		void Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
			const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth);

	private:
		void BindEnvironment(const Environment& env);
		// The GPU mesh of a surface, uploaded from its CPU geometry the first time it is drawn and kept while the geometry lives.
		const Mesh& MeshFor(const WaterSurface& surface);

		struct CachedMesh
		{
			std::weak_ptr<const WaterGeometry> Source;   // tells a live geometry from a dead one whose address was reused
			std::unique_ptr<Mesh> Gpu;
		};

		Shader m_Shader;
		RHI::PipelineHandle m_Pipeline;   // depth test without writes, no culling, alpha blending
		RHI::PipelineHandle m_End;        // depth writes on and blending off again for the passes after
		std::unordered_map<const WaterGeometry*, CachedMesh> m_Meshes;
	};
}
