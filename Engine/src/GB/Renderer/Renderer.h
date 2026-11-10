#pragma once
#include "Camera.h"
#include "Framebuffer.h"
#include "FrameConstants.h"
#include "PostProcessor.h"
#include "GrassRenderer.h"
#include "RenderList.h"
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
		static constexpr uint32_t MaxPointLights = FrameConstants::MaxPointLights;

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
		// T69: fills m_Frame for this frame, writes it into m_FrameRing and binds it at FrameConstants::Binding (before the shadow pass).
		void UploadFrame(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows);
		void ShadowPass(const Scene& scene);
		void ScenePass(const Scene& scene, const Camera& camera);
		void GrassPass(const Scene& scene, const Camera& camera);
		void WaterPass(const Scene& scene);

		RenderStats m_Stats;
		// T78: the frame's objects and what the camera and the shadow map see of them (built and culled once at the start of Render), and the
		// buffers of the scene pass: the instanced objects by mesh, the translucent ones, the matrices of a batch. Kept between frames.
		RenderList m_List;
		std::vector<BatchEntry> m_Batches;
		std::vector<const RenderObject*> m_Translucent;
		std::vector<glm::mat4> m_Matrices;
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
		RHI::VertexLayoutHandle m_EmptyVAO;
		RHI::BufferHandle m_JointBuffer;
		RHI::BufferHandle m_InstanceBuffer;   // the matrices of one instanced draw (RenderObject::Instanced)
		// The state of the scene pass: the sky (no depth), the objects (depth, back faces culled).
		RHI::PipelineHandle m_SkyPipeline, m_PBRPipeline;
		// T69 (render M3): the per-frame block and the ring it is written into, one region per frame in flight. m_Frame is kept between
		// frames: the ground marks window and the environment map's values are only rewritten when there are marks or a map, as the loose
		// uniforms they replace kept their last value.
		FrameConstants m_Frame;
		std::unique_ptr<RHI::UploadRing> m_FrameRing;
		bool m_FrameRingFull = false;   // (logged once)

		void BindSkin(Shader& shader, const RenderObject& obj);

		// GPU timestamps of a frame, in the order they are issued: start, after the shadows, the sky, the objects, the grass, the water, the
		// translucent objects, the particles, the resolve, the bloom, the SSAO and the composite (the last one is always the last issued: it says
		// the set is done). A ring of sets in flight, read oldest first when the GPU has them, never waiting: the CPU runs a frame or two
		// ahead, and with only two sets the one due for reuse was never ready, so the times froze. A set is not issued again until it has
		// been read; a frame that finds the ring full is not measured (the stats keep the last times read).
		enum TimerMark { kStart, kAfterShadows, kAfterSky, kAfterObjects, kAfterGrass, kAfterWater, kAfterTranslucent, kAfterParticles, kAfterResolve,
			kAfterBloom, kAfterSsao, kAfterComposite, kMarks };
		static constexpr int kTimerSets = 5;
		RHI::QueryHandle m_Timers[kTimerSets][kMarks] = {};
		int m_TimerHead = 0;          // the set the next measured frame writes
		int m_TimersPending = 0;      // sets issued and not read yet, the oldest at m_TimerHead - m_TimersPending
		bool m_TimerActive = false;   // this frame is being measured (the ring had room when it started)
		void Mark(int i);
		void ReadTimers();
	};
}
