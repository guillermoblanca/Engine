#include "WaterRenderer.h"
#include "Shaders.h"
#include <algorithm>
#include <glad/gl.h>
#include <string>

namespace GB
{
	WaterRenderer::WaterRenderer()
		: m_Shader(
			std::string(Shaders::WaterVS) + Shaders::WaterWaves + Shaders::WaterVSMain,
			std::string(Shaders::WaterFS) + Shaders::SkyCommon + Shaders::WaterWaves + Shaders::WaterFSMain)
	{
	}

	void WaterRenderer::BindEnvironment(const Environment& env)
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

	void WaterRenderer::Render(const Scene& scene, const Camera& camera, const glm::mat4& viewProj,
		const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth)
	{
		if (scene.Water.empty())
			return;

		const Environment& env = scene.Env;
		// The water reads the depth of everything opaque to measure how deep it is.
		Framebuffer::BlitDepth(sceneMSAA, sceneDepth);
		Framebuffer::BlitColor(sceneMSAA, sceneResolved);   // what the water refracts
		sceneMSAA.Bind();

		m_Shader.Bind();
		const bool ssr = scene.ScreenSpaceReflections;
		m_Shader.SetMat4("uViewProj", viewProj);
		m_Shader.SetVec3("uCamPos", camera.Position);
		m_Shader.SetMat4("uViewProj", viewProj);
		m_Shader.SetFloat("uSSR", ssr ? 1.0f : 0.0f);
		{
			const int pointCount = static_cast<int>(std::min<size_t>(scene.PointLights.size(), MaxPointLights));
			glm::vec3 positions[MaxPointLights] = {}, colors[MaxPointLights] = {};
			float radii[MaxPointLights] = {};
			for (int i = 0; i < pointCount; ++i)
			{
				positions[i] = scene.PointLights[i].Position;
				colors[i] = scene.PointLights[i].Color * scene.PointLights[i].Intensity;
				radii[i] = scene.PointLights[i].Radius;
			}
			m_Shader.SetInt("uPointCount", pointCount);
			if (pointCount > 0)
			{
				m_Shader.SetVec3Array("uPointPos", positions, pointCount);
				m_Shader.SetVec3Array("uPointColor", colors, pointCount);
				m_Shader.SetFloatArray("uPointRadius", radii, pointCount);
			}
		}
		m_Shader.SetFloat("uTime", env.Time);
		m_Shader.SetVec3("uSkyTop", env.SkyTop);
		m_Shader.SetVec3("uSkyHorizon", env.SkyHorizon);
		m_Shader.SetVec3("uSkyGround", env.SkyGround);
		m_Shader.SetFloat("uEnvIntensity", env.Intensity);
		m_Shader.SetVec3("uSunDir", glm::normalize(scene.Sun.Direction));
		m_Shader.SetVec3("uSunColor", scene.Sun.Color * scene.Sun.Intensity);
		m_Shader.SetVec3("uFogColor", env.FogColor);
		m_Shader.SetFloat("uFogDensity", env.FogDensity);
		m_Shader.SetFloat("uFogHeight", env.FogHeightFalloff);
		m_Shader.SetFloat("uSnowCover", env.SnowCover);
		m_Shader.SetFloat("uWetness", env.Wetness);
		BindEnvironment(env);
		m_Shader.SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		m_Shader.SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		glBindTextureUnit(1, sceneDepth.GetDepthTexture());
		glBindTextureUnit(3, sceneResolved.GetColorTexture());
		const bool ripples = scene.Ripples && scene.Ripples->IsValid();
		if (ripples)
		{
			m_Shader.SetVec4("uRippleRect", scene.Ripples->GetRect());
			glBindTextureUnit(2, scene.Ripples->GetTexture());
		}
		else
			m_Shader.SetVec4("uRippleRect", glm::vec4(0.0f));

		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		for (const WaterSurface* water : scene.Water)
		{
			if (!water)
				continue;
			const WaterParams& p = water->Params;
			const glm::vec2 dir = glm::length(p.Direction) > 1e-4f ? glm::normalize(p.Direction) : glm::vec2(1.0f, 0.0f);
			m_Shader.SetVec3("uOrigin", water->Position);
			m_Shader.SetInt("uRiver", water->IsRiver() ? 1 : 0);
			m_Shader.SetFloat("uOriginY", water->Position.y);
			m_Shader.SetVec3("uOriginXZ", water->Position);
			m_Shader.SetFloat("uMask", water->Radius);
			m_Shader.SetVec2("uWaveDir", dir);
			m_Shader.SetFloat("uWaveHeight", p.WaveHeight);
			m_Shader.SetFloat("uWaveLength", p.WaveLength);
			m_Shader.SetFloat("uSteep", p.Steepness);
			m_Shader.SetFloat("uChop", p.Choppiness);
			m_Shader.SetVec3("uShallow", p.ShallowColor);
			m_Shader.SetVec3("uDeep", p.DeepColor);
			m_Shader.SetFloat("uClarity", p.Clarity);
			m_Shader.SetFloat("uRoughness", p.Roughness);
			m_Shader.SetFloat("uFoam", p.Foam);
			m_Shader.SetFloat("uFoamWidth", p.FoamWidth);
			m_Shader.SetFloat("uRippleStrength", p.RippleStrength);
			water->GetMesh().Draw();
		}
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}
}
