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
		void CompositePass(const Framebuffer& sceneResolved, const Framebuffer& sceneDepth,
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
		uint32_t m_EmptyVAO = 0;
	};
}
