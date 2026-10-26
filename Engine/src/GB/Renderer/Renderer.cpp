#include "Renderer.h"
#include "Shaders.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace GB
{
	Renderer::Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples, uint32_t shadowMapSize)
		: m_Width(std::max(width, 1u)), m_Height(std::max(height, 1u)),
		  m_Samples(std::max(msaaSamples, 1u)), m_ShadowSize(shadowMapSize)
	{
		const std::string sky = Shaders::SkyCommon;
		m_ShadowShader = std::make_unique<Shader>(Shaders::ShadowVS, Shaders::ShadowFS);
		m_SkyShader = std::make_unique<Shader>(Shaders::FullscreenVS,
			std::string(Shaders::SkyFS) + sky + Shaders::SkyMain);
		m_BloomDown = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::BloomDownFS);
		m_BloomUp = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::BloomUpFS);
		m_Composite = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::CompositeFS);

		// The PBR shader needs the sky functions too: splice them in after the #version line.
		{
			std::string fs = Shaders::PBRFS;
			const size_t firstLine = fs.find('\n') + 1;
			fs.insert(firstLine, sky);
			m_PBRShader = std::make_unique<Shader>(Shaders::PBRVS, fs);
		}

		glCreateVertexArrays(1, &m_EmptyVAO);

		FramebufferSpec shadow;
		shadow.Width = shadow.Height = m_ShadowSize;
		shadow.Format = ColorFormat::None;
		shadow.DepthAsTexture = true;
		m_ShadowMap = std::make_unique<Framebuffer>(shadow);

		CreateTargets();

		m_Valid = m_ShadowShader->IsValid() && m_PBRShader->IsValid() && m_SkyShader->IsValid() &&
			m_BloomDown->IsValid() && m_BloomUp->IsValid() && m_Composite->IsValid() &&
			m_ShadowMap->IsComplete() && m_SceneMSAA->IsComplete() && m_SceneResolved->IsComplete();
		if (!m_Valid)
			GB_CORE_ERROR("Renderer failed to initialize");
	}

	Renderer::~Renderer()
	{
		glDeleteVertexArrays(1, &m_EmptyVAO);
	}

	void Renderer::CreateTargets()
	{
		FramebufferSpec spec;
		spec.Width = m_Width;
		spec.Height = m_Height;
		spec.Format = ColorFormat::RGBA16F;
		spec.Samples = m_Samples;
		m_SceneMSAA = std::make_unique<Framebuffer>(spec);

		spec.Samples = 1;
		spec.Depth = false;
		m_SceneResolved = std::make_unique<Framebuffer>(spec);

		m_BloomChain.clear();
		const uint32_t smallest = std::min(m_Width, m_Height);
		const uint32_t levels = std::clamp(static_cast<uint32_t>(std::log2(static_cast<float>(smallest))) - 2u, 1u, 6u);
		for (uint32_t i = 0; i < levels; ++i)
		{
			FramebufferSpec b;
			b.Width = std::max(m_Width >> (i + 1), 1u);
			b.Height = std::max(m_Height >> (i + 1), 1u);
			b.Format = ColorFormat::RGBA16F;
			b.Depth = false;
			m_BloomChain.push_back(std::make_unique<Framebuffer>(b));
		}
	}

	void Renderer::Resize(uint32_t width, uint32_t height)
	{
		width = std::max(width, 1u);
		height = std::max(height, 1u);
		if (width == m_Width && height == m_Height)
			return;
		m_Width = width;
		m_Height = height;
		CreateTargets();
	}

	void Renderer::ShadowPass(const Scene& scene, const glm::mat4& lightVP)
	{
		m_ShadowMap->Bind();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(2.0f, 4.0f);

		m_ShadowShader->Bind();
		m_ShadowShader->SetMat4("uLightVP", lightVP);
		for (const RenderObject& obj : scene.Objects)
		{
			if (!obj.MeshPtr || obj.Mat.Alpha == AlphaMode::Blend)
				continue;
			m_ShadowShader->SetMat4("uModel", obj.Transform);
			const bool displaced = obj.Mat.HeightMap && obj.Mat.HeightScale != 0.0f;
			m_ShadowShader->SetInt("uHasHeight", displaced ? 1 : 0);
			if (displaced)
			{
				m_ShadowShader->SetFloat("uHeightScale", obj.Mat.HeightScale);
				m_ShadowShader->SetFloat("uUVScale", obj.Mat.UVScale);
				obj.Mat.HeightMap->Bind(3);
			}
			obj.MeshPtr->Draw();
		}
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	void Renderer::ScenePass(const Scene& scene, const Camera& camera, const glm::mat4& lightVP, bool shadows)
	{
		const Environment& env = scene.Env;
		const float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
		const glm::mat4 viewProj = camera.GetProjection(aspect) * camera.GetView();

		m_SceneMSAA->Bind();
		glEnable(GL_MULTISAMPLE);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glDepthMask(GL_TRUE);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		const glm::vec3 sunDir = glm::normalize(scene.Sun.Direction);
		const glm::vec3 sunColor = scene.Sun.Color * scene.Sun.Intensity;

		auto setSky = [&](Shader& s)
		{
			s.SetVec3("uSkyTop", env.SkyTop);
			s.SetVec3("uSkyHorizon", env.SkyHorizon);
			s.SetVec3("uSkyGround", env.SkyGround);
			s.SetFloat("uEnvIntensity", env.Intensity);
			s.SetVec3("uSunDir", sunDir);
			s.SetVec3("uSunColor", sunColor);
		};

		// Background sky
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		m_SkyShader->Bind();
		setSky(*m_SkyShader);
		m_SkyShader->SetMat4("uInvViewProj", glm::inverse(viewProj));
		m_SkyShader->SetVec3("uCamPos", camera.Position);
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		// Opaque geometry
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		Shader& pbr = *m_PBRShader;
		pbr.Bind();
		setSky(pbr);
		pbr.SetMat4("uViewProj", viewProj);
		pbr.SetVec3("uCamPos", camera.Position);
		pbr.SetInt("uShadowsOn", shadows ? 1 : 0);
		pbr.SetMat4("uLightVP", lightVP);
		pbr.SetFloat("uShadowTexel", 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowSize));
		glBindTextureUnit(0, m_ShadowMap->GetDepthTexture());

		const int pointCount = static_cast<int>(std::min<size_t>(scene.PointLights.size(), MaxPointLights));
		glm::vec3 positions[MaxPointLights] = {}, colors[MaxPointLights] = {};
		for (int i = 0; i < pointCount; ++i)
		{
			positions[i] = scene.PointLights[i].Position;
			colors[i] = scene.PointLights[i].Color * scene.PointLights[i].Intensity;
		}
		pbr.SetInt("uPointCount", pointCount);
		if (pointCount > 0)
		{
			pbr.SetVec3Array("uPointPos", positions, pointCount);
			pbr.SetVec3Array("uPointColor", colors, pointCount);
		}

		auto drawObject = [&](const RenderObject& obj)
		{
			const Material& m = obj.Mat;
			if (m.DoubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);

			pbr.SetMat4("uModel", obj.Transform);
			pbr.SetVec3("uAlbedo", m.Albedo);
			pbr.SetFloat("uMetallic", m.Metallic);
			pbr.SetFloat("uRoughness", m.Roughness);
			pbr.SetFloat("uAO", m.AO);
			pbr.SetVec3("uEmissive", m.Emissive);

			pbr.SetInt("uHasAlbedoMap", m.AlbedoMap ? 1 : 0);
			pbr.SetInt("uHasNormalMap", m.NormalMap ? 1 : 0);
			pbr.SetInt("uHasHeightMap", m.HeightMap ? 1 : 0);
			pbr.SetInt("uHasHeight", m.HeightMap && m.HeightScale != 0.0f ? 1 : 0);
			pbr.SetInt("uHasMRMap", m.MetallicRoughnessMap ? 1 : 0);
			pbr.SetInt("uHasOcclusionMap", m.OcclusionMap ? 1 : 0);
			pbr.SetInt("uHasEmissiveMap", m.EmissiveMap ? 1 : 0);
			pbr.SetFloat("uHeightScale", m.HeightScale);
			pbr.SetFloat("uUVScale", m.UVScale);
			pbr.SetFloat("uNormalStrength", m.NormalStrength);
			pbr.SetVec2("uNormalFlip", { m.NormalFlipX ? -1.0f : 1.0f, m.NormalFlipY ? -1.0f : 1.0f });
			pbr.SetFloat("uCavityStrength", m.CavityStrength);
			pbr.SetFloat("uOcclusionStrength", m.OcclusionStrength);
			pbr.SetInt("uAlphaMode", static_cast<int>(m.Alpha));
			pbr.SetFloat("uAlphaCutoff", m.AlphaCutoff);
			pbr.SetFloat("uOpacity", m.Opacity);
			if (m.AlbedoMap) m.AlbedoMap->Bind(1);
			if (m.NormalMap) m.NormalMap->Bind(2);
			if (m.HeightMap) m.HeightMap->Bind(3);
			if (m.MetallicRoughnessMap) m.MetallicRoughnessMap->Bind(4);
			if (m.OcclusionMap) m.OcclusionMap->Bind(5);
			if (m.EmissiveMap) m.EmissiveMap->Bind(6);
			obj.MeshPtr->Draw();
		};

		// Opaque and alpha-masked first, then translucent objects back to front without depth writes.
		std::vector<const RenderObject*> translucent;
		for (const RenderObject& obj : scene.Objects)
		{
			if (!obj.MeshPtr)
				continue;
			if (obj.Mat.Alpha == AlphaMode::Blend)
				translucent.push_back(&obj);
			else
				drawObject(obj);
		}

		if (!translucent.empty())
		{
			auto distance2 = [&](const RenderObject* o)
			{
				const glm::vec3 d = glm::vec3(o->Transform[3]) - camera.Position;
				return glm::dot(d, d);
			};
			std::sort(translucent.begin(), translucent.end(),
				[&](const RenderObject* a, const RenderObject* b) { return distance2(a) > distance2(b); });

			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthMask(GL_FALSE);
			for (const RenderObject* obj : translucent)
				drawObject(*obj);
			glDepthMask(GL_TRUE);
			glDisable(GL_BLEND);
		}
		glDisable(GL_CULL_FACE);

		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);
	}

	void Renderer::BloomPass(const Environment& env)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glBindVertexArray(m_EmptyVAO);

		// Down: scene -> level 0 (with threshold) -> level N-1
		m_BloomDown->Bind();
		uint32_t srcTexture = m_SceneResolved->GetColorTexture();
		uint32_t srcW = m_Width, srcH = m_Height;
		for (size_t i = 0; i < m_BloomChain.size(); ++i)
		{
			Framebuffer& dst = *m_BloomChain[i];
			dst.Bind();
			glBindTextureUnit(0, srcTexture);
			m_BloomDown->SetVec2("uTexel", { 1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH) });
			m_BloomDown->SetInt("uPrefilter", i == 0 ? 1 : 0);
			m_BloomDown->SetFloat("uThreshold", env.BloomThreshold);
			m_BloomDown->SetFloat("uKnee", env.BloomThreshold * 0.5f);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			srcTexture = dst.GetColorTexture();
			srcW = dst.GetWidth();
			srcH = dst.GetHeight();
		}

		// Up: additive tent filter from the smallest level back to level 0
		m_BloomUp->Bind();
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		for (size_t i = m_BloomChain.size() - 1; i > 0; --i)
		{
			const Framebuffer& src = *m_BloomChain[i];
			m_BloomChain[i - 1]->Bind();
			glBindTextureUnit(0, src.GetColorTexture());
			m_BloomUp->SetVec2("uTexel", { 1.0f / static_cast<float>(src.GetWidth()), 1.0f / static_cast<float>(src.GetHeight()) });
			m_BloomUp->SetFloat("uRadius", env.BloomRadius);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
		glDisable(GL_BLEND);
	}

	void Renderer::CompositePass(const Environment& env, const Framebuffer* target)
	{
		if (target)
			target->Bind();
		else
			Framebuffer::BindDefault(m_Width, m_Height);

		glDisable(GL_DEPTH_TEST);
		m_Composite->Bind();
		glBindTextureUnit(0, m_SceneResolved->GetColorTexture());
		glBindTextureUnit(1, m_BloomChain.front()->GetColorTexture());
		m_Composite->SetFloat("uExposure", env.Exposure);
		m_Composite->SetFloat("uBloomIntensity", env.BloomIntensity);
		m_Composite->SetFloat("uVignette", env.Vignette);
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glEnable(GL_DEPTH_TEST);
	}

	void Renderer::Render(const Scene& scene, const Camera& camera, const Framebuffer* target)
	{
		if (!m_Valid)
			return;

		glm::mat4 lightVP(1.0f);
		const bool shadows = scene.Sun.CastShadows;
		if (shadows)
		{
			const Environment& env = scene.Env;
			const glm::vec3 dir = glm::normalize(scene.Sun.Direction);
			const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
			const glm::mat4 view = glm::lookAt(env.ShadowCenter - dir * env.ShadowRadius * 2.0f, env.ShadowCenter, up);
			const float r = env.ShadowRadius;
			lightVP = glm::ortho(-r, r, -r, r, 0.0f, r * 4.0f) * view;
			ShadowPass(scene, lightVP);
		}

		ScenePass(scene, camera, lightVP, shadows);
		BloomPass(scene.Env);
		CompositePass(scene.Env, target);
	}
}
