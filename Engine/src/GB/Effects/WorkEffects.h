#pragma once
#include "GB/Effects/WorkFxLook.h"
#include "GB/Renderer/Particles.h"
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	struct Scene;

	// A45: the field work's particles (GB/Effects/WorkFxLook.h, visuals.json "workFx") and the pushers of the plants.
	//
	// The runtime: one particle system per layer of each activity, shared by every worker; and the pushers of the bodies near the viewer.
	// Per frame: Configure (the look; a change rebuilds the systems), then Work and Touch for whoever works and walks, then Update, then
	// AddToScene. Deterministic for the same calls (its own generator).
	class WorkEffects
	{
	public:
		explicit WorkEffects(uint32_t seed = 7);

		void Configure(const WorkFxLook& look);
		const WorkFxLook& Look() const { return m_Look; }
		void SetViewer(const glm::vec3& viewer) { m_Viewer = viewer; }
		void SetGround(std::function<float(float, float)> ground);
		// How lit the scene is, 0..1 (1 = day): the particles are unlit, so their colours are scaled by it or they would glow at night
		void SetLight(float light) { m_Light = light; }

		// `who` does `activity` at `at` (its feet) facing `forward`; `fraction` is how far its montage's cycle is (0..1, < 0: none, the work's
		// own Cycle is used). Call every frame it works; an activity without an effect does nothing.
		void Work(const std::string& who, const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float fraction, float dt);
		// A body at `at` this frame: the plants around it lean (only the pushable materials, and only the MaxPushers nearest the viewer).
		void Touch(const std::string& who, const glm::vec3& at);
		// Where the pushable plants are (a field: centre x, z and radius). Once any area is given, only the bodies inside one push (so the
		// MaxPushers slots go to the crops and not to the animals grazing elsewhere); the areas are kept until the next Update.
		void PushArea(const glm::vec3& centreXZRadius) { m_AreasNext.push_back(centreXZRadius); }
		// A worker with nobody behind it (tests, captures, `visuals workfx`): does `activity` at `at` for `seconds`.
		void Pretend(const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float seconds);

		void Update(float dt);
		// Adds the systems to scene.Particles (once) and sets scene.Pushers and the push strengths (replacing what was there).
		void AddToScene(Scene& scene) const;

		size_t LiveParticles() const;
		size_t PusherCount() const { return m_Out.size(); }
		size_t Systems() const { return m_Systems.size(); }

	private:
		struct Layer { std::string Activity; size_t Index = 0; std::unique_ptr<ParticleSystem> System; };
		struct Worker { float Clock = 0.0f; std::vector<float> Carry; bool Seen = false; };
		struct Pusher { glm::vec3 At{ 0.0f }, Lag{ 0.0f }; float Strength = 0.0f; bool Seen = false, Fresh = true; };
		struct Pretender { std::string Activity; glm::vec3 At{ 0.0f }, Forward{ 0.0f, 0.0f, 1.0f }; float Left = 0.0f; std::string Key; };

		float Random();
		void Emit(Worker& w, const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float fraction, float dt);

		WorkFxLook m_Look;
		bool m_Configured = false;
		std::vector<Layer> m_Systems;
		std::map<std::string, Worker> m_Workers;
		std::map<std::string, Pusher> m_Pushers;
		std::vector<Pretender> m_Pretend;
		std::vector<glm::vec4> m_Out;   // the pushers sent: x, z, radius, strength
		std::vector<glm::vec3> m_Areas, m_AreasNext;   // the fields of the last frame (PushArea), and those told for the next
		std::function<float(float, float)> m_Ground;
		glm::vec3 m_Viewer{ 0.0f };
		float m_Light = 1.0f;
		uint64_t m_Rng = 1;
		uint32_t m_Seed = 7;
		int m_PretendSerial = 0;
	};
}
