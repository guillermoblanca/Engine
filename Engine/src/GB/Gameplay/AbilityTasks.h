#pragma once
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Game/Interaction.h"
#include <string>

namespace GB
{
	// What an ability context gives a task for one frame.
	struct AbilityTaskContext
	{
		AbilitySystemComponent& Owner;
		AbilityInstance& Ability;
		AbilityRuntime* Runtime;   // null when the ability runs on timers
		CharacterInput& Input;     // what the task wants the character to do this frame
	};

	// A step of an ability that takes time. An ability runs its tasks one after another; a task that fails ends the ability.
	class AbilityTask
	{
	public:
		enum class State { Running, Succeeded, Failed };
		virtual ~AbilityTask() = default;
		virtual const char* Name() const = 0;
		virtual void Begin(AbilityTaskContext& c) = 0;
		virtual void Tick(AbilityTaskContext& c, float dt) = 0;
		// The ability was cancelled while this task ran: stop what it started.
		virtual void Cancel(AbilityTaskContext&) {}
		State GetState() const { return m_State; }
		const std::string& Error() const { return m_Error; }

	protected:
		void Succeed() { m_State = State::Succeeded; }
		void Fail(const std::string& why) { m_State = State::Failed; m_Error = why; }

	private:
		State m_State = State::Running;
		std::string m_Error;
	};

	// Walks the character to the target's interaction point and settles on it with a motion warp: the InteractionRunner, without an action of
	// its own (the montage is the next task). Without a runtime there is nowhere to walk: it is done at once.
	class MoveToTargetTask : public AbilityTask
	{
	public:
		const char* Name() const override { return "MoveToTarget"; }
		void Begin(AbilityTaskContext& c) override;
		void Tick(AbilityTaskContext& c, float dt) override;
		void Cancel(AbilityTaskContext&) override { m_Runner.Cancel(); }

	private:
		InteractionRunner m_Runner;
	};

	// Plays the ability's montage and waits for it to end. Its notifies make the ability's events happen (FireNotify); if it ends well the
	// ones that did not happen yet do. Interrupted = failed. Without an animation it lasts the ability's Duration and the notifies come on
	// the clock, at the same fractions.
	class PlayMontageAndWaitTask : public AbilityTask
	{
	public:
		const char* Name() const override { return "PlayMontageAndWait"; }
		void Begin(AbilityTaskContext& c) override;
		void Tick(AbilityTaskContext& c, float dt) override;
		void Cancel(AbilityTaskContext& c) override;

	private:
		bool m_Real = false;
		float m_Time = 0.0f;
		float m_Rate = 1.0f;   // playback speed of this use (the tool's)
		std::string m_Montage;
	};

	// Waits for a gameplay event (at or below `tag`) to reach the owner; fails after `timeout` seconds.
	class WaitGameplayEventTask : public AbilityTask
	{
	public:
		WaitGameplayEventTask(std::string tag, float timeout) : m_Tag(std::move(tag)), m_Timeout(timeout) {}
		const char* Name() const override { return "WaitGameplayEvent"; }
		void Begin(AbilityTaskContext& c) override;
		void Tick(AbilityTaskContext& c, float dt) override;

	private:
		bool Arrived(const AbilityTaskContext& c) const;
		std::string m_Tag;
		float m_Timeout, m_Time = 0.0f;
	};
}
