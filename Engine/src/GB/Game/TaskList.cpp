#include "GB/Game/TaskList.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace GB
{
	void TaskRunner::Set(std::vector<Task> tasks)
	{
		Clear();
		m_Tasks = std::move(tasks);
	}

	void TaskRunner::Clear()
	{
		m_Tasks.clear();
		m_Index = 0;
		m_Started = m_Failed = false;
		m_Error.clear();
		m_AI.Stop();
		m_Interaction.Cancel();
	}

	void TaskRunner::Fail(const std::string& why)
	{
		m_Failed = true;
		m_Error = why;
		m_AI.Stop();
		m_Interaction.Cancel();
	}

	std::string TaskRunner::Describe() const
	{
		if (m_Failed) return "failed: " + m_Error;
		if (m_Index >= m_Tasks.size()) return m_Tasks.empty() ? "no tasks" : "done";
		std::string s = std::to_string(m_Index + 1) + "/" + std::to_string(m_Tasks.size()) + " " + DescribeTask(m_Tasks[m_Index]);
		if (m_Tasks[m_Index].What == Task::Kind::Interact)
		{
			static const char* names[] = { "idle", "approach", "align", "act", "done", "failed" };
			s += std::string(" (") + names[static_cast<int>(m_Interaction.GetPhase())] + ")";
		}
		return s;
	}

	CharacterInput TaskRunner::Update(CharacterController& controller, const CharacterWorld& world, AnimInstance* anim, float dt,
		const Resolver& resolver, const PropHandler& props, const AbilityHandler& abilities)
	{
		CharacterInput none;
		// instantaneous tasks run back to back in the same frame
		while (!Done())
		{
			const Task& t = m_Tasks[m_Index];
			if (t.What == Task::Kind::Attach || t.What == Task::Kind::Detach)
			{
				if (!props || !props(t.Prop, t.What == Task::Kind::Attach)) { Fail("could not " + DescribeTask(t)); return none; }
				Next();
				continue;
			}
			break;
		}
		if (Done()) return none;

		const Task& t = m_Tasks[m_Index];
		switch (t.What)
		{
		case Task::Kind::Wait:
			if (!m_Started) { m_Started = true; m_Time = 0.0f; }
			m_Time += dt;
			if (m_Time >= t.Seconds) Next();
			return none;
		case Task::Kind::GoTo:
			if (!m_Started)
			{
				m_Started = true;
				m_AI.Run = t.Run;
				m_AI.MoveTo(t.Position);
			}
			m_AI.WalkSpeed = controller.Config.WalkSpeed;
			m_AI.RunSpeed = controller.Config.RunSpeed;
			if (m_AI.Arrived()) { Next(); return none; }
			if (m_AI.Stuck()) { Fail("could not reach (" + std::to_string(t.Position.x) + ", " + std::to_string(t.Position.y) + "): " + m_AI.StuckReason()); return none; }
			m_AI.Profile = Profile;
			return m_AI.Update(controller, world, dt);
		case Task::Kind::Interact:
		{
			if (!m_Started)
			{
				m_Started = true;
				InteractionTarget target;
				std::string error;
				if (!resolver || !resolver(t.Entity, t.Point, target, &error)) { Fail(error.empty() ? "no way to find interaction points" : error); return none; }
				if (!m_Interaction.Start(target, anim, false)) { Fail(m_Interaction.Error()); return none; }
			}
			m_Interaction.Profile = Profile;
			const CharacterInput in = m_Interaction.Update(controller, world, anim, dt);
			for (const std::string& e : m_Interaction.Events())
				if (e.rfind("gives:", 0) == 0 && props) props(e.substr(6), true);
			if (m_Interaction.GetPhase() == InteractionRunner::Phase::Done) Next();
			else if (m_Interaction.GetPhase() == InteractionRunner::Phase::Failed) Fail(m_Interaction.Error());
			return in;
		}
		case Task::Kind::Ability:
		{
			std::string error;
			if (!m_Started)
			{
				m_Started = true;
				if (!abilities.Start || !abilities.Start(t.Prop, t.Entity, error))
				{
					// only its cooldown stands in the way (a new trip that starts with the ability it ended with): it waits, it does not fail. A
					// failure here made the tribe give the same orders again every frame until the cooldown was over (found by editor_play: a dozen
					// failed trips in a quarter of a second, and the "stuck" invariant)
					if (error.rfind("cooling down", 0) == 0) { m_Started = false; return none; }
					Fail(error.empty() ? "no abilities here" : error);
					return none;
				}
			}
			const int state = abilities.Poll ? abilities.Poll(error) : 1;
			if (state < 0) Fail(error.empty() ? "the ability failed" : error);
			else if (state > 0) Next();
			return none;   // the ability moves the character
		}
		default:
			Next();
			return none;
		}
	}
}
