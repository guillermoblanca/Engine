#pragma once
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Techs.h"
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	// Knowledge per agent and per clan (H11, E4 + E5): who knows which technology (GB/Gameplay/Techs.h), how it is found and how it spreads.
	//
	// - Each AGENT knows some technologies (Known), is half-way through understanding others (Progress, 0..1: at 1 it knows it) and remembers how many
	//   times it tried each one (Tries). It joins knowing the technologies of the start (TechDef::Known), so with era-1 content it makes what it made
	//   before technologies existed.
	// - The CLAN knows what its members have told it at the camp (Share, like the clan's spatial memory) and loses a technology when nobody alive knows it.
	// - Discovery, all three deterministic (a hash of the seed, the names and the counters; no global random number generator):
	//   EXPERIMENTING: an agent with the materials of a recipe of a technology it does not know (in the store or its bag) and that knows what that
	//     technology requires tries it; each try succeeds with the chance Discovery.Experiment, which grows with each failure (Rules.Growth), takes
	//     Discovery.Seconds and may spoil one unit of material (Discovery.Waste).
	//   OBSERVING: whoever is within Rules.ObserveRadius of someone who uses a technology (makes one of its recipes, works one of its nodes) gains
	//     Discovery.Observe of understanding.
	//   TEACHING: every Rules.LessonSeconds the camp holds a lesson: each idle learner there gains Discovery.Teach of the first technology (by id) that
	//     someone else at the camp knows and it can learn.
	// Without technologies (SetTechs(nullptr), the default) everything is known and nothing is gated: the code that does not care keeps working.

	struct AgentKnowledge
	{
		std::set<std::string> Known;                  // technology ids
		std::map<std::string, float> Progress;        // understanding of what it is learning (watching, being taught), 0..1
		std::map<std::string, int> Tries;             // experiments made with each technology it has not found yet
		double NextTry = 0.0;                         // it does not experiment again before this time (the tribe's clock)
		std::vector<std::string> Other;               // names a save gave it that are not technologies ("place.camp"): kept as they came
		bool operator==(const AgentKnowledge&) const = default;
	};

	// What the knowledge needs to know of an agent at a moment (the tribe fills it from its members).
	struct KnowledgeAgent
	{
		std::string Name;
		glm::vec2 Pos{ 0.0f };
		bool Alive = true;
		bool AtCamp = false;   // close enough to the camp to teach and to learn there
		bool Idle = false;     // free to attend a lesson (resting, warming up, chatting...)
		// H9 + H10 (GB/Game/Society.h): how fast it learns (a child learns faster) and how well it teaches (an elder better) in a lesson, and whom it
		// would rather learn from (its parents, then those it likes best): the first of them at the camp who can teach it is its teacher.
		float Learn = 1.0f, Teach = 1.0f;
		std::vector<std::string> Mentors;
	};

	// Something worth writing in the log: who ("" = the clan) and what.
	struct KnowledgeEvent
	{
		std::string Who;
		std::string Text;
		std::string Other;   // the teacher of a lesson ("" otherwise): the tribe makes them closer (H10)
	};

	class ClanKnowledge
	{
	public:
		// The technologies (they must outlive this); nullptr = nothing is gated. Agents that joined before keep what they know and learn those of the start.
		void SetTechs(const TechTree* techs);
		const TechTree* Techs() const { return m_Techs; }
		bool Gated() const { return m_Techs != nullptr; }
		// Forgets every agent and what the clan knows (the clan starts again with the technologies of the start).
		void Clear();

		// An agent joins knowing the technologies of the start (nothing happens if it is there already); Leave forgets it.
		void Join(const std::string& name);
		void Leave(const std::string& name);
		// H13: an agent from another clan joins with what it knew there (and the technologies of the start). It replaces what this clan had of the name.
		void Admit(const std::string& name, const AgentKnowledge& knows);
		// H13: someone of another clan shows `learner` a technology (a meeting of envoys): it gains `amount` of understanding when it can learn it.
		std::vector<KnowledgeEvent> Instruct(const std::string& learner, const std::string& tech, float amount, const std::string& how);
		const AgentKnowledge* Of(const std::string& name) const;
		const std::map<std::string, AgentKnowledge>& Agents() const { return m_Agents; }
		// What the clan knows: what its members told it at the camp, and the technologies of the start.
		const std::set<std::string>& Clan() const { return m_Clan; }

		// ---- queries (all true without technologies)
		bool Knows(const std::string& name, const std::string& tech) const;
		// It does not know it yet and knows everything it requires.
		bool CanLearn(const std::string& name, const std::string& tech) const;
		bool CanMake(const std::string& name, const std::string& recipe) const;
		bool CanGather(const std::string& name, const std::string& node) const;
		// The recipes of `book` the agent knows how to make, for RecipeBook::WhyNot / Craft (an unknown agent knows the common ones only).
		std::set<std::string> KnownRecipes(const std::string& name, const RecipeBook& book) const;

		// ---- the editor and tests: "" or what is wrong. GrantClan teaches it to every agent and to the clan.
		std::string Grant(const std::string& name, const std::string& tech);
		std::string GrantClan(const std::string& tech);

		// ---- discovery
		// The technology the agent would experiment with now ("" = none): the first by id that it can learn, can be found by trying, it is not waiting
		// to try again and one of whose recipes has its inputs in `store` and `bag` together. `recipe` gets that recipe.
		std::string NextExperiment(const std::string& name, double now, const Inventory& store, const Inventory& bag, const RecipeBook& book, std::string* recipe = nullptr) const;
		// One try with that technology at `now` (the agent has spent its Seconds already): it finds it or not, and may spoil a unit of material (from the
		// store first). Nothing happens when there is nothing to try.
		std::vector<KnowledgeEvent> Experiment(const std::string& name, double now, Inventory& store, Inventory& bag, const RecipeBook& book);
		// `doer` made `recipe` / worked `node` at `where`: those around who do not know the technology it used understand a bit more of it.
		std::vector<KnowledgeEvent> WitnessRecipe(const std::string& doer, const std::string& recipe, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents);
		std::vector<KnowledgeEvent> WitnessNode(const std::string& doer, const std::string& node, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents);
		std::vector<KnowledgeEvent> Witness(const std::string& doer, const std::string& tech, const glm::vec2& where, const std::vector<KnowledgeAgent>& agents);
		// The agent, at the camp, tells the clan what it knows.
		std::vector<KnowledgeEvent> Share(const std::string& name);
		// The clock of the lessons: true when one is due (the caller then builds the view of the agents and calls Lesson).
		bool Tick(float dt);
		// A lesson at the camp, and the clan forgetting what no living agent knows any more.
		std::vector<KnowledgeEvent> Lesson(const std::vector<KnowledgeAgent>& agents);

		// ---- for people
		std::string Status(const std::string& name) const;   // "knows ...; learning ... 34%; tried ... x2"
		std::string ClanStatus() const;
		std::string Name(const std::string& tech) const;      // the technology's name, or its id

		// ---- saving (docs/GUARDADO.md): what each agent knows as names ("tech.<id>", plus the Other names) for Save::ActorState::Knowledge, and the
		// rest (the clan, what is half learnt, the tries, the clocks) as the "knowledge-clan" section.
		std::vector<std::string> Names(const std::string& name) const;
		// An empty list (a save from before knowledge) leaves the agent as it is (the technologies of the start).
		void SetNames(const std::string& name, const std::vector<std::string>& names);
		std::string ToJson() const;
		// Reads it back over the agents there are (agents it names that are not there are skipped). False (nothing changes) when it is not readable.
		bool FromJson(const std::string& json);

	private:
		double Roll(const std::string& name, const std::string& tech, int salt, int n) const;   // deterministic 0..1
		void Learn(const std::string& name, AgentKnowledge& k, const std::string& tech);
		void Understand(const std::string& name, AgentKnowledge& k, const std::string& tech, float amount, const std::string& how, std::vector<KnowledgeEvent>& out);

		const TechTree* m_Techs = nullptr;
		std::map<std::string, AgentKnowledge> m_Agents;
		std::set<std::string> m_Clan;
		std::vector<std::string> m_ClanOther;   // names of a save that are not technologies, kept
		float m_LessonTimer = 0.0f;
	};
}
