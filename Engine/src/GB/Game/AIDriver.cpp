#include "GB/Game/AIDriver.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kPlannedWaypointRadius = 0.45f;   // a planned path has clearance of about half a metre: its corners are cut less
		constexpr float kWindowSeconds = 1.0f;            // the time over which progress is judged
		constexpr float kMaxJudgedStep = 0.1f;            // s of one frame that count towards that judgement
		constexpr float kMinProgress = 0.3f;              // m moved in that window below which the walker is not getting anywhere

		// Steers `dir` around the characters ahead: each one in the way pushes to the side away from it (always the right one when it is head-on,
		// so two that meet pass each other), one too close pushes straight away, and the speed drops when someone is right in front.
		glm::vec2 Avoid(const glm::vec2& pos, const glm::vec2& dir, float radius, const CircleObstacle* others, size_t count, float lookAhead, float& speedScale)
		{
			const glm::vec2 left(-dir.y, dir.x);
			glm::vec2 steer(0.0f);
			speedScale = 1.0f;
			for (size_t k = 0; k < count; ++k)
			{
				const glm::vec2 rel = others[k].Center - pos;
				const float dist = glm::length(rel);
				const float reach = others[k].Radius + radius + 0.2f;
				if (dist < 1e-4f || dist > lookAhead + reach) continue;
				const float ahead = glm::dot(rel, dir);
				if (ahead < -0.2f) continue;   // behind: it is the one who has to get out of the way
				if (dist < reach + 0.15f)   // touching: step back a little and round it (only stepping back, with the path through it, is a standoff)
				{
					const float lat = dir.x * rel.y - dir.y * rel.x;
					steer -= rel / dist * 0.8f;
					steer += left * (lat > 0.05f ? -1.0f : (lat < -0.05f ? 1.0f : -1.0f)) * 1.5f;
					speedScale = std::min(speedScale, 0.6f);
					continue;
				}
				const float lateral = dir.x * rel.y - dir.y * rel.x;   // > 0: it is on the left
				if (std::fabs(lateral) > reach + 0.3f) continue;
				const float side = lateral > 0.05f ? -1.0f : (lateral < -0.05f ? 1.0f : -1.0f);
				steer += left * side * 1.5f * (1.0f - dist / (lookAhead + reach));
				if (std::fabs(lateral) < reach) speedScale = std::min(speedScale, std::max(0.35f, (dist - reach) / 0.8f));
			}
			const glm::vec2 out = dir + steer;
			return glm::length(out) > 1e-4f ? glm::normalize(out) : dir;
		}
	}

	void AIDriver::SetPath(std::vector<glm::vec2> points)
	{
		m_CrowdSeconds = 0.0f;
		m_Path = std::move(points);
		m_Index = 0;
		m_Arrived = m_Stuck = m_Routed = m_NeedPlan = m_Partial = false;
		m_StillTime = 0.0f;
		m_Replans = 0;
		m_WindowTime = 0.0f;
	}

	void AIDriver::MoveTo(const glm::vec2& xz)
	{
		SetPath({ xz });
		m_Goal = xz;
		m_Routed = true;
		m_NeedPlan = true;
	}

	CharacterInput AIDriver::InputFor(const glm::vec2& dir, float speed, const CharacterConfig& config)
	{
		CharacterInput in;
		if (speed <= 1e-4f || glm::length(dir) < 1e-6f) return in;
		const glm::vec2 d = glm::normalize(dir);
		// the controller takes camera-relative input: a camera whose forward is `d` and a push straight ahead
		in.CameraYaw = std::atan2(-d.x, -d.y);
		in.Run = speed > config.WalkSpeed + 1e-3f;
		const float top = in.Run ? config.RunSpeed : config.WalkSpeed;
		in.Move = { 0.0f, std::min(speed / top, 1.0f) };
		return in;
	}

	CharacterInput AIDriver::Update(const CharacterController& c, float dt)
	{
		if (m_Index >= m_Path.size()) return CharacterInput();
		const bool last = m_Index + 1 == m_Path.size();
		const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
		const glm::vec2 to = m_Path[m_Index] - here;
		const float dist = glm::length(to);
		if (dist < (last ? ArriveRadius : WaypointRadius))
		{
			if (last) { m_Arrived = true; m_Path.clear(); m_Index = 0; return CharacterInput(); }
			++m_Index;
			return Update(c, dt);
		}
		const float top = Run ? RunSpeed : WalkSpeed;
		float speed = top;
		if (last && dist < SlowRadius) speed = std::max(top * dist / SlowRadius, 0.4f);   // ease in, never crawl

		// pushing against something without getting anywhere: give up instead of pushing forever
		m_StillTime = c.WasBlocked() && c.GetSpeed() < 0.3f ? m_StillTime + dt : 0.0f;
		if (m_StillTime > StuckSeconds) { m_Stuck = true; m_Why = "pushed against something"; m_Path.clear(); m_Index = 0; return CharacterInput(); }

		CharacterConfig config = c.Config;
		config.WalkSpeed = WalkSpeed;
		config.RunSpeed = RunSpeed;
		return InputFor(to / dist, speed, config);
	}

	bool AIDriver::Plan(const CharacterController& c, const NavGrid& nav, const CharacterWorld& world, bool avoidNeighbors)
	{
		NavProfile profile = Profile;
		profile.MaxSlope = c.Config.MaxSlope;
		profile.MaxWadeDepth = c.Config.MaxWadeDepth;
		const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
		std::vector<CircleObstacle> near;   // characters close by: while planning around a stall they are walls
		if (avoidNeighbors && world.DynamicFrom < world.Obstacles.size())
			for (size_t k = world.DynamicFrom; k < world.Obstacles.size(); ++k)
			{
				const float d = glm::distance(world.Obstacles[k].Center, here);
				if (d < 6.0f && d > world.Obstacles[k].Radius + 0.5f) near.push_back(world.Obstacles[k]);   // (not one it stands against: it would be a wall around itself)
			}
		NavPath path;
		bool ok = nav.FindPath(profile, here, m_Goal, path, near.empty() ? nullptr : &near);
		if (!ok && !near.empty()) ok = nav.FindPath(profile, here, m_Goal, path);   // the crowd cuts the way: without it the goal may be open (people move)
		m_NeedPlan = false;
		m_PlanRevision = nav.Revision();
		m_WindowStart = here;
		m_WindowTime = 0.0f;
		m_StillTime = 0.0f;
		if (path.Points.empty())
		{
			m_Path.clear();
			m_Index = 0;
			m_Stuck = !ok;
			if (m_Stuck) m_Why = "no path to the goal";
			return false;
		}
		m_Path = std::move(path.Points);
		m_Index = 0;
		m_Partial = path.Partial;
		return true;
	}

	// Not getting anywhere: plan again with the neighbours as walls; after a few times without headway, give up.
	void AIDriver::Stall(const CharacterController& c, const NavGrid& nav, const CharacterWorld& world)
	{
		if (!nav.AllowPlan()) return;
		nav.NoteReplan();
		// held up by people (not by the terrain): wait for them to move on, planning around them, and only give up after a long time
		const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
		bool crowd = false;
		if (world.DynamicFrom < world.Obstacles.size())
			for (size_t k = world.DynamicFrom; k < world.Obstacles.size() && !crowd; ++k)
				crowd = glm::distance(world.Obstacles[k].Center, here) < world.Obstacles[k].Radius + c.Config.Radius + 0.9f;
		if (crowd && m_CrowdSeconds < CrowdPatience)
		{
			m_CrowdSeconds += kWindowSeconds;
			Plan(c, nav, world, true);
			return;
		}
		float nearestOther = 1e9f;
		if (world.DynamicFrom < world.Obstacles.size())
			for (size_t k = world.DynamicFrom; k < world.Obstacles.size(); ++k) nearestOther = std::min(nearestOther, glm::distance(world.Obstacles[k].Center, here));
		if (m_Replans + 1 > MaxReplans) { m_Stuck = true; { char b[160]; std::snprintf(b, sizeof b, "no headway after replanning, at (%.2f, %.2f), %.2f m from the goal, speed %.2f, blocked %d, waypoint %zu/%zu, nearest character %.2f m, crowd waited %.0f s", static_cast<double>(here.x), static_cast<double>(here.y), static_cast<double>(glm::distance(here, m_Goal)), static_cast<double>(c.GetSpeed()), c.WasBlocked() ? 1 : 0, m_Index, m_Path.size(), static_cast<double>(nearestOther), static_cast<double>(m_CrowdSeconds)); m_Why = b; } m_Path.clear(); m_Index = 0; return; }
		const int replans = m_Replans + 1;
		Plan(c, nav, world, true);
		m_Replans = replans;
	}

	CharacterInput AIDriver::Update(const CharacterController& c, const CharacterWorld& world, float dt)
	{
		const NavGrid* nav = m_Routed && world.Nav && world.Nav->Ready() ? world.Nav : nullptr;
		if (!nav) return Update(c, dt);
		if (m_Arrived || m_Stuck) return CharacterInput();

		const glm::vec2 here(c.GetPosition().x, c.GetPosition().z);
		// someone else stands on the goal (a camp spot, a node): next to them is as near as it gets
		bool occupied = false;
		if (glm::distance(here, m_Goal) < 1.6f && world.DynamicFrom < world.Obstacles.size())
			for (size_t k = world.DynamicFrom; k < world.Obstacles.size() && !occupied; ++k)
				occupied = glm::distance(world.Obstacles[k].Center, m_Goal) < world.Obstacles[k].Radius + c.Config.Radius + 0.1f;
		if (glm::distance(here, m_Goal) < ArriveRadius || (occupied && c.GetSpeed() < 0.5f && glm::distance(here, m_Goal) < 1.3f)) { m_Arrived = true; m_Path.clear(); m_Index = 0; m_NeedPlan = false; return CharacterInput(); }
		if (m_NeedPlan)
		{
			if (!nav->AllowPlan()) return CharacterInput();   // (a crowd sets off over a few frames)
			Plan(c, *nav, world, false);
			if (m_Stuck) return CharacterInput();
		}
		else if (nav->Revision() != m_PlanRevision)   // the world changed: plan again if it changed along the way
		{
			const glm::vec2 end = m_Path.empty() ? m_Goal : m_Path.back();
			if (nav->ChangedSince(m_PlanRevision, here, end, 3.0f))
			{
				if (!nav->AllowPlan()) return CharacterInput();
				const int replans = m_Replans;
				nav->NoteReplan();
				Plan(c, *nav, world, false);
				m_Replans = replans;
				if (m_Stuck) return CharacterInput();
			}
			else m_PlanRevision = nav->Revision();
		}

		// headway is judged over a window; the old "blocked and barely moving" rule triggers the same remedy
		// (a frame that took a long time, a model loading or the simulation sped up, is not a long time of standing still: the judging clock is capped)
		const float judged = std::min(dt, kMaxJudgedStep);
		m_WindowTime += judged;
		m_StillTime = c.WasBlocked() && c.GetSpeed() < 0.3f ? m_StillTime + judged : 0.0f;
		const bool stalled = m_StillTime > StuckSeconds || (m_WindowTime >= kWindowSeconds && glm::distance(here, m_WindowStart) < kMinProgress);
		if (stalled)
		{
			Stall(c, *nav, world);
			if (m_Stuck) return CharacterInput();
		}
		else if (m_WindowTime >= kWindowSeconds)
		{
			if (glm::distance(here, m_WindowStart) > 0.8f) { m_Replans = 0; m_CrowdSeconds = 0.0f; }
			m_WindowStart = here;
			m_WindowTime = 0.0f;
		}

		if (m_Index >= m_Path.size()) return CharacterInput();
		// waypoints reached on the way
		while (m_Index + 1 < m_Path.size() && glm::distance(here, m_Path[m_Index]) < kPlannedWaypointRadius) ++m_Index;
		const bool last = m_Index + 1 == m_Path.size();
		const glm::vec2 to = m_Path[m_Index] - here;
		const float dist = glm::length(to);
		if (last && dist < ArriveRadius)
		{
			if (m_Partial) { m_Stuck = true; m_Why = "the goal is cut off (nearest point reached)"; }   // the nearest it could get to a goal it cannot reach
			else m_Arrived = true;
			m_Path.clear();
			m_Index = 0;
			return CharacterInput();
		}
		const float top = Run ? RunSpeed : WalkSpeed;
		float speed = top;
		if (last && dist < SlowRadius) speed = std::max(top * dist / SlowRadius, 0.4f);

		glm::vec2 dir = to / std::max(dist, 1e-4f);
		if (world.DynamicFrom < world.Obstacles.size())
		{
			float scale = 1.0f;
			dir = Avoid(here, dir, c.Config.Radius, world.Obstacles.data() + world.DynamicFrom, world.Obstacles.size() - world.DynamicFrom, AvoidDistance, scale);
			speed = std::max(speed * scale, 0.4f * scale);
		}
		CharacterConfig config = c.Config;
		config.WalkSpeed = WalkSpeed;
		config.RunSpeed = RunSpeed;
		return InputFor(dir, speed, config);
	}
}
