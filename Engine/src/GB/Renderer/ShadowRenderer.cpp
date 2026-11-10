#include "ShadowRenderer.h"

#include "Shaders.h"

namespace GB
{
	ShadowRenderer::ShadowRenderer()
		: m_Shader(Shaders::Assemble(Shaders::ShadowVS), Shaders::Assemble(Shaders::ShadowFS))
	{
		RHI::Device& device = RHI::GetDevice();
		m_JointBuffer = device.CreateStreamBuffer();
		m_InstanceBuffer = device.CreateStreamBuffer();
		RHI::PipelineDesc pipeline;
		pipeline.Program = m_Shader.GetHandle();
		pipeline.DepthTest = true;
		pipeline.DepthWrite = true;
		pipeline.Offset = RHI::PolygonOffset{ true, 2.0f, 4.0f };
		m_Pipeline = device.CreatePipeline(pipeline);
		RHI::PipelineDesc end;
		end.Offset = RHI::PolygonOffset{};
		m_End = device.CreatePipeline(end);
	}

	ShadowRenderer::~ShadowRenderer()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyBuffer(m_JointBuffer);
		device.DestroyBuffer(m_InstanceBuffer);
		device.DestroyPipeline(m_Pipeline);
		device.DestroyPipeline(m_End);
	}

	void ShadowRenderer::BindSkin(const RenderObject& object)
	{
		const bool skinned = object.JointMatrices && !object.JointMatrices->empty() &&
			object.MeshPtr && object.MeshPtr->HasSkin();
		m_Shader.SetInt("uSkinned", skinned ? 1 : 0);
		if (!skinned)
			return;

		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.UploadStream(m_JointBuffer, object.JointMatrices->size() * sizeof(glm::mat4), object.JointMatrices->data());
		cmd.BindStorageBuffer(0, m_JointBuffer);
	}

	void ShadowRenderer::Render(const Scene& scene, const RenderList& list, const Framebuffer& target, const glm::mat4& lightVP)
	{
		m_Shader.SetFloat("uTime", scene.Env.Time);
		m_Shader.SetVec3("uWind", scene.Env.Wind);
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		RHI::PassDesc pass = target.Pass("shadows");
		pass.ClearDepth = true;
		cmd.BeginPass(pass);
		cmd.BindPipeline(m_Pipeline);
		m_Shader.SetMat4("uLightVP", lightVP);
		auto draw = [&](const RenderObject& object, uint32_t copies)
		{
			m_Shader.SetMat4("uModel", object.Transform);
			m_Shader.SetInt("uInstanced", copies > 0 ? 1 : 0);
			m_Shader.SetFloat("uWindStrength", object.Mat.WindSway);
			m_Shader.SetFloat("uWindHeight", object.Mat.WindHeight);
			m_Shader.SetFloat("uWindFlutter", object.Mat.WindFlutter);
			BindSkin(object);

			const bool masked = object.Mat.Alpha == AlphaMode::Mask && object.Mat.AlbedoMap;
			m_Shader.SetInt("uAlphaMode", masked ? 1 : 0);
			m_Shader.SetFloat("uUVScale", object.Mat.UVScale);
			if (masked)
			{
				m_Shader.SetFloat("uAlphaCutoff", object.Mat.AlphaCutoff);
				m_Shader.SetFloat("uOpacity", object.Mat.Opacity);
				object.Mat.AlbedoMap->Bind(1);
			}

			const bool displaced = object.Mat.HeightMap && object.Mat.HeightScale != 0.0f;
			m_Shader.SetInt("uHasHeight", displaced ? 1 : 0);
			if (displaced)
			{
				m_Shader.SetFloat("uHeightScale", object.Mat.HeightScale);
				m_Shader.SetFloat("uUVScale", object.Mat.UVScale);
				object.Mat.HeightMap->Bind(3);
			}
			if (copies > 0) object.MeshPtr->DrawInstanced(copies);
			else if (object.IndexCount > 0) object.MeshPtr->DrawRange(object.IndexFirst, object.IndexCount);
			else object.MeshPtr->Draw();
		};
		// The casters (no translucent objects; the culling against the light's frustum is done), in scene order. The instanced ones wait for
		// their mesh's batch: one call per mesh after the loop, by mesh address and in scene order inside one (RenderObject::Instanced).
		m_Batches.clear();
		const std::vector<RenderItem>& items = list.Items();
		for (const uint32_t i : list.Casters())
		{
			const RenderItem& item = items[i];
			const RenderObject& object = scene.Objects[item.Object];
			if (item.Has(RenderItem::Batched)) m_Batches.push_back({ object.MeshPtr, item.Object });
			else draw(object, 0);
		}
		SortBatches(m_Batches);
		for (size_t first = 0; first < m_Batches.size();)
		{
			size_t last = first + 1;
			while (last < m_Batches.size() && m_Batches[last].MeshPtr == m_Batches[first].MeshPtr) ++last;
			const RenderObject& head = scene.Objects[m_Batches[first].Object];
			if (last - first == 1) draw(head, 0);
			else
			{
				m_Matrices.clear();
				for (size_t b = first; b < last; ++b) m_Matrices.push_back(scene.Objects[m_Batches[b].Object].Transform);
				cmd.UploadStream(m_InstanceBuffer, m_Matrices.size() * sizeof(glm::mat4), m_Matrices.data());
				cmd.BindStorageBuffer(2, m_InstanceBuffer);
				draw(head, static_cast<uint32_t>(m_Matrices.size()));
			}
			first = last;
		}
		cmd.BindPipeline(m_End);
		cmd.EndPass();
	}
}
