#pragma once
#include "GB/Game/TaskList.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	// The abstract agent of the level of detail (H6): the same orders a full actor runs (a task list: goto, wait, ability), carried out with no body at
	// all: it walks the waypoints of a route at the actor's speed and its abilities run on timers on the agent's own ability system (which must have no
	// runtime lent to it), so the same things happen at the same moments and the tribe decides exactly as it does for a full actor. Far from the camera
	// this costs a few arithmetic operations instead of an animation, IK and a skinned mesh; when the camera comes near, the tribe puts the actor
	// where the agent is and gives it the orders again.
	//
	// The route (H5) comes from whoever owns the agent: with navigation it is the path the body itself would plan (same grid, same profile), so a far
	// member goes round the lake and never stands inside a wall, and a goal it cannot get to fails as it would for the full actor; with no route given,
	// the straight line (a test world with no ground).
	class AbstractAgent
	{
	public:
		// Where an entity is (x, z); false when it is gone.
		using Locate = std::function<bool(const std::string& entity, glm::vec2& pos)>;
		// The way from `from` to within `reach` of `to`: the waypoints (the last one is `to` itself when it can be stood on, else the nearest place
		// within reach of it), or false when there is no way. Empty function: the straight line.
		using Route = std::function<bool(const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points)>;

		void Place(const glm::vec2& pos) { m_Pos = m_Safe = pos; m_Tasks.clear(); m_Path.clear(); m_Failed = false; m_Error.clear(); m_Started = false; }
		bool Do(const std::string& text, std::string& error);
		void Update(float dt, AbilitySystemComponent* who, GameplayWorld& gameplay, const Locate& locate, float speed, const Route& route = Route());

		bool Busy() const { return m_Index < m_Tasks.size() && !m_Failed; }
		bool Failed() const { return m_Failed; }
		const std::string& Error() const { return m_Error; }
		const glm::vec2& Position() const { return m_Pos; }
		// The last point of a route it stood on (or where it was placed): somewhere the body can surely be put back.
		const glm::vec2& SafePosition() const { return m_Safe; }
		const std::vector<glm::vec2>& Path() const { return m_Path; }

	private:
		enum class Phase { Start, Walk, Act, Wait };
		void Next() { ++m_Index; m_Phase = Phase::Start; m_Path.clear(); }
		void Fail(const std::string& why) { m_Failed = true; m_Error = "failed: " + why; m_Tasks.clear(); m_Index = 0; m_Path.clear(); }
		bool Plan(const glm::vec2& to, float reach, const Route& route);
		bool Walk(float step);   // along m_Path; true on arrival

		std::vector<Task> m_Tasks;
		size_t m_Index = 0;
		Phase m_Phase = Phase::Start;
		std::vector<glm::vec2> m_Path;
		size_t m_Next = 0;
		float m_EndReach = 0.0f;   // how near the last waypoint is near enough (the reach when it is the goal itself, 0 when it is a place beside it)
		glm::vec2 m_Pos{ 0.0f }, m_Goal{ 0.0f }, m_Safe{ 0.0f };
		float m_Timer = 0.0f;
		bool m_Failed = false, m_Started = false;
		std::string m_Error;
	};
}
