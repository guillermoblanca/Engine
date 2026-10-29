#pragma once

#include "Framebuffer.h"
#include "Scene.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace GB
{
	// Renders the scene's shadow casters into a caller-owned depth framebuffer.
	//
	// Skinning uses the same SSBO contract as the forward shaders: joint matrices
	// are uploaded to binding 0 for each skinned object.
	class ShadowRenderer
	{
	public:
		ShadowRenderer();
		~ShadowRenderer();
		ShadowRenderer(const ShadowRenderer&) = delete;
		ShadowRenderer& operator=(const ShadowRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		// The framebuffer must have a depth attachment (normally DepthAsTexture).
		// Its viewport is selected by Framebuffer::Bind().
		void Render(const Scene& scene, const Framebuffer& target, const glm::mat4& lightVP);

	private:
		void BindSkin(const RenderObject& object);

		Shader m_Shader;
		uint32_t m_JointBuffer = 0;
	};
}
