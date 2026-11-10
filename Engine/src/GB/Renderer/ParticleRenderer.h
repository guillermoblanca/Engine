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
		void ParticlePass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
			Framebuffer& sceneMSAA, Framebuffer& sceneDepth);

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
