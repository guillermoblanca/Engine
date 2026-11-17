#include "GB/AI/Abstract.h"
#include <algorithm>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kReach = 1.6f;   // an actor works a target from about this far (its work point)
		constexpr float kStepBack = 3.0f;   // the longest hop back to the last ground of a route, out of a pocket (Plan)
	}

	bool AbstractAgent::Do(const std::string& text, std::string& error)
	{
		std::vector<Task> tasks;
		if (!ParseTasks(text, tasks, &error)) return false;
		m_Tasks = std::move(tasks);
		m_Index = 0;
		m_Phase = Phase::Start;
		m_Path.clear();
		m_Failed = false;
		m_Error.clear();
		return true;
	}

	// Asks the owner for the way; without a route (or a goal where it already stands) the way is the straight line, as it always was.
	bool AbstractAgent::Plan(const glm::vec2& to, float reach, const Route& route)
	{
		m_Path.clear();
		m_Next = 0;
		m_From = m_Pos;
		m_Routed = false;
		if (!route) m_Safe = m_Pos;
		if (route && glm::distance(m_Pos, to) > reach + 1e-3f)
		{
			bool found = route(m_Pos, to, reach, m_Path) && !m_Path.empty();
			if (found) m_Safe = m_Pos;   // (there is a way out of here: ground of a route)
			else if (glm::distance(m_Safe, m_Pos) > 1e-3f && glm::distance(m_Safe, m_Pos) <= kStepBack)
			{
				// Shut in a pocket of the grid (pushed there, or the world changed round it): no way out from where it stands, and it would fail every
				// trip until it died of thirst (GB.exe, "From nothing", seed 7: four of a clan by a reed bed). It hops back to the last ground of a route
				// it stood on, a short step, and goes from there.
				m_Path.clear();
				found = route(m_Safe, to, reach, m_Path) && !m_Path.empty();
				if (found) { m_Path.insert(m_Path.begin(), m_Safe); m_From = m_Safe; }
			}
			if (!found) { m_Path.clear(); return false; }
			m_Routed = true;
			// A route that ends short of the goal, but within its reach (the nearest ground to a node that is itself an obstacle), used to leave the
			// member exactly where the route ended -- which can be inside the node's own obstacle circle, nearer than the work point. Ending the walk
			// at the goal, with its reach, stands it at the work point instead.
			if (reach > 0.0f && m_Path.back() != to && glm::distance(m_Path.back(), to) <= reach + 1e-3f) m_Path.push_back(to);
		}
		else m_Path.push_back(to);
		m_EndReach = m_Path.back() == to ? reach : 0.0f;
		return true;
	}

	// Back along the route walked (from where it stands to the waypoints passed, the last first, and where the route began) to the first point r
	// from `target`: ground of the route all of it. A route that never went r away from the target gives its start.
	glm::vec2 AbstractAgent::BackAlongRoute(const glm::vec2& target, float r) const
	{
		glm::vec2 at = m_Pos;
		for (size_t k = m_Next + 1; k-- > 0;)
		{
			const glm::vec2 next = k > 0 ? m_Path[k - 1] : m_From;
			if (glm::distance(next, target) >= r)
			{
				glm::vec2 near = at, far = next;   // the distance to the target grows from near to far past r: halve the segment
				for (int i = 0; i < 24; ++i)
				{
					const glm::vec2 mid = (near + far) * 0.5f;
					if (glm::distance(mid, target) < r) near = mid; else far = mid;
				}
				return far;
			}
			at = next;
		}
		return at;
	}

	// One step along the waypoints. The last one is reached at `m_EndReach` from it (the work point of a target), the others are passed through
	// (and remembered as safe ground). With a single waypoint this is exactly the old straight walk.
	bool AbstractAgent::Walk(float step)
	{
		while (m_Next < m_Path.size())
		{
			const bool last = m_Next + 1 == m_Path.size();
			const glm::vec2 target = m_Path[m_Next];
			const float r = last ? m_EndReach : 0.0f;
			const glm::vec2 d = target - m_Pos;
			const float len = glm::length(d);
			if (len - r > step) { m_Pos += d / len * step; return false; }
			if (last)
			{
				// r is 0 for a plain GoTo (Update's GoTo case always plans with reach 0: arrive exactly at the point) and the work reach for every
				// Ability-with-an-entity task (the work point of a tree, a rock...). A member that arrives at this leg already closer than r (the task
				// started near its target, or a routed hop landed there) used to stay where it was, which can be inside the target's own small obstacle
				// circle (the `ground` invariant of tests/settlement_test.cpp caught it once erosion moved a gather target for one seed): with a reach
				// it now always ends exactly r from the target, the work point. A GoTo (r == 0) is untouched: it is how the camp moves (MoveCamp), and
				// there arriving exactly at the point is the whole job.
				// After a route, a member already closer than r goes back along the way it came until it is r away: pushed straight out it went
				// through a thin wall of the grid into a pocket with no way out (GB.exe "From nothing", seed 7: the reed bed 16 of Ash, four dead of
				// thirst). Only with no route walked (the task started within reach) is it pushed straight out, as before.
				if (r > 0.0f && len <= r && m_Routed) m_Pos = BackAlongRoute(target, r);
				else if (r > 0.0f) { if (len > 1e-4f) m_Pos = target - d / len * r; }
				else if (len > r) m_Pos = target - d / len * r;
				m_Safe = r > 0.0f ? m_Safe : m_Pos;
				return true;
			}
			m_Pos = m_Safe = target;
			step -= len;
			++m_Next;
		}
		return true;
	}

	void AbstractAgent::Update(float dt, AbilitySystemComponent* who, GameplayWorld& gameplay, const Locate& locate, float speed, const Route& route, const Gate& mayPlan)
	{
		if (!Busy()) return;
		const Task& t = m_Tasks[m_Index];
		// a route is a search of the grid: it waits its turn in the plan budget (the straight line and a goal where it stands do not)
		auto turn = [&](const glm::vec2& to, float reach) { return !route || !mayPlan || glm::distance(m_Pos, to) <= reach + 1e-3f || mayPlan(); };
		switch (t.What)
		{
		case Task::Kind::GoTo:
		{
			if (m_Phase == Phase::Start)
			{
				if (!turn(t.Position, 0.0f)) return;
				if (!Plan(t.Position, 0.0f, route))
				{
					char where[64];
					std::snprintf(where, sizeof where, "no way to %.1f %.1f", static_cast<double>(t.Position.x), static_cast<double>(t.Position.y));
					Fail(where);
					return;
				}
				m_Phase = Phase::Walk;
			}
			if (Walk(speed * (t.Run ? 2.0f : 1.0f) * dt)) Next();
			return;
		}
		case Task::Kind::Wait:
			if (m_Phase == Phase::Start) { m_Timer = t.Seconds; m_Phase = Phase::Wait; }
			m_Timer -= dt;
			if (m_Timer <= 0.0f) Next();
			return;
		case Task::Kind::Ability:
		{
			if (m_Phase == Phase::Start)
			{
				if (!t.Entity.empty() && !locate(t.Entity, m_Goal)) { Fail("the target '" + t.Entity + "' is gone"); return; }
				if (t.Entity.empty()) m_Goal = m_Pos;
				if (!turn(m_Goal, t.Entity.empty() ? 0.0f : kReach)) return;   // (located again next frame: it may have moved)
				m_Phase = Phase::Walk;
				if (!Plan(m_Goal, t.Entity.empty() ? 0.0f : kReach, route)) { Fail("cannot get next to '" + t.Entity + "'"); return; }
			}
			if (m_Phase == Phase::Walk)
			{
				if (!Walk(speed * dt)) return;   // an actor stands at the work point of the target
				std::string why;
				if (!who || !who->Activate(t.Prop, t.Entity.empty() ? nullptr : gameplay.Find(t.Entity), &why, t.Entity))
				{
					if (why.rfind("cooling down", 0) == 0) return;   // only its cooldown: it waits there, as the actor's task runner does
					Fail(why.empty() ? "the ability could not start" : why);
					return;
				}
				m_Phase = Phase::Act;
				return;
			}
			if (who && who->AnyActive()) return;
			if (who && !who->LastError().empty()) { Fail(who->LastError()); return; }
			Next();
			return;
		}
		default:
			Next();
			return;
		}
	}
}
