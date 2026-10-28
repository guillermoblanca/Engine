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
	// all: it walks in a straight line at the actor's speed and its abilities run on timers on the agent's own ability system (which must have no
	// runtime lent to it), so the same things happen at the same moments and the tribe decides exactly as it does for a full actor. Far from the camera
	// this costs a few arithmetic operations instead of an animation, IK and a skinned mesh; when the camera comes near, the tribe puts the actor
	// where the agent is and gives it the orders again.
	class AbstractAgent
	{
	public:
		// Where an entity is (x, z); false when it is gone.
		using Locate = std::function<bool(const std::string& entity, glm::vec2& pos)>;

		void Place(const glm::vec2& pos) { m_Pos = pos; m_Tasks.clear(); m_Failed = false; m_Error.clear(); m_Started = false; }
		bool Do(const std::string& text, std::string& error);
		void Update(float dt, AbilitySystemComponent* who, GameplayWorld& gameplay, const Locate& locate, float speed);

		bool Busy() const { return m_Index < m_Tasks.size() && !m_Failed; }
		bool Failed() const { return m_Failed; }
		const std::string& Error() const { return m_Error; }
		const glm::vec2& Position() const { return m_Pos; }

	private:
		enum class Phase { Start, Walk, Act, Wait };
		void Next() { ++m_Index; m_Phase = Phase::Start; }
		void Fail(const std::string& why) { m_Failed = true; m_Error = "failed: " + why; m_Tasks.clear(); m_Index = 0; }

		std::vector<Task> m_Tasks;
		size_t m_Index = 0;
		Phase m_Phase = Phase::Start;
		glm::vec2 m_Pos{ 0.0f }, m_Goal{ 0.0f };
		float m_Timer = 0.0f;
		bool m_Failed = false, m_Started = false;
		std::string m_Error;
	};
}
