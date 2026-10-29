#include "ParticleRenderer.h"
#include "Image.h"
#include "Scene.h"
#include "Shader.h"
#include "Shaders.h"
#include "Texture.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>

namespace GB
{
	namespace
	{
		float Hash2(int x, int y)
		{
			uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return static_cast<float>((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
		}

		float ValueNoise(float x, float y)
		{
			const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			const float fx = x - static_cast<float>(ix), fy = y - static_cast<float>(iy);
			const float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
			const float a = Hash2(ix, iy), b = Hash2(ix + 1, iy), c = Hash2(ix, iy + 1), d = Hash2(ix + 1, iy + 1);
			return glm::mix(glm::mix(a, b, ux), glm::mix(c, d, ux), uy);
		}

		Image MakeSprite(ParticleSprite kind, uint32_t size)
		{
			Image image = Image::Solid(size, size, 255, 255, 255, 0);
			for (uint32_t y = 0; y < size; ++y)
			{
				for (uint32_t x = 0; x < size; ++x)
				{
					const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
					const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
					const float r = std::sqrt(u * u + v * v);
					float a = 0.0f;
					if (kind == ParticleSprite::Smoke)
					{
						float n = 0.0f, amp = 0.5f, freq = 3.0f;
						for (int o = 0; o < 4; ++o, amp *= 0.5f, freq *= 2.0f)
							n += amp * ValueNoise((u + 3.0f) * freq, (v + 3.0f) * freq);
						a = glm::smoothstep(1.0f, 0.3f, r) * glm::clamp(0.3f + 1.1f * n, 0.0f, 1.0f);
					}
					else if (kind == ParticleSprite::Streak)
						a = std::pow(std::max(1.0f - std::fabs(v), 0.0f), 2.6f) * (1.0f - glm::smoothstep(0.55f, 1.0f, std::fabs(u)));
					else if (kind == ParticleSprite::Spark)
						a = std::pow(std::max(1.0f - r, 0.0f), 2.2f);
					else
						a = std::pow(std::max(1.0f - r, 0.0f), 1.6f);
					image.Pixels[(static_cast<size_t>(y) * size + x) * 4 + 3] =
						static_cast<uint8_t>(glm::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}
			return image;
		}
	}

	ParticleRenderer::ParticleRenderer()
	{
		m_Shader = std::make_unique<Shader>(Shaders::ParticleVS, Shaders::ParticleFS);
		m_SpriteSoft = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::SoftCircle, 64), TextureEncoding::Linear);
		m_SpriteSmoke = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Smoke, 128), TextureEncoding::Linear);
		m_SpriteSpark = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Spark, 32), TextureEncoding::Linear);
		m_SpriteStreak = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Streak, 64), TextureEncoding::Linear);
		glCreateBuffers(1, &m_VBO);
		glCreateVertexArrays(1, &m_VAO);
		glVertexArrayVertexBuffer(m_VAO, 0, m_VBO, 0, sizeof(ParticleInstance));
		glVertexArrayBindingDivisor(m_VAO, 0, 1);
		for (GLuint i = 0; i < 4; ++i)
		{
			glEnableVertexArrayAttrib(m_VAO, i);
			glVertexArrayAttribFormat(m_VAO, i, 4, GL_FLOAT, GL_FALSE, i * 16);
			glVertexArrayAttribBinding(m_VAO, i, 0);
		}
	}

	ParticleRenderer::~ParticleRenderer()
	{
		glDeleteBuffers(1, &m_VBO);
		glDeleteVertexArrays(1, &m_VAO);
	}

	bool ParticleRenderer::IsValid() const
	{
		return m_Shader && m_Shader->IsValid();
	}

	void ParticleRenderer::DrawBatch(const ParticleBatch& batch)
	{
		if (!batch.Instances || batch.Instances->empty())
			return;
		const size_t bytes = batch.Instances->size() * sizeof(ParticleInstance);
		glNamedBufferData(m_VBO, static_cast<GLsizeiptr>(bytes), batch.Instances->data(), GL_STREAM_DRAW);
		glBlendFunc(GL_SRC_ALPHA, batch.Blend == ParticleBlend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

		const Texture2D* sprite = m_SpriteSoft.get();
		if (batch.Sprite == ParticleSprite::Smoke) sprite = m_SpriteSmoke.get();
		else if (batch.Sprite == ParticleSprite::Spark) sprite = m_SpriteSpark.get();
		else if (batch.Sprite == ParticleSprite::Streak) sprite = m_SpriteStreak.get();
		else if (batch.Sprite == ParticleSprite::Custom && batch.CustomTexture) sprite = batch.CustomTexture;
		sprite->Bind(0);

		m_Shader->SetFloat("uStretch", batch.Stretch);
		m_Shader->SetVec2("uSheet", batch.Sheet);
		m_Shader->SetFloat("uSoft", batch.SoftDistance);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(batch.Instances->size()));
	}

	void ParticleRenderer::ParticlePass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
		Framebuffer& sceneMSAA, Framebuffer& sceneDepth)
	{
		if (scene.Particles.empty() && scene.ParticleBatches.empty())
			return;

		const glm::mat4 view = camera.GetView();
		const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
		const Environment& env = scene.Env;
		Framebuffer::BlitDepth(sceneMSAA, sceneDepth);
		sceneMSAA.Bind();

		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		m_Shader->Bind();
		m_Shader->SetMat4("uViewProj", viewProj);
		m_Shader->SetVec3("uCamRight", right);
		m_Shader->SetVec3("uCamUp", up);
		m_Shader->SetVec3("uCamPos", camera.Position);
		m_Shader->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		m_Shader->SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		m_Shader->SetVec3("uFogColor", env.FogColor);
		m_Shader->SetFloat("uFogDensity", env.FogDensity);
		m_Shader->SetFloat("uFogHeight", env.FogHeightFalloff);
		glBindTextureUnit(1, sceneDepth.GetDepthTexture());
		glBindVertexArray(m_VAO);

		for (const ParticleSystem* system : scene.Particles)
		{
			if (!system)
				continue;
			system->BuildInstances(camera.Position, m_Scratch);
			DrawBatch(system->MakeBatch(m_Scratch));
		}
		for (const ParticleBatch& batch : scene.ParticleBatches)
			DrawBatch(batch);

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
}
