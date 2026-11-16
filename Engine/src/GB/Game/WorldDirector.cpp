#include "GB/Game/WorldDirector.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include "GB/Telemetry/TelemetrySink.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

// T195: the director of the world (see WorldDirector.h). Reads the clans, pays the interventions, keeps the book.
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
		std::string WaitKey(const std::string& action, int clan) { return action + ":" + std::to_string(clan); }
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }   // (the editor's first clan has no name of its own)

		// a number of the section: present, a number, within [lo, hi]
		bool ReadNumber(const Json& o, const char* key, float& out, float lo, float hi, const std::string& where, std::string& error)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_number() || it->get<double>() < lo || it->get<double>() > hi)
			{
				error = where + key + ": expected a number in " + Num(lo, 2) + ".." + Num(hi, 2);
				return false;
			}
			out = it->get<float>();
			return true;
		}
		bool ReadMap(const Json& o, const char* key, std::map<std::string, float>& out, const std::string& where, std::string& error)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_object()) { error = where + key + ": expected an object"; return false; }
			std::map<std::string, float> m = out;
			for (const auto& [k, v] : it->items())
			{
				if (!v.is_number() || v.get<double>() < 0.0 || v.get<double>() > 1000.0) { error = where + key + ": " + k + ": expected a number in 0..1000"; return false; }
				m[k] = v.get<float>();
			}
			out = std::move(m);
			return true;
		}
		bool KnownKeys(const Json& o, std::initializer_list<const char*> keys, const std::string& where, std::string& error)
		{
			for (const auto& [k, v] : o.items())
				if (std::none_of(keys.begin(), keys.end(), [&](const char* known) { return k == known; })) { error = where + "unknown key '" + k + "'"; return false; }
			return true;
		}
	}

	DirectorRules DefaultDirectorRules() { return DirectorRules{}; }
	std::string ReadDirectorFocusRules(const nlohmann::json& d, DirectorRules& r);   // T238/T239: "focus" and "calling" (DirectorFocus.cpp)

	std::string LoadDirectorRules(const std::string& json, DirectorRules& live)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("director");
		if (section == doc.end()) return {};
		if (!section->is_object()) return "director: expected an object";
		const Json& d = *section;
		DirectorRules r = live;
		std::string e;
		const std::string w = "director: ";
		if (!KnownKeys(d, { "start", "max", "minFaith", "faith", "sandboxPerDay", "costs", "waits", "pressure", "logMax", "daySeconds", "vote", "messenger", "sign", "focus", "calling" }, w, e)) return e;
		if (e = ReadDirectorFocusRules(d, r); !e.empty()) return e;
		if (const auto it = d.find("vote"); it != d.end())   // T236
		{
			if (!it->is_object()) return w + "vote: expected an object";
			const std::string wv = w + "vote: ";
			if (!KnownKeys(*it, { "base", "faith", "affinity", "shaman", "survival", "majority", "doubtBand", "retries", "culture" }, wv, e)) return e;
			DirectorVoteRules& v = r.Vote;
			if (!ReadNumber(*it, "base", v.Base, -10.0f, 10.0f, wv, e) || !ReadNumber(*it, "faith", v.Faith, 0.0f, 10.0f, wv, e) || !ReadNumber(*it, "affinity", v.Affinity, 0.0f, 10.0f, wv, e)
				|| !ReadNumber(*it, "shaman", v.Shaman, 0.0f, 10.0f, wv, e) || !ReadNumber(*it, "survival", v.Survival, 0.0f, 10.0f, wv, e) || !ReadNumber(*it, "majority", v.Majority, 0.0f, 1.0f, wv, e)
				|| !ReadNumber(*it, "doubtBand", v.DoubtBand, 0.0f, 1.0f, wv, e) || !ReadMap(*it, "culture", v.Culture, wv, e)) return e;
			if (const auto n = it->find("retries"); n != it->end())
			{
				if (!n->is_number_integer() || n->get<int>() < 0 || n->get<int>() > 10) return wv + "retries: expected an integer in 0..10";
				v.Retries = n->get<int>();
			}
			if (v.DoubtBand > v.Majority) return wv + "doubtBand: more than majority";
		}
		if (const auto it = d.find("messenger"); it != d.end())   // T237
		{
			if (!it->is_object()) return w + "messenger: expected an object";
			if (!KnownKeys(*it, { "radius" }, w + "messenger: ", e) || !ReadNumber(*it, "radius", r.MessengerRadius, 1.0f, 1000.0f, w + "messenger: ", e)) return e;
		}
		if (const auto it = d.find("sign"); it != d.end())   // T237
		{
			if (!it->is_object()) return w + "sign: expected an object";
			const std::string ws = w + "sign: ";
			if (!KnownKeys(*it, { "refundUnseen", "signDays", "voteDays", "buildCost", "missing", "reach", "fulfilled", "keep", "traits" }, ws, e)) return e;
			DirectorSignRules& s = r.Sign;
			if (!ReadNumber(*it, "refundUnseen", s.RefundUnseen, 0.0f, 1000.0f, ws, e) || !ReadNumber(*it, "signDays", s.SignDays, 0.01f, 100.0f, ws, e)
				|| !ReadNumber(*it, "voteDays", s.VoteDays, 0.0f, 10.0f, ws, e) || !ReadNumber(*it, "buildCost", s.BuildCost, 0.0f, 10.0f, ws, e)
				|| !ReadNumber(*it, "missing", s.Missing, 0.0f, 10.0f, ws, e) || !ReadNumber(*it, "reach", s.Reach, 0.0f, 1000.0f, ws, e)
				|| !ReadNumber(*it, "fulfilled", s.FulfilledGain, 0.0f, 1000.0f, ws, e) || !ReadMap(*it, "traits", s.Traits, ws, e)) return e;
			if (const auto n = it->find("keep"); n != it->end())
			{
				if (!n->is_number_integer() || n->get<int>() < 0 || n->get<int>() > 1000) return ws + "keep: expected an integer in 0..1000";
				s.Keep = n->get<int>();
			}
		}
		if (!ReadNumber(d, "start", r.Start, 0.0f, 1000.0f, w, e) || !ReadNumber(d, "max", r.Max, 1.0f, 1000.0f, w, e) || !ReadNumber(d, "minFaith", r.MinFaith, 0.0f, 1.0f, w, e)
			|| !ReadNumber(d, "sandboxPerDay", r.SandboxPerDay, 0.0f, 1000.0f, w, e) || !ReadNumber(d, "daySeconds", r.DaySeconds, 1.0f, 100000.0f, w, e)) return e;
		if (const auto it = d.find("logMax"); it != d.end())
		{
			if (!it->is_number_integer() || it->get<int>() < 1 || it->get<int>() > 10000) return w + "logMax: expected an integer in 1..10000";
			r.LogMax = it->get<int>();
		}
		if (const auto it = d.find("faith"); it != d.end())
		{
			if (!it->is_object()) return w + "faith: expected an object";
			const std::string wf = w + "faith: ";
			if (!KnownKeys(*it, { "rite", "omen", "daily", "shaman" }, wf, e)) return e;
			if (!ReadNumber(*it, "rite", r.RiteGain, 0.0f, 100.0f, wf, e) || !ReadNumber(*it, "omen", r.OmenGain, 0.0f, 100.0f, wf, e)
				|| !ReadNumber(*it, "daily", r.DailyGain, 0.0f, 100.0f, wf, e) || !ReadNumber(*it, "shaman", r.ShamanDevotion, 0.0f, 1.0f, wf, e)) return e;
		}
		if (!ReadMap(d, "costs", r.Costs, w, e) || !ReadMap(d, "waits", r.Waits, w, e)) return e;
		if (const auto it = d.find("pressure"); it != d.end())
		{
			if (!it->is_object()) return w + "pressure: expected an object";
			const std::string wp = w + "pressure: ";
			if (!KnownKeys(*it, { "decision", "insist", "contradict", "decay", "tired", "heresy", "insistDays" }, wp, e)) return e;
			if (!ReadNumber(*it, "decision", r.PressureDecision, 0.0f, 100.0f, wp, e) || !ReadNumber(*it, "insist", r.PressureInsist, 0.0f, 100.0f, wp, e)
				|| !ReadNumber(*it, "contradict", r.PressureContradict, 0.0f, 100.0f, wp, e) || !ReadNumber(*it, "decay", r.PressureDecay, 0.0f, 100.0f, wp, e)
				|| !ReadNumber(*it, "tired", r.PressureTired, 0.0f, 1000.0f, wp, e) || !ReadNumber(*it, "heresy", r.PressureHeresy, 0.0f, 1000.0f, wp, e)
				|| !ReadNumber(*it, "insistDays", r.InsistDays, 0.0f, 100.0f, wp, e)) return e;
		}
		if (r.Start > r.Max) return w + "start: more than max";
		live = std::move(r);
		return {};
	}

	// ------------------------------------------------------------------ reading the clans
	namespace
	{
		const Clan* ClanIn(const ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			const Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
	}

	float WorldDirector::FaithOf(int clan) const
	{
		const Clan* c = ClanIn(m_To.Clans, clan);
		if (!c) return 0.0f;
		if (m_Sandbox)   // the lab: the faith set, or every clan believes (MODO_DIOS_DECISIONES §3.0)
		{
			const auto it = m_FaithSet.find(clan);
			return it != m_FaithSet.end() ? it->second : 1.0f;
		}
		const SocialState& s = c->Folk->Social();
		const std::string dominant = s.Dominant();
		if (dominant.empty()) return 0.0f;
		const auto it = s.Beliefs.find(dominant);
		return it == s.Beliefs.end() ? 0.0f : std::clamp(it->second, 0.0f, 1.0f);
	}

	float WorldDirector::DevotionOf(int clan, const std::string& person) const
	{
		const Clan* c = ClanIn(m_To.Clans, clan);
		if (!c) return 0.0f;
		const SocialState& s = c->Folk->Social();
		int held = 0;
		for (const auto& [k, n] : s.Counts) if (k.rfind("rite.", 0) == 0) held += n;
		const SocialMember* m = s.Find(person);
		const float attendance = held > 0 && m ? std::min(1.0f, static_cast<float>(m->Rites) / static_cast<float>(held)) : 0.0f;
		const float shaman = !s.Shaman.empty() && s.Shaman == person ? Rules.ShamanDevotion : 0.0f;
		return std::clamp(FaithOf(clan) * (0.5f + 0.5f * attendance) + shaman, 0.0f, 1.0f);
	}

	float WorldDirector::MeanDevotion(int clan) const
	{
		const Clan* c = ClanIn(m_To.Clans, clan);
		if (!c) return 0.0f;
		float sum = 0.0f;
		int n = 0;
		for (const TribeMember& m : c->Folk->Members())
			if (!m.Dead) { sum += DevotionOf(clan, m.Name); ++n; }
		return n > 0 ? sum / static_cast<float>(n) : 0.0f;
	}

	void WorldDirector::Gain(float amount)
	{
		if (amount <= 0.0f) return;
		const float before = m_Influence;
		m_Influence = std::min(Rules.Max, m_Influence + amount);
		m_Gained += static_cast<double>(m_Influence - before);
	}

	void WorldDirector::Look(bool count)
	{
		if (!m_To.Clans) return;
		for (const auto& c : m_To.Clans->All())
		{
			if (!c->Folk) continue;
			const SocialState& s = c->Folk->Social();
			int attended = 0, omens = 0;
			for (const auto& [name, m] : s.Members) attended += m.Rites;
			for (const auto& [k, n] : s.Counts) if (k.rfind("omen.", 0) == 0) omens += n;
			const auto seen = m_Seen.find(c->Id);
			if (count && seen != m_Seen.end() && !m_Sandbox)
			{
				const float faith = FaithOf(c->Id);
				const int rites = std::max(0, attended - seen->second.first), read = std::max(0, omens - seen->second.second);
				Gain(Rules.RiteGain * static_cast<float>(rites) * faith + Rules.OmenGain * static_cast<float>(read) * (0.5f + faith));
			}
			m_Seen[c->Id] = { attended, omens };
		}
	}

	void WorldDirector::Update()
	{
		if (!m_To.Clans) return;
		const double now = Now();
		UpdateSigns();   // T237 (nothing without signs)
		UpdateFocus();   // T238/T239 (nothing without a focus or a calling)
		Look(m_Looked);
		if (!m_Looked) { m_Looked = true; m_Day = static_cast<int>(std::floor(now / static_cast<double>(Rules.DaySeconds))); m_SandboxAt = now; return; }
		if (m_Sandbox)
		{
			Gain(static_cast<float>((now - m_SandboxAt) / static_cast<double>(Rules.DaySeconds)) * Rules.SandboxPerDay);
			m_SandboxAt = now;
		}
		const int day = static_cast<int>(std::floor(now / static_cast<double>(Rules.DaySeconds)));
		if (day > m_Day)
		{
			const int days = day - m_Day;
			m_Day = day;
			if (!m_Sandbox)
				for (const auto& c : m_To.Clans->All())
				{
					if (!c->Folk) continue;
					int alive = 0;
					for (const TribeMember& m : c->Folk->Members()) if (!m.Dead) ++alive;
					Gain(Rules.DailyGain * static_cast<float>(alive) * MeanDevotion(c->Id));
				}
			for (auto it = m_Pressure.begin(); it != m_Pressure.end();)
			{
				it->second = std::max(0.0f, it->second - Rules.PressureDecay * static_cast<float>(days));
				it = it->second <= 0.0f ? m_Pressure.erase(it) : std::next(it);
			}
		}
	}

	// ------------------------------------------------------------------ paying
	double WorldDirector::WaitLeft(const std::string& action, int clan) const
	{
		if (m_Sandbox) return 0.0;
		const auto it = m_Waits.find(WaitKey(action, clan));
		return it == m_Waits.end() ? 0.0 : std::max(0.0, it->second - Now());
	}

	DirectorCheck WorldDirector::CanSpend(const std::string& action, int clan, bool hand) const
	{
		DirectorCheck c;
		const auto cost = Rules.Costs.find(action);
		c.Cost = cost == Rules.Costs.end() ? 0.0f : cost->second;
		const Clan* target = clan >= 0 ? ClanIn(m_To.Clans, clan) : nullptr;
		if (clan >= 0 && !target) { c.Why = "no clan " + std::to_string(clan); return c; }
		if (hand && clan >= 0 && FaithOf(clan) < Rules.MinFaith)
		{
			c.Why = NameOf(*target) + " does not believe enough: faith " + Num(FaithOf(clan), 2) + " under " + Num(Rules.MinFaith, 2);
			return c;
		}
		if (const double left = WaitLeft(action, clan); left > 0.0)
		{
			c.Why = action + " must wait " + Num(left / static_cast<double>(Rules.DaySeconds), 2) + " days" + (target ? " on " + NameOf(*target) : std::string());
			return c;
		}
		if (m_Influence + 1e-4f < c.Cost) { c.Why = "not enough influence: " + Num(m_Influence) + " of " + Num(c.Cost); return c; }
		c.Ok = true;
		return c;
	}

	DirectorCheck WorldDirector::Spend(const std::string& action, int clan, bool hand, const std::string& text)
	{
		DirectorCheck c = CanSpend(action, clan, hand);
		if (!c.Ok) return c;
		m_Influence = std::max(0.0f, m_Influence - c.Cost);
		if (const auto wait = Rules.Waits.find(action); wait != Rules.Waits.end() && wait->second > 0.0f)
			m_Waits[WaitKey(action, clan)] = Now() + static_cast<double>(wait->second * Rules.DaySeconds);
		if (hand && clan >= 0) m_Pressure[clan] += Rules.PressureDecision;
		const Clan* target = clan >= 0 ? ClanIn(m_To.Clans, clan) : nullptr;
		Write(action, target ? NameOf(*target) : std::string("world"), c.Cost, text, clan);
		return c;
	}

	void WorldDirector::Fulfilled(int clan, float influence, const std::string& text)
	{
		Gain(influence);
		const Clan* target = ClanIn(m_To.Clans, clan);
		Write("fulfilled", target ? NameOf(*target) : std::string("world"), -influence, text, clan);
	}

	void WorldDirector::AddPressure(int clan, float amount, const std::string& why)
	{
		if (amount <= 0.0f || !ClanIn(m_To.Clans, clan)) return;
		m_Pressure[clan] += amount;
		Write("pressure", NameOf(*ClanIn(m_To.Clans, clan)), 0.0f, why, clan);
	}

	float WorldDirector::PressureOf(int clan) const
	{
		const auto it = m_Pressure.find(clan);
		return it == m_Pressure.end() ? 0.0f : it->second;
	}

	std::string WorldDirector::PressureLevel(int clan) const
	{
		const float p = PressureOf(clan);
		return p >= Rules.PressureHeresy ? "heresy" : p >= Rules.PressureTired ? "tired" : "low";
	}

	void WorldDirector::Write(const std::string& action, const std::string& target, float cost, const std::string& text, int clan)
	{
		DirectorEntry e;
		e.At = Now();
		e.Action = action;
		e.Target = target;
		e.Cost = cost;
		e.Text = text;
		m_Book.push_back(e);
		if (static_cast<int>(m_Book.size()) > Rules.LogMax) m_Book.erase(m_Book.begin(), m_Book.begin() + (static_cast<long>(m_Book.size()) - Rules.LogMax));
		// a telemetry row "director" (in the clan's run; the world's acts go to the first clan's sink)
		TelemetrySink* sink = nullptr;
		int row = clan;
		if (const Clan* c = ClanIn(m_To.Clans, clan)) sink = c->Folk->Telemetry();
		else if (m_To.Clans && !m_To.Clans->All().empty() && m_To.Clans->All().front()->Folk) { sink = m_To.Clans->All().front()->Folk->Telemetry(); row = m_To.Clans->All().front()->Id; }
		if (!sink) return;
		TelemetryEvent t;
		t.Clan = row;
		t.Time = e.At;
		t.Kind = "director";
		t.Who = "god";
		t.Other = target;
		t.Result = action;
		t.Text = text + " (cost " + Num(cost) + ", influence " + Num(m_Influence) + ")";
		sink->Event(t);
	}

	// ------------------------------------------------------------------ orders
	std::string WorldDirector::Status() const
	{
		const double day = Now() / static_cast<double>(Rules.DaySeconds);
		std::string s = "director: day " + Num(day, 2) + ", influence " + Num(m_Influence) + " of " + Num(Rules.Max) + " (gained from faith " + Num(m_Gained) + "), sandbox "
			+ (m_Sandbox ? "on" : "off") + ", the hand needs faith " + Num(Rules.MinFaith, 2) + "; book " + std::to_string(m_Book.size()) + " entries";
		if (m_To.Clans)
			for (const auto& c : m_To.Clans->All())
			{
				if (!c->Folk) continue;
				int alive = 0;
				for (const TribeMember& m : c->Folk->Members()) if (!m.Dead) ++alive;
				const float faith = FaithOf(c->Id);
				const std::string dominant = c->Folk->Social().Dominant();
				std::string waits;
				for (const auto& [action, days] : Rules.Waits)
					if (const double left = WaitLeft(action, c->Id); left > 0.0) waits += " " + action + " " + Num(left / static_cast<double>(Rules.DaySeconds), 2) + "d";
				s += "\nclan " + std::to_string(c->Id) + " " + (c->Name.empty() ? c->Folk->ClanName() : c->Name) + ": faith " + Num(faith, 2) + (dominant.empty() ? std::string() : " (" + dominant + ")")
					+ (m_Sandbox && m_FaithSet.count(c->Id) ? " [set]" : "") + ", devotion " + Num(MeanDevotion(c->Id), 2) + " over " + std::to_string(alive) + " living, pressure "
					+ Num(PressureOf(c->Id)) + " (" + PressureLevel(c->Id) + "), hand " + (faith >= Rules.MinFaith ? "yes" : "no") + ", waits:" + (waits.empty() ? std::string(" none") : waits);
			}
		return s;
	}

	DirectorReply WorldDirector::Omen(const std::vector<std::string>& a)
	{
		// director omen <id> [clan <c> | all | <c>]
		if (a.size() < 2) return { false, "usage: director omen <id> [clan <c>|all]" };
		if (!m_To.Clans) return { false, "director: no clans" };
		const std::string id = a[1];
		std::string target = a.size() >= 4 && a[2] == "clan" ? a[3] : a.size() >= 3 ? a[2] : "all";
		std::vector<Clan*> clans;
		if (target == "all") { for (const auto& c : m_To.Clans->All()) if (c->Folk) clans.push_back(c.get()); }
		else if (Clan* c = m_To.Clans->Find(target); c && c->Folk) clans.push_back(c);
		else return { false, "director omen: no clan '" + target + "'" };
		std::string done, refused;
		int fired = 0;
		for (Clan* c : clans)
		{
			const SocialRules& R = c->Folk->Config.Social;
			const std::string name = c->Name.empty() ? c->Folk->ClanName() : c->Name;
			std::string event;
			if (const auto found = R.Beliefs.Omens.find(id); found != R.Beliefs.Omens.end()) event = found->second.Event;
			else for (const auto& [oid, def] : R.Beliefs.Omens) if (def.Event == id) { event = def.Event; break; }
			if (!R.On || event.empty()) { refused += "; " + name + ": no omen '" + id + "'"; continue; }
			const DirectorCheck paid = Spend("omen", c->Id, false, "omen " + id + " on " + name);
			if (!paid.Ok) { refused += "; " + name + ": " + paid.Why; continue; }
			c->Folk->Omen(event);   // the same way as the natural omen (Tribe::Omen): moods, beliefs, maybe a rite
			done += (done.empty() ? "" : ", ") + name;
			++fired;
		}
		if (fired == 0) return { false, "director omen " + id + ": nothing happened" + refused };
		return { true, "director omen " + id + " on " + done + " (influence " + Num(m_Influence) + ")" + refused };
	}

	DirectorReply WorldDirector::Command(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status") return { true, Status() };
		if (sub == "sandbox")
		{
			if (a.size() < 2 || (a[1] != "on" && a[1] != "off")) return { true, std::string("director sandbox ") + (m_Sandbox ? "on" : "off") };
			const bool on = a[1] == "on";
			if (on != m_Sandbox)
			{
				m_Sandbox = on;
				m_SandboxAt = Now();
				if (!on) m_FaithSet.clear();
				Write("sandbox", "world", 0.0f, std::string("sandbox ") + (on ? "on" : "off"), -1);
			}
			return { true, std::string("director sandbox ") + (on ? "on: the influence refills on a fixed clock (" + Num(Rules.SandboxPerDay) + " a day), no waits" : "off: the influence comes from the faith") };
		}
		if (sub == "faith")
		{
			if (!m_Sandbox) return { false, "director faith: only in the sandbox (director sandbox on)" };
			if (a.size() < 3 || !m_To.Clans) return { false, "usage: director faith <clan> <0..1>" };
			const Clan* c = m_To.Clans->Find(a[1]);
			if (!c) return { false, "director faith: no clan '" + a[1] + "'" };
			char* end = nullptr;
			const float v = std::strtof(a[2].c_str(), &end);
			if (end == a[2].c_str() || v < 0.0f || v > 1.0f) return { false, "director faith: expected a number in 0..1" };
			m_FaithSet[c->Id] = v;
			Write("faith", NameOf(*c), 0.0f, "faith set to " + Num(v, 2), c->Id);
			return { true, "director faith " + NameOf(*c) + " " + Num(v, 2) };
		}
		if (sub == "omen") return Omen(a);
		if (sub == "sign" || sub == "signs") return SignCommand(a);
		if (sub == "focus") return FocusCommand(a);
		if (sub == "calling") return CallingCommand(a);
		if (sub == "log")
		{
			std::string s = "director log: " + std::to_string(m_Book.size()) + " entries";
			const size_t from = m_Book.size() > 20 ? m_Book.size() - 20 : 0;
			for (size_t i = from; i < m_Book.size(); ++i)
			{
				const DirectorEntry& e = m_Book[i];
				s += "\n" + Num(e.At / static_cast<double>(Rules.DaySeconds), 2) + "d " + e.Action + " " + e.Target + " cost " + Num(e.Cost) + ": " + e.Text;
			}
			return { true, s };
		}
		return { false, "usage: director status | sandbox on|off | faith <clan> <0..1> | omen <id> [clan <c>|all] | sign [preview] <clan> <building> <x> <z>|camp <dx> <dz> [yaw] | signs | focus [preview] <clan> <group>|off [soft|strong] [days] | calling [preview] <person> <role> | log" };
	}

	// ------------------------------------------------------------------ saving
	void WorldDirector::Clear()
	{
		m_Influence = Rules.Start;
		m_Sandbox = false;
		m_Looked = false;
		m_Day = 0;
		m_SandboxAt = 0.0;
		m_Pressure.clear();
		m_FaithSet.clear();
		m_Waits.clear();
		m_Seen.clear();
		m_Book.clear();
		m_Gained = 0.0;
		m_Signs.clear();
		m_NextSign = 1;
		m_Focus.clear();   // (the tribes of a new world start without a bias)
		m_Callings.clear();
		m_NextCalling = 1;
		m_Biased.clear();
	}

	std::string WorldDirector::ToJson() const
	{
		Json j;
		j["influence"] = m_Influence;
		j["sandbox"] = m_Sandbox;
		j["looked"] = m_Looked;
		j["day"] = m_Day;
		j["sandboxAt"] = m_SandboxAt;
		j["gained"] = m_Gained;
		Json pressure = Json::object(), faith = Json::object(), seen = Json::object();
		for (const auto& [c, p] : m_Pressure) pressure[std::to_string(c)] = p;
		for (const auto& [c, f] : m_FaithSet) faith[std::to_string(c)] = f;
		for (const auto& [c, s] : m_Seen) seen[std::to_string(c)] = Json::array({ s.first, s.second });
		j["pressure"] = pressure;
		j["faithSet"] = faith;
		j["seen"] = seen;
		j["waits"] = m_Waits;
		Json book = Json::array();
		for (const DirectorEntry& e : m_Book) book.push_back({ { "at", e.At }, { "action", e.Action }, { "target", e.Target }, { "cost", e.Cost }, { "text", e.Text } });
		j["book"] = book;
		if (const std::string focus = FocusToJson(); !focus.empty()) j["focus"] = Json::parse(focus);   // T238/T239 (only when there is any)
		if (!m_Signs.empty())   // T237 (a save without signs stays as it was)
		{
			Json signs = Json::array();
			for (const DirectorSign& s : m_Signs)
				signs.push_back({ { "id", s.Id }, { "clan", s.Clan }, { "building", s.Building }, { "x", s.X }, { "z", s.Z }, { "yaw", s.Yaw }, { "posted", s.Posted },
					{ "seenAt", s.SeenAt }, { "votedAt", s.VotedAt }, { "status", s.Status }, { "messenger", s.Messenger }, { "site", s.Site }, { "why", s.Why },
					{ "paid", s.Paid }, { "tries", s.Tries }, { "yes", s.Yes }, { "voters", s.Voters } });
			j["signs"] = signs;
			j["nextSign"] = m_NextSign;
		}
		return j.dump();
	}

	bool WorldDirector::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		try
		{
			WorldDirector d;
			d.Rules = Rules;
			d.m_To = m_To;
			d.m_Influence = j.value("influence", Rules.Start);
			d.m_Sandbox = j.value("sandbox", false);
			d.m_Looked = j.value("looked", false);
			d.m_Day = j.value("day", 0);
			d.m_SandboxAt = j.value("sandboxAt", 0.0);
			d.m_Gained = j.value("gained", 0.0);
			if (const auto it = j.find("pressure"); it != j.end()) for (const auto& [k, v] : it->items()) d.m_Pressure[std::stoi(k)] = v.get<float>();
			if (const auto it = j.find("faithSet"); it != j.end()) for (const auto& [k, v] : it->items()) d.m_FaithSet[std::stoi(k)] = v.get<float>();
			if (const auto it = j.find("seen"); it != j.end()) for (const auto& [k, v] : it->items()) d.m_Seen[std::stoi(k)] = { v.at(0).get<int>(), v.at(1).get<int>() };
			if (const auto it = j.find("waits"); it != j.end()) d.m_Waits = it->get<std::map<std::string, double>>();
			if (const auto it = j.find("book"); it != j.end())
				for (const Json& e : *it)
					d.m_Book.push_back({ e.value("at", 0.0), e.value("action", std::string()), e.value("target", std::string()), e.value("text", std::string()), e.value("cost", 0.0f) });
			if (const auto it = j.find("signs"); it != j.end())   // T237
				for (const Json& e : *it)
				{
					DirectorSign s;
					s.Id = e.value("id", 0);
					s.Clan = e.value("clan", -1);
					s.Building = e.value("building", std::string());
					s.X = e.value("x", 0.0f);
					s.Z = e.value("z", 0.0f);
					s.Yaw = e.value("yaw", 0.0f);
					s.Posted = e.value("posted", 0.0);
					s.SeenAt = e.value("seenAt", 0.0);
					s.VotedAt = e.value("votedAt", 0.0);
					s.Status = e.value("status", std::string("waiting"));
					s.Messenger = e.value("messenger", std::string());
					s.Site = e.value("site", std::string());
					s.Why = e.value("why", std::string());
					s.Paid = e.value("paid", 0.0f);
					s.Tries = e.value("tries", 0);
					s.Yes = e.value("yes", 0);
					s.Voters = e.value("voters", 0);
					d.m_Signs.push_back(s);
				}
			d.m_NextSign = j.value("nextSign", 1);
			if (const auto it = j.find("focus"); it != j.end() && !d.FocusFromJson(it->dump())) return false;   // T238/T239
			d.m_Biased = m_Biased;
			*this = std::move(d);
			if (!m_Focus.empty() || !m_Callings.empty() || !m_Biased.empty()) PushBias();   // (the tribes do not save it: written again now)
			return true;
		}
		catch (const std::exception&) { return false; }
	}
}
