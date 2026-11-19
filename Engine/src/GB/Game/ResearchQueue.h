#pragma once
// T324 (documentation/01_diseno/ARBOL_TECNICO.md §11.2.4 and §11.7.6): the player's research queue. An ordered list of technologies the
// player wants the clan to study (`game research add|remove|clear|top <tech>`). The clan's next steps (Tribe::UpdateGoals, which the
// experiments and the gathers of their materials read: ClanKnowledge::NextExperiment, Tribe::GoalInputs) start with the entries of the
// queue it can research now, in the queue's order: not known by the clan, every technology it requires known by the clan, found by trying
// (discovery.experiment > 0) and the world letting it (ClanKnowledge::Physical). So whoever works on "knowledge" (WorkPriorities, T306) tries
// the first of them it can; one the clan cannot research yet waits in its place, and a learnt one leaves the queue at the next plan.
// Discovery by luck (any experiment, watching, lessons) goes on as before. With an empty queue nothing here runs and nothing is saved
// ("research"), so the clans of the AI and GBSim are the same as before.
//
// The tribe owns one (Tribe::Research()) and calls it from one line in Tribe::UpdateGoals (Settlement.cpp). CPU only, deterministic.
#include <functional>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;

	class ResearchQueue
	{
	public:
		static constexpr size_t kMost = 32;   // the longest queue
		struct Reply
		{
			bool Ok = false;
			std::string Text;   // English (the commands' answers and the tests read it)
		};

		// Refused: a technology the tree has not, one the clan knows, one already queued (Add), a full queue. Each change sets the clan's next
		// steps again at once (Tribe::UpdateGoals).
		Reply Add(Tribe& tribe, const std::string& tech);
		Reply Remove(Tribe& tribe, const std::string& tech);
		Reply Clear(Tribe& tribe);
		// To the head of the queue (added there when it was not in it).
		Reply Top(Tribe& tribe, const std::string& tech);
		// T339 (K-c): to place `place` of the queue (1-based, clamped to the queue), for the drag of the Knowledge tab (`game research move <tech> <n>`).
		Reply Move(Tribe& tribe, const std::string& tech, int place);
		const std::vector<std::string>& All() const { return m_Queue; }

		// The clan can research it now (see above).
		static bool Researchable(const Tribe& tribe, const std::string& tech);
		// The first entry it can research now ("" = none: the clan chooses as before).
		std::string Current(const Tribe& tribe) const;
		// What the clan can research now and is not queued, in id order (the UI's list to add from), and how many unknown technologies are not
		// shown because it cannot yet (ARBOL_TECNICO §11.2.5: never by name).
		static std::vector<std::string> Open(const Tribe& tribe, const std::vector<std::string>& except = {});
		// T338 (D6): `shown` keeps only the technologies the views draw (TechStatus::ShownFilter: those with a tier); empty = all of them.
		static int Hidden(const Tribe& tribe, const std::function<bool(const std::string&)>& shown = {});
		// "research queue: 1. pottery 34% researching; 2. weaving waits for spinning | can research: ...; ? 12 more" (`game research`).
		std::string List(const Tribe& tribe, const std::function<bool(const std::string&)>& shown = {}) const;
		// What an entry is doing: "researching", "next", "waits for <tech>", "cannot be found by trying", "the world does not let it", "needs tier <n>" (T323), "known".
		static std::string Status(const Tribe& tribe, const std::string& tech, bool current);

		// ---- the tribe's hook
		// Tribe::UpdateGoals: drops the entries the clan knows (and ids the tree has not) and puts the researchable ones first, in order.
		void Lead(Tribe& tribe, std::vector<std::string>& goals);

		bool Empty() const { return m_Queue.empty(); }
		void Reset() { m_Queue.clear(); }

		// Saving (the tribe's "research" section, only written with a queue).
		std::string ToJson() const;
		bool FromJson(const std::string& json);   // false (and nothing changed) on a mistake

	private:
		static std::string Name(const Tribe& tribe, const std::string& tech);
		std::vector<std::string> m_Queue;   // technology ids, the first first
	};
}
