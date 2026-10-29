#pragma once

#include "Camera.h"
#include "Scene.h"
#include "Shader.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace GB
{
	// Renders all grass fields into the currently bound scene framebuffer.
	//
	// The shadow texture is supplied by the owner of the shadow pass. This keeps
	// grass rendering independent from the renderer's framebuffer lifetime while
	// preserving the texture-unit contract used by the grass shader.
	class GrassRenderer
	{
	public:
		explicit GrassRenderer(uint32_t shadowMapSize = 2048);
		~GrassRenderer();
		GrassRenderer(const GrassRenderer&) = delete;
		GrassRenderer& operator=(const GrassRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		void Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
			const glm::mat4& lightVP, bool shadows, uint32_t shadowTexture);

	private:
		void BindEnvironment(const Environment& env);

		Shader m_Shader;
		uint32_t m_ShadowMapSize = 1;
		uint32_t m_GrassVAO = 0;
	};
}
