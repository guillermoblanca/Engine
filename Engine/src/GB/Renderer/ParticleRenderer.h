#pragma once
#include "GB/Renderer/RHI/RHI.h"

#include "Camera.h"
#include "Framebuffer.h"
#include "Particles.h"
#include <glm/mat4x4.hpp>
#include <memory>
#include <vector>

namespace GB
{
	struct Scene;

	class ParticleRenderer
	{
	public:
		ParticleRenderer();
		~ParticleRenderer();
		ParticleRenderer(const ParticleRenderer&) = delete;
		ParticleRenderer& operator=(const ParticleRenderer&) = delete;

		bool IsValid() const;
		// The camera (its matrices and axes, near and far) and the fog come from the frame block (FrameConstants), bound by Renderer::Render;
		// `camera` sorts the particles here.
		void ParticlePass(const Scene& scene, const Camera& camera, Framebuffer& sceneMSAA, Framebuffer& sceneDepth);

	private:
		void DrawBatch(const ParticleBatch& batch);

		std::unique_ptr<class Shader> m_Shader;
		std::unique_ptr<class Texture2D> m_SpriteSoft, m_SpriteSmoke, m_SpriteSpark, m_SpriteStreak;
		RHI::VertexLayoutHandle m_VAO;
		RHI::BufferHandle m_VBO;
		RHI::PipelineHandle m_Pipeline;   // depth test without writes, no culling (the blend is set per batch)
		RHI::PipelineHandle m_End;        // depth writes on and blending off again for the passes after
		std::vector<ParticleInstance> m_Scratch;
	};
}
