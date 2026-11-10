#include "ShadowRenderer.h"

#include "Shaders.h"
#include "Visibility.h"
#include <map>

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

	void ShadowRenderer::Render(const Scene& scene, const Framebuffer& target, const glm::mat4& lightVP)
	{
		m_Shader.SetFloat("uTime", scene.Env.Time);
		m_Shader.SetVec3("uWind", scene.Env.Wind);
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		RHI::PassDesc pass = target.Pass("shadows");
		pass.ClearDepth = true;
		cmd.BeginPass(pass);
		cmd.BindPipeline(m_Pipeline);
		m_Shader.SetMat4("uLightVP", lightVP);
		const Frustum lightFrustum = MakeFrustum(lightVP);
		std::map<const Mesh*, std::vector<const RenderObject*>> instanced;   // (RenderObject::Instanced: one call per mesh, after the loop)
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
		for (const RenderObject& object : scene.Objects)
		{
			if (!object.MeshPtr || object.Mat.Alpha == AlphaMode::Blend)
				continue;

			glm::vec3 boundsCenter;
			float boundsRadius;
			if (BoundsOf(object, boundsCenter, boundsRadius) &&
				!InFrustum(lightFrustum, boundsCenter, boundsRadius))
				continue;
			if (object.Instanced && !object.JointMatrices && object.IndexCount == 0) instanced[object.MeshPtr].push_back(&object);
			else draw(object, 0);
		}
		std::vector<glm::mat4> matrices;
		for (const auto& [mesh, objects] : instanced)
		{
			if (objects.size() == 1) { draw(*objects.front(), 0); continue; }
			matrices.clear();
			for (const RenderObject* o : objects) matrices.push_back(o->Transform);
			cmd.UploadStream(m_InstanceBuffer, matrices.size() * sizeof(glm::mat4), matrices.data());
			cmd.BindStorageBuffer(2, m_InstanceBuffer);
			draw(*objects.front(), static_cast<uint32_t>(matrices.size()));
		}
		cmd.BindPipeline(m_End);
		cmd.EndPass();
	}
}
