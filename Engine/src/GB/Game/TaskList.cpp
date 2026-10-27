#include "GB/Game/TaskList.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace GB
{
	namespace
	{
		std::vector<std::string> Words(const std::string& line)
		{
			std::vector<std::string> w;
			std::istringstream in(line);
			std::string word;
			while (in >> word) w.push_back(word);
			return w;
		}
		bool IsNumber(const std::string& s)
		{
			char* end = nullptr;
			std::strtod(s.c_str(), &end);
			return !s.empty() && end && *end == '\0';
		}
		float Number(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
	}

	bool ParseTasks(const std::string& text, std::vector<Task>& out, std::string* error)
	{
		out.clear();
		std::string normalized = text;
		for (char& c : normalized) if (c == ';' || c == '|') c = '\n';
		std::istringstream in(normalized);
		std::string line;
		int lineNumber = 0;
		auto fail = [&](const std::string& why) { if (error) *error = "task " + std::to_string(out.size() + 1) + ": " + why; return false; };
		while (std::getline(in, line))
		{
			++lineNumber;
			const size_t hash = line.find('#');
			if (hash != std::string::npos) line = line.substr(0, hash);
			const std::vector<std::string> w = Words(line);
			if (w.empty()) continue;
			Task t;
			if (w[0] == "goto")
			{
				if (w.size() < 3 || !IsNumber(w[1]) || !IsNumber(w[2])) return fail("goto <x> <z> [run]");
				t.What = Task::Kind::GoTo;
				t.Position = { Number(w[1]), Number(w[2]) };
				t.Run = w.size() > 3 && w[3] == "run";
			}
			else if (w[0] == "wait")
			{
				if (w.size() < 2 || !IsNumber(w[1])) return fail("wait <seconds>");
				t.What = Task::Kind::Wait;
				t.Seconds = Number(w[1]);
			}
			else if (w[0] == "interact")
			{
				if (w.size() < 2) return fail("interact <entity> [point]   (entity names with spaces: use '_' instead)");
				t.What = Task::Kind::Interact;
				t.Entity = w[1];
				for (char& c : t.Entity) if (c == '_') c = ' ';
				t.Point = w.size() > 2 ? w[2] : "";
			}
			else if (w[0] == "attach")
			{
				if (w.size() < 2) return fail("attach <prop>");
				t.What = Task::Kind::Attach;
				t.Prop = w[1];
			}
			else if (w[0] == "detach")
			{
				t.What = Task::Kind::Detach;
				t.Prop = w.size() > 1 ? w[1] : "";
			}
			else if (w[0] == "ability")
			{
				if (w.size() < 2) return fail("ability <name> [entity]   (entity names with spaces: use '_' instead)");
				t.What = Task::Kind::Ability;
				t.Prop = w[1];
				t.Entity = w.size() > 2 ? w[2] : "";
				for (char& c : t.Entity) if (c == '_') c = ' ';
			}
			else
				return fail("unknown task '" + w[0] + "' (goto, interact, wait, attach, detach, ability)");
			out.push_back(std::move(t));
		}
		return true;
	}

	std::string DescribeTask(const Task& t)
	{
		char b[160];
		switch (t.What)
		{
		case Task::Kind::GoTo: std::snprintf(b, sizeof b, "goto %.1f %.1f%s", static_cast<double>(t.Position.x), static_cast<double>(t.Position.y), t.Run ? " run" : ""); return b;
		case Task::Kind::Wait: std::snprintf(b, sizeof b, "wait %.1f", static_cast<double>(t.Seconds)); return b;
		case Task::Kind::Interact: return "interact " + t.Entity + (t.Point.empty() ? "" : "/" + t.Point);
		case Task::Kind::Attach: return "attach " + t.Prop;
		case Task::Kind::Detach: return "detach" + (t.Prop.empty() ? "" : " " + t.Prop);
		case Task::Kind::Ability: return "ability " + t.Prop + (t.Entity.empty() ? "" : " " + t.Entity);
		}
		return "";
	}

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
			if (m_AI.Stuck()) { Fail("could not reach (" + std::to_string(t.Position.x) + ", " + std::to_string(t.Position.y) + ")"); return none; }
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
				if (!abilities.Start || !abilities.Start(t.Prop, t.Entity, error)) { Fail(error.empty() ? "no abilities here" : error); return none; }
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
