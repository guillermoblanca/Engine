#include "Renderer.h"
#include "ParticleRenderer.h"
#include "Visibility.h"
#include "Shaders.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	namespace
	{
	}

	Renderer::Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples, uint32_t shadowMapSize)
		: m_Width(std::max(width, 1u)), m_Height(std::max(height, 1u)),
		  m_Samples(std::max(msaaSamples, 1u)), m_ShadowSize(shadowMapSize)
	{
		const std::string sky = Shaders::SkyCommon;
		m_SkyShader = std::make_unique<Shader>(Shaders::FullscreenVS,
			std::string(Shaders::SkyFS) + sky + Shaders::SkyMain);

		// The PBR shader needs the sky functions too: splice them in after the #version line.
		{
			std::string fs = Shaders::PBRFS;
			const size_t firstLine = fs.find('\n') + 1;
			fs.insert(firstLine, sky);
			m_PBRShader = std::make_unique<Shader>(Shaders::PBRVS, fs);
		}

		glCreateVertexArrays(1, &m_EmptyVAO);
		glCreateBuffers(1, &m_JointBuffer);

		m_ParticleRenderer = std::make_unique<ParticleRenderer>();

		FramebufferSpec shadow;
		shadow.Width = shadow.Height = m_ShadowSize;
		shadow.Format = ColorFormat::None;
		shadow.DepthAsTexture = true;
		m_ShadowMap = std::make_unique<Framebuffer>(shadow);

		CreateTargets();
		m_PostProcessor = std::make_unique<PostProcessor>(m_Width, m_Height);
		m_GrassRenderer = std::make_unique<GrassRenderer>(m_ShadowSize);
		m_WaterRenderer = std::make_unique<WaterRenderer>();
		m_ShadowRenderer = std::make_unique<ShadowRenderer>();

		m_Valid = m_ShadowRenderer->IsValid() && m_PBRShader->IsValid() && m_SkyShader->IsValid() &&
			m_ParticleRenderer->IsValid() && m_GrassRenderer->IsValid() && m_WaterRenderer->IsValid() &&
			m_ShadowMap->IsComplete() && m_SceneMSAA->IsComplete() && m_SceneResolved->IsComplete() &&
			m_SceneDepth->IsComplete() && m_PostProcessor->IsValid();
		if (!m_Valid)
			GB_CORE_ERROR("Renderer failed to initialize");
	}

	Renderer::~Renderer()
	{
		glDeleteVertexArrays(1, &m_EmptyVAO);
		glDeleteBuffers(1, &m_JointBuffer);
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

		// Single-sample copy of the scene depth (soft particles, water depth...).
		FramebufferSpec depthSpec;
		depthSpec.Width = m_Width;
		depthSpec.Height = m_Height;
		depthSpec.Format = ColorFormat::None;
		depthSpec.Depth = true;
		depthSpec.DepthAsTexture = true;
		depthSpec.DepthCompare = false;
		m_SceneDepth = std::make_unique<Framebuffer>(depthSpec);

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
		m_PostProcessor->Resize(width, height);
	}

	void Renderer::BindSkin(Shader& shader, const RenderObject& obj)
	{
		const bool skinned = obj.JointMatrices && !obj.JointMatrices->empty() && obj.MeshPtr->HasSkin();
		shader.SetInt("uSkinned", skinned ? 1 : 0);
		if (!skinned)
			return;
		glNamedBufferData(m_JointBuffer, static_cast<GLsizeiptr>(obj.JointMatrices->size() * sizeof(glm::mat4)),
			obj.JointMatrices->data(), GL_STREAM_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_JointBuffer);
	}

	void Renderer::ShadowPass(const Scene& scene, const glm::mat4& lightVP)
	{
		m_ShadowRenderer->Render(scene, *m_ShadowMap, lightVP);
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
			s.SetVec3("uFogColor", env.FogColor);
			s.SetFloat("uFogDensity", env.FogDensity);
			s.SetFloat("uFogHeight", env.FogHeightFalloff);
			s.SetFloat("uWetness", env.Wetness);
			s.SetFloat("uSnowCover", env.SnowCover);
			BindEnvironment(s, env);
		};

		// Background sky
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		m_SkyShader->Bind();
		setSky(*m_SkyShader);
		m_SkyShader->SetMat4("uInvViewProj", glm::inverse(viewProj));
		m_SkyShader->SetVec3("uCamPos", camera.Position);
		m_SkyShader->SetFloat("uStars", env.Stars);
		m_SkyShader->SetFloat("uCloudCover", env.CloudCover);
		m_SkyShader->SetFloat("uTime", env.Time);
		m_SkyShader->SetVec3("uWind", env.Wind);
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
		pbr.SetFloat("uTime", env.Time);
		pbr.SetVec3("uWind", env.Wind);
		if (scene.Ground && scene.Ground->IsValid())
		{
			pbr.SetVec4("uTrailRect", scene.Ground->GetRect());
			glBindTextureUnit(8, scene.Ground->GetTexture());
		}
		pbr.SetMat4("uLightVP", lightVP);
		pbr.SetFloat("uShadowTexel", 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowSize));
		glBindTextureUnit(0, m_ShadowMap->GetDepthTexture());

		const int pointCount = static_cast<int>(std::min<size_t>(scene.PointLights.size(), MaxPointLights));
		glm::vec3 positions[MaxPointLights] = {}, colors[MaxPointLights] = {};
		float radii[MaxPointLights] = {};
		for (int i = 0; i < pointCount; ++i)
		{
			positions[i] = scene.PointLights[i].Position;
			colors[i] = scene.PointLights[i].Color * scene.PointLights[i].Intensity;
			radii[i] = scene.PointLights[i].Radius;
		}
		pbr.SetInt("uPointCount", pointCount);
		if (pointCount > 0)
		{
			pbr.SetVec3Array("uPointPos", positions, pointCount);
			pbr.SetVec3Array("uPointColor", colors, pointCount);
			pbr.SetFloatArray("uPointRadius", radii, pointCount);
		}

		auto drawObject = [&](const RenderObject& obj)
		{
			const Material& m = obj.Mat;
			if (m.DoubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);

			pbr.SetMat4("uModel", obj.Transform);
			BindSkin(pbr, obj);
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
			pbr.SetFloat("uWindStrength", m.WindSway);
			pbr.SetFloat("uWindHeight", m.WindHeight);
			pbr.SetFloat("uWindFlutter", m.WindFlutter);
			pbr.SetInt("uNoNormalFlip", m.FoliageNormals ? 1 : 0);
			pbr.SetInt("uHasMacroMap", m.MacroMap ? 1 : 0);
			pbr.SetInt("uHasTerrainNormals", m.SurfaceNormalMap && m.MacroMap ? 1 : 0);
			if (m.SurfaceNormalMap && m.MacroMap) m.SurfaceNormalMap->Bind(10);
			pbr.SetFloat("uGrain", m.Grain);
			pbr.SetFloat("uGrainScale", m.GrainScale);
			pbr.SetFloat("uMoss", m.Moss);
			pbr.SetVec3("uMossColor", m.MossColor);
			pbr.SetFloat("uSand", m.Sand);
			pbr.SetVec3("uSandColor", m.SandColor);
			pbr.SetFloat("uBaseY", m.BaseY);
			pbr.SetFloat("uWaterLine", m.WaterLine);
			const bool trail = scene.Ground && scene.Ground->IsValid() && m.TrailDepth > 0.0f;
			pbr.SetFloat("uTrailDepth", trail ? m.TrailDepth : 0.0f);
			if (trail)
			{
				pbr.SetVec3("uTrailTint", m.TrailTint);
				pbr.SetFloat("uTrailBoost", m.TrailNormalBoost);
			}
			if (m.MacroMap)
			{
				pbr.SetVec4("uMacroRect", m.MacroRect);
				m.MacroMap->Bind(7);
			}
			pbr.SetFloat("uHeightScale", m.HeightScale);
			pbr.SetFloat("uUVScale", m.UVScale);
			pbr.SetFloat("uNormalStrength", m.NormalStrength);
			pbr.SetVec2("uNormalFlip", { m.NormalFlipX ? -1.0f : 1.0f, m.NormalFlipY ? -1.0f : 1.0f });
			pbr.SetFloat("uCavityStrength", m.CavityStrength);
			pbr.SetFloat("uOcclusionStrength", m.OcclusionStrength);
			pbr.SetInt("uAlphaMode", static_cast<int>(m.Alpha));
			const bool coverage = m.Alpha == AlphaMode::Mask && m_Samples > 1;   // leaf edges antialiased by the MSAA target
			pbr.SetInt("uAlphaToCoverage", coverage ? 1 : 0);
			if (coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
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
		m_Stats = {};
		const Frustum frustum = MakeFrustum(viewProj);
		for (const RenderObject& obj : scene.Objects)
		{
			if (!obj.MeshPtr)
				continue;
			glm::vec3 bc; float br;
			if (BoundsOf(obj, bc, br))
			{
				float maxDistance = obj.MaxDistance;
				if (scene.DrawDistance > 0.0f) maxDistance = maxDistance > 0.0f ? std::min(maxDistance, scene.DrawDistance) : scene.DrawDistance;
				const bool tooFar = maxDistance > 0.0f && glm::distance(bc, camera.Position) - br > maxDistance;
				if (tooFar || !InFrustum(frustum, bc, br))
				{
					++m_Stats.ObjectsCulled;
					continue;
				}
			}
			++m_Stats.ObjectsDrawn;
			if (obj.Mat.Alpha == AlphaMode::Blend)
				translucent.push_back(&obj);
			else
				drawObject(obj);
		}
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

		GrassPass(scene, camera, viewProj, lightVP, shadows);
		WaterPass(scene, camera, viewProj);

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

		m_ParticleRenderer->ParticlePass(scene, camera, viewProj, *m_SceneMSAA, *m_SceneDepth);

		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);
	}

	void Renderer::GrassPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows)
	{
		m_GrassRenderer->Render(scene, camera, viewProj, lightVP, shadows, m_ShadowMap->GetDepthTexture());
	}

	void Renderer::BindEnvironment(Shader& shader, const Environment& env) const
	{
		const bool has = env.EnvMap && env.EnvMap->IsValid();
		shader.SetInt("uHasEnvMap", has ? 1 : 0);
		if (!has)
			return;
		shader.SetFloat("uEnvMaxLod", static_cast<float>(env.EnvMap->GetMipCount() - 1));
		shader.SetFloat("uEnvRotation", env.EnvMap->Rotation / 6.2831853f);
		shader.SetFloat("uEnvExposure", env.EnvMap->Exposure);
		glBindTextureUnit(9, env.EnvMap->GetID());
	}

	void Renderer::WaterPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj)
	{
		m_WaterRenderer->Render(scene, camera, viewProj, *m_SceneMSAA, *m_SceneResolved, *m_SceneDepth);
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
		m_PostProcessor->BloomPass(*m_SceneResolved, scene.Env);
		{
			const PostSettings& post = scene.Post;
			const bool ao = post.SSAO > 0.0f && !camera.Orthographic, dof = post.DepthOfField > 0.0f && !camera.Orthographic;
			if (ao || dof)
				Framebuffer::BlitDepth(*m_SceneMSAA, *m_SceneDepth);
			if (ao)
				m_PostProcessor->AmbientOcclusionPass(*m_SceneDepth, post, camera);
			if (post.AutoExposure)
				m_PostProcessor->AdaptationPass(*m_SceneResolved, post);
		}
		m_PostProcessor->CompositePass(*m_SceneResolved, *m_SceneDepth, scene.Env, scene.Post, camera, target);
	}
}
