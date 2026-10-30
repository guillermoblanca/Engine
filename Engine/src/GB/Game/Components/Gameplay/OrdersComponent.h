#pragma once
#include "GB/Animation/AnimInstance.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/TaskList.h"
#include "GB/Navigation/NavGrid.h"
#include <string>
#include <vector>

namespace GB
{
	// Who gives an actor its orders. The same actor can change source at any time (a player that an AI walks somewhere, an NPC that a
	// person takes over): the controller, the animation and the abilities do not know the difference.
	enum class OrderSource
	{
		Direct,   // the input the host pushes with SetInput (keyboard, pad); none this frame = standing still
		AI,       // AIDriver walks it to a point (MoveTo)
		Tasks,    // a task list (RunTasks): go to, interact, wait, ability...
		Script,   // a timeline of keys (SetScript)
		Replay,   // a recording of the inputs of an earlier run (StartReplay)
	};

	// What the orders need of the actor in the frame they are read: its body, the animation the tasks may drive (null without one), and the
	// two settings that belong to the actor.
	struct OrdersFrame
	{
		CharacterController& Controller;
		CharacterWorld& World;
		AnimInstance* Anim = nullptr;
		const TaskRunner::Resolver& Interactions;
		bool KeysTakeOver = false;   // a key pressed drops an AI walk or a task list and the host's input drives
		const NavProfile& Navigation;
		const std::string& Name;
	};

	// The orders of an actor: the host's input, an AI walk, a task list, a script, a replay, and the recording of what was given. It chooses the
	// input of each frame and nothing else: it moves nothing and knows no model.
	class OrdersComponent : public GBObjectComponent
	{
	public:
		// The task list asks the actor to put a prop in the hand and to run an ability: the actor says how.
		void SetTaskHandlers(TaskRunner::PropHandler props, TaskRunner::AbilityHandler abilities) { m_PropHandler = std::move(props); m_AbilityHandler = std::move(abilities); }

		// The input of this frame from whoever gives the orders. A source with nothing left to say (a script that ended, tasks done, an AI that
		// arrived or is stuck) leaves the actor to the host's input.
		CharacterInput Choose(const OrdersFrame& frame, float dt);

		OrderSource Source() const { return m_Source; }
		void Release();                                         // back to Direct: the host's input again
		void SetInput(const CharacterInput& input) { m_Manual = input; m_HasManual = true; }
		void ForgetInput() { m_HasManual = false; }
		void SetScript(const std::string& text);                // see ParseCharacterScript; time starts now
		bool ScriptRunning() const { return m_Source == OrderSource::Script && !m_Script.empty() && m_ScriptTime < ScriptLength(); }
		float ViewYaw() const { return m_ViewYaw; }             // what "forward" is relative to for Direct and Script input; scripts turn it
		void SetViewYaw(float yaw) { m_ViewYaw = yaw; }
		void MoveTo(const glm::vec2& xz, bool run, float walkSpeed, float runSpeed);
		bool AIArrived() const { return m_Source == OrderSource::AI && m_AI.Arrived(); }
		bool AIStuck() const { return m_Source == OrderSource::AI && m_AI.Stuck(); }
		// False (and `error`) when the text is not a task list.
		bool RunTasks(const std::string& text, std::string& error);
		void StopTasks();
		bool TasksRunning() const { return m_Source == OrderSource::Tasks && m_Tasks.Running(); }
		bool TasksFailed() const { return m_Tasks.Failed(); }
		size_t TaskCount() const { return m_Tasks.Count(); }
		std::string TaskStatus() const { return m_Tasks.Describe(); }
		// Recording and replay: the input given to the controller each frame (after abilities) is kept, and a recording plays back exactly.
		void Record(bool on) { m_Recording = on; if (on) m_Recorded.clear(); }
		void Note(const CharacterInput& in) { if (m_Recording) m_Recorded.push_back(in); }
		const std::vector<CharacterInput>& Recorded() const { return m_Recorded; }
		void StartReplay(std::vector<CharacterInput> frames);
		bool ReplayFinished() const { return m_Source == OrderSource::Replay && m_ReplayIndex >= m_Replay.size(); }
		// " [AI walking]", " [tasks]"...: how the source reads in a description ("" for Direct).
		std::string SourceTag() const;

		void EndPlay(GBObject&) override { Release(); }

	private:
		void Clear(OrderSource keep);   // drops every source but `keep` (whose own state the caller sets next)
		float ScriptLength() const;

		OrderSource m_Source = OrderSource::Direct;
		TaskRunner m_Tasks;
		AIDriver m_AI;
		std::vector<CharacterScriptStep> m_Script;
		float m_ScriptTime = 0.0f;
		float m_ViewYaw = 3.14159265f;
		CharacterInput m_Manual;
		bool m_HasManual = false;
		bool m_Recording = false;
		std::vector<CharacterInput> m_Recorded, m_Replay;
		size_t m_ReplayIndex = 0;
		bool m_FailLogged = false;   // the failure of the current orders has been logged
		TaskRunner::PropHandler m_PropHandler;
		TaskRunner::AbilityHandler m_AbilityHandler;
	};
}
