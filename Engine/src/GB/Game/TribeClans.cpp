// H13 · the part of the tribe the clans around use (GB/Game/Clans.h): giving a member orders from outside, a line in the log, and a member leaving
// for another clan or joining from one with everything that is its own. The decision hooks themselves (TribeOutside) are one line each in
// Tribe.cpp: Decide asks for the outside's options, Execute and Finish hand it the options and jobs of its kinds, and a gather is reported.
#include "GB/Game/Tribe.h"

namespace GB
{
	bool Tribe::Send(TribeMember& m, const std::string& orders, const std::string& job, const std::string& detail)
	{
		if (m.Dead || !m.Body || !Order(m, orders)) return false;
		m.Job = job;
		m.Detail = detail;
		return true;
	}

	void Tribe::NoteClan(const std::string& text) { TellClan(text); }

	bool Tribe::Detach(const std::string& name, MemberTransfer& out)
	{
		const auto it = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& m) { return m.Name == name; });
		if (it == m_Members.end()) return false;
		if (it->Abstract) SetDetail(*it, true);   // its body takes its place again (and its ability system its runtime) before it goes
		out = MemberTransfer();
		out.Member = *it;
		TribeMember& m = out.Member;
		m.Job.clear();
		m.Detail.clear();
		m.Why.clear();
		m.Mind.Orders.clear();
		m.HasSpot = false;
		m.SpotTries = 0;
		m.Gone.clear();
		if (const Person* p = m_Society.Find(name)) { out.Who = *p; out.HasPerson = true; }
		if (const AgentKnowledge* k = m_Knowledge.Of(name)) { out.Knows = *k; out.HasKnowledge = true; }
		Say(*it, "leaves the clan");
		return RemoveMember(name);   // (its claims, its job on the board, its bonds and its partner here)
	}

	void Tribe::Admit(MemberTransfer&& in)
	{
		TribeMember m = std::move(in.Member);
		m.Abstract = false;
		m.Job.clear();
		m.Detail.clear();
		m.Mind.Orders.clear();
		m.HasSpot = false;
		m.Mind.Memory.Merge(m_Clan);   // what the clan knows of its land, as a newcomer is told on arrival
		if (in.HasPerson) m_Society.Admit(m.Name, in.Who);
		else m_Society.Join(Config.Society, m.Name);
		if (in.HasKnowledge) m_Knowledge.Admit(m.Name, in.Knows);
		else m_Knowledge.Join(m.Name);
		if (m.Body)
		{
			m.Body->SetGrowth(m_Society.Growth(Config.Society, m.Name));
			std::string error;
			m.Body->Do("wait 0.5", error);   // what it was doing for its old clan stops: it decides again here
		}
		m_Members.push_back(std::move(m));
		Say(m_Members.back(), "joins the clan");
	}
}
