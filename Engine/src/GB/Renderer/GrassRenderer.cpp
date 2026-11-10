#include "GrassRenderer.h"
#include "GrassGpu.h"
#include "GroundInteraction.h"
#include "RenderStats.h"
#include "GB/World/Grass.h"
#include "Shaders.h"
#include <algorithm>

namespace GB
{
	GrassRenderer::GrassRenderer(uint32_t shadowMapSize)
		: m_Shader(
			Shaders::Assemble(Shaders::GrassVS),
			Shaders::Assemble(Shaders::GrassFS)),
		  m_ShadowMapSize(std::max(shadowMapSize, 1u))
	{
		RHI::Device& device = RHI::GetDevice();
		RHI::VertexLayoutDesc layout;
		layout.Bindings.push_back({ 0, {}, 0, 0, 1 });
		for (uint32_t i = 0; i < 3; ++i)
			layout.Attribs.push_back({ i, 4, RHI::AttribType::Float, i * 16, 0 });
		m_GrassVAO = device.CreateVertexLayout(layout);
		RHI::PipelineDesc pipeline;
		pipeline.Program = m_Shader.GetHandle();
		pipeline.CullMode = RHI::Cull::None;
		pipeline.DepthTest = true;
		pipeline.DepthWrite = true;
		pipeline.AlphaToCoverage = true;
		m_Pipeline = device.CreatePipeline(pipeline);
	}

	GrassRenderer::~GrassRenderer()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyVertexLayout(m_GrassVAO);
		device.DestroyPipeline(m_Pipeline);
	}

	void GrassRenderer::BindEnvironment(const Environment& env)
	{
		const bool has = env.EnvMap && env.EnvMap->IsValid();
		m_Shader.SetInt("uHasEnvMap", has ? 1 : 0);
		if (!has)
			return;
		m_Shader.SetFloat("uEnvMaxLod", static_cast<float>(env.EnvMap->GetMipCount() - 1));
		m_Shader.SetFloat("uEnvRotation", env.EnvMap->Rotation / 6.2831853f);
		m_Shader.SetFloat("uEnvExposure", env.EnvMap->Exposure);
		RHI::GetDevice().Commands().BindTexture(9, env.EnvMap->GetHandle());
	}

	void GrassRenderer::Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
		const glm::mat4& lightVP, bool shadows, RHI::TextureHandle shadowTexture)
	{
		if (scene.Grass.empty())
			return;

		const Environment& env = scene.Env;
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BindPipeline(m_Pipeline);
		m_Shader.SetMat4("uViewProj", viewProj);
		m_Shader.SetVec3("uCamPos", camera.Position);
		m_Shader.SetFloat("uTime", env.Time);
		m_Shader.SetVec3("uWind", env.Wind);
		m_Shader.SetFloat("uSnowCover", env.SnowCover);
		m_Shader.SetFloat("uWetness", env.Wetness);
		BindEnvironment(env);
		m_Shader.SetVec3("uSkyTop", env.SkyTop);
		m_Shader.SetVec3("uSkyHorizon", env.SkyHorizon);
		m_Shader.SetVec3("uSkyGround", env.SkyGround);
		m_Shader.SetFloat("uEnvIntensity", env.Intensity);
		m_Shader.SetVec3("uSunDir", glm::normalize(scene.Sun.Direction));
		m_Shader.SetVec3("uSunColor", scene.Sun.Color * scene.Sun.Intensity);
		m_Shader.SetVec3("uFogColor", env.FogColor);
		m_Shader.SetFloat("uFogDensity", env.FogDensity);
		m_Shader.SetFloat("uFogHeight", env.FogHeightFalloff);
		m_Shader.SetInt("uShadowsOn", shadows ? 1 : 0);
		m_Shader.SetMat4("uLightVP", lightVP);
		m_Shader.SetFloat("uShadowTexel", 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowMapSize));
		cmd.BindTexture(0, shadowTexture);

		const bool trails = scene.Ground && scene.Ground->IsValid();
		if (trails)
		{
			m_Shader.SetVec4("uTrailRect", scene.Ground->GetRect());
			cmd.BindTexture(8, { scene.Ground->GetTexture() });
		}

		cmd.BindVertexLayout(m_GrassVAO);

		const glm::vec3 forward = camera.GetForward();
		for (const GrassGpu* gpu : scene.Grass)
		{
			if (!gpu || gpu->GetField().GetBladeCount() == 0)
				continue;
			const GrassField* field = &gpu->GetField();
			const RHI::BufferHandle buffer = gpu->GetHandle();
			const GrassParams& p = field->Params;
			const float drawDistance = p.DrawDistance * std::max(scene.GrassDistanceScale, 0.0f);
			if (drawDistance <= 0.0f)
				continue;   // grass off
			m_Shader.SetFloat("uDrawDistance", drawDistance);
			m_Shader.SetFloat("uTrailBend", trails ? p.TrailBend : 0.0f);
			m_Shader.SetVec3("uBase", p.BaseColor);
			m_Shader.SetVec3("uTip", p.TipColor);
			m_Shader.SetVec3("uDry", p.DryColor);
			m_Shader.SetFloat("uDryAmount", p.DryAmount);
			for (const GrassChunk& chunk : field->GetChunks())
			{
				const glm::vec3 d = chunk.Center - camera.Position;
				if (glm::length(d) - chunk.Radius > drawDistance || glm::dot(d, forward) < -chunk.Radius)
					continue;
				cmd.SetVertexBuffer(m_GrassVAO, 0, buffer, static_cast<size_t>(chunk.First) * sizeof(GrassBlade), sizeof(GrassBlade));
				cmd.DrawInstanced(RHI::Topology::TriangleStrip, 0, 10, chunk.Count);
				DrawCounter::Add(8ull * chunk.Count);
			}
		}
		cmd.SetAlphaToCoverage(false);
	}
}
