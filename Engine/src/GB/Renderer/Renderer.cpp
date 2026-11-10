#include "Renderer.h"
#include "GroundInteraction.h"
#include "ParticleRenderer.h"
#include "Visibility.h"
#include "Shaders.h"
#include "ShadowView.h"
#include "GB/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	Renderer::Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples, uint32_t shadowMapSize)
		: m_Width(std::max(width, 1u)), m_Height(std::max(height, 1u)),
		  m_Samples(std::max(msaaSamples, 1u)), m_ShadowSize(shadowMapSize)
	{
		m_SkyShader = std::make_unique<Shader>(Shaders::Assemble(Shaders::FullscreenVS), Shaders::Assemble(Shaders::SkyFS));
		m_PBRShader = std::make_unique<Shader>(Shaders::Assemble(Shaders::PBRVS), Shaders::Assemble(Shaders::PBRFS));

		RHI::Device& device = RHI::GetDevice();
		m_EmptyVAO = device.CreateVertexLayout({});
		m_JointBuffer = device.CreateStreamBuffer();
		m_InstanceBuffer = device.CreateStreamBuffer();
		for (auto& set : m_Timers)
			for (RHI::QueryHandle& q : set)
				q = device.CreateTimestamp();
		{
			RHI::PipelineDesc skyState;
			skyState.Program = m_SkyShader->GetHandle();
			skyState.Multisample = true;
			skyState.DepthTest = false;
			skyState.DepthWrite = false;
			m_SkyPipeline = device.CreatePipeline(skyState);
			RHI::PipelineDesc pbr;
			pbr.Program = m_PBRShader->GetHandle();
			pbr.DepthTest = true;
			pbr.DepthWrite = true;
			pbr.CullMode = RHI::Cull::Back;
			m_PBRPipeline = device.CreatePipeline(pbr);
		}

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
			m_SceneDepth->IsComplete() && m_SceneAmbient->IsComplete() && m_PostProcessor->IsValid();
		if (!m_Valid)
			GB_CORE_ERROR("Renderer failed to initialize");
	}

	Renderer::~Renderer()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyVertexLayout(m_EmptyVAO);
		device.DestroyBuffer(m_JointBuffer);
		device.DestroyBuffer(m_InstanceBuffer);
		for (auto& set : m_Timers)
			for (RHI::QueryHandle q : set)
				device.DestroyTimestamp(q);
		device.DestroyPipeline(m_SkyPipeline);
		device.DestroyPipeline(m_PBRPipeline);
	}

	void Renderer::CreateTargets()
	{
		FramebufferSpec spec;
		spec.Width = m_Width;
		spec.Height = m_Height;
		spec.Format = ColorFormat::RGBA16F;
		spec.Samples = m_Samples;
		spec.Extra = ColorFormat::R11G11B10F;   // location 1: the ambient light (no alpha needed: the blends weigh by the source alpha)
		m_SceneMSAA = std::make_unique<Framebuffer>(spec);

		spec.Samples = 1;
		spec.Depth = false;
		spec.Extra = ColorFormat::None;
		m_SceneResolved = std::make_unique<Framebuffer>(spec);
		FramebufferSpec ambientSpec = spec;
		ambientSpec.Format = ColorFormat::R11G11B10F;
		m_SceneAmbient = std::make_unique<Framebuffer>(ambientSpec);

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

	void Renderer::Mark(int i)
	{
		if (m_TimerActive)
			RHI::GetDevice().Commands().WriteTimestamp(m_Timers[m_TimerHead][i]);
	}

	void Renderer::ReadTimers()
	{
		// Drain the ring oldest first while the GPU has the results (queries finish in order: the last mark of a set says the whole set is
		// done); the newest set read gives the stats. Never waits: a set still in flight stays for a later frame.
		RHI::Device& device = RHI::GetDevice();
		while (m_TimersPending > 0)
		{
			const int set = (m_TimerHead - m_TimersPending + kTimerSets) % kTimerSets;
			if (!device.TimestampReady(m_Timers[set][kMarks - 1]))
				break;
			uint64_t t[kMarks];
			for (int i = 0; i < kMarks; ++i) t[i] = device.TimestampNs(m_Timers[set][i]);
			--m_TimersPending;
			auto ms = [&](int a, int b) { return static_cast<float>(static_cast<double>(t[b] - t[a]) * 1e-6); };
			m_Stats.GpuShadowMs = ms(kStart, kAfterShadows); m_Stats.GpuSceneMs = ms(kAfterShadows, kAfterObjects); m_Stats.GpuGrassMs = ms(kAfterObjects, kAfterGrass);
			m_Stats.GpuWaterMs = ms(kAfterGrass, kAfterWater); m_Stats.GpuEffectsMs = ms(kAfterWater, kAfterResolve); m_Stats.GpuPostMs = ms(kAfterResolve, kAfterComposite);
			m_Stats.GpuTotalMs = ms(kStart, kAfterComposite);
			m_Stats.GpuSkyMs = ms(kAfterShadows, kAfterSky); m_Stats.GpuTranslucentMs = ms(kAfterWater, kAfterTranslucent);
			m_Stats.GpuParticlesMs = ms(kAfterTranslucent, kAfterParticles); m_Stats.GpuResolveMs = ms(kAfterParticles, kAfterResolve);
			m_Stats.GpuBloomMs = ms(kAfterResolve, kAfterBloom); m_Stats.GpuSsaoMs = ms(kAfterBloom, kAfterSsao); m_Stats.GpuCompositeMs = ms(kAfterSsao, kAfterComposite);
		}
	}

	void Renderer::BindSkin(Shader& shader, const RenderObject& obj)
	{
		const bool skinned = obj.JointMatrices && !obj.JointMatrices->empty() && obj.MeshPtr->HasSkin();
		shader.SetInt("uSkinned", skinned ? 1 : 0);
		if (!skinned)
			return;
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.UploadStream(m_JointBuffer, obj.JointMatrices->size() * sizeof(glm::mat4), obj.JointMatrices->data());
		cmd.BindStorageBuffer(0, m_JointBuffer);
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

		// The ambient target is only written when the SSAO will use it (every shader of the scene writes location 1: 0 where there is none).
		const bool ambientOut = scene.Post.SSAO > 0.0f && !camera.Orthographic;
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		m_SceneMSAA->SetExtraEnabled(ambientOut);
		RHI::PassDesc pass = m_SceneMSAA->Pass("scene");
		pass.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		pass.ClearDepth = true;
		cmd.BeginPass(pass);

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
		cmd.BindPipeline(m_SkyPipeline);
		setSky(*m_SkyShader);
		m_SkyShader->SetMat4("uInvViewProj", glm::inverse(viewProj));
		m_SkyShader->SetVec3("uCamPos", camera.Position);
		m_SkyShader->SetFloat("uStars", env.Stars);
		m_SkyShader->SetFloat("uCloudCover", env.CloudCover);
		m_SkyShader->SetFloat("uTime", env.Time);
		m_SkyShader->SetVec3("uWind", env.Wind);
		cmd.BindVertexLayout(m_EmptyVAO);
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		Mark(kAfterSky);

		// Opaque geometry
		Shader& pbr = *m_PBRShader;
		cmd.BindPipeline(m_PBRPipeline);
		setSky(pbr);
		pbr.SetMat4("uViewProj", viewProj);
		pbr.SetVec3("uCamPos", camera.Position);
		pbr.SetInt("uShadowsOn", shadows ? 1 : 0);
		pbr.SetFloat("uTime", env.Time);
		pbr.SetVec3("uWind", env.Wind);
		if (scene.Ground && scene.Ground->IsValid())
		{
			pbr.SetVec4("uTrailRect", scene.Ground->GetRect());
			cmd.BindTexture(8, { scene.Ground->GetTexture() });
		}
		pbr.SetMat4("uLightVP", lightVP);
		pbr.SetFloat("uShadowTexel", 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowSize));
		cmd.BindTexture(0, m_ShadowMap->DepthTexture());

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

		// `copies` > 0: an instanced draw of that many copies, their matrices already in m_InstanceBuffer (the object's material for all)
		using Clock = std::chrono::steady_clock;
		double drawMs = 0.0;   // T70: the part of the object loop spent in here (uniforms, binds, draw), the rest is culling and batching
		auto drawObject = [&](const RenderObject& obj, uint32_t copies = 0)
		{
			const auto drawStart = Clock::now();
			const Material& m = obj.Mat;
			cmd.SetCull(m.DoubleSided ? RHI::Cull::None : RHI::Cull::Back);

			pbr.SetMat4("uModel", obj.Transform);
			pbr.SetInt("uInstanced", copies > 0 ? 1 : 0);
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
			cmd.SetAlphaToCoverage(coverage);
			pbr.SetFloat("uAlphaCutoff", m.AlphaCutoff);
			pbr.SetFloat("uOpacity", m.Opacity);
			if (m.AlbedoMap) m.AlbedoMap->Bind(1);
			if (m.NormalMap) m.NormalMap->Bind(2);
			if (m.HeightMap) m.HeightMap->Bind(3);
			if (m.MetallicRoughnessMap) m.MetallicRoughnessMap->Bind(4);
			if (m.OcclusionMap) m.OcclusionMap->Bind(5);
			if (m.EmissiveMap) m.EmissiveMap->Bind(6);
			if (copies > 0) obj.MeshPtr->DrawInstanced(copies);
			else if (obj.IndexCount > 0) obj.MeshPtr->DrawRange(obj.IndexFirst, obj.IndexCount);
			else obj.MeshPtr->Draw();
			drawMs += std::chrono::duration<double, std::milli>(Clock::now() - drawStart).count();
		};
		// the instanced objects that passed the culling, by mesh, drawn after the loop in one call each
		std::map<const Mesh*, std::vector<const RenderObject*>> instanced;

		// Opaque and alpha-masked first, then translucent objects back to front without depth writes.
		std::vector<const RenderObject*> translucent;
		m_Stats = {};
		// T70: the CPU time and the RHI work of the object loop (this one, the instanced batches and, below, the translucent objects)
		auto loopStart = Clock::now();
		RHI::FrameCounters loopBefore = RHI::Counters::Frame;
		auto endLoop = [&]()
		{
			m_Stats.CpuObjectsMs += static_cast<float>(std::chrono::duration<double, std::milli>(Clock::now() - loopStart).count());
			m_Stats.CpuObjectDrawMs = static_cast<float>(drawMs);
			m_Stats.ObjectSubmit += RHI::Counters::Frame.Since(loopBefore);
		};
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
			else if (obj.Instanced && !obj.JointMatrices && obj.IndexCount == 0)
				instanced[obj.MeshPtr].push_back(&obj);
			else
				drawObject(obj);
		}
		std::vector<glm::mat4> matrices;
		for (const auto& [mesh, objects] : instanced)
		{
			if (objects.size() == 1) { drawObject(*objects.front()); continue; }
			matrices.clear();
			for (const RenderObject* o : objects) matrices.push_back(o->Transform);
			cmd.UploadStream(m_InstanceBuffer, matrices.size() * sizeof(glm::mat4), matrices.data());
			cmd.BindStorageBuffer(2, m_InstanceBuffer);
			drawObject(*objects.front(), static_cast<uint32_t>(matrices.size()));
			++m_Stats.InstancedDraws;
		}
		cmd.SetAlphaToCoverage(false);
		endLoop();

		const uint32_t beforeGrass = DrawCounter::Calls;
		Mark(kAfterObjects);
		GrassPass(scene, camera, viewProj, lightVP, shadows);
		Mark(kAfterGrass);
		m_Stats.GrassCalls = DrawCounter::Calls - beforeGrass;
		const uint32_t beforeWater = DrawCounter::Calls;
		WaterPass(scene, camera, viewProj);
		Mark(kAfterWater);
		m_Stats.WaterCalls = DrawCounter::Calls - beforeWater;

		if (!translucent.empty())
		{
			loopStart = Clock::now();
			loopBefore = RHI::Counters::Frame;
			auto distance2 = [&](const RenderObject* o)
			{
				const glm::vec3 d = glm::vec3(o->Transform[3]) - camera.Position;
				return glm::dot(d, d);
			};
			std::sort(translucent.begin(), translucent.end(),
				[&](const RenderObject* a, const RenderObject* b) { return distance2(a) > distance2(b); });

			cmd.SetBlend({ true, RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha, {} });
			cmd.SetDepthWrite(false);
			for (const RenderObject* obj : translucent)
				drawObject(*obj);
			cmd.SetDepthWrite(true);
			cmd.SetBlend({});
			endLoop();
		}
		cmd.SetCull(RHI::Cull::None);
		Mark(kAfterTranslucent);

		m_ParticleRenderer->ParticlePass(scene, camera, viewProj, *m_SceneMSAA, *m_SceneDepth);
		Mark(kAfterParticles);

		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);
		if (ambientOut)
			Framebuffer::BlitExtra(*m_SceneMSAA, *m_SceneAmbient);
	}

	void Renderer::GrassPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows)
	{
		m_GrassRenderer->Render(scene, camera, viewProj, lightVP, shadows, m_ShadowMap->DepthTexture());
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
		RHI::GetDevice().Commands().BindTexture(9, env.EnvMap->GetHandle());
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
		DrawCounter::Calls = 0;
		DrawCounter::Triangles = 0;
		RHI::Counters::Frame = {};
		uint32_t shadowCalls = 0;
		const RenderStats gpu = m_Stats;   // (ScenePass starts its stats over: the GPU times of the frames before are kept)
		m_TimerActive = m_TimersPending < kTimerSets;   // (all the marks of a frame, or none: a set not read yet is never issued again)
		Mark(kStart);
		const auto shadowStart = std::chrono::steady_clock::now();
		if (shadows)
		{
			lightVP = ShadowViewProj(scene.Env.ShadowCenter, scene.Sun.Direction, scene.Env.ShadowRadius, m_ShadowSize);   // snapped to texels
			ShadowPass(scene, lightVP);
			shadowCalls = DrawCounter::Calls;
		}
		const float shadowMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadowStart).count());
		const RHI::FrameCounters shadowSubmit = RHI::Counters::Frame;
		Mark(kAfterShadows);

		ScenePass(scene, camera, lightVP, shadows);
		Mark(kAfterResolve);
		m_Stats.CpuShadowMs = shadowMs;
		m_Stats.ShadowSubmit = shadowSubmit;
		m_Stats.GpuShadowMs = gpu.GpuShadowMs; m_Stats.GpuSceneMs = gpu.GpuSceneMs; m_Stats.GpuGrassMs = gpu.GpuGrassMs; m_Stats.GpuWaterMs = gpu.GpuWaterMs;
		m_Stats.GpuEffectsMs = gpu.GpuEffectsMs; m_Stats.GpuPostMs = gpu.GpuPostMs; m_Stats.GpuTotalMs = gpu.GpuTotalMs;
		m_Stats.GpuSkyMs = gpu.GpuSkyMs; m_Stats.GpuTranslucentMs = gpu.GpuTranslucentMs; m_Stats.GpuParticlesMs = gpu.GpuParticlesMs; m_Stats.GpuResolveMs = gpu.GpuResolveMs;
		m_Stats.GpuBloomMs = gpu.GpuBloomMs; m_Stats.GpuSsaoMs = gpu.GpuSsaoMs; m_Stats.GpuCompositeMs = gpu.GpuCompositeMs;
		m_Stats.ShadowCalls = shadowCalls;
		m_Stats.DrawCalls = DrawCounter::Calls;
		m_Stats.Triangles = DrawCounter::Triangles;
		m_Stats.SceneCalls = m_Stats.DrawCalls - shadowCalls - m_Stats.GrassCalls - m_Stats.WaterCalls;
		m_PostProcessor->BloomPass(*m_SceneResolved, scene.Env);
		Mark(kAfterBloom);
		{
			const PostSettings& post = scene.Post;
			const bool ao = post.SSAO > 0.0f && !camera.Orthographic, dof = post.DepthOfField > 0.0f && !camera.Orthographic;
			if (ao || dof)
				Framebuffer::BlitDepth(*m_SceneMSAA, *m_SceneDepth);
			if (ao)
				m_PostProcessor->AmbientOcclusionPass(*m_SceneDepth, post, camera);
			Mark(kAfterSsao);
			if (post.AutoExposure)
				m_PostProcessor->AdaptationPass(*m_SceneResolved, post);
		}
		m_PostProcessor->CompositePass(*m_SceneResolved, *m_SceneDepth, *m_SceneAmbient, scene.Env, scene.Post, camera, target);
		Mark(kAfterComposite);
		m_Stats.Submit = RHI::Counters::Frame;
		if (m_TimerActive)
		{
			RHI::GetDevice().Commands().Flush();   // (without it the last commands can wait in the driver's queue until the swap, and the post-processing reads as that wait)
			m_TimerHead = (m_TimerHead + 1) % kTimerSets;
			++m_TimersPending;
		}
		ReadTimers();
	}
}
