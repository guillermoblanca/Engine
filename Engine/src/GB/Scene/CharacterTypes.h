#pragma once
#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// The plain data a character is stepped with: its input, and what it can stand on and bump into. They live here, below Game, so
	// navigation (which builds its grid from a CharacterWorld) and the animation do not depend on the controller (GB/Game/CharacterController.h).

	class CharacterController;   // GB/Game/CharacterController.h
	class NavGrid;               // GB/Navigation/NavGrid.h

	struct CharacterInput
	{
		glm::vec2 Move{ 0.0f };   // x = right, y = forward, relative to the camera; any length (clamped to 1)
		bool Run = false;
		bool Jump = false;        // held; the jump starts on the press (edge) while standing on the ground
		float CameraYaw = 0.0f;   // yaw of the OrbitController convention: the camera sits at (sin yaw, cos yaw) from the target
	};

	struct CircleObstacle
	{
		glm::vec2 Center{ 0.0f };   // xz
		float Radius = 0.5f;
	};

	// What the character can stand on and bump into. Every callback is optional.
	struct CharacterWorld
	{
		std::function<float(float, float)> GroundHeight;   // terrain height at x,z (default 0)
		std::function<float(float, float)> WaterLevel;     // surface height, or <= -1e8 where there is no water
		std::vector<CircleObstacle> Obstacles;
		glm::vec2 BoundsMin{ -1e9f }, BoundsMax{ 1e9f };    // the character stays inside (xz)
		const NavGrid* Nav = nullptr;                       // where an AIDriver finds paths (null: it walks in a straight line); must outlive the world
		size_t DynamicFrom = static_cast<size_t>(-1);       // Obstacles from this index on are other characters (moving): the AIDriver steers around them
	};
}
