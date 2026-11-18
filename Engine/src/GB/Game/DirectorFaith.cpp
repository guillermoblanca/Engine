#include "GB/Game/WorldDirector.h"
#include "GB/Game/Clans.h"
#include "GB/Game/History.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

// D43 (the balance of the faith), T241 (the abuse: tired, skeptics, heresy; MODO_DIOS_DECISIONES.md §4.4) and §6.2 A (the believers and the
// defeat). The faith of a clan in the god is the director's own (WorldDirector.h, DirectorFaithRules): the clans' beliefs are the simulation's
// and nothing here writes them. Under the pressure nothing of a clan changes; at heresy the skeptics are marked in the director's book (never in
// the Tribe) and a third of the clan skeptic for a season splits it (ClanWorld::Split with the cause "heresy").
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
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }
		bool Adult(const Tribe& t, const TribeMember& m) { return !m.Dead && !m.Player && !t.People().IsChild(t.Config.Society, m.Name); }
		int Living(const Tribe& t)
		{
			int n = 0;
			for (const TribeMember& m : t.Members()) if (!m.Dead) ++n;
			return n;
		}
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

	// { "faith": { ..., "clan": { "start", "grow", "decay", "rite", "omen", "fulfilled", "hand", "refused", "tired" } },
	//   "skeptic": { "devotion", "boldness", "share", "days", "faith" }, "defeat": { "graceDays" } }
	std::string ReadDirectorFaithRules(const Json& d, DirectorRules& r)
	{
		std::string e;
		if (const auto f = d.find("faith"); f != d.end() && f->is_object())
			if (const auto it = f->find("clan"); it != f->end())
			{
				const std::string w = "director: faith: clan: ";
				if (!it->is_object()) return w + "expected an object";
				if (!Known(*it, { "start", "grow", "decay", "rite", "omen", "fulfilled", "hand", "refused", "tired" }, w, e)) return e;
				DirectorFaithRules& c = r.Faith;
				if (!Number(*it, "start", c.Start, 0.0f, 1.0f, w, e) || !Number(*it, "grow", c.Grow, 0.0f, 1.0f, w, e) || !Number(*it, "decay", c.Decay, 0.0f, 1.0f, w, e)
					|| !Number(*it, "rite", c.Rite, 0.0f, 1.0f, w, e) || !Number(*it, "omen", c.Omen, 0.0f, 1.0f, w, e) || !Number(*it, "fulfilled", c.Fulfilled, 0.0f, 1.0f, w, e)
					|| !Number(*it, "hand", c.Hand, 0.0f, 1.0f, w, e) || !Number(*it, "refused", c.Refused, 0.0f, 1.0f, w, e) || !Number(*it, "tired", c.Tired, 0.0f, 1.0f, w, e)) return e;
			}
		if (const auto it = d.find("skeptic"); it != d.end())
		{
			const std::string w = "director: skeptic: ";
			if (!it->is_object()) return w + "expected an object";
			if (!Known(*it, { "devotion", "boldness", "share", "days", "faith", "ease", "minGroup", "gapDays" }, w, e)) return e;
			DirectorSkepticRules& s = r.Skeptic;
			if (!Number(*it, "devotion", s.Devotion, 0.0f, 1.0f, w, e) || !Number(*it, "boldness", s.Boldness, 0.0f, 1.0f, w, e) || !Number(*it, "share", s.Share, 0.0f, 1.0f, w, e)
				|| !Number(*it, "days", s.Days, 0.0f, 100.0f, w, e) || !Number(*it, "faith", s.Faith, 0.0f, 1.0f, w, e) || !Number(*it, "ease", s.Ease, 0.0f, 1.0f, w, e)
				|| !Number(*it, "gapDays", s.GapDays, 0.0f, 1000.0f, w, e)) return e;
			if (const auto n = it->find("minGroup"); n != it->end())   // D49
			{
				if (!n->is_number_integer() || n->get<int>() < 2 || n->get<int>() > 100) return w + "minGroup: expected an integer in 2..100";
				s.MinGroup = n->get<int>();
			}
		}
		if (const auto it = d.find("defeat"); it != d.end())
		{
			const std::string w = "director: defeat: ";
			if (!it->is_object()) return w + "expected an object";
			if (!Known(*it, { "graceDays" }, w, e) || !Number(*it, "graceDays", r.Defeat.GraceDays, 0.0f, 1000.0f, w, e)) return e;
		}
		return {};
	}

	// ------------------------------------------------------------------ D43: the faith
	void WorldDirector::FaithGain(int clan, float amount)
	{
		if (amount <= 0.0f || !ClanAt(m_To.Clans, clan)) return;
		const float f = FaithOf(clan);
		const float scale = Tired(clan) ? Rules.Faith.Tired : 1.0f;
		m_Faith[clan] = std::clamp(f + scale * amount * (1.0f - f), 0.0f, 1.0f);
	}

	void WorldDirector::UpdateFaith(int days)
	{
		if (!m_To.Clans || days <= 0) return;
		for (const auto& c : m_To.Clans->All())
		{
			if (!c->Folk) continue;
			const auto it = m_Faith.find(c->Id);
			float f = it == m_Faith.end() ? Rules.Faith.Start : it->second;
			if (Living(*c->Folk) == 0) { m_Faith[c->Id] = f; continue; }   // (a clan of nobody keeps what it had: nobody is left to believe)
			const float belief = BeliefOf(c->Id);
			const float scale = Tired(c->Id) ? Rules.Faith.Tired : 1.0f;
			float rites = std::min(1.0f, m_RiteShare.count(c->Id) ? m_RiteShare[c->Id] : 0.0f);   // (the rites of the day that turned)
			for (int d = 0; d < days; ++d)
			{
				f = std::clamp(f + scale * (Rules.Faith.Grow * belief + Rules.Faith.Rite * rites) * (1.0f - f) - Rules.Faith.Decay * f, 0.0f, 1.0f);
				rites = 0.0f;
			}
			m_Faith[c->Id] = f;
		}
		m_RiteShare.clear();
		m_OmenDay.clear();
	}

	void WorldDirector::Refused(int clan, const std::string& what, const std::string& text)
	{
		++m_Refusals;
		if (!ClanAt(m_To.Clans, clan)) return;
		if (!m_Sandbox) m_Faith[clan] = std::max(0.0f, FaithOf(clan) - Rules.Faith.Refused);
		AddPressure(clan, Rules.PressureMurmur, "the clan murmurs against the " + what + ": " + text);
	}

	// ------------------------------------------------------------------ T241: the abuse
	bool WorldDirector::IsSkeptic(int clan, const std::string& person) const
	{
		for (const DirectorSkeptic& k : m_Skeptics) if (k.Clan == clan && k.Person == person) return true;
		return false;
	}

	void WorldDirector::Chronicle(int clan, const std::string& kind, const std::string& who, const std::string& text)
	{
		if (!m_To.Clans) return;
		WorldHistory& past = m_To.Clans->Past();
		HistoryEvent e;
		e.At = past.HasClock() ? past.Clock() : Now();
		e.Kind = kind;
		e.Who = who;
		e.Clan = clan;
		e.Text = text;
		e.Weight = 3;   // (the chronicle's default filter shows it)
		if (const Clan* c = m_To.Clans->Find(clan); c && c->Folk && !who.empty())
			if (const TribeMember* m = c->Folk->FindMember(who); m && m->Body) { e.Where = m->Body->Position(); e.HasWhere = true; }
		past.Add(e);
	}

	void WorldDirector::UpdateAbuse()
	{
		if (!m_To.Clans || (m_Pressure.empty() && m_Skeptics.empty() && m_Level.empty() && m_HeresySince.empty())) return;
		const double now = Now();
		// the skeptics who died or left their clan are no longer the clan's
		m_Skeptics.erase(std::remove_if(m_Skeptics.begin(), m_Skeptics.end(), [&](const DirectorSkeptic& k)
			{
				const Clan* c = ClanAt(m_To.Clans, k.Clan);
				const TribeMember* m = c ? c->Folk->FindMember(k.Person) : nullptr;
				return !m || m->Dead;
			}), m_Skeptics.end());
		std::vector<int> clans;
		for (const auto& [id, p] : m_Pressure) clans.push_back(id);
		for (const auto& [id, l] : m_Level) clans.push_back(id);
		for (const DirectorSkeptic& k : m_Skeptics) clans.push_back(k.Clan);
		std::sort(clans.begin(), clans.end());
		clans.erase(std::unique(clans.begin(), clans.end()), clans.end());
		std::vector<int> split;
		for (const int id : clans)
		{
			Clan* c = ClanAt(m_To.Clans, id);
			if (!c) { m_Level.erase(id); m_HeresySince.erase(id); continue; }
			Tribe& t = *c->Folk;
			const std::string name = NameOf(*c);
			// the level the clan is at: told once when it rises (the chronicle) and forgotten when it falls back
			const std::string level = PressureLevel(id);
			const std::string was = m_Level.count(id) ? m_Level[id] : std::string("low");
			if (level != was)
			{
				if (level == "low") m_Level.erase(id);
				else m_Level[id] = level;
				if (was == "low" && level != "low")
				{
					t.NoteClan("no longer listens so closely to the dreams");
					Chronicle(id, "tired", std::string(), "the " + name + " no longer listen so closely to the dreams");
					Write("tired", name, 0.0f, name + " is tired of the god (pressure " + Num(PressureOf(id)) + ")", id);
				}
			}
			// heresy: the adults of little devotion and bold enough turn skeptics (one look a day is enough: each update is cheap)
			if (level == "heresy")
			{
				std::string dreamer;   // whose dream they laugh at: the clan's last messenger or dreamer, else its shaman
				double latest = -1.0;
				for (const DirectorSign& s : m_Signs) if (s.Clan == id && !s.Messenger.empty() && s.Posted > latest) { latest = s.Posted; dreamer = s.Messenger; }
				for (const DirectorVision& v : m_Visions) if (v.Clan == id && !v.Dreamer.empty() && v.Since > latest) { latest = v.Since; dreamer = v.Dreamer; }
				if (dreamer.empty()) dreamer = t.Social().Shaman;
				std::vector<const TribeMember*> members;
				for (const TribeMember& m : t.Members()) if (Adult(t, m)) members.push_back(&m);
				std::sort(members.begin(), members.end(), [](const TribeMember* a, const TribeMember* b) { return a->Name < b->Name; });
				const float boldness = Rules.Skeptic.Boldness - Rules.Skeptic.Ease * std::max(0.0f, PressureOf(id) - Rules.PressureHeresy);
				for (const TribeMember* m : members)
				{
					if (IsSkeptic(id, m->Name) || m->Name == dreamer) continue;
					const float devotion = DevotionOf(id, m->Name);
					float bold = 0.5f;
					if (const Person* p = t.People().Find(m->Name)) if (const auto b = p->Traits.find("boldness"); b != p->Traits.end()) bold = b->second;
					if (devotion >= Rules.Skeptic.Devotion || bold <= boldness) continue;
					m_Skeptics.push_back({ id, m->Name, now, devotion });
					const std::string line = dreamer.empty() ? std::string("laughs at the god's dreams") : "laughs at the dream of " + dreamer;
					t.Note(*m, line);
					Chronicle(id, "skeptic", m->Name, m->Name + " (" + m->Role + ") " + line);
					Write("skeptic", name, 0.0f, m->Name + " of " + name + " turns skeptic (devotion " + Num(devotion, 2) + ")", id);
				}
			}
			// the split: a third of the adults skeptic for a season; D49: a group of MinGroup at least (and a clan big enough that they can go
			// with half of it staying, as ClanWorld::Split keeps it), and GapDays since the clan's last heresy
			int adults = 0, skeptics = 0;
			for (const TribeMember& m : t.Members())
				if (Adult(t, m)) { ++adults; if (IsSkeptic(id, m.Name)) ++skeptics; }
			const int alive = Living(t);
			const int canGo = alive - std::max(2, (alive + 1) / 2);
			const auto last = m_HeresyLast.find(id);
			const bool rested = last == m_HeresyLast.end() || now - last->second >= static_cast<double>(Rules.Skeptic.GapDays * Rules.DaySeconds);
			if (skeptics >= Rules.Skeptic.MinGroup && canGo >= Rules.Skeptic.MinGroup && rested && adults > 0 && static_cast<float>(skeptics) >= Rules.Skeptic.Share * static_cast<float>(adults))
			{
				if (!m_HeresySince.count(id)) m_HeresySince[id] = now;
				else if (now - m_HeresySince[id] >= static_cast<double>(Rules.Skeptic.Days * Rules.DaySeconds)) split.push_back(id);
			}
			else m_HeresySince.erase(id);
		}
		// (after the loop: a split founds a clan and the clans' list grows)
		for (const int id : split)
		{
			Clan* c = ClanAt(m_To.Clans, id);
			if (!c) continue;
			const std::string name = NameOf(*c);
			SplitCause cause;
			cause.Why = "heresy: they laugh at the god's dreams";
			for (const DirectorSkeptic& k : m_Skeptics) if (k.Clan == id) cause.Members.push_back(k.Person);
			cause.MinGroup = Rules.Skeptic.MinGroup;   // D49
			std::vector<int> before;
			for (const auto& o : m_To.Clans->All()) before.push_back(o->Id);
			const std::string why = m_To.Clans->Split(id, true, &cause);
			if (!why.empty())
			{
				m_HeresySince[id] = now;   // (it cannot now: a season more)
				Write("heresy", name, 0.0f, "the skeptics of " + name + " cannot leave: " + why, id);
				continue;
			}
			m_HeresySince.erase(id);
			m_HeresyLast[id] = now;   // D49: the clan rests GapDays before another heresy
			++m_HeresySplits;
			int founded = -1;
			for (const auto& o : m_To.Clans->All()) if (std::find(before.begin(), before.end(), o->Id) == before.end()) founded = o->Id;
			if (founded >= 0)
			{
				m_HeresyLast[founded] = now;   // (and so does the clan of the heretics)
				m_Faith[founded] = Rules.Skeptic.Faith;   // the heretics hardly believe
				const Clan* nc = ClanAt(m_To.Clans, founded);
				Write("heresy", name, 0.0f, "the skeptics of " + name + " leave to found the " + (nc ? NameOf(*nc) : std::string("new clan")), id);
			}
			// the loudest are gone (a clan of heretics needs no mark), the ones who stayed keep quiet; the pressure falls by half
			m_Skeptics.erase(std::remove_if(m_Skeptics.begin(), m_Skeptics.end(), [&](const DirectorSkeptic& k) { return k.Clan == id; }), m_Skeptics.end());
			if (const auto p = m_Pressure.find(id); p != m_Pressure.end()) p->second *= 0.5f;
		}
	}

	// ------------------------------------------------------------------ §6.2 A: the believers and the defeat
	DirectorBelievers WorldDirector::Believers() const
	{
		DirectorBelievers b;
		if (!m_To.Clans) return b;
		for (const auto& c : m_To.Clans->All())
		{
			if (!c->Folk) continue;
			const int alive = Living(*c->Folk);
			if (alive == 0 || FaithOf(c->Id) < Rules.MinFaith) continue;
			++b.Clans;
			b.People += alive;
		}
		return b;
	}

	void WorldDirector::CheckDefeat()
	{
		if (!m_Stakes || Defeated() || !m_Looked || !m_To.Clans) return;
		const double now = Now();
		if (m_Colony)   // T269 (C-4): the colony is lost when the player's clan has nobody alive, or is no more; faith has nothing to do with it
		{
			if (m_ColonyClan < 0) return;   // (no clan was ever the player's yet: a world still empty)
			const Clan* mine = m_To.Clans->Find(m_ColonyClan);
			if (mine && mine->Folk && Living(*mine->Folk) > 0) return;
			m_DefeatAt = now;
			m_Book.push_back({ now, "defeat", "world", "your clan is gone: nobody of it is alive", 0.0f });
			return;
		}
		if (now < static_cast<double>(Rules.Defeat.GraceDays * Rules.DaySeconds)) return;
		if (Believers().Clans > 0) return;
		m_DefeatAt = now;
		m_Book.push_back({ now, "defeat", "world", "nobody believes in you any more", 0.0f });   // (the book only: no row of telemetry, no event of the history)
	}

	DirectorReply WorldDirector::DefeatCommand(const std::vector<std::string>& a)
	{
		const DirectorBelievers b = Believers();
		const std::string believers = "believers: " + std::to_string(b.Clans) + " clans, " + std::to_string(b.People) + " people";
		if (a[0] == "believers") return { true, believers };
		const std::string day = Num(m_DefeatAt / static_cast<double>(Rules.DaySeconds), 2);
		if (a.size() > 1 && a[1] == "status")
			return { true, "director defeat: " + (Defeated() ? "on day " + day + ": " + (m_Colony ? "your clan is gone" : "nobody believes in you any more") : std::string("none")) + (m_Stakes ? "" : " (no stakes: this game cannot be lost)") + "; " + believers };
		if (a.size() > 1 && a[1] == "watch")   // [Watch the world]: the god goes on looking, without the hand
		{
			if (!Defeated()) return { false, "director defeat watch: the game is not lost" };
			m_DefeatSeen = true;
			return { true, "director defeat: the god watches the world, without the hand" };
		}
		if (Defeated()) return { true, "director defeat: on day " + day + ": " + (m_Colony ? "your clan is gone" : "nobody believes in you any more") };
		if (!m_Sandbox) return { false, "director defeat: only in the sandbox (director sandbox on); status: " + believers };
		m_DefeatAt = Now();
		m_Book.push_back({ m_DefeatAt, "defeat", "world", "nobody believes in you any more (the sandbox)", 0.0f });
		return { true, "director defeat: nobody believes in you any more (day " + Num(m_DefeatAt / static_cast<double>(Rules.DaySeconds), 2) + ")" };
	}

	std::string WorldDirector::ReportLine() const
	{
		std::string s = "director faith:";
		if (m_To.Clans)
			for (const auto& c : m_To.Clans->All())
			{
				if (!c->Folk) continue;
				int skeptics = 0;
				for (const DirectorSkeptic& k : m_Skeptics) if (k.Clan == c->Id) ++skeptics;
				s += " " + NameOf(*c) + " " + Num(FaithOf(c->Id), 2) + " (alive " + std::to_string(Living(*c->Folk)) + ", pressure " + Num(PressureOf(c->Id)) + ", skeptics " + std::to_string(skeptics) + ");";
			}
		const DirectorBelievers b = Believers();
		s += "\ndirector: votes " + std::to_string(m_Votes) + ", refused " + std::to_string(m_Refusals) + "; skeptics " + std::to_string(m_Skeptics.size()) + "; heresy splits "
			+ std::to_string(m_HeresySplits) + "; believers " + std::to_string(b.Clans) + " clans, " + std::to_string(b.People) + " people; defeat: "
			+ (Defeated() ? "day " + Num(m_DefeatAt / static_cast<double>(Rules.DaySeconds), 2) : std::string("none"));
		return s;
	}

	// ------------------------------------------------------------------ saving
	std::string WorldDirector::FaithToJson() const
	{
		if (m_Faith.empty() && m_Skeptics.empty() && !m_Stakes && !Defeated() && m_Votes == 0) return {};
		Json j;
		Json faith = Json::object(), since = Json::object(), level = Json::object(), skeptics = Json::array();
		Json rites = Json::object();
		for (const auto& [c, f] : m_Faith) faith[std::to_string(c)] = f;
		for (const auto& [c, r] : m_RiteShare) rites[std::to_string(c)] = r;
		Json omens = Json::object();
		for (const auto& [c, n] : m_OmenDay) omens[std::to_string(c)] = n;
		j["omens"] = omens;
		for (const auto& [c, t] : m_HeresySince) since[std::to_string(c)] = t;
		if (!m_HeresyLast.empty())   // D49 (a save with no heresy stays as it was)
		{
			Json last = Json::object();
			for (const auto& [c, t] : m_HeresyLast) last[std::to_string(c)] = t;
			j["heresyLast"] = last;
		}
		for (const auto& [c, l] : m_Level) level[std::to_string(c)] = l;
		for (const DirectorSkeptic& k : m_Skeptics) skeptics.push_back({ { "clan", k.Clan }, { "person", k.Person }, { "since", k.Since }, { "devotion", k.Devotion } });
		j["clans"] = faith;
		j["rites"] = rites;
		j["heresySince"] = since;
		j["level"] = level;
		j["skeptics"] = skeptics;
		j["heresySplits"] = m_HeresySplits;
		j["refusals"] = m_Refusals;
		j["votes"] = m_Votes;
		j["stakes"] = m_Stakes;
		j["defeatAt"] = m_DefeatAt;
		j["defeatSeen"] = m_DefeatSeen;
		return j.dump();
	}

	bool WorldDirector::FaithFromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		m_Faith.clear();
		m_RiteShare.clear();
		m_HeresySince.clear();
		m_Level.clear();
		m_Skeptics.clear();
		if (const auto it = j.find("clans"); it != j.end()) for (const auto& [k, v] : it->items()) m_Faith[std::stoi(k)] = v.get<float>();
		if (const auto it = j.find("rites"); it != j.end()) for (const auto& [k, v] : it->items()) m_RiteShare[std::stoi(k)] = v.get<float>();
		m_OmenDay.clear();
		if (const auto it = j.find("omens"); it != j.end()) for (const auto& [k, v] : it->items()) m_OmenDay[std::stoi(k)] = v.get<int>();
		if (const auto it = j.find("heresySince"); it != j.end()) for (const auto& [k, v] : it->items()) m_HeresySince[std::stoi(k)] = v.get<double>();
		m_HeresyLast.clear();
		if (const auto it = j.find("heresyLast"); it != j.end()) for (const auto& [k, v] : it->items()) m_HeresyLast[std::stoi(k)] = v.get<double>();   // D49
		if (const auto it = j.find("level"); it != j.end()) for (const auto& [k, v] : it->items()) m_Level[std::stoi(k)] = v.get<std::string>();
		if (const auto it = j.find("skeptics"); it != j.end())
			for (const Json& e : *it) m_Skeptics.push_back({ e.value("clan", -1), e.value("person", std::string()), e.value("since", 0.0), e.value("devotion", 0.0f) });
		m_HeresySplits = j.value("heresySplits", 0);
		m_Refusals = j.value("refusals", 0);
		m_Votes = j.value("votes", 0);
		m_Stakes = j.value("stakes", false);
		m_DefeatAt = j.value("defeatAt", -1.0);
		m_DefeatSeen = j.value("defeatSeen", false);
		return true;
	}
}
