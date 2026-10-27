#pragma once
#include "GB/World/Physics.h"
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace GB
{
	struct DestructibleDesc
	{
		float Toughness = 40.0f;        // impulse (N s) a single hit or blast needs to break it
		bool Accumulate = true;         // smaller hits add up as damage
		glm::ivec3 Split{ 2, 2, 2 };    // boxes: fragments along each axis; spheres break into 4 smaller spheres
		float DebrisLifetime = 8.0f;    // seconds before fragments disappear (they shrink during the last second)
		bool FragmentsBreakAgain = false;
		float FragmentToughness = 1e9f; // when FragmentsBreakAgain
		float BlastSpeed = 4.0f;        // extra speed (m/s) thrown outward at the pieces
	};

	struct BreakEvent
	{
		glm::vec3 Position{ 0.0f };
		float Size = 0.0f;              // bounding radius of what broke
		float Impulse = 0.0f;           // what broke it
		int Fragments = 0;
		int FromBody = -1;
	};

	// Breaks rigid bodies into pieces when they are hit hard enough or caught in a blast; the pieces are ordinary
	// rigid bodies that live for a while and then vanish. Headless: it only works through the PhysicsWorld.
	class DestructionSystem
	{
	public:
		explicit DestructionSystem(PhysicsWorld& world) : m_World(world) {}

		void Add(int body, const DestructibleDesc& desc);
		bool IsDestructible(int body) const { return m_Destructibles.count(body) != 0; }
		bool IsDebris(int body) const { return m_Debris.count(body) != 0; }
		// 1 for a whole body, fading to 0 in the last second of a fragment's life (for rendering the shrink).
		float GetVisualScale(int body) const;

		// Call every frame with the contacts of the step (PhysicsWorld::TakeContacts) after stepping the world.
		void Update(float dt, const std::vector<ContactEvent>& contacts);
		// A blast: pushes bodies (PhysicsWorld::Explode) and breaks the destructible ones it is strong enough for.
		void Explode(const glm::vec3& center, float radius, float speedAtCenter);
		// Damage a body directly (a bullet, an axe...); breaks it if the accumulated damage exceeds its toughness.
		void Damage(int body, float impulse, const glm::vec3& point);
		void Break(int body, float impulse);

		// Called for every new fragment: (new body, body it came from, fragment size relative to the original).
		std::function<void(int, int, const glm::vec3&)> OnFragment;
		std::vector<BreakEvent> TakeBreaks() { return std::move(m_Breaks); }
		size_t GetDebrisCount() const { return m_Debris.size(); }
		size_t GetBrokenCount() const { return m_BrokenTotal; }

	private:
		struct Info { DestructibleDesc Desc; float Damage = 0.0f; };
		struct Debris { float Age = 0.0f, Lifetime = 8.0f; };
		PhysicsWorld& m_World;
		std::unordered_map<int, Info> m_Destructibles;
		std::unordered_map<int, Debris> m_Debris;
		std::vector<BreakEvent> m_Breaks;
		std::vector<int> m_PendingBreak;
		std::vector<float> m_PendingImpulse;
		size_t m_BrokenTotal = 0;
		uint32_t m_Rng = 1234567u;
		float Rand() { m_Rng = m_Rng * 1664525u + 1013904223u; return static_cast<float>(m_Rng >> 8) / 16777216.0f; }
	};
}
