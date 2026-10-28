// H9 + H10 · the society of the tribe: life and death, families, genes, personality, relations and culture (the state and its rules are in
// GB/Game/Society.h). Kept apart from Tribe.cpp like the work of H7 (TribeWork.cpp); the hooks there are few: AddMember joins a founder, Die tells
// the society, Update runs StepSociety, Decide calls SocietyFactors, Execute reads WorkPace, Practise reads the learning rate, the lessons read who
// teaches whom, and Finish/Eat/Tell report what happened between members (Worked, SharedFood, Quarrel, ClanEvent). Deterministic like the rest.
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kNearCamp = 5.0f;   // (Tribe.cpp's: within this of the camp a member is "at the camp"; twice that for the lessons)
		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }
	}

	// ------------------------------------------------------------------ the days
	void Tribe::StepSociety(float dt)
	{
		const SocietyRules& rules = Config.Society;
		// members sitting together at the camp grow closer: each with its nearest few within CampRadius (a pair once per round)
		m_CampTimer += dt;
		if (m_CampTimer >= rules.Relations.CampSeconds)
		{
			m_CampTimer = 0.0f;
			std::vector<std::pair<const TribeMember*, glm::vec2>> here;
			for (const TribeMember& m : m_Members)
				if (!m.Dead && m.Body && glm::distance(Where(m), Config.Camp) <= kNearCamp * 2.0f) here.emplace_back(&m, Where(m));
			std::set<std::pair<std::string, std::string>> done;
			for (const auto& [m, pos] : here)
			{
				std::vector<std::pair<float, const TribeMember*>> near;
				for (const auto& [o, opos] : here)
					if (o != m && glm::distance(pos, opos) <= rules.Relations.CampRadius) near.emplace_back(glm::distance(pos, opos), o);
				std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
				for (size_t k = 0; k < near.size() && k < static_cast<size_t>(rules.Relations.CampNearest); ++k)
				{
					const std::string& a = m->Name, & b = near[k].second->Name;
					if (done.insert(a < b ? std::make_pair(a, b) : std::make_pair(b, a)).second) Relate(a, b, "camp");
				}
			}
		}
		if (m_Society.Tick(rules, dt, Config.Needs.DaySeconds)) SocietyDay();
	}

	void Tribe::SocietyDay()
	{
		const SocietyRules& rules = Config.Society;
		ClanState clan;
		clan.Food = m_Items ? static_cast<float>(FoodIn(m_Store)) : 0.0f;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) ++clan.Alive;
		clan.Shelter = HasFire();
		SocietyEvents(m_Society.Day(rules, clan));
		// the children grow (and who came of age stands at its full height)
		for (TribeMember& m : m_Members)
			if (!m.Dead && m.Body)
				if (const Person* p = m_Society.Find(m.Name); p && p->Age < rules.Life.ChildUntil + 1.0f) m.Body->SetGrowth(m_Society.Growth(rules, m.Name));
	}

	void Tribe::SocietyEvents(const std::vector<SocietyEvent>& events)
	{
		auto member = [&](const std::string& name) -> TribeMember*
		{
			const auto it = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == name; });
			return it == m_Members.end() ? nullptr : &*it;
		};
		auto say = [&](const std::string& who, const std::string& text)
		{
			if (TribeMember* m = member(who)) { Say(*m, text); return; }
			TribeMember clan;   // the clan itself speaks
			clan.Name = "clan";
			clan.Role = "all";
			Say(clan, text);
		};
		for (const SocietyEvent& e : events)
		{
			if (e.Kind == "death")
			{
				if (TribeMember* m = member(e.Who); m && !m->Dead) Die(*m, e.Text);   // (Die tells the society, which knows already)
			}
			else if (e.Kind == "birth") Born(e.Who, e.Other);
			else if (e.Kind == "norm" || e.Kind == "tradition") say(std::string(), e.Text);
			else say(e.Who, e.Text);
		}
	}

	// A couple has a child: it gets the next free name of the data, a body from the host (beside its mother, at a child's height), its parents' genes
	// mixed and the trade of the children; it joins knowing what everyone knows at the start, and what the clan remembers of the land.
	void Tribe::Born(const std::string& mother, const std::string& father)
	{
		const SocietyRules& rules = Config.Society;
		const auto mom = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == mother; });
		TribeMember voice;
		voice.Name = mother;
		voice.Role = mom != m_Members.end() ? mom->Role : std::string("all");
		if (!m_Spawner) { Say(voice, "would have a child, but there is no one to give it a body (no spawner)"); return; }
		std::string child;
		for (size_t n = m_Members.size(); child.empty(); ++n)
		{
			const std::string name = MemberName(Config.Work, n);
			const bool taken = m_Society.Find(name) || std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == name; });
			if (!taken) child = name;
		}
		const glm::vec2 at = mom != m_Members.end() && mom->Body && !mom->Dead ? Where(*mom) : Config.Camp;
		ITribeBody* body = m_Spawner(child, at, rules.Life.ChildHeight);
		if (!body) { Say(voice, "would have a child, but it got no body"); return; }
		m_Society.Bear(rules, child, mother, father);
		std::string role = rules.Life.ChildRole;
		if (!Config.Roles.count(role))
		{
			int alive = 0;
			for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) ++alive;
			const std::vector<std::string> plan = PlanRoles(alive + 1);
			role = plan.empty() ? std::string("forager") : plan.back();
		}
		AddMember(child, role, body);
		m_Members.back().Mind.Memory.Merge(m_Clan);   // what the clan knows of the land, as if its parents had told it
		Say(voice, "has a child: " + child + " (with " + father + ")");
		ClanEvent("birth");
	}

	// ------------------------------------------------------------------ the brain
	// Genes and personality (nature), the stage of life (a child leaves heavy work, an elder does less of it) and the clan's norms and traditions
	// (culture) scale every option; a lonely member seeks the companion it likes best among those it saw lately.
	void Tribe::SocietyFactors(const TribeMember& m, std::vector<UtilityOption>& options) const
	{
		const SocietyRules& rules = Config.Society;
		if (!m_Society.Find(m.Name)) return;
		for (UtilityOption& o : options)
		{
			std::string family;
			if (o.Kind == "gather")
			{
				if (const MemoryEntry* e = m.Mind.Memory.Find(o.Target)) family = SkillFamilyOf(e->Kind);
			}
			else if (o.Kind == "tool" || o.Kind == "experiment") family = SkillFamilyOf("craft");
			else if (o.Kind == "fire" || o.Kind == "stoke") family = SkillFamilyOf(o.Kind);
			const Society::Factors f = m_Society.OptionFactor(rules, m.Name, o.Kind, family);
			if (f.Total() == 1.0f) continue;
			o.Score *= f.Total();
			if (f.Nature != 1.0f) Factor(o, "nature", f.Nature);
			if (f.Life != 1.0f) Factor(o, "life", f.Life);
			if (f.Culture != 1.0f) Factor(o, "culture", f.Culture);
		}
		const auto social = std::find_if(options.begin(), options.end(), [](const UtilityOption& o) { return o.Kind == "socialize"; });
		if (social == options.end()) return;
		std::vector<std::string> seen;
		const glm::vec2 pos = Where(m);
		for (const MemoryEntry* e : m.Mind.Memory.OfKind("agent"))
			if (e->Name != m.Name && m_Time - e->Seen <= 120.0 && glm::distance(pos, e->Pos) > 3.0f)
				if (const Person* p = m_Society.Find(e->Name); p && !p->Dead) seen.push_back(e->Name);
		const std::string best = m_Society.Favourite(rules, m.Name, seen);
		if (best.empty()) return;
		const float affinity = m_Society.Affinity(rules, m.Name, best);
		if (best != social->Target && affinity > m_Society.Affinity(rules, m.Name, social->Target) + 0.05f)
		{
			social->Target = best;
			social->Where = m.Mind.Memory.Find(best)->Pos;
			social->Note += "; seeks " + best;
		}
		if (affinity > 0.0f)
		{
			const float f = 1.0f + 0.5f * affinity;   // a friend is worth the walk
			social->Score *= f;
			Factor(*social, "friend", f);
		}
	}

	// ------------------------------------------------------------------ what happens between members
	void Tribe::Worked(const TribeMember& m)
	{
		const float radius = Config.Society.Relations.WorkRadius;
		const glm::vec2 pos = Where(m);
		std::vector<std::pair<float, std::string>> near;
		for (const TribeMember& o : m_Members)
			if (&o != &m && !o.Dead && o.Body && o.Job == "gather")
				if (const float d = glm::distance(pos, Where(o)); d <= radius) near.emplace_back(d, o.Name);
		std::stable_sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		for (size_t k = 0; k < near.size() && k < 3; ++k) Relate(m.Name, near[k].second, "work");
	}

	void Tribe::SharedFood(const TribeMember& m)
	{
		if (m_FoodBy.empty() || m_FoodBy == m.Name) return;
		// the clan that values sharing feels it more (a norm of 0.5 is x1)
		const float scale = Config.Society.Culture.Norms.count("sharing") ? 0.5f + m_Society.Norm(Config.Society, "sharing") : 1.0f;
		Relate(m.Name, m_FoodBy, "food", scale);
	}

	void Tribe::Quarrel(const TribeMember& m, const std::string& node)
	{
		const auto it = m_LastWorker.find(node);
		if (it == m_LastWorker.end() || it->second == m.Name) return;
		const std::string other = it->second;
		const bool alive = std::any_of(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == other && !x.Dead; });
		if (!alive) return;
		Relate(m.Name, other, "conflict");
		Say(m, "is cross with " + other + ": " + other + " emptied " + node);
	}

	void Tribe::ClanEvent(const std::string& event) { SocietyEvents(m_Society.Note(Config.Society, event)); }

	// ------------------------------------------------------------------ for people
	std::string Tribe::FamilyReport(const std::string& name) const
	{
		return m_Society.Family(Config.Society, name) + "\n" + m_Society.Character(Config.Society, name);
	}

	std::string Tribe::RelationsReport(const std::string& name) const { return m_Society.Relations(Config.Society, name); }
}
