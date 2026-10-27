#pragma once
#include "GB/Game/CharacterController.h"
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Leads a CharacterController along a path by producing the same CharacterInput a player's keys would. A non-player
	// character is the same controller, the same animation and the same footsteps; only the input comes from here.
	class AIDriver
	{
	public:
		float WalkSpeed = 1.7f, RunSpeed = 4.6f;   // m/s (match CharacterConfig; used to choose walk or run input)
		bool Run = false;
		float ArriveRadius = 0.3f;      // within this of the last point it has arrived
		float WaypointRadius = 0.7f;    // within this of an intermediate point it moves on
		float SlowRadius = 1.5f;        // starts slowing this far from the last point
		float StuckSeconds = 1.5f;      // gives up when it is blocked and barely moving for this long

		void MoveTo(const glm::vec2& xz) { SetPath({ xz }); }
		void SetPath(std::vector<glm::vec2> points);
		void Stop() { m_Path.clear(); m_Index = 0; m_Arrived = m_Stuck = false; }
		bool Arrived() const { return m_Arrived; }
		bool Stuck() const { return m_Stuck; }
		bool HasTarget() const { return m_Index < m_Path.size(); }

		// The input for this frame: the controller's position and speed decide where to head.
		CharacterInput Update(const CharacterController& controller, float dt);

		// The input that makes a controller move along the world direction `dir` (xz, unit) at `speed` m/s.
		static CharacterInput InputFor(const glm::vec2& dir, float speed, const CharacterConfig& config);

	private:
		std::vector<glm::vec2> m_Path;
		size_t m_Index = 0;
		bool m_Arrived = false, m_Stuck = false;
		float m_StillTime = 0.0f;
	};
}
