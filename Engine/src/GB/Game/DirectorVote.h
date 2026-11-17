#pragma once
#include <map>
#include <string>
#include <vector>

// T236 (G4-1 of the god mode, documentation/01_diseno/MODO_DIOS_DECISIONES.md §3.0): the vote of a clan on an idea one of its people brings
// (the messenger of a sign, the dreamer of a vision). It is ProposalVote (PlayerMember.cpp) with two new terms, the faith of each voter and
// whether the messenger is the clan's shaman, and without the player's standing or lead. The messenger does not vote. Pure: it reads a list
// of voters the caller gathers (WorldDirector::BallotFor from a Tribe, or a test's synthetic clan) and changes nothing; the voters are counted
// in name order, so the same ballot gives the same count on any machine.
namespace GB
{
	// assets/data/director.json, "director.vote".
	struct DirectorVoteRules
	{
		float Base = -0.1f;
		float Faith = 0.8f;             // x the voter's devotion 0..1
		float Affinity = 0.3f;          // x its bond with the messenger -1..1 (D43: at 0.6 the bonds of a clan a few days old carried every vote)
		float Shaman = 0.3f;            // the messenger is the clan's shaman
		float Survival = 0.6f;          // a cost more while the clan lives in "survival" (Tribe::MaslowTier)
		float Majority = 0.5f;          // more than this share in favour: the clan obeys
		float DoubtBand = 0.35f;        // at least this share (and not a majority): the clan doubts and votes again later; under it, it resists
		int Retries = 1;                // how many times a doubt is voted again
		std::map<std::string, float> Culture = { { "sharing", 0.2f } };   // norm -> weight x (2 norm - 1), as ProposalVote
		bool operator==(const DirectorVoteRules&) const = default;
	};

	struct DirectorVoter
	{
		std::string Name;
		float Devotion = 0.0f;                    // 0..1 (WorldDirector::DevotionOf)
		float Bond = 0.0f;                        // with the messenger (Society::Affinity)
		std::map<std::string, float> Traits;      // personality 0..1 (Person::Traits)
		bool Skeptic = false;                     // T241: laughs at the god's dreams: always against, and its no counts twice
	};

	// What is voted: who brings it, what it weighs and who votes.
	struct DirectorBallot
	{
		std::string Messenger;                    // does not vote (even if listed among the voters)
		bool MessengerIsShaman = false;
		float Cost = 0.0f;                        // the decision's cost (a sign: buildCost + missing x the share of material missing; + Survival)
		float FaithScale = 1.0f;                  // T241: x rules.Faith (a tired clan listens half: 0.5)
		std::map<std::string, float> Traits;      // trait -> weight x (2 trait - 1) (per decision, in the data: a sign weighs diligence)
		std::map<std::string, float> Norms;       // the clan's norms 0..1 (Society::Norm)
		std::vector<DirectorVoter> Voters;
	};

	struct DirectorVoteResult
	{
		int Voters = 0, Yes = 0;
		int Skeptics = 0;                         // T241: voters who are skeptics (each no of theirs weighs two in the share)
		std::string Outcome;                      // "obey", "doubt" or "resist" (no voters: "resist")
		std::vector<std::string> Against;         // in name order
		bool Accepted() const { return Outcome == "obey"; }
		std::string Text() const { return "vote " + std::to_string(Yes) + " of " + std::to_string(Voters); }
	};

	// One voter's disposition (> 0: in favour).
	float DirectorDisposition(const DirectorVoteRules& rules, const DirectorBallot& ballot, const DirectorVoter& voter);
	// The count: every voter but the messenger, in name order (a name listed twice counts once).
	DirectorVoteResult CountDirectorVote(const DirectorVoteRules& rules, const DirectorBallot& ballot);
}
