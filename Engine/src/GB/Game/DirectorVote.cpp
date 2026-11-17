#include "GB/Game/DirectorVote.h"
#include <algorithm>

// T236: the vote of a clan on an idea of the god (see DirectorVote.h).
namespace GB
{
	float DirectorDisposition(const DirectorVoteRules& rules, const DirectorBallot& ballot, const DirectorVoter& voter)
	{
		float v = rules.Base + rules.Faith * ballot.FaithScale * std::clamp(voter.Devotion, 0.0f, 1.0f) + rules.Affinity * voter.Bond + (ballot.MessengerIsShaman ? rules.Shaman : 0.0f) - ballot.Cost;
		for (const auto& [trait, w] : ballot.Traits)
			if (const auto it = voter.Traits.find(trait); it != voter.Traits.end()) v += w * (2.0f * it->second - 1.0f);
		for (const auto& [norm, w] : rules.Culture)
			if (const auto it = ballot.Norms.find(norm); it != ballot.Norms.end()) v += w * (2.0f * it->second - 1.0f);
		return v;
	}

	DirectorVoteResult CountDirectorVote(const DirectorVoteRules& rules, const DirectorBallot& ballot)
	{
		std::vector<const DirectorVoter*> voters;
		for (const DirectorVoter& v : ballot.Voters)
			if (v.Name != ballot.Messenger) voters.push_back(&v);
		std::stable_sort(voters.begin(), voters.end(), [](const DirectorVoter* a, const DirectorVoter* b) { return a->Name < b->Name; });
		voters.erase(std::unique(voters.begin(), voters.end(), [](const DirectorVoter* a, const DirectorVoter* b) { return a->Name == b->Name; }), voters.end());
		DirectorVoteResult r;
		int weight = 0;   // (T241: a skeptic's no weighs two)
		for (const DirectorVoter* v : voters)
		{
			++r.Voters;
			++weight;
			if (v->Skeptic) { ++r.Skeptics; ++weight; r.Against.push_back(v->Name); }
			else if (DirectorDisposition(rules, ballot, *v) > 0.0f) ++r.Yes;
			else r.Against.push_back(v->Name);
		}
		const float share = weight > 0 ? static_cast<float>(r.Yes) / static_cast<float>(weight) : 0.0f;
		r.Outcome = r.Voters > 0 && share > rules.Majority ? "obey" : r.Voters > 0 && share >= rules.DoubtBand ? "doubt" : "resist";
		return r;
	}
}
