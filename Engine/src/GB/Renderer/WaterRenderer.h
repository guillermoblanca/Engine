#pragma once

#include "Camera.h"
#include "Framebuffer.h"
#include "Scene.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <cstdint>

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
		~WaterRenderer() = default;
		WaterRenderer(const WaterRenderer&) = delete;
		WaterRenderer& operator=(const WaterRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		void Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
			const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth);

	private:
		void BindEnvironment(const Environment& env);

		Shader m_Shader;
	};
}
