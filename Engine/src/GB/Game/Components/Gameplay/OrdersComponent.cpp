#include "GB/Game/Components/Gameplay/OrdersComponent.h"
#include "GB/Game/Interaction.h"
#include "GB/Log.h"

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kViewTurnSpeed = 2.0f;   // rad/s of a script's camera turn (the third-person camera's own rotate speed)

		float Wrap(float a)
		{
			while (a > kPi) a -= 2.0f * kPi;
			while (a < -kPi) a += 2.0f * kPi;
			return a;
		}
	}

	void OrdersComponent::Clear(OrderSource keep)
	{
		if (keep != OrderSource::Tasks) m_Tasks.Clear();
		if (keep != OrderSource::Script) m_Script.clear();
		if (keep != OrderSource::AI) m_AI.Stop();
	}

	void OrdersComponent::Release()
	{
		Clear(OrderSource::Direct);
		m_Source = OrderSource::Direct;
	}

	bool OrdersComponent::RunTasks(const std::string& text, std::string& error)
	{
		std::vector<Task> tasks;
		if (!ParseTasks(text, tasks, &error)) return false;
		if (tasks.empty()) { error = "no tasks"; return false; }
		Clear(OrderSource::Tasks);
		m_FailLogged = false;
		m_Tasks.Set(std::move(tasks));
		m_Source = OrderSource::Tasks;
		return true;
	}

	void OrdersComponent::StopTasks()
	{
		m_Tasks.Clear();
		if (m_Source == OrderSource::Tasks) m_Source = OrderSource::Direct;
	}

	void OrdersComponent::SetScript(const std::string& text)
	{
		Clear(OrderSource::Script);
		m_Script = ParseCharacterScript(text);
		m_ScriptTime = 0.0f;
		m_Source = OrderSource::Script;
	}

	float OrdersComponent::ScriptLength() const
	{
		float total = 0.0f;
		for (const CharacterScriptStep& s : m_Script) total += s.Seconds;
		return total;
	}

	void OrdersComponent::MoveTo(const glm::vec2& xz, bool run, float walkSpeed, float runSpeed)
	{
		Clear(OrderSource::AI);
		m_Source = OrderSource::AI;
		m_AI.WalkSpeed = walkSpeed;
		m_AI.RunSpeed = runSpeed;
		m_AI.Run = run;
		m_AI.MoveTo(xz);
	}

	void OrdersComponent::StartReplay(std::vector<CharacterInput> frames)
	{
		Release();
		m_Replay = std::move(frames);
		m_ReplayIndex = 0;
		m_Source = OrderSource::Replay;
	}

	std::string OrdersComponent::SourceTag() const
	{
		switch (m_Source)
		{
		case OrderSource::AI: return m_AI.Arrived() ? " [AI arrived]" : m_AI.Stuck() ? " [AI stuck]" : " [AI walking]";
		case OrderSource::Tasks: return " [tasks]";
		case OrderSource::Script: return " [script]";
		case OrderSource::Replay: return " [replay]";
		case OrderSource::Direct: break;
		}
		return {};
	}

	CharacterInput OrdersComponent::Choose(const OrdersFrame& frame, float dt)
	{
		CharacterInput manual;
		manual.CameraYaw = m_ViewYaw;
		if (m_HasManual) manual = m_Manual;
		m_HasManual = false;
		// a person at the keys takes the body back from an AI walk or a task list (a chase, a trip to a node): the orders are dropped, the keys count
		if ((m_Source == OrderSource::AI || m_Source == OrderSource::Tasks) && frame.KeysTakeOver && (glm::length(manual.Move) > 0.1f || manual.Jump))
			Release();
		switch (m_Source)
		{
		case OrderSource::Script:
			if (!m_Script.empty())
			{
				const CharacterScriptStep* step = SampleCharacterScript(m_Script, m_ScriptTime);
				m_ScriptTime += dt;
				if (step)
				{
					m_ViewYaw = Wrap(m_ViewYaw + step->CameraTurn * kViewTurnSpeed * dt);
					CharacterInput in;
					in.Move = step->Move;
					in.Run = step->Run;
					in.Jump = step->Jump;
					in.CameraYaw = m_ViewYaw;
					return in;
				}
			}
			break;
		case OrderSource::Tasks:
			if (m_Tasks.Running())
			{
				const TaskRunner::Resolver none = [](const std::string&, const std::string&, InteractionTarget&, std::string*) { return false; };
				m_Tasks.Profile = frame.Navigation;
				const CharacterInput in = m_Tasks.Update(frame.Controller, frame.World, frame.Anim, dt, frame.Interactions ? frame.Interactions : none, m_PropHandler, m_AbilityHandler);
				if (m_Tasks.Failed() && !m_FailLogged) { m_FailLogged = true; GB_CLIENT_WARN("{}: orders failed ({})", frame.Name, m_Tasks.Describe()); }
				return in;
			}
			break;
		case OrderSource::AI:
			if (!m_AI.Arrived() && !m_AI.Stuck()) { m_AI.Profile = frame.Navigation; return m_AI.Update(frame.Controller, frame.World, dt); }
			break;
		case OrderSource::Replay:
			if (m_ReplayIndex < m_Replay.size()) return m_Replay[m_ReplayIndex++];
			break;
		case OrderSource::Direct:
			break;
		}
		return manual;
	}
}
