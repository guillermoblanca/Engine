#include "Renderer.h"
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

		// White RGB with a procedural alpha mask (the particle color comes from the emitter).
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
					{
						// Long axis along u: flat core, soft ends; gaussian-ish across v.
						a = std::pow(std::max(1.0f - std::fabs(v), 0.0f), 2.6f) * (1.0f - glm::smoothstep(0.55f, 1.0f, std::fabs(u)));
					}
					else if (kind == ParticleSprite::Spark)
						a = std::pow(std::max(1.0f - r, 0.0f), 2.2f);
					else
						a = std::pow(std::max(1.0f - r, 0.0f), 1.6f);
					image.Pixels[(static_cast<size_t>(y) * size + x) * 4 + 3] = static_cast<uint8_t>(glm::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			}
			return image;
		}
	}

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
		glCreateBuffers(1, &m_JointBuffer);

		m_ParticleShader = std::make_unique<Shader>(Shaders::ParticleVS, Shaders::ParticleFS);
		m_SpriteSoft = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::SoftCircle, 64), TextureEncoding::Linear);
		m_SpriteSmoke = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Smoke, 128), TextureEncoding::Linear);
		m_SpriteSpark = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Spark, 32), TextureEncoding::Linear);
		m_SpriteStreak = std::make_unique<Texture2D>(MakeSprite(ParticleSprite::Streak, 64), TextureEncoding::Linear);
		{
			const std::string sky2 = Shaders::SkyCommon;
			m_GrassShader = std::make_unique<Shader>(
				std::string(Shaders::GrassVS) + Shaders::GrassTrailCommon + Shaders::GrassVSMain,
				std::string(Shaders::GrassFS) + sky2 + Shaders::GrassFSMain);
			glCreateVertexArrays(1, &m_GrassVAO);
			glVertexArrayBindingDivisor(m_GrassVAO, 0, 1);
			for (GLuint i = 0; i < 2; ++i)
			{
				glEnableVertexArrayAttrib(m_GrassVAO, i);
				glVertexArrayAttribFormat(m_GrassVAO, i, 4, GL_FLOAT, GL_FALSE, i * 16);
				glVertexArrayAttribBinding(m_GrassVAO, i, 0);
			}
		}
		{
			const std::string sky3 = Shaders::SkyCommon;
			m_WaterShader = std::make_unique<Shader>(
				std::string(Shaders::WaterVS) + Shaders::WaterWaves + Shaders::WaterVSMain,
				std::string(Shaders::WaterFS) + sky3 + Shaders::WaterWaves + Shaders::WaterFSMain);
		}
		glCreateBuffers(1, &m_ParticleVBO);
		glCreateVertexArrays(1, &m_ParticleVAO);
		glVertexArrayVertexBuffer(m_ParticleVAO, 0, m_ParticleVBO, 0, sizeof(ParticleInstance));
		glVertexArrayBindingDivisor(m_ParticleVAO, 0, 1);
		for (GLuint i = 0; i < 4; ++i)
		{
			glEnableVertexArrayAttrib(m_ParticleVAO, i);
			glVertexArrayAttribFormat(m_ParticleVAO, i, 4, GL_FLOAT, GL_FALSE, i * 16);
			glVertexArrayAttribBinding(m_ParticleVAO, i, 0);
		}

		FramebufferSpec shadow;
		shadow.Width = shadow.Height = m_ShadowSize;
		shadow.Format = ColorFormat::None;
		shadow.DepthAsTexture = true;
		m_ShadowMap = std::make_unique<Framebuffer>(shadow);

		CreateTargets();

		m_Valid = m_ShadowShader->IsValid() && m_PBRShader->IsValid() && m_SkyShader->IsValid() &&
			m_BloomDown->IsValid() && m_BloomUp->IsValid() && m_Composite->IsValid() && m_ParticleShader->IsValid() && m_GrassShader->IsValid() && m_WaterShader->IsValid() &&
			m_ShadowMap->IsComplete() && m_SceneMSAA->IsComplete() && m_SceneResolved->IsComplete() && m_SceneDepth->IsComplete();
		if (!m_Valid)
			GB_CORE_ERROR("Renderer failed to initialize");
	}

	Renderer::~Renderer()
	{
		glDeleteVertexArrays(1, &m_EmptyVAO);
		glDeleteBuffers(1, &m_JointBuffer);
		glDeleteBuffers(1, &m_ParticleVBO);
		glDeleteVertexArrays(1, &m_ParticleVAO);
		glDeleteVertexArrays(1, &m_GrassVAO);
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
		m_ShadowShader->SetFloat("uTime", scene.Env.Time);
		m_ShadowShader->SetVec3("uWind", scene.Env.Wind);
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
			m_ShadowShader->SetFloat("uWindStrength", obj.Mat.WindSway);
			m_ShadowShader->SetFloat("uWindHeight", obj.Mat.WindHeight);
			m_ShadowShader->SetFloat("uWindFlutter", obj.Mat.WindFlutter);
			BindSkin(*m_ShadowShader, obj);
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

		ParticlePass(scene, camera, viewProj);

		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);
	}

	void Renderer::GrassPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows)
	{
		if (scene.Grass.empty())
			return;
		const Environment& env = scene.Env;
		Shader& g = *m_GrassShader;
		g.Bind();
		g.SetMat4("uViewProj", viewProj);
		g.SetVec3("uCamPos", camera.Position);
		g.SetFloat("uTime", env.Time);
		g.SetVec3("uWind", env.Wind);
		g.SetFloat("uSnowCover", env.SnowCover);
		g.SetFloat("uWetness", env.Wetness);
		BindEnvironment(g, env);
		g.SetVec3("uSkyTop", env.SkyTop);
		g.SetVec3("uSkyHorizon", env.SkyHorizon);
		g.SetVec3("uSkyGround", env.SkyGround);
		g.SetFloat("uEnvIntensity", env.Intensity);
		g.SetVec3("uSunDir", glm::normalize(scene.Sun.Direction));
		g.SetVec3("uSunColor", scene.Sun.Color * scene.Sun.Intensity);
		g.SetVec3("uFogColor", env.FogColor);
		g.SetFloat("uFogDensity", env.FogDensity);
		g.SetFloat("uFogHeight", env.FogHeightFalloff);
		g.SetInt("uShadowsOn", shadows ? 1 : 0);
		g.SetMat4("uLightVP", lightVP);
		g.SetFloat("uShadowTexel", 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowSize));
		glBindTextureUnit(0, m_ShadowMap->GetDepthTexture());
		const bool trails = scene.Ground && scene.Ground->IsValid();
		if (trails)
		{
			g.SetVec4("uTrailRect", scene.Ground->GetRect());
			glBindTextureUnit(8, scene.Ground->GetTexture());
		}

		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);   // soft blade edges with the MSAA target
		glBindVertexArray(m_GrassVAO);

		const glm::vec3 forward = camera.GetForward();
		for (const GrassField* field : scene.Grass)
		{
			if (!field || field->GetBladeCount() == 0)
				continue;
			const uint32_t buffer = field->GetBuffer();
			const GrassParams& p = field->Params;
			g.SetFloat("uDrawDistance", p.DrawDistance);
			g.SetFloat("uTrailBend", trails ? p.TrailBend : 0.0f);
			g.SetVec3("uBase", p.BaseColor);
			g.SetVec3("uTip", p.TipColor);
			g.SetVec3("uDry", p.DryColor);
			g.SetFloat("uDryAmount", p.DryAmount);
			for (const GrassChunk& chunk : field->GetChunks())
			{
				const glm::vec3 d = chunk.Center - camera.Position;
				if (glm::length(d) - chunk.Radius > p.DrawDistance || glm::dot(d, forward) < -chunk.Radius)
					continue;
				glVertexArrayVertexBuffer(m_GrassVAO, 0, buffer, static_cast<GLintptr>(chunk.First) * sizeof(GrassBlade), sizeof(GrassBlade));
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 10, static_cast<GLsizei>(chunk.Count));
			}
		}
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
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
		if (scene.Water.empty())
			return;
		const Environment& env = scene.Env;
		// The water reads the depth of everything opaque to measure how deep it is.
		Framebuffer::BlitDepth(*m_SceneMSAA, *m_SceneDepth);
		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);   // what the water refracts
		m_SceneMSAA->Bind();

		Shader& w = *m_WaterShader;
		w.Bind();
		const bool ssr = scene.ScreenSpaceReflections;
		w.SetMat4("uViewProj", viewProj);
		w.SetVec3("uCamPos", camera.Position);
		w.SetMat4("uViewProj", viewProj);
		w.SetFloat("uSSR", ssr ? 1.0f : 0.0f);
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
			w.SetInt("uPointCount", pointCount);
			if (pointCount > 0)
			{
				w.SetVec3Array("uPointPos", positions, pointCount);
				w.SetVec3Array("uPointColor", colors, pointCount);
				w.SetFloatArray("uPointRadius", radii, pointCount);
			}
		}
		w.SetFloat("uTime", env.Time);
		w.SetVec3("uSkyTop", env.SkyTop);
		w.SetVec3("uSkyHorizon", env.SkyHorizon);
		w.SetVec3("uSkyGround", env.SkyGround);
		w.SetFloat("uEnvIntensity", env.Intensity);
		w.SetVec3("uSunDir", glm::normalize(scene.Sun.Direction));
		w.SetVec3("uSunColor", scene.Sun.Color * scene.Sun.Intensity);
		w.SetVec3("uFogColor", env.FogColor);
		w.SetFloat("uFogDensity", env.FogDensity);
		w.SetFloat("uFogHeight", env.FogHeightFalloff);
		w.SetFloat("uSnowCover", env.SnowCover);
		w.SetFloat("uWetness", env.Wetness);
		BindEnvironment(w, env);
		w.SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		w.SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		glBindTextureUnit(1, m_SceneDepth->GetDepthTexture());
		glBindTextureUnit(3, m_SceneResolved->GetColorTexture());
		const bool ripples = scene.Ripples && scene.Ripples->IsValid();
		if (ripples)
		{
			w.SetVec4("uRippleRect", scene.Ripples->GetRect());
			glBindTextureUnit(2, scene.Ripples->GetTexture());
		}
		else
			w.SetVec4("uRippleRect", glm::vec4(0.0f));

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
			w.SetVec3("uOrigin", water->Position);
			w.SetInt("uRiver", water->IsRiver() ? 1 : 0);
			w.SetFloat("uOriginY", water->Position.y);
			w.SetVec3("uOriginXZ", water->Position);
			w.SetFloat("uMask", water->Radius);
			w.SetVec2("uWaveDir", dir);
			w.SetFloat("uWaveHeight", p.WaveHeight);
			w.SetFloat("uWaveLength", p.WaveLength);
			w.SetFloat("uSteep", p.Steepness);
			w.SetFloat("uChop", p.Choppiness);
			w.SetVec3("uShallow", p.ShallowColor);
			w.SetVec3("uDeep", p.DeepColor);
			w.SetFloat("uClarity", p.Clarity);
			w.SetFloat("uRoughness", p.Roughness);
			w.SetFloat("uFoam", p.Foam);
			w.SetFloat("uFoamWidth", p.FoamWidth);
			w.SetFloat("uRippleStrength", p.RippleStrength);
			water->GetMesh().Draw();
		}
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
	}

	void Renderer::DrawBatch(const ParticleBatch& batch)
	{
		if (!batch.Instances || batch.Instances->empty())
			return;
		const size_t bytes = batch.Instances->size() * sizeof(ParticleInstance);
		glNamedBufferData(m_ParticleVBO, static_cast<GLsizeiptr>(bytes), batch.Instances->data(), GL_STREAM_DRAW);

		if (batch.Blend == ParticleBlend::Additive)
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		else
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		const Texture2D* sprite = m_SpriteSoft.get();
		if (batch.Sprite == ParticleSprite::Smoke) sprite = m_SpriteSmoke.get();
		else if (batch.Sprite == ParticleSprite::Spark) sprite = m_SpriteSpark.get();
		else if (batch.Sprite == ParticleSprite::Streak) sprite = m_SpriteStreak.get();
		else if (batch.Sprite == ParticleSprite::Custom && batch.CustomTexture) sprite = batch.CustomTexture;
		sprite->Bind(0);

		m_ParticleShader->SetFloat("uStretch", batch.Stretch);
		m_ParticleShader->SetVec2("uSheet", batch.Sheet);
		m_ParticleShader->SetFloat("uSoft", batch.SoftDistance);
		glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(batch.Instances->size()));
	}

	void Renderer::ParticlePass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj)
	{
		if (scene.Particles.empty() && scene.ParticleBatches.empty())
			return;

		const glm::mat4 view = camera.GetView();
		const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
		const Environment& env = scene.Env;

		// Single-sample depth for soft particles.
		Framebuffer::BlitDepth(*m_SceneMSAA, *m_SceneDepth);
		m_SceneMSAA->Bind();

		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);     // particles are tested against the scene but never occlude each other
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		m_ParticleShader->Bind();
		m_ParticleShader->SetMat4("uViewProj", viewProj);
		m_ParticleShader->SetVec3("uCamRight", right);
		m_ParticleShader->SetVec3("uCamUp", up);
		m_ParticleShader->SetVec3("uCamPos", camera.Position);
		m_ParticleShader->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		m_ParticleShader->SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		m_ParticleShader->SetVec3("uFogColor", env.FogColor);
		m_ParticleShader->SetFloat("uFogDensity", env.FogDensity);
		m_ParticleShader->SetFloat("uFogHeight", env.FogHeightFalloff);
		glBindTextureUnit(1, m_SceneDepth->GetDepthTexture());
		glBindVertexArray(m_ParticleVAO);

		for (const ParticleSystem* system : scene.Particles)
		{
			if (!system)
				continue;
			system->BuildInstances(camera.Position, m_ParticleScratch);
			DrawBatch(system->MakeBatch(m_ParticleScratch));
		}
		for (const ParticleBatch& batch : scene.ParticleBatches)
			DrawBatch(batch);

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
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
