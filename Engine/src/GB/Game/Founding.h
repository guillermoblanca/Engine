#pragma once
#include "GB/Game/FoundingRules.h"
#include "GB/Game/JobBoard.h"
#include "GB/Game/Society.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeConfig;
	struct TribeRole;

	// T310 (C2, documentation/01_diseno/ARRANQUE_Y_LLEGADAS.md §2): the people the player's clan is founded with. A pool of candidates (8 by
	// default, FoundingRules::Candidates) drawn from the game's seed (the stream SeedStream::Founding) with Society::Draft: adults with their sex,
	// genes, traits, a suggested trade (the role of their strongest gene of FoundingRules::Trades) and a first level in that trade's aptitude.
	// A candidate can be drawn again (a new roll: a new person with the next name) unless it is locked; 1 to 6 of them are picked; Seat puts a
	// picked one into the clan as it was drawn. Deterministic: the same seed and the same rolls give the same pool. Both hosts found the player's
	// clan through it (the editor's `tribe build ... party`, GBSim --party), so GB.exe and GBSim found the same people.
	struct FoundingCandidate
	{
		int Slot = 0;
		uint32_t Roll = 0;          // how many times this slot was drawn again
		std::string Name;
		Person Who;                 // as Society::Draft made it (age, sex, genes, traits)
		std::string Trade;          // the suggested role ("" when the trades name no role the clan has)
		std::string Skill;          // its aptitude family (TribeRole::Skill)
		float SkillLevel = 0.0f;    // the level it starts with (FoundingRules::SkillMin..SkillMax)
		float Aptitude = 0.0f;      // the sum of the genes of FoundingRules::Trades (the "most versatile": the best sum)
		bool Locked = false;
	};

	class ColonyFounding
	{
	public:
		// A new pool for a game whose world has this seed, with the content of the clan (its founding rules, its society's genes and traits, its
		// roles and the names of its work rules).
		void Start(const TribeConfig& config, uint32_t worldSeed);
		void Clear() { *this = ColonyFounding(); }
		bool Started() const { return m_Started; }
		uint32_t WorldSeed() const { return m_WorldSeed; }
		const FoundingRules& Rules() const { return m_Rules; }
		const std::vector<FoundingCandidate>& Candidates() const { return m_Candidates; }

		// Draws slot `i` again ("" or why not: a locked or unknown slot). RerollAll draws every unlocked slot again and says how many.
		std::string Reroll(int i);
		int RerollAll();
		std::string Lock(int i, bool on);
		// The slots chosen (FoundingRules::PartyMin..PartyMax, each once). "" or why not; on a mistake the choice stays as it was.
		std::string Pick(const std::vector<int>& slots);
		const std::vector<int>& Picked() const { return m_Picked; }
		// The choice of `n` when the player has not picked: one alone is the most versatile (the best Aptitude); more are as many women as men
		// (give or take one), the best of each. In slot order.
		std::vector<int> DefaultPick(int n) const;
		// The party of `n`: the picked slots when there are `n` of them, else DefaultPick(n).
		std::vector<int> PartyOf(int n) const;
		int Rolls() const { return m_Rolls; }

		// After Tribe::AddMember of a candidate's name: its person replaces the founder Join made (age, sex, genes, traits) and, with `skill`, it
		// starts with its trade's aptitude. Nothing happens when the clan has no member of that name.
		static void Seat(Tribe& tribe, const FoundingCandidate& c, bool skill);
		// One line per candidate (`game founding`), and the kit of `n` people.
		std::string Report() const;
		std::string KitText(int n) const;

	private:
		FoundingCandidate Draw(int slot, uint32_t roll) const;

		bool m_Started = false;
		uint32_t m_WorldSeed = 0;
		FoundingRules m_Rules;
		SocietyRules m_Society;     // the clan's, with the founding stream's seed
		WorkRules m_Work;           // (the names)
		std::map<std::string, std::string> m_RoleSkills;   // role -> its aptitude family
		std::vector<FoundingCandidate> m_Candidates;
		std::vector<int> m_Picked;
		int m_Rolls = 0;
	};
}
