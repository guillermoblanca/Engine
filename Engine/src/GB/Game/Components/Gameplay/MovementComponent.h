#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/MotionState.h"
#include <vector>

namespace GB
{
	// The body of an actor in the world: the controller (where it stands, how it walks), the world it walks in (ground, water, bounds, the static
	// obstacles and the other actors' circles), its size and the motion read from it. It needs no model: the same body moves a drawn character and
	// an abstract one. Whoever gives orders (OrdersComponent, an ability) hands it the input of the frame.
	class MovementComponent : public GBObjectComponent
	{
	public:
		// Puts the body at the free dry spot nearest to `start`.
		void Init(const CharacterWorld& world, float height, const glm::vec2& start, float yaw, const std::vector<CircleObstacle>& others);

		CharacterController& Controller() { return m_Controller; }
		const CharacterController& Controller() const { return m_Controller; }
		CharacterWorld& World() { return m_World; }
		const CharacterWorld& World() const { return m_World; }
		const MotionState& Motion() const { return m_Motion; }
		float Height() const { return m_Height; }

		// Grows or shrinks it to `height` metres: the capsule's radius and how deep it wades follow. Returns the factor the size changed by (0: nothing did).
		float SetHeight(float height);
		void SetOthers(const std::vector<CircleObstacle>& others);
		// Replaces the static obstacles (trees felled or planted, a door that closed).
		void SetStaticObstacles(const std::vector<CircleObstacle>& obstacles);
		void SetNavigation(const NavGrid* nav) { m_World.Nav = nav; }   // null: straight lines
		size_t StaticObstacles() const { return m_StaticObstacles; }
		void Teleport(float x, float z, float yaw) { m_Controller.SetPosition(m_World, x, z, yaw); }

		// A set with a walk and no run (a box, a barrow) caps the speeds: never faster than `limit`, never above the controller's own.
		bool HasOwnSpeeds() const { return m_BaseRun > 0.0f; }
		void LimitSpeeds(float limit);
		// One step with the input of the frame, then the motion of the body.
		void Step(const CharacterInput& input, float dt);
		// The motion without a step: the body is moved by something else (a ragdoll).
		void Track(float dt) { m_Motion = m_Tracker.Update(m_Controller, dt); }

	private:
		void ApplyBodySize();

		CharacterController m_Controller;
		CharacterWorld m_World;
		size_t m_StaticObstacles = 0;
		float m_Height = 1.75f;
		float m_BaseWalk = 0.0f, m_BaseRun = 0.0f;   // the controller's own speeds, before a set with no run clip caps them
		MotionTracker m_Tracker;
		MotionState m_Motion;
	};
}
