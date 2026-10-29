#include "GB/Game/ClanHostility.h"
#include <algorithm>
#include <cstdio>

namespace GB
{
	void ClanHostility::Bind(ClanWorld* clans, IHostility* fallback)
	{
		m_Fallback = fallback;
		if (clans != m_Clans) m_Listening = false;
		m_Clans = clans;
		if (!m_Clans || m_Listening) return;
		m_Listening = true;
		std::weak_ptr<Heard> heard = m_Heard;
		m_Clans->Listen([heard](const DiplomacyEvent& e)
		{
			const std::shared_ptr<Heard> h = heard.lock();
			if (!h) return;
			static const char* kKinds[] = { "stance", "pact", "break", "raid", "theft", "killing" };
			if (std::none_of(std::begin(kKinds), std::end(kKinds), [&](const char* k) { return e.Kind == k; })) return;
			char t[32];
			std::snprintf(t, sizeof t, "%.1f ", e.At);
			h->Lines.push_back(t + e.Kind + ": " + e.Text);
			if (h->Lines.size() > 40) h->Lines.pop_front();
		});
	}

	ClanId ClanHostility::Id(const std::string& side) const
	{
		const Clan* c = m_Clans ? m_Clans->Find(side) : nullptr;
		return c ? c->Id : -1;
	}

	float ClanHostility::Hostility(const std::string& a, const std::string& b) const
	{
		const ClanId ia = Id(a), ib = Id(b);
		if (ia >= 0 && ib >= 0) return std::clamp(m_Clans->Hostility(ia, ib), 0.0f, 1.0f);
		return m_Fallback ? m_Fallback->Hostility(a, b) : 0.0f;
	}

	std::vector<RaidTarget> ClanHostility::RaidTargets(const std::string& raider) const
	{
		const ClanId me = Id(raider);
		if (me < 0) return m_Fallback ? m_Fallback->RaidTargets(raider) : std::vector<RaidTarget>();
		std::vector<RaidTarget> out;
		for (const auto& c : m_Clans->All())
		{
			if (c->Id == me || !c->Folk || !m_Clans->MayRaid(me, c->Id)) continue;
			RaidTarget t;
			t.Clan = c->Name;
			t.Camp = c->Folk->Config.Camp;
			t.Store = &c->Folk->Store();
			t.Defense = c->Folk->Defense();   // its palisades put a raider off (H8)
			out.push_back(t);
		}
		return out;
	}

	void ClanHostility::Settled(const std::string& clan, const glm::vec2& camp, Inventory* store, float defense)
	{
		if (Id(clan) < 0 && m_Fallback) m_Fallback->Settled(clan, camp, store, defense);   // (a clan's camp and store are its tribe's: the clan world knows them)
	}

	void ClanHostility::Wronged(const std::string& victim, const std::string& culprit, const std::string& what, float weight)
	{
		const ClanId v = Id(victim), c = Id(culprit);
		if (v < 0 || c < 0) { if (m_Fallback) m_Fallback->Wronged(victim, culprit, what, weight); return; }
		// the diplomacy's events: a killing weighs as a raid (the weight the conflict gives it, 2x), a theft as a theft
		const std::string event = what == "theft" ? "theft" : "raid";
		m_Clans->Relate(c, v, event, weight, culprit + " " + (what == "killing" ? "killed one of " + victim : what == "theft" ? "stole from " + victim : "raided " + victim), v);
	}

	float ClanHostility::Grievance(const std::string& victim, const std::string& culprit) const
	{
		const ClanId v = Id(victim), c = Id(culprit);
		if (v < 0 || c < 0) return m_Fallback ? m_Fallback->Grievance(victim, culprit) : 0.0f;
		float g = m_Clans->Grievance(v, c);
		for (const auto& [region, claimants] : m_Clans->Contested())
			if (std::find(claimants.begin(), claimants.end(), v) != claimants.end() && std::find(claimants.begin(), claimants.end(), c) != claimants.end()) g += Contest;
		return g;
	}

	std::vector<std::string> ClanHostility::Recent() const { return { m_Heard->Lines.begin(), m_Heard->Lines.end() }; }

	std::string ClanHostility::Report() const
	{
		std::string s;
		if (m_Clans)
			for (const auto& a : m_Clans->All())
				for (const auto& b : m_Clans->All())
				{
					if (a->Id == b->Id) continue;
					const float h = std::clamp(m_Clans->Hostility(a->Id, b->Id), 0.0f, 1.0f);
					const bool raid = m_Clans->MayRaid(a->Id, b->Id);
					if (h <= 0.0f && !raid) continue;
					char t[160];
					std::snprintf(t, sizeof t, "%s -> %s hostile %.2f%s (grievance %.2f)\n", a->Name.c_str(), b->Name.c_str(), static_cast<double>(h), raid ? ", may raid" : "",
						static_cast<double>(Grievance(a->Name, b->Name)));
					s += t;
				}
		for (const std::string& l : m_Heard->Lines) s += l + "\n";
		return s.empty() ? "no clan is hostile to another\n" : s;
	}
}
