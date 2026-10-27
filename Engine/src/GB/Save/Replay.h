#pragma once
#include "GB/Save/SaveWorld.h"

namespace GB::Save
{
	// A simulation that advances in fixed ticks and takes its orders as text commands: what a replay drives. It is also a SaveHost, so a replay
	// can start from any saved state.
	class ISimulation : public ISaveHost
	{
	public:
		virtual void Step() = 0;   // one fixed tick: advances the clock by Clock().Dt
		virtual bool Command(const std::string& line, std::string& message) = 0;
	};

	struct ReplayEvent
	{
		uint64_t Tick = 0;       // the tick the order was given at, before that tick is simulated
		std::string Command;
	};

	// The orders of a session plus the state it began in: playing them again from that state must end in the same state.
	struct Replay
	{
		int Format = 1;
		SaveGame Start;
		uint64_t Ticks = 0;
		std::vector<ReplayEvent> Events;
		Digest Final;
		bool HasFinal = false;
	};

	std::string ReplayToString(const Replay& replay);
	Result ParseReplay(const std::string& text, Replay& out);

	class ReplayRecorder
	{
	public:
		bool Active() const { return m_Active; }
		// Takes the current state as the start. Warnings (an ability in flight) go to `result`.
		Result Begin(const ISimulation& sim);
		void Record(uint64_t tick, const std::string& command);
		size_t EventCount() const { return m_Replay.Events.size(); }
		// Closes the recording with the state at the end as the one a replay must reach.
		Replay End(const ISimulation& sim);
		void Cancel() { m_Active = false; m_Replay = Replay(); }

	private:
		bool m_Active = false;
		uint64_t m_StartTick = 0;
		Replay m_Replay;
	};

	struct ReplayRun
	{
		Result Status;                          // could it run at all
		bool Matches = false;                   // reached the recorded final state (always false without one)
		Digest Actual;
		std::vector<std::string> Differing;     // parts of the state that differ from the recorded final one
		int FailedCommands = 0;
		uint64_t TicksRun = 0;
	};
	// Loads the start state into `sim`, plays every order at its tick and compares the end with the recorded one.
	ReplayRun PlayReplay(ISimulation& sim, const Replay& replay);

	// Splits a command line on spaces; "double quotes" keep words together.
	std::vector<std::string> SplitCommand(const std::string& line);
}
