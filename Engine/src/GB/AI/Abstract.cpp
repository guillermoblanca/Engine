#include "GB/AI/Abstract.h"
#include <algorithm>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kReach = 1.6f;   // an actor works a target from about this far (its work point)
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
		m_Safe = m_Pos;
		if (route && glm::distance(m_Pos, to) > reach + 1e-3f)
		{
			if (!route(m_Pos, to, reach, m_Path) || m_Path.empty()) { m_Path.clear(); return false; }
		}
		else m_Path.push_back(to);
		m_EndReach = m_Path.back() == to ? reach : 0.0f;
		return true;
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
				if (len > r) m_Pos = target - d / len * r;
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
				if (!who || !who->Activate(t.Prop, t.Entity.empty() ? nullptr : gameplay.Find(t.Entity), &why, t.Entity)) { Fail(why.empty() ? "the ability could not start" : why); return; }
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
