#include "WaterRenderer.h"
#include "WaterRipples.h"
#include "Shaders.h"
#include "GB/World/Water/WaterWaves.h"
#include <algorithm>
#include <iterator>
#include <string>

namespace GB
{
	WaterRenderer::WaterRenderer()
		: m_Shader(
			InjectWaveSet(Shaders::Assemble(Shaders::WaterVS)),
			InjectWaveSet(Shaders::Assemble(Shaders::WaterFS)))
	{
		RHI::Device& device = RHI::GetDevice();
		RHI::PipelineDesc pipeline;
		pipeline.Program = m_Shader.GetHandle();
		pipeline.DepthTest = true;
		pipeline.DepthWrite = false;
		pipeline.CullMode = RHI::Cull::None;
		pipeline.Blend = RHI::BlendState{ true, RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha, {} };
		m_Pipeline = device.CreatePipeline(pipeline);
		RHI::PipelineDesc end;
		end.DepthWrite = true;
		end.Blend = RHI::BlendState{};
		m_End = device.CreatePipeline(end);
	}

	WaterRenderer::~WaterRenderer()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyPipeline(m_Pipeline);
		device.DestroyPipeline(m_End);
	}

	const Mesh& WaterRenderer::MeshFor(const WaterSurface& surface)
	{
		const std::shared_ptr<const WaterGeometry>& geometry = surface.Geometry();
		const auto found = m_Meshes.find(geometry.get());
		if (found != m_Meshes.end() && !found->second.Source.expired())
			return *found->second.Gpu;

		// A geometry that is gone leaves its mesh behind: drop those before adding another.
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
			it = it->second.Source.expired() ? m_Meshes.erase(it) : std::next(it);

		CachedMesh entry;
		entry.Source = geometry;
		entry.Gpu = geometry->IsRiver()
			? std::make_unique<Mesh>(geometry->RiverVertices, geometry->RiverIndices)
			: std::make_unique<Mesh>(Mesh::CreateGrid(geometry->GridSize, geometry->GridSubdivisions));
		return *(m_Meshes[geometry.get()] = std::move(entry)).Gpu;
	}

	void WaterRenderer::Render(const Scene& scene, const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth)
	{
		if (scene.Water.empty())
			return;

		// The water reads the depth of everything opaque to measure how deep it is.
		Framebuffer::BlitDepth(sceneMSAA, sceneDepth);
		Framebuffer::BlitColor(sceneMSAA, sceneResolved);   // what the water refracts
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BeginPass(sceneMSAA.Pass("water"));
		cmd.BindPipeline(m_Pipeline);
		const bool ssr = scene.ScreenSpaceReflections;
		m_Shader.SetFloat("uSSR", ssr ? 1.0f : 0.0f);
		cmd.BindTexture(1, sceneDepth.DepthTexture());
		cmd.BindTexture(3, sceneResolved.ColorTexture());
		const bool ripples = scene.Ripples && scene.Ripples->IsValid();
		if (ripples)
		{
			m_Shader.SetVec4("uRippleRect", scene.Ripples->GetRect());
			cmd.BindTexture(2, { scene.Ripples->GetTexture() });
		}
		else
			m_Shader.SetVec4("uRippleRect", glm::vec4(0.0f));

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
			MeshFor(*water).Draw();
		}
		cmd.BindPipeline(m_End);
	}
}
