#pragma once
// T341 (documentation/02_tecnico/gameplay/PROPUESTA_ARBOL_2026-11-19.md K-b, regla 2): ONE state per technology for the clan, worked out here and
// read by every view of knowledge (the tree, the queue of the Knowledge tab, the book's sheet), so they never say different things: the tree used
// to call flint knapping "can be found now" while the queue did not offer it (the clan has no flint at hand).
//
//   known        the clan knows it
//   studying     the first entry of the research queue the clan can research now (ResearchQueue::Current): who works on knowledge tries it
//   queued       in the queue (its place, 1-based) and not the one being studied; `Why` says what it waits for
//   own pace     not queued, but someone has worked some of it out by luck, watching or lessons (ClanKnowledge::Research)
//   studiable    the clan can research it now (ResearchQueue::Researchable, the sim's own gate) and its tier is open
//   blocked      what it requires is known, but something else is missing: a material or a station at hand (`physical`), its tier
//                (T323), or it cannot be found by trying (only from others). `Why` and `Missing` say which
//   other clan   a clan the player's has dealings with knows it (`From`): it can be learnt from them
//   unknown      none of the above: never named (ARBOL_TECNICO §11.2.5, decision D2). `Hint` is an unknown prerequisite that is named ("after X")
//
// Named (D2): every state but `unknown`. What is studiable, has progress, or a clan with dealings knows is named; so are the queued and the blocked
// ones whose prerequisites are known (the approved mock-up a1 shows "Flint knapping: no flint at hand").
//
// Tiers (D6, K-a): only the technologies with a `tier` are drawn and counted (the 54 of the Neolithic tree; tier 4 = era 2, one block); the 9
// without one (era3, era4, medieval) are left out. The tiers come from TechDef::Tier (T323) through TechTierMap::FromTree. A tree none of whose technologies has a tier (the tests'
// trees in memory) shows every technology.
// OpenTier() is the clan's open step (GB::TechTiers::Open, T323; 99 when the data has no steps).
//
// CPU only (no ImGui), deterministic, built on the fly from the tribe: the UI makes one per frame it draws.
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TechTree;
	class ClanWorld;

	// The `tier` of each technology of a tree (TechDef::Tier: LoadTechs reads it; T347 took out the second reader of the files).
	class TechTierMap
	{
	public:
		// The tiers of a tree (TechDef::Tier, T323).
		static TechTierMap FromTree(const TechTree& tree);
		void Clear() { m_Of.clear(); }
		int Of(const std::string& tech) const;   // -1: no tier
		bool Any() const { return !m_Of.empty(); }
		const std::map<std::string, int>& All() const { return m_Of; }
		void Set(const std::string& tech, int tier) { m_Of[tech] = tier; }   // (tests)

	private:
		std::map<std::string, int> m_Of;
	};

	enum class TechState { Known, Studying, Queued, OwnPace, Studiable, Blocked, OtherClan, Unknown };
	enum class TechWhy { None, Requires, Tier, Material, Station, NotByTrying };

	// A clan the player's has dealings with, and what it knows (TechStatus::Dealings fills them from the clan world).
	struct TechNeighbour
	{
		std::string Name;
		const std::set<std::string>* Known = nullptr;
	};

	class TechStatus
	{
	public:
		static constexpr int kEraBlockTier = 4;   // D6: tier 4 (era 2) is drawn as one "?" block, not by name

		struct Entry
		{
			std::string Tech;
			TechState State = TechState::Unknown;
			TechWhy Why = TechWhy::None;            // why it cannot be studied now (Queued, Blocked; Unknown: Requires or Tier)
			std::vector<std::string> Missing;       // Requires: the prerequisites not known; Material: the items not at hand; Station: the stations
			int Queue = 0;                          // its place in the research queue (1-based; 0: not queued)
			float Progress = 0.0f;                  // the furthest anyone has got (0..1)
			std::vector<std::string> Who;           // who is on it
			std::string From;                       // OtherClan: the clan that knows it (the first by name)
			std::string Hint;                       // Unknown: a prerequisite not known that is named ("after X"); "" = none
			int Tier = -1;                          // -1: no tier
			bool Named = false;
			bool Shown = true;                      // drawn and counted (D6: it has a tier, or there are no tiers at all)
		};

		struct Input
		{
			const TechTierMap* Tiers = nullptr;    // nullptr: TechTierMap::FromTree(the clan's tree)
			std::vector<TechNeighbour> Neighbours;
			int OpenTier = -1;                      // < 0: OpenTier(tribe)
		};

		TechStatus() = default;
		TechStatus(const Tribe& tribe, const Input& input);

		// The highest tier the clan has opened (GB::TechTiers::Open; 99 when the data has no steps).
		static int OpenTier(const Tribe& tribe);
		// The clans the player's has dealings with: met, not hostile, and friendly, at a truce, allied, or that have traded or taught each other.
		static bool Dealings(const ClanWorld& world, int a, int b);
		static std::vector<TechNeighbour> Neighbours(const ClanWorld& world, int clan);

		// The entry of a technology (a technology the tree has not: an unknown, unnamed, unshown entry).
		const Entry& Of(const std::string& tech) const;
		const std::map<std::string, Entry>& All() const { return m_Entries; }
		bool Named(const std::string& tech) const { return Of(tech).Named; }
		bool Shown(const std::string& tech) const { return Of(tech).Shown; }

		// The counters of the views (only the shown technologies): known, studiable now, named and not known, and the "?" (not named).
		int Total() const { return m_Total; }
		int KnownCount() const { return m_Known; }
		int StudiableCount() const { return m_Studiable; }
		int UnnamedCount() const { return m_Unnamed; }
		// The "?" of one tier (and of tier >= kEraBlockTier together when `tier` is kEraBlockTier).
		int UnnamedIn(int tier) const;
		// The filter of ResearchQueue::Hidden / List: the shown technologies.
		std::function<bool(const std::string&)> ShownFilter() const;

		// For people and tests: "studying", "queued 2 (waits for requires: gamma)", "blocked material: flint", "other clan Ceniza", "unknown after alpha"...
		static const char* StateWord(TechState s);
		static const char* WhyWord(TechWhy w);
		std::string Describe(const std::string& tech) const;

	private:
		std::map<std::string, Entry> m_Entries;
		Entry m_None;
		int m_Total = 0, m_Known = 0, m_Studiable = 0, m_Unnamed = 0;
	};
}
