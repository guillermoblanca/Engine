#pragma once
#include "GB/Game/Interaction.h"
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	// What a non-player character is told to do, one thing after another. Written as text ("goto 10 5 run; interact Apple Tree pick;
	// wait 2; attach box"), separated by ';', '|' or new lines; '#' starts a comment.
	//   goto <x> <z> [run]       walk (or run) to a point of the world
	//   interact <entity> [point] go to an entity's interaction point and use it
	//   wait <seconds>
	//   attach <prop> / detach [prop]   pick a prop up or let go (what the props are is up to the host)
	struct Task
	{
		enum class Kind { GoTo, Wait, Interact, Attach, Detach };
		Kind What = Kind::Wait;
		glm::vec2 Position{ 0.0f };
		bool Run = false;
		float Seconds = 0.0f;
		std::string Entity, Point, Prop;
	};

	// False (and `error`) at the first line that is not a task; the tasks before it are kept in `out`.
	bool ParseTasks(const std::string& text, std::vector<Task>& out, std::string* error = nullptr);
	std::string DescribeTask(const Task& task);

	// Runs a task list for one character. It gives the controller's input like the AI does (the same CharacterInput as keys), uses the
	// InteractionRunner for interactions, and leaves everything the animation does to the animation: the character it drives can be
	// a player's or an NPC's.
	class TaskRunner
	{
	public:
		// Finds an interaction point by entity and point name.
		using Resolver = std::function<bool(const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error)>;
		// Attaches (true) or lets go of (false) a prop by name; an empty name on detach means everything. False = it could not.
		using PropHandler = std::function<bool(const std::string& prop, bool attach)>;

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
			const Resolver& resolver, const PropHandler& props);

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
