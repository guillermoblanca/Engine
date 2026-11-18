#include "GB/Game/WorldDirector.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

// T238 (the focus of a clan) and T239 (the calling of a person), MODO_DIOS_DECISIONES.md §3.b. The director keeps them and writes each clan's
// TribeBias (GB/Game/TribeBias.h), which the tribe reads in two one-line hooks (Decide, ReviewRoles). Only the public face of the Tribe is used;
// nothing here writes a tribe while there is no focus and no calling.
namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		std::string Num(double v, int decimals = 1)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%.*f", decimals, v);
			return b;
		}
		Clan* ClanAt(ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
		const Clan* ClanAt(const ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			const Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }
		bool Survival(const Tribe& t) { return t.MaslowTier() == "survival"; }
		const char* Strength(bool strong) { return strong ? "strong" : "soft"; }

		bool Number(const Json& o, const char* key, float& out, float lo, float hi, const std::string& where, std::string& e)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_number() || it->get<double>() < lo || it->get<double>() > hi) { e = where + key + ": expected a number in " + Num(lo, 2) + ".." + Num(hi, 2); return false; }
			out = it->get<float>();
			return true;
		}
		bool Known(const Json& o, std::initializer_list<const char*> keys, const std::string& where, std::string& e)
		{
			for (const auto& [k, v] : o.items())
				if (std::none_of(keys.begin(), keys.end(), [&](const char* known) { return k == known; })) { e = where + "unknown key '" + k + "'"; return false; }
			return true;
		}
	}

	// { "focus": { "soft", "strong", "upkeep": { "soft", "strong" }, "maxDays", "faith", "trait", "drift",
	//              "groups": { <id>: { "kinds": [], "foodGather", "otherGather", "trait", "norm" } } },
	//   "calling": { "days", "skill", "refund", "traits": {} } }   (the costs are "focus", "focus_strong" and "calling" in "costs")
	std::string ReadDirectorFocusRules(const Json& d, DirectorRules& r)
	{
		std::string e;
		if (const auto it = d.find("focus"); it != d.end())
		{
			const std::string w = "director: focus: ";
			if (!it->is_object()) return "director: focus: expected an object";
			if (!Known(*it, { "soft", "strong", "upkeep", "maxDays", "faith", "trait", "drift", "groups" }, w, e)) return e;
			DirectorFocusRules& f = r.Focus;
			if (!Number(*it, "soft", f.Soft, 1.0f, 10.0f, w, e) || !Number(*it, "strong", f.Strong, 1.0f, 10.0f, w, e) || !Number(*it, "faith", f.Faith, 0.0f, 10.0f, w, e)
				|| !Number(*it, "trait", f.Trait, 0.0f, 10.0f, w, e) || !Number(*it, "drift", f.Drift, 0.0f, 1.0f, w, e)) return e;
			if (const auto u = it->find("upkeep"); u != it->end())
			{
				if (!u->is_object() || !Known(*u, { "soft", "strong" }, w + "upkeep: ", e)) return e.empty() ? w + "upkeep: expected an object" : e;
				if (!Number(*u, "soft", f.UpkeepSoft, 0.0f, 1000.0f, w + "upkeep: ", e) || !Number(*u, "strong", f.UpkeepStrong, 0.0f, 1000.0f, w + "upkeep: ", e)) return e;
			}
			if (const auto n = it->find("maxDays"); n != it->end())
			{
				if (!n->is_number_integer() || n->get<int>() < 1 || n->get<int>() > 100) return w + "maxDays: expected an integer in 1..100";
				f.MaxDays = n->get<int>();
			}
			if (const auto g = it->find("groups"); g != it->end())
			{
				if (!g->is_object() || g->empty()) return w + "groups: expected a non-empty object";
				std::map<std::string, DirectorFocusGroup> groups;
				for (const auto& [id, v] : g->items())
				{
					const std::string wg = w + "groups: " + id + ": ";
					if (!v.is_object() || !Known(v, { "kinds", "foodGather", "otherGather", "trait", "norm" }, wg, e)) return e.empty() ? wg + "expected an object" : e;
					DirectorFocusGroup grp;
					const auto kinds = v.find("kinds");
					if (kinds == v.end() || !kinds->is_array()) return wg + "kinds: expected an array of option kinds";
					for (const Json& k : *kinds) { if (!k.is_string()) return wg + "kinds: expected strings"; grp.Kinds.push_back(k.get<std::string>()); }
					grp.FoodGather = v.value("foodGather", false);
					grp.OtherGather = v.value("otherGather", false);
					grp.Trait = v.value("trait", std::string());
					grp.Norm = v.value("norm", std::string());
					if (grp.Kinds.empty() && !grp.FoodGather && !grp.OtherGather) return wg + "pushes no option";
					groups[id] = std::move(grp);
				}
				f.Groups = std::move(groups);
			}
			if (f.Strong < f.Soft) return w + "strong: less than soft";
		}
		if (const auto it = d.find("calling"); it != d.end())
		{
			const std::string w = "director: calling: ";
			if (!it->is_object()) return "director: calling: expected an object";
			if (!Known(*it, { "days", "skill", "refund", "traits" }, w, e)) return e;
			DirectorCallingRules& c = r.Calling;
			if (!Number(*it, "days", c.Days, 0.01f, 1000.0f, w, e) || !Number(*it, "skill", c.Skill, 0.0f, 1000.0f, w, e) || !Number(*it, "refund", c.Refund, 0.0f, 1.0f, w, e)) return e;
			if (const auto t = it->find("traits"); t != it->end())
			{
				if (!t->is_object()) return w + "traits: expected an object";
				std::map<std::string, float> traits;
				for (const auto& [k, v] : t->items()) { if (!v.is_number()) return w + "traits: " + k + ": expected a number"; traits[k] = v.get<float>(); }
				c.Traits = std::move(traits);
			}
		}
		return {};
	}

	// ------------------------------------------------------------------ the focus
	const DirectorFocus* WorldDirector::FocusOf(int clan) const
	{
		for (const DirectorFocus& f : m_Focus) if (f.Clan == clan) return &f;
		return nullptr;
	}

	float WorldDirector::FocusMultiplier(int clan, const std::string& person, bool strong, const std::string& group) const
	{
		const Clan* c = ClanAt(m_To.Clans, clan);
		const auto g = Rules.Focus.Groups.find(group);
		if (!c || g == Rules.Focus.Groups.end()) return 1.0f;
		float trait = 0.5f;
		if (const Person* p = c->Folk->People().Find(person))
			if (const auto it = p->Traits.find(g->second.Trait); it != p->Traits.end()) trait = it->second;
		const float M = strong ? Rules.Focus.Strong : Rules.Focus.Soft;
		const float x = std::clamp(Rules.Focus.Faith * DevotionOf(clan, person) + Rules.Focus.Trait * (2.0f * trait - 1.0f), 0.0f, 1.0f);
		return 1.0f + (M - 1.0f) * x;
	}

	DirectorCheck WorldDirector::CanFocus(int clan, const std::string& group, bool strong) const
	{
		DirectorCheck c;
		const auto g = Rules.Focus.Groups.find(group);
		if (g == Rules.Focus.Groups.end())
		{
			c.Why = "no focus '" + group + "' (";
			for (const auto& [id, def] : Rules.Focus.Groups) c.Why += (c.Why.back() == '(' ? "" : ", ") + id;
			c.Why += ")";
			return c;
		}
		c = CanSpend(strong ? "focus_strong" : "focus", clan, true);
		if (!c.Ok) return c;
		const Clan* target = ClanAt(m_To.Clans, clan);
		if (target && group != "food" && Survival(*target->Folk))
		{
			c.Ok = false;
			c.Why = NameOf(*target) + " is hungry (survival): only the focus on food";
		}
		return c;
	}

	DirectorReply WorldDirector::SetFocus(int clan, const std::string& group, bool strong, int days)
	{
		const DirectorCheck can = CanFocus(clan, group, strong);
		if (!can.Ok) return { false, "director focus: cannot: " + can.Why };
		const Clan* c = ClanAt(m_To.Clans, clan);
		days = std::clamp(days, 1, Rules.Focus.MaxDays);
		const std::string name = NameOf(*c);
		if (const DirectorFocus* was = FocusOf(clan); was && was->Group != group && Now() - was->Since < static_cast<double>(Rules.DaySeconds))   // T241: an order against the last within a day
			AddPressure(clan, Rules.PressureContradict, "the god turns " + name + " from " + was->Group + " to " + group + " within a day");
		const DirectorCheck paid = Spend(strong ? "focus_strong" : "focus", clan, true, name + " turns to " + group + " (" + Strength(strong) + ", " + std::to_string(days) + " days)");
		if (!paid.Ok) return { false, "director focus: cannot: " + paid.Why };
		m_Focus.erase(std::remove_if(m_Focus.begin(), m_Focus.end(), [&](const DirectorFocus& f) { return f.Clan == clan; }), m_Focus.end());
		DirectorFocus f;
		f.Clan = clan;
		f.Group = group;
		f.Strong = strong;
		f.Since = Now();
		f.Until = f.Since + static_cast<double>(days) * static_cast<double>(Rules.DaySeconds);
		f.PaidDay = static_cast<int>(std::floor(f.Since / static_cast<double>(Rules.DaySeconds)));
		m_Focus.push_back(f);
		PushBias();
		int follow = 0, alive = 0;
		for (const TribeMember& m : c->Folk->Members())
			if (!m.Dead) { ++alive; if (FocusMultiplier(clan, m.Name, strong, group) > 1.1f) ++follow; }
		return { true, "director focus " + name + " " + group + " " + Strength(strong) + " for " + std::to_string(days) + " days: " + std::to_string(follow) + " of " + std::to_string(alive)
			+ " follow it (cost " + Num(paid.Cost) + ", upkeep " + Num(strong ? Rules.Focus.UpkeepStrong : Rules.Focus.UpkeepSoft) + " a day, influence " + Num(m_Influence) + ")" };
	}

	DirectorReply WorldDirector::EndFocus(int clan, const std::string& why)
	{
		const auto it = std::find_if(m_Focus.begin(), m_Focus.end(), [&](const DirectorFocus& f) { return f.Clan == clan; });
		if (it == m_Focus.end()) return { false, "director focus: the clan has no focus" };
		const Clan* c = ClanAt(m_To.Clans, clan);
		const std::string text = (c ? NameOf(*c) : std::to_string(clan)) + " leaves the focus on " + it->Group + ": " + why;
		m_Focus.erase(it);
		Write("focus_end", c ? NameOf(*c) : std::string("world"), 0.0f, text, clan);
		PushBias();
		return { true, "director focus: " + text };
	}

	// ------------------------------------------------------------------ the calling
	bool WorldDirector::FindPerson(const std::string& person, int& clan) const
	{
		if (!m_To.Clans) return false;
		for (const auto& c : m_To.Clans->All())
		{
			if (!c->Folk) continue;
			const TribeMember* m = c->Folk->FindMember(person);
			if (m && !m->Dead) { clan = c->Id; return true; }
		}
		return false;
	}

	DirectorCallingPreview WorldDirector::PreviewCalling(const std::string& person, const std::string& role) const
	{
		DirectorCallingPreview p;
		p.Person = person;
		p.Role = role;
		const auto cost = Rules.Costs.find("calling");
		p.Cost = cost == Rules.Costs.end() ? 0.0f : cost->second;
		int clan = -1;
		if (!FindPerson(person, clan)) { p.Why = "nobody alive called '" + person + "'"; return p; }
		const Tribe& t = *ClanAt(m_To.Clans, clan)->Folk;
		const TribeMember& m = *t.FindMember(person);
		p.Current = m.Role;
		const auto r = t.Config.Roles.find(role);
		if (r == t.Config.Roles.end() || r->second.Fixed || r->second.Skill.empty()) { p.Why = "no trade '" + role + "'"; return p; }
		if (m.Player) { p.Why = person + " is a player: it chooses its own trade"; return p; }
		if (t.People().IsChild(t.Config.Society, m.Name)) { p.Why = person + " is a child"; return p; }
		if (m.Role == role) { p.Why = person + " is already a " + role; return p; }
		int alive = 0;
		for (const TribeMember& o : t.Members()) if (!o.Dead) ++alive;
		if (alive < t.Config.Work.Roles.MinClan) { p.Why = "the clan is too small to change trades (" + std::to_string(alive) + " of " + std::to_string(t.Config.Work.Roles.MinClan) + ")"; return p; }
		if (const auto own = t.Config.Roles.find(m.Role); own != t.Config.Roles.end() && own->second.Min > 0)   // (the review's gate: the last hands of an essential trade stay)
		{
			int hands = 0;
			for (const TribeMember& o : t.Members()) if (!o.Dead && !o.Player && o.Role == m.Role) ++hands;
			if (hands <= own->second.Min) { p.Why = person + " is one of the last hands the clan keeps as " + m.Role; return p; }
		}
		const DirectorCheck can = CanSpend("calling", clan, true);
		p.Cost = can.Cost;
		if (!can.Ok) { p.Why = can.Why; return p; }
		// the person's own disposition (§3.0): no messenger, no bond, the calling's traits and the clan's culture, no cost
		DirectorBallot b;
		b.Traits = Rules.Calling.Traits;
		for (const auto& [norm, w] : Rules.Vote.Culture) b.Norms[norm] = t.People().Norm(t.Config.Society, norm);
		DirectorVoter v;
		v.Name = person;
		v.Devotion = DevotionOf(clan, person);
		if (const Person* who = t.People().Find(person)) v.Traits = who->Traits;
		p.Disposition = DirectorDisposition(Rules.Vote, b, v);
		p.Accepts = p.Disposition > 0.0f;
		p.Skill = SkillLevel(m.Skills, r->second.Skill) + Rules.Calling.Skill;
		const auto own = t.Config.Roles.find(m.Role);
		p.Own = own == t.Config.Roles.end() || own->second.Skill.empty() ? 0.0f : SkillLevel(m.Skills, own->second.Skill);
		p.Ok = true;
		return p;
	}

	DirectorReply WorldDirector::GiveCalling(const std::string& person, const std::string& role)
	{
		const DirectorCallingPreview p = PreviewCalling(person, role);
		if (!p.Ok) return { false, "director calling: cannot: " + p.Why };
		int clan = -1;
		FindPerson(person, clan);
		const DirectorCheck paid = Spend("calling", clan, true, person + " dreams of the trade of " + role);
		if (!paid.Ok) return { false, "director calling: cannot: " + paid.Why };
		if (!p.Accepts)
		{
			Refund(clan, paid.Cost * Rules.Calling.Refund, person + " pays no heed to the calling");
			return { true, "director calling: " + person + " dreams of the trade of " + role + ", but pays no heed (disposition " + Num(p.Disposition, 2) + "); "
				+ Num(paid.Cost * Rules.Calling.Refund) + " back (influence " + Num(m_Influence) + ")" };
		}
		m_Callings.erase(std::remove_if(m_Callings.begin(), m_Callings.end(), [&](const DirectorCalling& c) { return c.Person == person; }), m_Callings.end());
		DirectorCalling c;
		c.Clan = clan;
		c.Order = m_NextCalling++;
		c.Person = person;
		c.Role = role;
		c.Since = Now();
		c.Until = c.Since + static_cast<double>(Rules.Calling.Days) * static_cast<double>(Rules.DaySeconds);
		m_Callings.push_back(c);
		PushBias();
		return { true, "director calling: " + person + " dreams of the trade of " + role + " and takes it to heart (disposition " + Num(p.Disposition, 2) + ", skill " + Num(p.Skill, 0)
			+ " with the calling against " + Num(p.Own, 0) + " as " + p.Current + "; cost " + Num(paid.Cost) + ", influence " + Num(m_Influence) + ")" };
	}

	// T269 (MODO_COLONIA §6): "change trade", the colony's calling: at once, with no cost and no disposition, and fixed (an endless calling holds
	// it against the review of the trades, TribeBias HeldByCalling) until the player sets it free (`auto`). The gates kept are the ones that make
	// it no trade at all: a dead or unknown person, a child, a player's character, an unknown or fixed trade. In the colony, only the player's clan.
	DirectorReply WorldDirector::ChangeTrade(const std::string& person, const std::string& role)
	{
		int clan = -1;
		if (!FindPerson(person, clan)) return { false, "director role: nobody alive called '" + person + "'" };
		if (m_Colony && clan != ColonyClan()) return { false, "director role: " + person + " is not of your clan" };
		Clan* c = ClanAt(m_To.Clans, clan);
		Tribe& t = *c->Folk;
		const std::string name = person.find(':') == std::string::npos ? person : person.substr(person.find(':') + 1);
		TribeMember* m = nullptr;
		for (TribeMember& o : t.Members()) if (!o.Dead && o.Name == name) m = &o;
		if (!m) return { false, "director role: nobody alive called '" + person + "'" };
		const bool release = role == "auto" || role == "free";
		const auto held = std::find_if(m_Callings.begin(), m_Callings.end(), [&](const DirectorCalling& k) { return k.Person == m->Name; });
		if (release)
		{
			if (held == m_Callings.end() || !held->Fixed) return { false, "director role: " + m->Name + " has no trade fixed by the player" };
			m_Callings.erase(held);
			PushBias();
			Write("role", NameOf(*c), 0.0f, m->Name + " is free to change trade again", clan);
			return { true, "director role: " + m->Name + " keeps the trade of " + m->Role + " but the clan may change it again" };
		}
		const auto r = t.Config.Roles.find(role);
		if (r == t.Config.Roles.end() || r->second.Fixed || r->second.Skill.empty()) return { false, "director role: no trade '" + role + "'" };
		if (m->Player) return { false, "director role: " + m->Name + " is a player: it chooses its own trade" };
		if (t.People().IsChild(t.Config.Society, m->Name)) return { false, "director role: " + m->Name + " is a child" };
		const std::string was = m->Role;
		if (was != role)
		{
			m->Role = role;   // (the review's own change: Tribe::ReviewRoles sets the role and says it)
			t.Note(*m, "takes up the trade of " + role + " (the player's choice)");
		}
		if (held != m_Callings.end()) m_Callings.erase(held);
		DirectorCalling k;
		k.Clan = clan;
		k.Order = m_NextCalling++;
		k.Person = m->Name;
		k.Role = role;
		k.Since = k.Until = Now();
		k.Fixed = true;
		m_Callings.push_back(k);
		PushBias();
		Write("role", NameOf(*c), 0.0f, m->Name + (was == role ? " keeps the trade of " : " takes up the trade of ") + role + ", fixed", clan);
		return { true, "director role: " + m->Name + (was == role ? " keeps the trade of " + role : " was a " + was + " and is a " + role + " now") + ", fixed (director role " + m->Name + " auto lets the clan change it)" };
	}

	// ------------------------------------------------------------------ every update
	void WorldDirector::UpdateFocus()
	{
		if (m_Focus.empty() && m_Callings.empty() && m_Biased.empty()) return;
		const double now = Now();
		const int day = static_cast<int>(std::floor(now / static_cast<double>(Rules.DaySeconds)));
		bool changed = false;
		for (size_t i = 0; i < m_Focus.size();)
		{
			DirectorFocus& f = m_Focus[i];
			Clan* c = ClanAt(m_To.Clans, f.Clan);
			if (!c) { m_Focus.erase(m_Focus.begin() + static_cast<long>(i)); changed = true; continue; }
			if (now >= f.Until) { EndFocus(f.Clan, "its days are over"); continue; }   // (EndFocus erases it and writes the bias)
			const bool paused = f.Group != "food" && Survival(*c->Folk);
			if (paused != f.Paused) { f.Paused = paused; changed = true; }
			bool ended = false;
			while (f.PaidDay < day)
			{
				++f.PaidDay;
				changed = true;   // (a new day: the devotions moved, m(p) again)
				if (f.Paused) continue;   // (paused: no upkeep, no drift)
				const float upkeep = f.Strong ? Rules.Focus.UpkeepStrong : Rules.Focus.UpkeepSoft;
				if (m_Influence + 1e-4f < upkeep) { EndFocus(f.Clan, "not enough influence for its upkeep"); ended = true; break; }
				m_Influence = std::max(0.0f, m_Influence - upkeep);
				Write("focus_upkeep", NameOf(*c), upkeep, "the focus on " + f.Group + " goes on", f.Clan);
				const auto g = Rules.Focus.Groups.find(f.Group);
				if (g != Rules.Focus.Groups.end() && !g->second.Norm.empty() && Rules.Focus.Drift > 0.0f)
					c->Folk->People().NudgeNorm(c->Folk->Config.Society, g->second.Norm, Rules.Focus.Drift);   // the mark on the culture (as a rite's norms)
			}
			if (!ended) ++i;
		}
		for (size_t i = 0; i < m_Callings.size();)
		{
			const DirectorCalling& c = m_Callings[i];
			int clan = -1;
			if ((!c.Fixed && now >= c.Until) || !FindPerson(c.Person, clan) || clan != c.Clan) { m_Callings.erase(m_Callings.begin() + static_cast<long>(i)); changed = true; continue; }
			++i;
		}
		if (changed) PushBias();
	}

	void WorldDirector::PushBias()
	{
		if (!m_To.Clans) return;
		std::vector<int> clans = m_Biased;
		for (const DirectorFocus& f : m_Focus) clans.push_back(f.Clan);
		for (const DirectorCalling& c : m_Callings) clans.push_back(c.Clan);
		std::sort(clans.begin(), clans.end());
		clans.erase(std::unique(clans.begin(), clans.end()), clans.end());
		m_Biased.clear();
		for (const int id : clans)
		{
			Clan* c = ClanAt(m_To.Clans, id);
			if (!c) continue;
			TribeBias bias;
			if (const DirectorFocus* f = FocusOf(id); f && !f->Paused)
				if (const auto g = Rules.Focus.Groups.find(f->Group); g != Rules.Focus.Groups.end())
				{
					bias.Focus.Group = f->Group;
					bias.Focus.Kinds = g->second.Kinds;
					bias.Focus.FoodGather = g->second.FoodGather;
					bias.Focus.OtherGather = g->second.OtherGather;
					for (const TribeMember& m : c->Folk->Members())
						if (!m.Dead) if (const float k = FocusMultiplier(id, m.Name, f->Strong, f->Group); k > 1.0f) bias.Focus.Mult[m.Name] = k;
				}
			for (const DirectorCalling& k : m_Callings)
				if (k.Clan == id) bias.Callings[k.Person] = { k.Role, Rules.Calling.Skill, k.Order };
			const bool any = bias.Focus.Active() || !bias.Callings.empty();
			c->Folk->Bias = std::move(bias);
			if (any) m_Biased.push_back(id);
		}
	}

	// ------------------------------------------------------------------ orders
	DirectorReply WorldDirector::FocusCommand(const std::vector<std::string>& a)
	{
		// director focus [preview] <clan> <group>|off [soft|strong] [days]; director focus (the list)
		if (!m_To.Clans) return { false, "director: no clans" };
		if (a.size() < 2)
		{
			std::string s = "director focus: " + std::to_string(m_Focus.size()) + " active";
			for (const DirectorFocus& f : m_Focus)
			{
				const Clan* c = ClanAt(m_To.Clans, f.Clan);
				s += "\nclan " + std::to_string(f.Clan) + " " + (c ? NameOf(*c) : std::string()) + ": " + f.Group + " " + Strength(f.Strong) + ", "
					+ Num(std::max(0.0, f.Until - Now()) / static_cast<double>(Rules.DaySeconds), 2) + " days left" + (f.Paused ? " (paused: survival)" : "");
			}
			return { true, s };
		}
		const bool preview = a[1] == "preview";
		const size_t at = preview ? 2 : 1;
		if (a.size() < at + 2) return { false, "usage: director focus [preview] <clan> <group>|off [soft|strong] [days]" };
		const Clan* c = m_To.Clans->Find(a[at]);
		if (!c || !c->Folk) return { false, "director focus: no clan '" + a[at] + "'" };
		const std::string group = a[at + 1];
		if (group == "off" || group == "none") return EndFocus(c->Id, "the god lets it go");
		bool strong = false;
		int days = 3;
		for (size_t i = at + 2; i < a.size(); ++i)
		{
			if (a[i] == "strong") strong = true;
			else if (a[i] == "soft") strong = false;
			else
			{
				char* end = nullptr;
				const long n = std::strtol(a[i].c_str(), &end, 10);
				if (end == a[i].c_str() || *end != '\0' || n < 1 || n > Rules.Focus.MaxDays) return { false, "director focus: days: expected 1.." + std::to_string(Rules.Focus.MaxDays) };
				days = static_cast<int>(n);
			}
		}
		if (preview)
		{
			const DirectorCheck can = CanFocus(c->Id, group, strong);
			int follow = 0, alive = 0;
			for (const TribeMember& m : c->Folk->Members())
				if (!m.Dead) { ++alive; if (FocusMultiplier(c->Id, m.Name, strong, group) > 1.1f) ++follow; }
			const float upkeep = strong ? Rules.Focus.UpkeepStrong : Rules.Focus.UpkeepSoft;
			return { can.Ok, "director focus preview " + NameOf(*c) + " " + group + " " + Strength(strong) + ": " + (can.Ok ? std::string("can") : "cannot: " + can.Why) + "; cost " + Num(can.Cost)
				+ " + " + Num(upkeep) + " a day; " + std::to_string(follow) + " of " + std::to_string(alive) + " would follow it" };
		}
		return SetFocus(c->Id, group, strong, days);
	}

	DirectorReply WorldDirector::CallingCommand(const std::vector<std::string>& a)
	{
		// director calling [preview] <person> <role>; director calling (the list)
		if (a.size() < 2)
		{
			std::string s = "director calling: " + std::to_string(m_Callings.size()) + " running";
			for (const DirectorCalling& c : m_Callings)
				s += "\n" + c.Person + " -> " + c.Role + ", " + (c.Fixed ? std::string("fixed by the player") : Num(std::max(0.0, c.Until - Now()) / static_cast<double>(Rules.DaySeconds), 2) + " days left");
			return { true, s };
		}
		const bool preview = a[1] == "preview";
		const size_t at = preview ? 2 : 1;
		if (a.size() < at + 2) return { false, "usage: director calling [preview] <person> <role> | calling [preview] <clan> devout|curious <role>" };
		std::string person = a[at], role = a[at + 1];
		if (a.size() >= at + 3 && (a[at + 1] == "devout" || a[at + 1] == "curious"))   // the god names a kind of person: the same one on any seed
		{
			const Clan* c = m_To.Clans ? m_To.Clans->Find(a[at]) : nullptr;
			if (!c || !c->Folk) return { false, "director calling: no clan '" + a[at] + "'" };
			role = a[at + 2];
			const Tribe& t = *c->Folk;
			float best = -1.0f;
			person.clear();
			for (const TribeMember& m : t.Members())   // (in the order of the members; ties go to the name first in order)
			{
				if (m.Dead || m.Player || m.Role == role || t.People().IsChild(t.Config.Society, m.Name)) continue;
				float v = DevotionOf(c->Id, m.Name);
				if (a[at + 1] == "curious")
					if (const Person* p = t.People().Find(m.Name)) { const auto it = p->Traits.find("curiosity"); v *= it == p->Traits.end() ? 0.5f : it->second; }
				if (v > best + 1e-6f || (std::fabs(v - best) <= 1e-6f && m.Name < person)) { best = v; person = m.Name; }
			}
			if (person.empty()) return { false, "director calling: nobody in " + NameOf(*c) + " to call to " + role };
		}
		if (!preview) return GiveCalling(person, role);
		const DirectorCallingPreview p = PreviewCalling(person, role);
		if (!p.Ok) return { false, "director calling preview " + person + " " + role + ": cannot: " + p.Why };
		return { true, "director calling preview " + p.Person + " " + p.Role + ": " + (p.Accepts ? "would accept" : "would not") + " (disposition " + Num(p.Disposition, 2) + ", skill "
			+ Num(p.Skill, 0) + " with the calling against " + Num(p.Own, 0) + " as " + p.Current + "; cost " + Num(p.Cost) + ")" };
	}

	// ------------------------------------------------------------------ saving
	std::string WorldDirector::FocusToJson() const
	{
		if (m_Focus.empty() && m_Callings.empty()) return {};
		Json j;
		Json focus = Json::array(), callings = Json::array();
		for (const DirectorFocus& f : m_Focus)
			focus.push_back({ { "clan", f.Clan }, { "group", f.Group }, { "strong", f.Strong }, { "since", f.Since }, { "until", f.Until }, { "paidDay", f.PaidDay }, { "paused", f.Paused } });
		for (const DirectorCalling& c : m_Callings)
		{
			callings.push_back({ { "clan", c.Clan }, { "order", c.Order }, { "person", c.Person }, { "role", c.Role }, { "since", c.Since }, { "until", c.Until } });
			if (c.Fixed) callings.back()["fixed"] = true;   // T269 (only then: a god's calling saves as before)
		}
		j["clans"] = focus;
		j["callings"] = callings;
		j["nextCalling"] = m_NextCalling;
		return j.dump();
	}

	bool WorldDirector::FocusFromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		m_Focus.clear();
		m_Callings.clear();
		if (const auto it = j.find("clans"); it != j.end())
			for (const Json& e : *it)
			{
				DirectorFocus f;
				f.Clan = e.value("clan", -1);
				f.Group = e.value("group", std::string());
				f.Strong = e.value("strong", false);
				f.Since = e.value("since", 0.0);
				f.Until = e.value("until", 0.0);
				f.PaidDay = e.value("paidDay", 0);
				f.Paused = e.value("paused", false);
				m_Focus.push_back(f);
			}
		if (const auto it = j.find("callings"); it != j.end())
			for (const Json& e : *it)
			{
				DirectorCalling c;
				c.Clan = e.value("clan", -1);
				c.Order = e.value("order", 0);
				c.Person = e.value("person", std::string());
				c.Role = e.value("role", std::string());
				c.Since = e.value("since", 0.0);
				c.Until = e.value("until", 0.0);
				c.Fixed = e.value("fixed", false);   // T269
				m_Callings.push_back(c);
			}
		m_NextCalling = j.value("nextCalling", 1);
		return true;
	}
}
