#include "GB/Game/Tribe.h"
#include "GB/AI/Needs.h"
#include "GB/Game/SettlementPlanner.h"
#include <algorithm>
#include <cstdio>

// V5: the needs a content pack adds in data (DataNeeds.h): their fall, the verb that answers them and what doing it gives. The six needs the
// engine knows stay in Tribe.cpp (StepNeeds, Decide); these only add a level, an option and its end.
namespace GB
{
	namespace
	{
		std::string Attr(const NeedDef& n) { return "Need." + n.Id; }
	}

	bool Tribe::DataNeedActive(const NeedDef& n) const
	{
		if (n.Era.empty()) return true;
		const TechTree* tree = m_Knowledge.Techs();
		if (!tree || m_Era.empty()) return false;
		const std::vector<EraDef>& eras = tree->Rules.Eras;
		auto at = [&](const std::string& id) { return std::find_if(eras.begin(), eras.end(), [&](const EraDef& e) { return e.Id == id; }) - eras.begin(); };
		const auto need = at(n.Era), here = at(m_Era);
		return need < static_cast<std::ptrdiff_t>(eras.size()) && here < static_cast<std::ptrdiff_t>(eras.size()) && here >= need;   // (the eras in the order the packs list them)
	}

	float Tribe::DataNeedLevel(const TribeMember& m, const NeedDef& n) const
	{
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		return asc && asc->Attributes().Has(Attr(n)) ? asc->Attribute(Attr(n)) : 100.0f;
	}

	void Tribe::StepDataNeeds(TribeMember&, AbilitySystemComponent& asc, float dt)
	{
		for (const NeedDef& n : Config.DataNeeds.Needs)
		{
			if (!DataNeedActive(n)) continue;
			const std::string a = Attr(n);
			if (!asc.Attributes().Has(a)) asc.Attributes().Define(a, 100.0f, 0.0f, 100.0f);   // (a newborn, an older save, an era just reached)
			SatisfyNeed(asc, a.c_str(), -n.DecayPerDay * dt / std::max(1.0f, Config.Needs.DaySeconds));
		}
	}

	const Tribe::Placed* Tribe::ProvidingBuilding(const glm::vec2& from, const std::string& provision, std::vector<Placed>& keep) const
	{
		const Placed* best = nullptr;
		if (!m_Buildings || provision.empty()) return best;
		float bestDistance = 0.0f;
		for (const Placed& p : Placements())
		{
			if (!p.State.Done) continue;
			const auto it = p.Def->Provides.find(provision);
			if (it == p.Def->Provides.end() || it->second <= 0.0f) continue;
			const float d = glm::distance(from, p.Pos);
			if (!best || d < bestDistance || (d == bestDistance && p.Name < best->Name)) { keep.assign(1, p); best = &keep.front(); bestDistance = d; }
		}
		return best;
	}

	void Tribe::DataNeedOptions(TribeMember& m, std::vector<UtilityOption>& opts)
	{
		if (m.Player || Config.DataNeeds.Needs.empty()) return;
		const glm::vec2 pos = Where(m);
		for (const NeedDef& n : Config.DataNeeds.Needs)
		{
			if (!DataNeedActive(n)) continue;
			const float level = DataNeedLevel(m, n);
			if (level >= n.Threshold) continue;
			UtilityOption o;
			o.Kind = n.Verb;
			o.Target = n.Id;
			std::vector<Placed> keep;
			if (const Placed* p = ProvidingBuilding(pos, n.Provision, keep))
				o.Where = SiteFront(p->Pos, *p->Def, 0.6f * (static_cast<float>(IndexOf(m) % 3) - 1.0f));
			else if (!n.Item.empty() && (m.Body->Bag().Count(n.Item) > 0 || m_Store.Count(n.Item) > 0))
				o.Where = CampSpot(m);
			else continue;   // nowhere to do it yet: the need waits (the planner may raise what provides it)
			o.Score = n.Weight * Urgency(level, n.Threshold) * Falloff(glm::distance(pos, o.Where), 60.0f);
			o.Commit = n.Seconds;
			o.Note = n.Name;
			opts.push_back(o);
		}
	}

	bool Tribe::ExecuteDataNeed(TribeMember& m, const UtilityOption& o)
	{
		const NeedDef* n = Config.DataNeeds.ByVerb(o.Kind);
		if (!n) return false;
		Inventory& bag = m.Body->Bag();
		std::string detail;
		std::vector<Placed> keep;
		if (const Placed* p = ProvidingBuilding(Where(m), n->Provision, keep)) detail = p->Name;
		else if (!n->Item.empty())
		{
			if (bag.Count(n->Item) == 0 && m_Store.Transfer(*m_Items, bag, n->Item, 1) == 0) return false;
			detail = n->Item;
		}
		else return false;
		char go[96];
		std::snprintf(go, sizeof go, "goto %.2f %.2f | wait %.1f", static_cast<double>(o.Where.x), static_cast<double>(o.Where.y), static_cast<double>(n->Seconds));
		if (!Order(m, go)) return false;
		m.Job = o.Kind;
		m.Detail = detail;
		return true;
	}

	bool Tribe::FinishDataNeed(TribeMember& m, const std::string& job)
	{
		const NeedDef* n = Config.DataNeeds.ByVerb(job);
		if (!n) return false;
		AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		if (!asc || HasFailed(m)) return true;
		float gain = n->Amount;
		if (!n->Provision.empty())
		{
			std::vector<Placed> keep;
			const Placed* p = ProvidingBuilding(Where(m), n->Provision, keep);
			if (p && p->Name == m.Detail && glm::distance(Where(m), p->Pos) <= p->Def->Reach() + 2.0f) gain *= std::max(1.0f, p->Def->Provides.at(n->Provision));
			else if (n->Item.empty() || m.Detail != n->Item) return true;   // it did not get there
		}
		if (!n->Item.empty() && m.Detail == n->Item && m.Body->Bag().Remove(n->Item, 1) == 0) return true;
		const std::string a = Attr(*n);
		if (!asc->Attributes().Has(a)) asc->Attributes().Define(a, 100.0f, 0.0f, 100.0f);
		SatisfyNeed(*asc, a.c_str(), gain);
		const std::string& v = n->Verb;   // (the log speaks of the member: "prays", "washes")
		const bool es = v.back() == 's' || v.back() == 'x' || (v.size() > 1 && (v.compare(v.size() - 2, 2, "sh") == 0 || v.compare(v.size() - 2, 2, "ch") == 0));
		Say(m, v + (es ? "es" : "s") + (m.Detail.empty() ? std::string() : " at " + m.Detail) + " (" + n->Name + ")");
		return true;
	}
}
