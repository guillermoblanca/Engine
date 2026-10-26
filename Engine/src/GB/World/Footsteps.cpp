#include "Footsteps.h"
#include <cmath>

namespace GB
{
	FootstepTracker::FootstepTracker(int footCount) : m_Feet(static_cast<size_t>(std::max(footCount, 0))) {}

	void FootstepTracker::Reset()
	{
		for (FootState& f : m_Feet)
			f = {};
	}

	std::vector<FootPlant> FootstepTracker::Update(const std::vector<glm::vec3>& feet,
		const std::function<float(float, float)>& ground, const glm::vec3& heading, float dt)
	{
		std::vector<FootPlant> plants;
		if (m_Feet.size() < feet.size())
			m_Feet.resize(feet.size());

		for (size_t i = 0; i < feet.size(); ++i)
		{
			FootState& state = m_Feet[i];
			const glm::vec3& p = feet[i];
			const float groundY = ground ? ground(p.x, p.z) : 0.0f;
			const float height = p.y - groundY;

			const bool wasDown = state.Down;
			if (!state.Known)
			{
				state.Down = height < PlantHeight;   // first sight: no event, just learn the state
				state.Known = true;
			}
			else if (state.Down && height > LiftHeight)
				state.Down = false;
			else if (!state.Down && height < PlantHeight)
			{
				state.Down = true;
				const glm::vec3 delta = p - state.Last;
				const float speed = dt > 1e-5f ? glm::length(glm::vec2(delta.x, delta.z)) / dt : 0.0f;
				if (speed >= MinLandingSpeed)
				{
					FootPlant plant;
					plant.Foot = static_cast<int>(i);
					plant.Position = { p.x, groundY, p.z };
					plant.Heading = heading;
					plant.Speed = speed;
					plants.push_back(plant);
				}
			}
			(void)wasDown;
			state.Last = p;
		}
		return plants;
	}
}
