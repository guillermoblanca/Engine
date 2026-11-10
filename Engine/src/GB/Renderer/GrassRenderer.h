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
		GrassRenderer();
		~GrassRenderer();
		GrassRenderer(const GrassRenderer&) = delete;
		GrassRenderer& operator=(const GrassRenderer&) = delete;

		bool IsValid() const { return m_Shader.IsValid(); }

		// The camera, sun, sky, fog, shadow and wind values come from the frame block (FrameConstants), bound by Renderer::Render with the
		// environment map; `camera` only culls the chunks here.
		void Render(const Scene& scene, const Camera& camera, RHI::TextureHandle shadowTexture);

	private:
		Shader m_Shader;
		RHI::VertexLayoutHandle m_GrassVAO;   // a blade per instance; the chunk's range of the field's buffer is bound per draw
		RHI::PipelineHandle m_Pipeline;       // depth, no culling, alpha to coverage
	};
}
