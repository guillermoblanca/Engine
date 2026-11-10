#pragma once

#include "Framebuffer.h"
#include "RenderList.h"
#include "Scene.h"
#include "Shader.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

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
		// The pass clears its depth and draws at its size. It draws the casters of `list`, culled with the frustum of `lightVP`
		// (RenderList::Cull; T78: the culling is shared with the scene pass): in scene order, and the instanced batches after them.
		void Render(const Scene& scene, const RenderList& list, const Framebuffer& target, const glm::mat4& lightVP);

	private:
		void BindSkin(const RenderObject& object);

		std::vector<BatchEntry> m_Batches;   // the instanced casters of the frame, sorted by mesh (kept between frames)
		std::vector<glm::mat4> m_Matrices;   // the matrices of one batch

		Shader m_Shader;
		RHI::BufferHandle m_JointBuffer;
		RHI::BufferHandle m_InstanceBuffer;   // the matrices of one instanced draw
		RHI::PipelineHandle m_Pipeline;       // depth test and writes, polygon offset
		RHI::PipelineHandle m_End;            // the polygon offset off again for the passes after
	};
}
