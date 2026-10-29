#include "PostProcessor.h"
#include "Shaders.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	PostProcessor::PostProcessor(uint32_t width, uint32_t height)
		: m_Width(std::max(width, 1u)), m_Height(std::max(height, 1u))
	{
		m_BloomDown = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::BloomDownFS);
		m_BloomUp = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::BloomUpFS);
		m_Composite = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::CompositeFS);
		m_AOShader = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::AOFS);
		m_AOBlurShader = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::AOBlurFS);
		m_LumShader = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::LumFS);
		m_AdaptShader = std::make_unique<Shader>(Shaders::FullscreenVS, Shaders::AdaptFS);
		glCreateVertexArrays(1, &m_EmptyVAO);
		CreateTargets();
	}

	PostProcessor::~PostProcessor()
	{
		glDeleteVertexArrays(1, &m_EmptyVAO);
	}

	bool PostProcessor::IsValid() const
	{
		return m_BloomDown->IsValid() && m_BloomUp->IsValid() && m_Composite->IsValid() &&
			m_AOShader->IsValid() && m_AOBlurShader->IsValid() && m_LumShader->IsValid() &&
			m_AdaptShader->IsValid() && m_AO->IsComplete() && m_AOBlur->IsComplete() &&
			m_LumSmall->IsComplete() && m_Adapt[0]->IsComplete() && m_Adapt[1]->IsComplete() &&
			!m_BloomChain.empty() && std::all_of(m_BloomChain.begin(), m_BloomChain.end(),
				[](const std::unique_ptr<Framebuffer>& framebuffer) { return framebuffer->IsComplete(); });
	}

	void PostProcessor::CreateTargets()
	{
		FramebufferSpec ao;
		ao.Width = std::max(m_Width / 2, 1u);
		ao.Height = std::max(m_Height / 2, 1u);
		ao.Format = ColorFormat::RGBA16F;
		ao.Depth = false;
		m_AO = std::make_unique<Framebuffer>(ao);
		m_AOBlur = std::make_unique<Framebuffer>(ao);

		FramebufferSpec lum = ao;
		lum.Width = 32;
		lum.Height = 18;
		m_LumSmall = std::make_unique<Framebuffer>(lum);
		FramebufferSpec one = ao;
		one.Width = 1;
		one.Height = 1;
		m_Adapt[0] = std::make_unique<Framebuffer>(one);
		m_Adapt[1] = std::make_unique<Framebuffer>(one);
		m_AdaptValid = false;

		m_BloomChain.clear();
		const uint32_t smallest = std::min(m_Width, m_Height);
		const uint32_t levels = std::clamp(static_cast<uint32_t>(std::log2(static_cast<float>(smallest))) - 2u, 1u, 6u);
		for (uint32_t i = 0; i < levels; ++i)
		{
			FramebufferSpec bloom;
			bloom.Width = std::max(m_Width >> (i + 1), 1u);
			bloom.Height = std::max(m_Height >> (i + 1), 1u);
			bloom.Format = ColorFormat::RGBA16F;
			bloom.Depth = false;
			m_BloomChain.push_back(std::make_unique<Framebuffer>(bloom));
		}
	}

	void PostProcessor::Resize(uint32_t width, uint32_t height)
	{
		m_Width = std::max(width, 1u);
		m_Height = std::max(height, 1u);
		CreateTargets();
	}

	void PostProcessor::BloomPass(const Framebuffer& sceneResolved, const Environment& env)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glBindVertexArray(m_EmptyVAO);
		m_BloomDown->Bind();
		uint32_t srcTexture = sceneResolved.GetColorTexture();
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

	void PostProcessor::AmbientOcclusionPass(const Framebuffer& sceneDepth, const PostSettings& post, const Camera& camera)
	{
		const float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
		const glm::mat4 proj = camera.GetProjection(aspect);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glBindVertexArray(m_EmptyVAO);
		m_AO->Bind();
		m_AOShader->Bind();
		m_AOShader->SetMat4("uProj", proj);
		m_AOShader->SetMat4("uInvProj", glm::inverse(proj));
		m_AOShader->SetFloat("uRadius", post.SSAORadius);
		m_AOShader->SetFloat("uBias", post.SSAOBias);
		glBindTextureUnit(0, sceneDepth.GetDepthTexture());
		glDrawArrays(GL_TRIANGLES, 0, 3);
		m_AOBlur->Bind();
		m_AOBlurShader->Bind();
		m_AOBlurShader->SetVec2("uTexel", { 1.0f / static_cast<float>(m_AO->GetWidth()), 1.0f / static_cast<float>(m_AO->GetHeight()) });
		m_AOBlurShader->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		glBindTextureUnit(0, m_AO->GetColorTexture());
		glBindTextureUnit(1, sceneDepth.GetDepthTexture());
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	void PostProcessor::AdaptationPass(const Framebuffer& sceneResolved, const PostSettings& post)
	{
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glBindVertexArray(m_EmptyVAO);
		m_LumSmall->Bind();
		m_LumShader->Bind();
		glBindTextureUnit(0, sceneResolved.GetColorTexture());
		glDrawArrays(GL_TRIANGLES, 0, 3);
		const int next = 1 - m_AdaptCurrent;
		m_Adapt[next]->Bind();
		m_AdaptShader->Bind();
		glBindTextureUnit(0, m_LumSmall->GetColorTexture());
		glBindTextureUnit(1, m_Adapt[m_AdaptCurrent]->GetColorTexture());
		m_AdaptShader->SetFloat("uKey", post.ExposureKey);
		m_AdaptShader->SetFloat("uMin", post.MinExposure);
		m_AdaptShader->SetFloat("uMax", post.MaxExposure);
		m_AdaptShader->SetFloat("uBlend", m_AdaptValid ? 1.0f - std::exp(-std::max(post.DeltaTime, 0.0f) * post.AdaptSpeed) : 1.0f);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		m_AdaptCurrent = next;
		m_AdaptValid = true;
	}

	void PostProcessor::CompositePass(const Framebuffer& sceneResolved, const Framebuffer& sceneDepth,
		const Environment& env, const PostSettings& post, const Camera& camera, const Framebuffer* target)
	{
		if (target)
			target->Bind();
		else
			Framebuffer::BindDefault(m_Width, m_Height);
		glDisable(GL_DEPTH_TEST);
		m_Composite->Bind();
		glBindTextureUnit(0, sceneResolved.GetColorTexture());
		glBindTextureUnit(1, m_BloomChain.front()->GetColorTexture());
		const bool ao = post.SSAO > 0.0f && !camera.Orthographic;
		const bool dof = post.DepthOfField > 0.0f && !camera.Orthographic;
		glBindTextureUnit(2, ao ? m_AOBlur->GetColorTexture() : sceneResolved.GetColorTexture());
		glBindTextureUnit(3, sceneDepth.GetDepthTexture());
		glBindTextureUnit(4, m_Adapt[m_AdaptCurrent]->GetColorTexture());
		m_Composite->SetInt("uTonemap", static_cast<int>(post.Mapper));
		m_Composite->SetFloat("uAOStrength", ao ? glm::clamp(post.SSAO, 0.0f, 1.0f) : 0.0f);
		m_Composite->SetFloat("uDOF", dof ? post.DepthOfField : 0.0f);
		m_Composite->SetFloat("uFocus", post.FocusDistance);
		m_Composite->SetFloat("uFocusRange", post.FocusRange);
		m_Composite->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		m_Composite->SetInt("uOrtho", camera.Orthographic ? 1 : 0);
		m_Composite->SetInt("uAutoExposure", post.AutoExposure ? 1 : 0);
		m_Composite->SetFloat("uSaturation", post.Saturation);
		m_Composite->SetFloat("uContrast", post.Contrast);
		m_Composite->SetVec3("uTint", post.Tint);
		m_Composite->SetFloat("uCA", post.ChromaticAberration);
		m_Composite->SetFloat("uGrain", post.Grain);
		m_TimeAccum += post.DeltaTime;
		m_Composite->SetFloat("uTime", m_TimeAccum);
		m_Composite->SetFloat("uExposure", env.Exposure);
		m_Composite->SetFloat("uBloomIntensity", env.BloomIntensity);
		m_Composite->SetFloat("uVignette", env.Vignette);
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glEnable(GL_DEPTH_TEST);
	}
}
