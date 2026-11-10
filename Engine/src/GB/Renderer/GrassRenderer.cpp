#include "GrassRenderer.h"
#include "GrassGpu.h"
#include "GroundInteraction.h"
#include "RenderStats.h"
#include "GB/World/Grass.h"
#include "Shaders.h"
#include <algorithm>

namespace GB
{
	GrassRenderer::GrassRenderer()
		: m_Shader(
			Shaders::Assemble(Shaders::GrassVS),
			Shaders::Assemble(Shaders::GrassFS))
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

	void GrassRenderer::Render(const Scene& scene, const Camera& camera, RHI::TextureHandle shadowTexture)
	{
		if (scene.Grass.empty())
			return;

		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BindPipeline(m_Pipeline);
		cmd.BindTexture(0, shadowTexture);

		const bool trails = scene.Ground && scene.Ground->IsValid();
		if (trails)
			cmd.BindTexture(8, { scene.Ground->GetTexture() });

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
