#pragma once
#include "GB/Renderer/Renderer.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	struct LightningDesc
	{
		glm::vec3 Center{ 0.0f };         // strikes land inside a disc around this point (on the ground, y = Center.y)
		float AreaRadius = 60.0f;
		float CloudHeight = 45.0f;        // bolts start this far above the ground
		glm::vec2 Interval{ 4.0f, 10.0f };   // seconds between automatic strikes
		bool Automatic = true;
		float Duration = 0.5f;            // how long a bolt stays visible (flickering)
		glm::vec3 Color{ 0.62f, 0.74f, 1.0f };
		float Brightness = 18.0f;         // HDR intensity of the bolt (feeds the bloom)
		float CoreWidth = 0.17f;
		float GlowWidth = 1.5f;
		float Jaggedness = 0.30f;         // sideways displacement relative to segment length
		int Levels = 6;                   // 2^Levels segments in the main bolt
		float BranchChance = 0.28f;       // per subdivision point
		int MaxBranches = 7;
		float LightIntensity = 5000.0f;   // radiant intensity of the flash light (1/d^2 falloff)
		float SkyBoost = 3.0f;            // extra environment intensity at the flash peak
		float SpeedOfSound = 343.0f;
		uint32_t Seed = 7;
	};

	struct LightningSegment
	{
		glm::vec3 A{ 0.0f }, B{ 0.0f };
		float Width = 1.0f;               // relative: 1 = main bolt, branches are thinner
	};

	// Sound of a strike, delivered after the light-to-sound delay. For the audio system.
	struct ThunderEvent
	{
		glm::vec3 Position{ 0.0f };
		float Distance = 0.0f;            // from the listener, meters
		float Loudness = 1.0f;            // 0..1, falls off with distance
	};

	// Procedural lightning: jagged branching bolts, a flickering flash, thunder events.
	class Lightning
	{
	public:
		explicit Lightning(const LightningDesc& desc = {});
		LightningDesc Desc;

		void SetListener(const glm::vec3& position) { m_Listener = position; }
		void Update(float dt);
		// Strikes now at `target` (a point on the ground), or at a random point of the area.
		void Strike(const glm::vec3* target = nullptr);

		bool IsActive() const { return m_Active; }
		float GetFlash() const { return m_Flash; }                       // 0..1 brightness of the current flash
		const glm::vec3& GetStrikePosition() const { return m_Target; }
		const std::vector<LightningSegment>& GetSegments() const { return m_Segments; }
		// Thunder events whose delay has elapsed since the last call.
		std::vector<ThunderEvent> TakeThunder();

		// Adds the bolt (as particle batches) and the flash light to the scene. Does not touch the
		// environment: use GetFlash() * Desc.SkyBoost to brighten the sky.
		void AddToScene(Scene& scene) const;

	private:
		float Random();
		float Random(float lo, float hi) { return lo + (hi - lo) * Random(); }
		glm::vec3 RandomPerpendicular(const glm::vec3& dir);
		void Subdivide(const glm::vec3& a, const glm::vec3& b, int depth, float width, bool allowBranches);
		void Rebuild();
		void ScheduleNext();

		std::vector<LightningSegment> m_Segments;
		std::vector<ParticleInstance> m_Glow, m_Core;
		glm::vec3 m_Target{ 0.0f }, m_Listener{ 0.0f };
		float m_Age = 0.0f, m_Flash = 0.0f, m_Timer = 0.0f;
		bool m_Active = false;
		float m_Pulse[3] = { 0.0f, 0.1f, 0.22f };
		float m_PulseAmp[3] = { 1.0f, 0.7f, 0.45f };
		int m_BranchesLeft = 0;
		uint64_t m_Rng = 1;

		struct PendingThunder { float Delay; ThunderEvent Event; };
		std::vector<PendingThunder> m_Pending;
		std::vector<ThunderEvent> m_Ready;
	};
}
