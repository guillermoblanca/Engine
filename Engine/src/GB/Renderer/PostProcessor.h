#pragma once

#include "Camera.h"
#include "Framebuffer.h"
#include "Scene.h"
#include "Shader.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace GB
{
	class PostProcessor
	{
	public:
		PostProcessor(uint32_t width, uint32_t height);
		~PostProcessor();
		PostProcessor(const PostProcessor&) = delete;
		PostProcessor& operator=(const PostProcessor&) = delete;

		bool IsValid() const;
		void Resize(uint32_t width, uint32_t height);

		void BloomPass(const Framebuffer& sceneResolved, const Environment& env);
		void AmbientOcclusionPass(const Framebuffer& sceneDepth, const PostSettings& post, const Camera& camera);
		void AdaptationPass(const Framebuffer& sceneResolved, const PostSettings& post);
		// `sceneAmbient`: the ambient part of the scene color (resolved); the SSAO is taken off it alone.
		void CompositePass(const Framebuffer& sceneResolved, const Framebuffer& sceneDepth, const Framebuffer& sceneAmbient,
			const Environment& env, const PostSettings& post, const Camera& camera, const Framebuffer* target);

	private:
		void CreateTargets();

		uint32_t m_Width, m_Height;
		std::unique_ptr<Shader> m_AOShader, m_AOBlurShader, m_LumShader, m_AdaptShader;
		std::unique_ptr<Shader> m_BloomDown, m_BloomUp, m_Composite;
		std::vector<std::unique_ptr<Framebuffer>> m_BloomChain;
		std::unique_ptr<Framebuffer> m_AO, m_AOBlur, m_LumSmall, m_Adapt[2];
		int m_AdaptCurrent = 0;
		bool m_AdaptValid = false;
		float m_TimeAccum = 0.0f;
		RHI::VertexLayoutHandle m_EmptyVAO;
		// Full-screen passes: no depth test, and no blending except the bloom's way up (additive). m_DepthOn: the depth test on again after
		// the composite, for whatever draws next.
		RHI::PipelineHandle m_BloomDownPipe, m_BloomUpPipe, m_AOPipe, m_AOBlurPipe, m_LumPipe, m_AdaptPipe, m_CompositePipe, m_DepthOn;
	};
}
