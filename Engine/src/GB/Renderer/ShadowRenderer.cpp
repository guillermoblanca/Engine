#include "ShadowRenderer.h"

#include "Shaders.h"
#include "Visibility.h"
#include <glad/gl.h>
#include <map>

namespace GB
{
	ShadowRenderer::ShadowRenderer()
		: m_Shader(Shaders::ShadowVS, Shaders::ShadowFS)
	{
		glCreateBuffers(1, &m_JointBuffer);
		glCreateBuffers(1, &m_InstanceBuffer);
	}

	ShadowRenderer::~ShadowRenderer()
	{
		glDeleteBuffers(1, &m_JointBuffer);
		glDeleteBuffers(1, &m_InstanceBuffer);
	}

	void ShadowRenderer::BindSkin(const RenderObject& object)
	{
		const bool skinned = object.JointMatrices && !object.JointMatrices->empty() &&
			object.MeshPtr && object.MeshPtr->HasSkin();
		m_Shader.SetInt("uSkinned", skinned ? 1 : 0);
		if (!skinned)
			return;

		glNamedBufferData(m_JointBuffer,
			static_cast<GLsizeiptr>(object.JointMatrices->size() * sizeof(glm::mat4)),
			object.JointMatrices->data(), GL_STREAM_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_JointBuffer);
	}

	void ShadowRenderer::Render(const Scene& scene, const Framebuffer& target, const glm::mat4& lightVP)
	{
		m_Shader.SetFloat("uTime", scene.Env.Time);
		m_Shader.SetVec3("uWind", scene.Env.Wind);
		target.Bind();
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(2.0f, 4.0f);

		m_Shader.Bind();
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
			glNamedBufferData(m_InstanceBuffer, static_cast<GLsizeiptr>(matrices.size() * sizeof(glm::mat4)), matrices.data(), GL_STREAM_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_InstanceBuffer);
			draw(*objects.front(), static_cast<uint32_t>(matrices.size()));
		}
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}
