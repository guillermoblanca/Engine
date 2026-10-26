#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	struct FootPlant
	{
		int Foot = 0;
		glm::vec3 Position{ 0.0f };   // where the foot came down (on the ground)
		glm::vec3 Heading{ 0.0f, 0.0f, 1.0f };
		float Speed = 0.0f;           // horizontal speed of the foot just before landing (m/s)
	};

	// Detects the moments a foot touches down, from the feet's world positions each frame (e.g. the foot
	// bones of an animated character). Hysteresis (lift height > plant height) avoids stutter.
	class FootstepTracker
	{
	public:
		explicit FootstepTracker(int footCount = 2);

		float PlantHeight = 0.08f;    // a foot counts as down within this height above the ground (meters)
		float LiftHeight = 0.14f;     // it must rise above this before it can land again
		float MinLandingSpeed = 0.0f; // ignore landings slower than this (m/s); 0 = report every landing

		// Feeds the feet positions (one per foot) and returns the touch-downs since the last call.
		// `ground(x, z)` gives the terrain height; `heading` is the direction the actor faces.
		std::vector<FootPlant> Update(const std::vector<glm::vec3>& feet, const std::function<float(float, float)>& ground,
			const glm::vec3& heading, float dt);
		void Reset();

	private:
		struct FootState { bool Down = false; glm::vec3 Last{ 0.0f }; bool Known = false; };
		std::vector<FootState> m_Feet;
	};
}
