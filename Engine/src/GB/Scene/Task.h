#pragma once
#include <glm/glm.hpp>
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
	//   ability <name> [entity]  use an ability (see GB/Gameplay) on an entity; it goes there, plays the action and the list goes on when it ends
	struct Task
	{
		enum class Kind { GoTo, Wait, Interact, Attach, Detach, Ability };
		Kind What = Kind::Wait;
		glm::vec2 Position{ 0.0f };
		bool Run = false;
		float Seconds = 0.0f;
		std::string Entity, Point, Prop;
	};

	// False (and `error`) at the first line that is not a task; the tasks before it are kept in `out`.
	bool ParseTasks(const std::string& text, std::vector<Task>& out, std::string* error = nullptr);
	std::string DescribeTask(const Task& task);
}
