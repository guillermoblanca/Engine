#include "GB/Game/Components/Gameplay/MovementComponent.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	void MovementComponent::Init(const CharacterWorld& world, float height, const glm::vec2& start, float yaw, const std::vector<CircleObstacle>& others)
	{
		m_Height = height;
		m_World = world;
		m_StaticObstacles = m_World.Obstacles.size();
		ApplyBodySize();
		m_BaseWalk = m_Controller.Config.WalkSpeed;
		m_BaseRun = m_Controller.Config.RunSpeed;
		SetOthers(others);
		const glm::vec2 spot = FindFreeSpot(m_World, start, 2.5f);
		m_Controller.SetPosition(m_World, spot.x, spot.y, yaw);
	}

	void MovementComponent::ApplyBodySize()
	{
		m_Controller.Config.Radius = std::max(0.2f * m_Height, 0.25f);
		m_Controller.Config.MaxWadeDepth = 0.55f * m_Height;
	}

	float MovementComponent::SetHeight(float height)
	{
		if (height <= 0.01f || m_Height <= 0.0f || std::fabs(height - m_Height) < 1e-4f) return 0.0f;
		const float factor = height / m_Height;
		m_Height = height;
		ApplyBodySize();
		return factor;
	}

	void MovementComponent::SetOthers(const std::vector<CircleObstacle>& others)
	{
		m_World.Obstacles.resize(m_StaticObstacles);
		m_World.DynamicFrom = m_StaticObstacles;
		m_World.Obstacles.insert(m_World.Obstacles.end(), others.begin(), others.end());
	}

	void MovementComponent::SetStaticObstacles(const std::vector<CircleObstacle>& obstacles)
	{
		m_World.Obstacles = obstacles;
		m_StaticObstacles = obstacles.size();
		m_World.DynamicFrom = m_StaticObstacles;
	}

	void MovementComponent::LimitSpeeds(float limit)
	{
		m_Limit = limit;
		ApplySpeeds();
	}

	void MovementComponent::SetPace(float pace)
	{
		pace = std::max(pace, 0.1f);
		if (pace == m_Pace) return;
		m_Pace = pace;
		ApplySpeeds();
	}

	void MovementComponent::ApplySpeeds()
	{
		if (m_BaseRun <= 0.0f) return;   // (before Init: the controller keeps its own)
		m_Controller.Config.WalkSpeed = std::min(m_BaseWalk, m_Limit) * m_Pace;
		m_Controller.Config.RunSpeed = std::min(m_BaseRun, m_Limit) * m_Pace;
	}

	void MovementComponent::Step(const CharacterInput& input, float dt)
	{
		m_Controller.Step(input, m_World, dt);
		m_Motion = m_Tracker.Update(m_Controller, dt);
	}
}
