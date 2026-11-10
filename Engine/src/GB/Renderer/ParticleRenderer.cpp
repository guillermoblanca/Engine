#include "ParticleRenderer.h"
#include "RenderStats.h"
#include "GB/Scene/Image.h"
#include "Scene.h"
#include "Shader.h"
#include "Shaders.h"
#include "Texture.h"
#include <algorithm>
#include <cmath>

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
		RHI::Device& device = RHI::GetDevice();
		m_VBO = device.CreateStreamBuffer();
		RHI::VertexLayoutDesc layout;
		layout.Bindings.push_back({ 0, m_VBO, 0, sizeof(ParticleInstance), 1 });
		for (uint32_t i = 0; i < 4; ++i)
			layout.Attribs.push_back({ i, 4, RHI::AttribType::Float, i * 16, 0 });
		m_VAO = device.CreateVertexLayout(layout);
		RHI::PipelineDesc pipeline;
		pipeline.Program = m_Shader->GetHandle();
		pipeline.DepthTest = true;
		pipeline.DepthWrite = false;
		pipeline.CullMode = RHI::Cull::None;
		m_Pipeline = device.CreatePipeline(pipeline);
		RHI::PipelineDesc end;
		end.DepthWrite = true;
		end.Blend = RHI::BlendState{};
		m_End = device.CreatePipeline(end);
	}

	ParticleRenderer::~ParticleRenderer()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyBuffer(m_VBO);
		device.DestroyVertexLayout(m_VAO);
		device.DestroyPipeline(m_Pipeline);
		device.DestroyPipeline(m_End);
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
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.UploadStream(m_VBO, bytes, batch.Instances->data());
		cmd.SetBlend({ true, RHI::BlendFactor::SrcAlpha, batch.Blend == ParticleBlend::Additive ? RHI::BlendFactor::One : RHI::BlendFactor::OneMinusSrcAlpha, {} });

		const Texture2D* sprite = m_SpriteSoft.get();
		if (batch.Sprite == ParticleSprite::Smoke) sprite = m_SpriteSmoke.get();
		else if (batch.Sprite == ParticleSprite::Spark) sprite = m_SpriteSpark.get();
		else if (batch.Sprite == ParticleSprite::Streak) sprite = m_SpriteStreak.get();
		else if (batch.Sprite == ParticleSprite::Custom && batch.CustomTexture) sprite = batch.CustomTexture;
		sprite->Bind(0);

		m_Shader->SetFloat("uStretch", batch.Stretch);
		m_Shader->SetVec2("uSheet", batch.Sheet);
		m_Shader->SetFloat("uSoft", batch.SoftDistance);
		DrawCounter::Add(2ull * batch.Instances->size());
		cmd.DrawInstanced(RHI::Topology::TriangleStrip, 0, 4, static_cast<uint32_t>(batch.Instances->size()));
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
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BeginPass(sceneMSAA.Pass("particles"));
		cmd.BindPipeline(m_Pipeline);
		m_Shader->SetMat4("uViewProj", viewProj);
		m_Shader->SetVec3("uCamRight", right);
		m_Shader->SetVec3("uCamUp", up);
		m_Shader->SetVec3("uCamPos", camera.Position);
		m_Shader->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		m_Shader->SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		m_Shader->SetVec3("uFogColor", env.FogColor);
		m_Shader->SetFloat("uFogDensity", env.FogDensity);
		m_Shader->SetFloat("uFogHeight", env.FogHeightFalloff);
		cmd.BindTexture(1, sceneDepth.DepthTexture());
		cmd.BindVertexLayout(m_VAO);

		for (const ParticleSystem* system : scene.Particles)
		{
			if (!system)
				continue;
			system->BuildInstances(camera.Position, m_Scratch);
			DrawBatch(system->MakeBatch(m_Scratch));
		}
		for (const ParticleBatch& batch : scene.ParticleBatches)
			DrawBatch(batch);

		cmd.BindPipeline(m_End);
	}
}
