#pragma once

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
		uint32_t m_VAO = 0, m_VBO = 0;
		std::vector<ParticleInstance> m_Scratch;
	};
}
