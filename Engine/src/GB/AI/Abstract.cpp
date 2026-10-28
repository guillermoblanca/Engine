#include "GB/AI/Abstract.h"
#include <algorithm>

namespace GB
{
	bool AbstractAgent::Do(const std::string& text, std::string& error)
	{
		std::vector<Task> tasks;
		if (!ParseTasks(text, tasks, &error)) return false;
		m_Tasks = std::move(tasks);
		m_Index = 0;
		m_Phase = Phase::Start;
		m_Failed = false;
		m_Error.clear();
		return true;
	}

	void AbstractAgent::Update(float dt, AbilitySystemComponent* who, GameplayWorld& gameplay, const Locate& locate, float speed)
	{
		if (!Busy()) return;
		const Task& t = m_Tasks[m_Index];
		switch (t.What)
		{
		case Task::Kind::GoTo:
		{
			const glm::vec2 d = t.Position - m_Pos;
			const float step = speed * (t.Run ? 2.0f : 1.0f) * dt;
			if (glm::length(d) <= step) { m_Pos = t.Position; Next(); }
			else m_Pos += glm::normalize(d) * step;
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
				m_Phase = Phase::Walk;
				if (!t.Entity.empty() && !locate(t.Entity, m_Goal)) { Fail("the target '" + t.Entity + "' is gone"); return; }
				if (t.Entity.empty()) m_Goal = m_Pos;
			}
			if (m_Phase == Phase::Walk)
			{
				const glm::vec2 d = m_Goal - m_Pos;
				const float reach = t.Entity.empty() ? 0.0f : 1.6f;   // an actor stands at the work point of the target
				const float step = speed * dt;
				if (glm::length(d) - reach > step) { m_Pos += glm::normalize(d) * step; return; }
				if (glm::length(d) > reach) m_Pos = m_Goal - glm::normalize(d) * reach;
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
