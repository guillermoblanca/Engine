#pragma once
#include "GB/Game/Interaction.h"
#include "GB/Scene/Task.h"
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	// Runs a task list for one character. It gives the controller's input like the AI does (the same CharacterInput as keys), uses the
	// InteractionRunner for interactions, and leaves everything the animation does to the animation: the character it drives can be
	// a player's or an NPC's.
	class TaskRunner
	{
	public:
		NavProfile Profile;   // how the walker values the terrain when it plans a path (role, load)

		// Finds an interaction point by entity and point name.
		using Resolver = std::function<bool(const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error)>;
		// Attaches (true) or lets go of (false) a prop by name; an empty name on detach means everything. False = it could not.
		using PropHandler = std::function<bool(const std::string& prop, bool attach)>;
		// Abilities are the host's: Start activates one (false and `error` when it cannot); Poll says how it goes (0 running, 1 ended well, -1 failed).
		// While one runs the ability moves the character, so the input the runner gives is empty.
		struct AbilityHandler
		{
			std::function<bool(const std::string& ability, const std::string& entity, std::string& error)> Start;
			std::function<int(std::string& error)> Poll;
		};

		void Set(std::vector<Task> tasks);
		void Clear();
		bool Done() const { return m_Index >= m_Tasks.size() || m_Failed; }
		bool Failed() const { return m_Failed; }
		bool Running() const { return !Done(); }
		const std::string& Error() const { return m_Error; }
		size_t Index() const { return m_Index; }
		size_t Count() const { return m_Tasks.size(); }
		std::string Describe() const;   // "2/4 interact Apple Tree/pick (act)", "done", "failed: ..."

		// Once per frame before the controller's step. Returns the input to step it with.
		CharacterInput Update(CharacterController& controller, const CharacterWorld& world, AnimInstance* anim, float dt,
			const Resolver& resolver, const PropHandler& props, const AbilityHandler& abilities = AbilityHandler());

	private:
		void Fail(const std::string& why);
		void Next() { ++m_Index; m_Started = false; }

		std::vector<Task> m_Tasks;
		size_t m_Index = 0;
		bool m_Started = false, m_Failed = false;
		float m_Time = 0.0f;
		std::string m_Error;
		AIDriver m_AI;
		InteractionRunner m_Interaction;
	};
}
