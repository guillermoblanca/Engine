// The social lane between clans (docs/VIDA_SOCIAL.md, H13): beliefs spread at the greetings of their members, and shared beliefs make trust.
// Kept apart from Clans.cpp; the hook there is one line in Greet.
#include "GB/Game/Clans.h"
#include <algorithm>

namespace GB
{
	float ClanWorld::ShareBeliefs(ClanId a, ClanId b)
	{
		Clan* ca = Find(a);
		Clan* cb = Find(b);
		if (a == b || !ca || !cb || !ca->Folk || !cb->Folk) return 0.5f;
		SocialState& sa = ca->Folk->Social();
		SocialState& sb = cb->Folk->Social();
		if (sa.Beliefs.empty() || sb.Beliefs.empty()) return 0.5f;
		const BeliefRules& rules = ca->Folk->Config.Social.Beliefs;
		const float similarity = SocialState::Similarity(sa.Beliefs, sb.Beliefs);
		// what alike beliefs (or opposed ones) do to the pair: trust and attitude
		ClanRelation& r = RelationRef(a, b);
		const float w = rules.Trust * (2.0f * similarity - 1.0f);
		r.Base = std::clamp(r.Base + w, -1.0f, 1.0f);
		r.Trust = std::clamp(r.Trust + (w > 0.0f ? Rules.Relations.TrustGain * w : Rules.Relations.TrustLoss * w), 0.0f, 1.0f);
		// and they spread: each clan's beliefs move towards the other's, more between open clans
		const float share = std::clamp(rules.Spread * 0.5f * (Openness(a) + Openness(b)), 0.0f, 0.5f);
		const std::map<std::string, float> before = sa.Beliefs;
		for (auto& [id, v] : sa.Beliefs) if (const auto o = sb.Beliefs.find(id); o != sb.Beliefs.end()) v += (o->second - v) * share;
		for (auto& [id, v] : sb.Beliefs) if (const auto o = before.find(id); o != before.end()) v += (o->second - v) * share;
		return similarity;
	}
}
