#pragma once
#include "Camera.h"
#include "Framebuffer.h"
#include "PostProcessor.h"
#include "GrassRenderer.h"
#include "Scene.h"
#include "ShadowRenderer.h"
#include "Shader.h"
#include "WaterRenderer.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace GB
{
	// Forward PBR renderer: shadow map -> MSAA HDR scene -> bloom -> ACES tonemap.
	class Renderer
	{
	public:
		static constexpr uint32_t MaxPointLights = 32;

		Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples = 4, uint32_t shadowMapSize = 2048);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const { return m_Valid; }
		void Resize(uint32_t width, uint32_t height);
		const RenderStats& GetStats() const { return m_Stats; }

		// Renders into `target`, or the default framebuffer when null.
		void Render(const Scene& scene, const Camera& camera, const Framebuffer* target = nullptr);

	private:
		void CreateTargets();
		void ShadowPass(const Scene& scene, const glm::mat4& lightVP);
		void ScenePass(const Scene& scene, const Camera& camera, const glm::mat4& lightVP, bool shadows);
		void GrassPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows);
		void BindEnvironment(Shader& shader, const Environment& env) const;
		void WaterPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj);

		RenderStats m_Stats;
		uint32_t m_Width, m_Height, m_Samples, m_ShadowSize;
		bool m_Valid = false;

		std::unique_ptr<ShadowRenderer> m_ShadowRenderer;
		std::unique_ptr<Shader> m_PBRShader, m_SkyShader;
		std::unique_ptr<class ParticleRenderer> m_ParticleRenderer;
		std::unique_ptr<GrassRenderer> m_GrassRenderer;
		std::unique_ptr<WaterRenderer> m_WaterRenderer;
		std::unique_ptr<Framebuffer> m_ShadowMap, m_SceneMSAA, m_SceneResolved, m_SceneDepth;
		std::unique_ptr<PostProcessor> m_PostProcessor;
		uint32_t m_EmptyVAO = 0;
		uint32_t m_JointBuffer = 0;

		void BindSkin(Shader& shader, const RenderObject& obj);
	};
}
