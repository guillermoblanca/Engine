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
		// The scene's ambient (indirect) light, written by the forward pass as a second target and resolved here: the SSAO only darkens this
		// part in the composite, not the sun or the lamps.
		std::unique_ptr<Framebuffer> m_SceneAmbient;
		std::unique_ptr<PostProcessor> m_PostProcessor;
		uint32_t m_EmptyVAO = 0;
		uint32_t m_JointBuffer = 0;
		uint32_t m_InstanceBuffer = 0;   // the matrices of one instanced draw (RenderObject::Instanced)

		void BindSkin(Shader& shader, const RenderObject& obj);

		// GPU timestamps of a frame: start, after the shadows, the objects, the grass, the water, the rest of the scene (particles, resolve) and the
		// post-processing. A ring of sets in flight, read oldest first when the GPU has them, never waiting: the CPU runs a frame or two
		// ahead, and with only two sets the one due for reuse was never ready, so the times froze. A set is not issued again until it has
		// been read; a frame that finds the ring full is not measured (the stats keep the last times read).
		static constexpr int kMarks = 7;
		static constexpr int kTimerSets = 5;
		uint32_t m_Timers[kTimerSets][kMarks] = {};
		int m_TimerHead = 0;          // the set the next measured frame writes
		int m_TimersPending = 0;      // sets issued and not read yet, the oldest at m_TimerHead - m_TimersPending
		bool m_TimerActive = false;   // this frame is being measured (the ring had room when it started)
		void Mark(int i);
		void ReadTimers();
	};
}
