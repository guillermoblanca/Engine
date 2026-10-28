#pragma once
#include "GB/Game/CharacterController.h"
#include "GB/Navigation/NavGrid.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	// Leads a CharacterController along a path by producing the same CharacterInput a player's keys would. A non-player
	// character is the same controller, the same animation and the same footsteps; only the input comes from here.
	//
	// With a navigation grid in the world (CharacterWorld::Nav) MoveTo plans a path over the terrain (around trunks, rocks and deep water, wading shallow
	// water only when it pays), follows it steering around the other characters, notices when it is not getting anywhere and plans again, and plans again
	// when the world changed under its path. Without one (or with SetPath) it walks the points in a straight line as it always did.
	class AIDriver
	{
	public:
		float WalkSpeed = 1.7f, RunSpeed = 4.6f;   // m/s (match CharacterConfig; used to choose walk or run input)
		bool Run = false;
		float ArriveRadius = 0.3f;      // within this of the last point it has arrived
		float WaypointRadius = 0.7f;    // within this of an intermediate point it moves on
		float SlowRadius = 1.5f;        // starts slowing this far from the last point
		float StuckSeconds = 1.5f;      // gives up when it is blocked and barely moving for this long
		NavProfile Profile;             // how this walker values the terrain (role, load); the slope and wading limits come from the controller
		int MaxReplans = 3;             // plans again this many times in a row without making headway before it gives up
		float CrowdPatience = 20.0f;    // seconds it waits for other characters in its way before that counts as being stuck
		float AvoidDistance = 2.5f;     // looks for other characters this far ahead

		void MoveTo(const glm::vec2& xz);
		void SetPath(std::vector<glm::vec2> points);
		void Stop() { m_Path.clear(); m_Index = 0; m_Arrived = m_Stuck = m_Routed = m_NeedPlan = false; }
		bool Arrived() const { return m_Arrived; }
		bool Stuck() const { return m_Stuck; }
		const std::string& StuckReason() const { return m_Why; }
		bool HasTarget() const { return m_Index < m_Path.size() || m_NeedPlan; }
		int Replans() const { return m_Replans; }                       // plans made in a row without headway for the current target
		const std::vector<glm::vec2>& Path() const { return m_Path; }   // the waypoints it is following (for drawing)
		size_t PathIndex() const { return m_Index; }

		// The input for this frame: the controller's position and speed decide where to head. This one never plans (a straight walk).
		CharacterInput Update(const CharacterController& controller, float dt);
		// The same with the world: plans, avoids and replans when the world has a navigation grid.
		CharacterInput Update(const CharacterController& controller, const CharacterWorld& world, float dt);

		// The input that makes a controller move along the world direction `dir` (xz, unit) at `speed` m/s.
		static CharacterInput InputFor(const glm::vec2& dir, float speed, const CharacterConfig& config);

	private:
		bool Plan(const CharacterController& c, const NavGrid& nav, const CharacterWorld& world, bool avoidNeighbors);
		void Stall(const CharacterController& c, const NavGrid& nav, const CharacterWorld& world);

		std::vector<glm::vec2> m_Path;
		size_t m_Index = 0;
		bool m_Arrived = false, m_Stuck = false;
		std::string m_Why;
		float m_StillTime = 0.0f;
		// planned walking
		glm::vec2 m_Goal{ 0.0f };
		bool m_Routed = false, m_NeedPlan = false, m_Partial = false;
		uint32_t m_PlanRevision = 0;
		int m_Replans = 0;
		float m_CrowdSeconds = 0.0f;
		float m_WindowTime = 0.0f;
		glm::vec2 m_WindowStart{ 0.0f };
	};
}
