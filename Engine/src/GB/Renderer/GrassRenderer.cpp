#include "GrassRenderer.h"
#include "Shaders.h"
#include <algorithm>
#include <glad/gl.h>

namespace GB
{
	GrassRenderer::GrassRenderer(uint32_t shadowMapSize)
		: m_Shader(
			std::string(Shaders::GrassVS) + Shaders::GrassTrailCommon + Shaders::GrassVSMain,
			std::string(Shaders::GrassFS) + Shaders::SkyCommon + Shaders::GrassFSMain),
		  m_ShadowMapSize(std::max(shadowMapSize, 1u))
	{
		glCreateVertexArrays(1, &m_GrassVAO);
		glVertexArrayBindingDivisor(m_GrassVAO, 0, 1);
		for (GLuint i = 0; i < 2; ++i)
		{
			glEnableVertexArrayAttrib(m_GrassVAO, i);
			glVertexArrayAttribFormat(m_GrassVAO, i, 4, GL_FLOAT, GL_FALSE, i * 16);
			glVertexArrayAttribBinding(m_GrassVAO, i, 0);
		}
	}

	GrassRenderer::~GrassRenderer()
	{
		glDeleteVertexArrays(1, &m_GrassVAO);
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
		glBindTextureUnit(9, env.EnvMap->GetID());
	}

	void GrassRenderer::Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
		const glm::mat4& lightVP, bool shadows, uint32_t shadowTexture)
	{
		if (scene.Grass.empty())
			return;

		const Environment& env = scene.Env;
		m_Shader.Bind();
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
		glBindTextureUnit(0, shadowTexture);

		const bool trails = scene.Ground && scene.Ground->IsValid();
		if (trails)
		{
			m_Shader.SetVec4("uTrailRect", scene.Ground->GetRect());
			glBindTextureUnit(8, scene.Ground->GetTexture());
		}

		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		glBindVertexArray(m_GrassVAO);

		const glm::vec3 forward = camera.GetForward();
		for (const GrassField* field : scene.Grass)
		{
			if (!field || field->GetBladeCount() == 0)
				continue;
			const uint32_t buffer = field->GetBuffer();
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
				glVertexArrayVertexBuffer(m_GrassVAO, 0, buffer,
					static_cast<GLintptr>(chunk.First) * sizeof(GrassBlade), sizeof(GrassBlade));
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 10, static_cast<GLsizei>(chunk.Count));
			}
		}
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	}
}
