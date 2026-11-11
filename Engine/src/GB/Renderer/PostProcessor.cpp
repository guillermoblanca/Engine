#include "PostProcessor.h"
#include "Shaders.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	PostProcessor::PostProcessor(uint32_t width, uint32_t height)
		: m_Width(std::max(width, 1u)), m_Height(std::max(height, 1u))
	{
		const std::string fullscreen = Shaders::Assemble(Shaders::FullscreenVS);
		m_BloomDown = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::BloomDownFS));
		m_BloomUp = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::BloomUpFS));
		m_Composite = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::CompositeFS));
		m_AOShader = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::AOFS));
		m_AOBlurShader = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::AOBlurFS));
		m_LumShader = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::LumFS));
		m_AdaptShader = std::make_unique<Shader>(fullscreen, Shaders::Assemble(Shaders::AdaptFS));
		RHI::Device& device = RHI::GetDevice();
		m_EmptyVAO = device.CreateVertexLayout({});
		// `first`: the first pass of a chain, which sets the depth test and the blending off; the next ones only change the program.
		auto pipeline = [&](const Shader& shader, bool first)
		{
			RHI::PipelineDesc desc;
			desc.Program = shader.GetHandle();
			if (first)
			{
				desc.DepthTest = false;
				desc.Blend = RHI::BlendState{};
			}
			return device.CreatePipeline(desc);
		};
		m_BloomDownPipe = pipeline(*m_BloomDown, true);
		{
			RHI::PipelineDesc up;
			up.Program = m_BloomUp->GetHandle();
			up.Blend = RHI::BlendState{ true, RHI::BlendFactor::One, RHI::BlendFactor::One, {} };
			m_BloomUpPipe = device.CreatePipeline(up);
		}
		m_AOPipe = pipeline(*m_AOShader, true);
		m_AOBlurPipe = pipeline(*m_AOBlurShader, false);
		m_LumPipe = pipeline(*m_LumShader, true);
		m_AdaptPipe = pipeline(*m_AdaptShader, false);
		{
			RHI::PipelineDesc composite;
			composite.Program = m_Composite->GetHandle();
			composite.DepthTest = false;
			m_CompositePipe = device.CreatePipeline(composite);
			RHI::PipelineDesc depthOn;
			depthOn.DepthTest = true;
			m_DepthOn = device.CreatePipeline(depthOn);
		}
		CreateTargets();
	}

	PostProcessor::~PostProcessor()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyVertexLayout(m_EmptyVAO);
		for (RHI::PipelineHandle p : { m_BloomDownPipe, m_BloomUpPipe, m_AOPipe, m_AOBlurPipe, m_LumPipe, m_AdaptPipe, m_CompositePipe, m_DepthOn })
			device.DestroyPipeline(p);
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
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BindPipeline(m_BloomDownPipe);
		cmd.BindVertexLayout(m_EmptyVAO);
		RHI::TextureHandle srcTexture = sceneResolved.ColorTexture();
		uint32_t srcW = m_Width, srcH = m_Height;
		for (size_t i = 0; i < m_BloomChain.size(); ++i)
		{
			Framebuffer& dst = *m_BloomChain[i];
			cmd.BeginPass(dst.Pass("bloom down"));
			cmd.BindTexture(0, srcTexture);
			m_BloomDown->SetVec2("uTexel", { 1.0f / static_cast<float>(srcW), 1.0f / static_cast<float>(srcH) });
			m_BloomDown->SetInt("uPrefilter", i == 0 ? 1 : 0);
			m_BloomDown->SetFloat("uThreshold", env.BloomThreshold);
			m_BloomDown->SetFloat("uKnee", env.BloomThreshold * 0.5f);
			cmd.Draw(RHI::Topology::Triangles, 0, 3);
			srcTexture = dst.ColorTexture();
			srcW = dst.GetWidth();
			srcH = dst.GetHeight();
		}
		cmd.BindPipeline(m_BloomUpPipe);
		for (size_t i = m_BloomChain.size() - 1; i > 0; --i)
		{
			const Framebuffer& src = *m_BloomChain[i];
			cmd.BeginPass(m_BloomChain[i - 1]->Pass("bloom up"));
			cmd.BindTexture(0, src.ColorTexture());
			m_BloomUp->SetVec2("uTexel", { 1.0f / static_cast<float>(src.GetWidth()), 1.0f / static_cast<float>(src.GetHeight()) });
			m_BloomUp->SetFloat("uRadius", env.BloomRadius);
			cmd.Draw(RHI::Topology::Triangles, 0, 3);
		}
		cmd.SetBlend({});
	}

	void PostProcessor::AmbientOcclusionPass(const Framebuffer& sceneDepth, const PostSettings& post, const Camera& camera)
	{
		const float aspect = static_cast<float>(m_Width) / static_cast<float>(m_Height);
		const glm::mat4 proj = camera.GetReversedZProjection(aspect);   // T137: the convention of the scene depth it reads
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BindPipeline(m_AOPipe);
		cmd.BindVertexLayout(m_EmptyVAO);
		cmd.BeginPass(m_AO->Pass("ssao"));
		m_AOShader->SetMat4("uProj", proj);
		m_AOShader->SetMat4("uInvProj", glm::inverse(proj));
		m_AOShader->SetFloat("uRadius", post.SSAORadius);
		m_AOShader->SetFloat("uBias", post.SSAOBias);
		cmd.BindTexture(0, sceneDepth.DepthTexture());
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		cmd.BeginPass(m_AOBlur->Pass("ssao blur"));
		cmd.BindPipeline(m_AOBlurPipe);
		m_AOBlurShader->SetVec2("uTexel", { 1.0f / static_cast<float>(m_AO->GetWidth()), 1.0f / static_cast<float>(m_AO->GetHeight()) });
		m_AOBlurShader->SetVec2("uNearFar", { camera.NearPlane, camera.FarPlane });
		cmd.BindTexture(0, m_AO->ColorTexture());
		cmd.BindTexture(1, sceneDepth.DepthTexture());
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
	}

	void PostProcessor::AdaptationPass(const Framebuffer& sceneResolved, const PostSettings& post)
	{
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BindPipeline(m_LumPipe);
		cmd.BindVertexLayout(m_EmptyVAO);
		cmd.BeginPass(m_LumSmall->Pass("luminance"));
		cmd.BindTexture(0, sceneResolved.ColorTexture());
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		const int next = 1 - m_AdaptCurrent;
		cmd.BeginPass(m_Adapt[next]->Pass("adaptation"));
		cmd.BindPipeline(m_AdaptPipe);
		cmd.BindTexture(0, m_LumSmall->ColorTexture());
		cmd.BindTexture(1, m_Adapt[m_AdaptCurrent]->ColorTexture());
		m_AdaptShader->SetFloat("uKey", post.ExposureKey);
		m_AdaptShader->SetFloat("uMin", post.MinExposure);
		m_AdaptShader->SetFloat("uMax", post.MaxExposure);
		m_AdaptShader->SetFloat("uBlend", m_AdaptValid ? 1.0f - std::exp(-std::max(post.DeltaTime, 0.0f) * post.AdaptSpeed) : 1.0f);
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		m_AdaptCurrent = next;
		m_AdaptValid = true;
	}

	void PostProcessor::CompositePass(const Framebuffer& sceneResolved, const Framebuffer& sceneDepth, const Framebuffer& sceneAmbient,
		const Environment& env, const PostSettings& post, const Camera& camera, const Framebuffer* target)
	{
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		RHI::PassDesc pass;
		if (target)
			pass = target->Pass();
		else
		{
			pass.Width = m_Width;
			pass.Height = m_Height;
		}
		pass.Name = "composite";
		cmd.BeginPass(pass);
		cmd.BindPipeline(m_CompositePipe);
		cmd.BindTexture(0, sceneResolved.ColorTexture());
		cmd.BindTexture(1, m_BloomChain.front()->ColorTexture());
		const bool ao = post.SSAO > 0.0f && !camera.Orthographic;
		const bool dof = post.DepthOfField > 0.0f && !camera.Orthographic;
		cmd.BindTexture(2, ao ? m_AOBlur->ColorTexture() : sceneResolved.ColorTexture());
		cmd.BindTexture(3, sceneDepth.DepthTexture());
		cmd.BindTexture(4, m_Adapt[m_AdaptCurrent]->ColorTexture());
		cmd.BindTexture(5, sceneAmbient.ColorTexture());
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
		cmd.BindVertexLayout(m_EmptyVAO);
		cmd.Draw(RHI::Topology::Triangles, 0, 3);
		cmd.BindPipeline(m_DepthOn);
		cmd.EndPass();
	}
}
