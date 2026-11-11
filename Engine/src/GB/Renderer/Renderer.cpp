#include "Renderer.h"
#include "GroundInteraction.h"
#include "ParticleRenderer.h"
#include "Visibility.h"
#include "Shaders.h"
#include "ShadowView.h"
#include "TerrainLayerTextures.h"
#include "GB/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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
		{
			const std::string_view foliage[] = { "GB_FOLIAGE" };   // T46
			// T128: the VS too, for the per-instance colour matrix (vFoliageColor)
			m_PBRFoliageShader = std::make_unique<Shader>(Shaders::Assemble(Shaders::PBRVS, foliage), Shaders::Assemble(Shaders::PBRFS, foliage));
			const std::string_view terrain[] = { "GB_TERRAIN_LAYERS" };   // T115
			m_PBRTerrainShader = std::make_unique<Shader>(Shaders::Assemble(Shaders::PBRVS), Shaders::Assemble(Shaders::PBRFS, terrain));
			// its layer maps, made in the background from now on: a world load then finds them ready
			TerrainLayerTextures::Prefetch(TerrainLayerTextures::Size(false));
		}

		RHI::Device& device = RHI::GetDevice();
		m_EmptyVAO = device.CreateVertexLayout({});
		m_JointBuffer = device.CreateStreamBuffer();
		m_InstanceBuffer = device.CreateStreamBuffer();
		// The per-frame block: one allocation a frame (16 KiB regions leave room for the blocks of M4 and M5).
		m_FrameRing = device.CreateUploadRing(16 * 1024);
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
			pbr.Program = m_PBRFoliageShader->GetHandle();
			m_PBRFoliagePipeline = device.CreatePipeline(pbr);
			pbr.Program = m_PBRTerrainShader->GetHandle();
			m_PBRTerrainPipeline = device.CreatePipeline(pbr);
		}

		m_ParticleRenderer = std::make_unique<ParticleRenderer>();

		FramebufferSpec shadow;
		shadow.Width = shadow.Height = m_ShadowSize;
		shadow.Format = ColorFormat::None;
		shadow.DepthAsTexture = true;
		m_ShadowMap = std::make_unique<Framebuffer>(shadow);

		CreateTargets();
		m_PostProcessor = std::make_unique<PostProcessor>(m_Width, m_Height);
		m_GrassRenderer = std::make_unique<GrassRenderer>();
		m_WaterRenderer = std::make_unique<WaterRenderer>();
		m_ShadowRenderer = std::make_unique<ShadowRenderer>();

		m_Valid = m_FrameRing && m_ShadowRenderer->IsValid() && m_PBRShader->IsValid() && m_PBRFoliageShader->IsValid() && m_PBRTerrainShader->IsValid() && m_SkyShader->IsValid() &&
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
		device.DestroyPipeline(m_PBRFoliagePipeline);
		device.DestroyPipeline(m_PBRTerrainPipeline);
		m_FrameRing.reset();
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

		m_SceneTransmission.reset();   // T101: made again at the new size the next time a transmissive surface is in view
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

	void Renderer::UploadFrame(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows)
	{
		// The values the loose uniforms got before, computed the same way (the pictures must not change by a bit).
		const Environment& env = scene.Env;
		FrameConstants& f = m_Frame;
		f.ViewProj = viewProj;
		f.InvViewProj = glm::inverse(viewProj);
		f.LightVP = lightVP;
		f.CamPos = camera.Position;
		const glm::mat4 view = camera.GetView();
		f.CamRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
		f.CamUp = glm::vec3(view[0][1], view[1][1], view[2][1]);
		f.NearFar = { camera.NearPlane, camera.FarPlane };
		f.Ortho = camera.Orthographic ? 1 : 0;
		f.SunDir = glm::normalize(scene.Sun.Direction);
		f.SunColor = scene.Sun.Color * scene.Sun.Intensity;
		f.SkyTop = env.SkyTop;
		f.SkyHorizon = env.SkyHorizon;
		f.SkyGround = env.SkyGround;
		f.EnvIntensity = env.Intensity;
		f.FogColor = env.FogColor;
		f.FogDensity = env.FogDensity;
		f.FogHeight = env.FogHeightFalloff;
		f.Wetness = env.Wetness;
		f.SnowCover = env.SnowCover;
		f.Stars = env.Stars;
		f.CloudCover = env.CloudCover;
		f.Time = env.Time;
		f.Wind = env.Wind;
		f.ShadowsOn = shadows ? 1 : 0;
		f.ShadowTexel = 2.0f * env.ShadowRadius / static_cast<float>(m_ShadowSize);
		const bool hasEnvMap = env.EnvMap && env.EnvMap->IsValid();
		f.HasEnvMap = hasEnvMap ? 1 : 0;
		if (hasEnvMap)
		{
			f.EnvMaxLod = static_cast<float>(env.EnvMap->GetMipCount() - 1);
			f.EnvRotation = env.EnvMap->Rotation / 6.2831853f;
			f.EnvExposure = env.EnvMap->Exposure;
		}
		if (scene.Ground && scene.Ground->IsValid())
			f.TrailRect = scene.Ground->GetRect();
		const uint32_t pointCount = static_cast<uint32_t>(std::min<size_t>(scene.PointLights.size(), MaxPointLights));
		f.PointCount = static_cast<int32_t>(pointCount);
		for (uint32_t i = 0; i < pointCount; ++i)
		{
			const PointLight& light = scene.PointLights[i];
			f.PointPosRadius[i] = glm::vec4(light.Position, light.Radius);
			f.PointColor[i] = glm::vec4(light.Color * light.Intensity, 0.0f);
		}

		RHI::Device& device = RHI::GetDevice();
		const RHI::RingAllocation block = m_FrameRing->Allocate(sizeof(FrameConstants), device.Limits().UniformBufferAlignment);
		if (!block)
		{
			if (!m_FrameRingFull) GB_CORE_ERROR("Renderer: the frame ring is full, the frame block is not updated");
			m_FrameRingFull = true;
			return;
		}
		std::memcpy(block.Ptr, &f, sizeof(FrameConstants));
		device.Commands().BindUniformBuffer(FrameConstants::Binding, block.Buffer, block.Offset, sizeof(FrameConstants));
	}

	void Renderer::ShadowPass(const Scene& scene)
	{
		m_ShadowRenderer->Render(scene, m_List, m_Materials, *m_ShadowMap);
	}

	void Renderer::ScenePass(const Scene& scene, const Camera& camera)
	{
		const Environment& env = scene.Env;

		// The ambient target is only written when the SSAO will use it (every shader of the scene writes location 1: 0 where there is none).
		const bool ambientOut = scene.Post.SSAO > 0.0f && !camera.Orthographic;
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		m_SceneMSAA->SetExtraEnabled(ambientOut);
		RHI::PassDesc pass = m_SceneMSAA->Pass("scene");
		pass.ClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		pass.ClearDepth = true;
		cmd.BeginPass(pass);

		// The frame's values are in the block bound by UploadFrame; the environment map is the one texture they need (the sky, the objects,
		// the grass and the water read it at unit 9, and nothing else binds that unit).
		if (env.EnvMap && env.EnvMap->IsValid())
			cmd.BindTexture(9, env.EnvMap->GetHandle());

		// Background sky
		cmd.BindPipeline(m_SkyPipeline);
		cmd.BindVertexLayout(m_EmptyVAO);
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		Mark(kAfterSky);

		// Opaque geometry
		Shader& pbr = *m_PBRShader;
		cmd.BindPipeline(m_PBRPipeline);
		if (scene.Ground && scene.Ground->IsValid())
			cmd.BindTexture(8, { scene.Ground->GetTexture() });
		cmd.BindTexture(0, m_ShadowMap->DepthTexture());

		// `copies` > 0: an instanced draw of that many copies, their matrices already in m_InstanceBuffer (the object's material for all)
		using Clock = std::chrono::steady_clock;
		double drawMs = 0.0;   // T70: the part of the object loop spent in here (uniforms, binds, draw), the rest is culling and batching
		// T36 (render M4): the material's values are in the material buffer (lib/material, bound by MaterialBuffer::BeginFrame); the draw sets
		// its entry, uMaterialId, and binds the material's maps (still loose until M6). uModel, uInstanced and uSkinned are the object's (M5).
		// T46: the foliage materials draw with the GB_FOLIAGE program; the pipeline is switched only when the kind changes (the pipeline sets
		// the cull, so it goes first). T128: the instanced batches are sorted by it (BatchEntry::Program), so a forest switches once.
		// T115: a material with terrain layers draws with the GB_TERRAIN_LAYERS program the same way. bound: the program bound (0 base,
		// 1 foliage, 2 terrain layers).
		uint32_t bound = 0;
		const auto programOf = [](const Material& m) -> uint32_t { return m.TerrainLayers ? 2u : FoliageOn(m) ? 1u : 0u; };
		auto drawObject = [&](const RenderObject& obj, uint32_t copies = 0)
		{
			const auto drawStart = Clock::now();
			const Material& m = obj.Mat;
			const TerrainLayerMaps* layers = m.TerrainLayers;
			const uint32_t kind = programOf(m);
			if (kind != bound)
			{
				cmd.BindPipeline(kind == 2 ? m_PBRTerrainPipeline : kind == 1 ? m_PBRFoliagePipeline : m_PBRPipeline);
				bound = kind;
			}
			Shader& program = kind == 2 ? *m_PBRTerrainShader : kind == 1 ? *m_PBRFoliageShader : pbr;
			if (layers)
			{
				layers->Splat->Bind(14);
				layers->Albedo->Bind(15);
				layers->Surface->Bind(16);
				program.SetVec4("uSplatRect", layers->SplatRect);
				program.SetVec4Array("uLayerParams", layers->Params, TerrainLayerMaps::kKinds);
				program.SetVec4Array("uLayerMacro", layers->MacroColor, TerrainLayerMaps::kKinds);
			}
			cmd.SetCull(m.DoubleSided ? RHI::Cull::None : RHI::Cull::Back);

			program.SetMat4("uModel", obj.Transform);
			program.SetInt("uInstanced", copies > 0 ? 1 : 0);
			BindSkin(program, obj);
			const uint32_t object = static_cast<uint32_t>(&obj - scene.Objects.data());
			program.SetInt("uMaterialId", static_cast<int>(m_Materials.Resolve(object, m)));
			if (m.SurfaceNormalMap && m.MacroMap) m.SurfaceNormalMap->Bind(10);
			if (m.MacroMap) m.MacroMap->Bind(7);
			const bool coverage = m.Alpha == AlphaMode::Mask && m_Samples > 1;   // leaf edges antialiased by the MSAA target
			cmd.SetAlphaToCoverage(coverage);
			if (m.AlbedoMap) m.AlbedoMap->Bind(1);
			if (m.NormalMap) m.NormalMap->Bind(2);
			if (m.HeightMap) m.HeightMap->Bind(3);
			if (m.MetallicRoughnessMap) m.MetallicRoughnessMap->Bind(4);
			if (m.OcclusionMap) m.OcclusionMap->Bind(5);
			if (m.EmissiveMap) m.EmissiveMap->Bind(6);
			if (m.InteriorMap) m.InteriorMap->Bind(13);   // T101 (read with LAYER_TRANSMISSION only)
			if (copies > 0) obj.MeshPtr->DrawInstanced(copies);
			else if (obj.IndexCount > 0) obj.MeshPtr->DrawRange(obj.IndexFirst, obj.IndexCount);
			else obj.MeshPtr->Draw();
			drawMs += std::chrono::duration<double, std::milli>(Clock::now() - drawStart).count();
		};
		// Opaque and alpha-masked first, then translucent objects back to front without depth writes. The instanced objects the camera sees
		// wait for their mesh's batch, drawn after the loop in one call each (m_Batches, sorted by mesh as the std::map of before T78 did).
		m_Batches.clear();
		std::vector<const RenderObject*>& translucent = m_Translucent;
		translucent.clear();
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
		// What the camera sees, culled at the start of Render (RenderList::Cull: its frustum and the draw distance), in scene order
		const std::vector<RenderItem>& items = m_List.Items();
		m_Stats.ObjectsCulled = m_List.Culled();
		for (const uint32_t i : m_List.Visible())
		{
			const RenderItem& item = items[i];
			const RenderObject& obj = scene.Objects[item.Object];
			++m_Stats.ObjectsDrawn;
			if (item.Has(RenderItem::Blend))
				translucent.push_back(&obj);
			else if (item.Has(RenderItem::Batched))
				m_Batches.push_back({ obj.MeshPtr, item.Object, programOf(obj.Mat) });
			else
				drawObject(obj);
		}
		SortBatches(m_Batches);
		for (size_t first = 0; first < m_Batches.size();)
		{
			size_t last = first + 1;
			while (last < m_Batches.size() && m_Batches[last].MeshPtr == m_Batches[first].MeshPtr) ++last;
			const RenderObject& head = scene.Objects[m_Batches[first].Object];   // (the batch draws with the first one's material)
			if (last - first == 1) drawObject(head);
			else
			{
				m_Matrices.clear();
				for (size_t b = first; b < last; ++b) m_Matrices.push_back(scene.Objects[m_Batches[b].Object].Transform);
				cmd.UploadStream(m_InstanceBuffer, m_Matrices.size() * sizeof(glm::mat4), m_Matrices.data());
				cmd.BindStorageBuffer(2, m_InstanceBuffer);
				drawObject(head, static_cast<uint32_t>(m_Matrices.size()));
				++m_Stats.InstancedDraws;
			}
			first = last;
		}
		cmd.SetAlphaToCoverage(false);
		endLoop();

		const uint32_t beforeGrass = DrawCounter::Calls;
		Mark(kAfterObjects);
		GrassPass(scene, camera);
		Mark(kAfterGrass);
		m_Stats.GrassCalls = DrawCounter::Calls - beforeGrass;
		const uint32_t beforeWater = DrawCounter::Calls;
		WaterPass(scene);
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

			// T34: a transmissive surface (glass, ice) reads the scene drawn so far (opaque, grass, water) from a resolved copy: one per frame,
			// only when such a surface is in view (nothing new to pay without one). The water copies before drawing itself, so the glass needs
			// its own to see the water.
			// T101: the copy has a target of its own with a mip chain (m_SceneTransmission, made the first time a transmissive surface is in
			// view): the mips are generated only when one of them is rough enough to blur (a smooth glass reads level 0), and the shader is told
			// how many levels it may read (uSceneColorLods). The opaque depth is resolved too (unit 12) for the refraction's depth test.
			const auto isTransmissive = [](const RenderObject* o) { return o->Mat.Alpha == AlphaMode::Blend && o->Mat.Transmission > 0.0f; };
			const bool transmissive = std::any_of(translucent.begin(), translucent.end(), isTransmissive);
			if (transmissive)
			{
				if (!m_SceneTransmission)
				{
					FramebufferSpec spec;
					spec.Width = m_Width;
					spec.Height = m_Height;
					spec.Format = ColorFormat::RGBA16F;
					spec.Depth = false;
					spec.Levels = RHI::FullMipLevels(m_Width, m_Height);
					m_SceneTransmission = std::make_unique<Framebuffer>(spec);
				}
				Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneTransmission);
				// The roughness that blurs more than a pixel at 1080p and 1 m is ~0.08 (radius r^2 * 0.3 / distance screens): a
				// metal-roughness map only lowers the material's roughness, so the material's is the bound.
				const bool rough = std::any_of(translucent.begin(), translucent.end(),
					[&](const RenderObject* o) { return isTransmissive(o) && o->Mat.Roughness > kTransmissionMipRoughness; });
				float lods = 0.0f;
				if (rough)
				{
					RHI::GetDevice().GenerateMips(m_SceneTransmission->ColorTexture());
					lods = static_cast<float>(m_SceneTransmission->GetLevels() - 1);
					++m_Stats.TransmissionMips;
				}
				m_PBRShader->SetFloat("uSceneColorLods", lods);
				Framebuffer::BlitDepth(*m_SceneMSAA, *m_SceneDepth);
				cmd.BindTexture(11, m_SceneTransmission->ColorTexture());
				cmd.BindTexture(12, m_SceneDepth->DepthTexture());
				++m_Stats.TransmissionCopies;
			}
			// The grass and the water bind programs of their own: the translucent objects need the PBR one back (and its shadow map and ground
			// marks units, which the grass binds too).
			cmd.BindPipeline(m_PBRPipeline);
			bound = 0;   // (no translucent material is foliage, FoliageOn, nor a terrain)
			cmd.BindTexture(0, m_ShadowMap->DepthTexture());
			if (scene.Ground && scene.Ground->IsValid())
				cmd.BindTexture(8, { scene.Ground->GetTexture() });

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

		m_ParticleRenderer->ParticlePass(scene, camera, *m_SceneMSAA, *m_SceneDepth);
		Mark(kAfterParticles);

		Framebuffer::BlitColor(*m_SceneMSAA, *m_SceneResolved);
		if (ambientOut)
			Framebuffer::BlitExtra(*m_SceneMSAA, *m_SceneAmbient);
	}

	void Renderer::GrassPass(const Scene& scene, const Camera& camera)
	{
		m_GrassRenderer->Render(scene, camera, m_ShadowMap->DepthTexture());
	}

	void Renderer::WaterPass(const Scene& scene)
	{
		m_WaterRenderer->Render(scene, *m_SceneMSAA, *m_SceneResolved, *m_SceneDepth);
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
		m_FrameRing->BeginFrame();
		Mark(kStart);

		// T78: the objects once, and what the camera and the shadow map see of them in one walk (the two passes used to walk the scene each).
		// Its CPU time goes into CpuObjectsMs, which held the scene pass's own culling before.
		const auto cullStart = std::chrono::steady_clock::now();
		const float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
		const glm::mat4 viewProj = camera.GetProjection(aspect) * camera.GetView();
		if (shadows)
			lightVP = ShadowViewProj(scene.Env.ShadowCenter, scene.Sun.Direction, scene.Env.ShadowRadius, m_ShadowSize);   // snapped to texels
		const Frustum cameraFrustum = MakeFrustum(viewProj), lightFrustum = MakeFrustum(lightVP);
		m_List.Build(scene.Objects, scene.DrawDistance);
		m_List.Cull(cameraFrustum, camera.Position, shadows ? &lightFrustum : nullptr);
		// T36: the materials' context for this frame and their buffer bound for both passes. The objects drawn alone have their material's id
		// resolved here and the new materials uploaded at once; the draws set the ids (an instanced batch resolves its first object's then).
		m_Materials.BeginFrame({ scene.Ground && scene.Ground->IsValid(), m_Samples > 1 }, scene.Objects.size());
		{
			const std::vector<RenderItem>& items = m_List.Items();
			for (const uint32_t i : m_List.Visible())
				if (!items[i].Has(RenderItem::Batched)) m_Materials.Prepare(items[i].Object, scene.Objects[items[i].Object].Mat);
			if (shadows)
				for (const uint32_t i : m_List.Casters())
					if (!items[i].Has(RenderItem::Batched)) m_Materials.Prepare(items[i].Object, scene.Objects[items[i].Object].Mat);
			m_Materials.Flush();
		}
		const float cullMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cullStart).count());
		UploadFrame(scene, camera, viewProj, lightVP, shadows);

		const auto shadowStart = std::chrono::steady_clock::now();
		if (shadows)
		{
			ShadowPass(scene);
			shadowCalls = DrawCounter::Calls;
		}
		const float shadowMs = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadowStart).count());
		const RHI::FrameCounters shadowSubmit = RHI::Counters::Frame;
		Mark(kAfterShadows);

		ScenePass(scene, camera);
		Mark(kAfterResolve);
		m_Stats.CpuCullMs = cullMs;
		m_Stats.CpuObjectsMs += cullMs;
		m_Stats.CpuShadowMs = shadowMs;
		m_Stats.ShadowSubmit = shadowSubmit;
		m_Stats.GpuShadowMs = gpu.GpuShadowMs; m_Stats.GpuSceneMs = gpu.GpuSceneMs; m_Stats.GpuGrassMs = gpu.GpuGrassMs; m_Stats.GpuWaterMs = gpu.GpuWaterMs;
		m_Stats.GpuEffectsMs = gpu.GpuEffectsMs; m_Stats.GpuPostMs = gpu.GpuPostMs; m_Stats.GpuTotalMs = gpu.GpuTotalMs;
		m_Stats.GpuSkyMs = gpu.GpuSkyMs; m_Stats.GpuTranslucentMs = gpu.GpuTranslucentMs; m_Stats.GpuParticlesMs = gpu.GpuParticlesMs; m_Stats.GpuResolveMs = gpu.GpuResolveMs;
		m_Stats.GpuBloomMs = gpu.GpuBloomMs; m_Stats.GpuSsaoMs = gpu.GpuSsaoMs; m_Stats.GpuCompositeMs = gpu.GpuCompositeMs;
		m_Stats.ShadowCalls = shadowCalls;
		m_Stats.DrawCalls = DrawCounter::Calls;
		m_Stats.Triangles = DrawCounter::Triangles;
		const MaterialBuffer::Stats& materials = m_Materials.FrameStats();
		m_Stats.MaterialResolves = materials.Resolves;
		m_Stats.MaterialsPacked = materials.Packed;
		m_Stats.MaterialsHashed = materials.Hashed;
		m_Stats.MaterialsAdded = materials.Added;
		m_Stats.Materials = materials.Count;
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
		m_FrameRing->EndFrame();
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
