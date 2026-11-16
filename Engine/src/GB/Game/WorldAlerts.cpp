#include "GB/Game/WorldAlerts.h"
#include "GB/Game/Clans.h"
#include "GB/Game/History.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

// T220: the god's alerts (see WorldAlerts.h). Only reads.
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

		bool IsCondition(const std::string& kind) { return kind == "hunger" || kind == "winter" || kind == "extinct"; }
		bool Counted(const std::string& kind) { return kind == "war" || kind == "inventor"; }   // (events that add up into one alert)

		std::string NameOr(const std::string& name, int id) { return name.empty() ? "clan " + std::to_string(id) : name; }

		// The English line of an alert (the editor writes its own in the player's language from the same fields).
		std::string TextOf(const Alert& a)
		{
			const std::string clan = NameOr(a.ClanName, a.Clan), other = NameOr(a.OtherName, a.Other);
			if (a.Kind == "hunger") return clan + ": " + Num(a.Value) + " days of food (" + std::to_string(static_cast<int>(a.Value2)) + " alive)";
			if (a.Kind == "winter")
			{
				const std::string spring = Num(a.Value2 + a.Value, 0);   // (until the spring: what it has plus what it lacks)
				return a.Value3 <= 0.0f ? clan + ": in the winter, " + Num(a.Value2, 0) + " days of food of " + spring + " (" + Num(a.Value, 0) + " short)"
					: clan + ": winter in " + Num(a.Value3, 0) + " days, " + Num(a.Value2, 0) + " days of food of " + spring + " (" + Num(a.Value, 0) + " short)";
			}
			if (a.Kind == "war") return clan + " attacks " + other + " (" + std::to_string(static_cast<int>(a.Value)) + (a.Value == 1.0f ? " clash)" : " clashes)");
			if (a.Kind == "extinct") return clan + " has died out (" + std::to_string(static_cast<int>(a.Value)) + " dead)";
			if (a.Kind == "inventor") return "inventor of " + clan + ": " + a.What + (a.Value > 1.0f ? " (" + std::to_string(static_cast<int>(a.Value)) + " discoveries)" : std::string());
			if (a.Kind == "split") return clan + " splits: the " + other + " are founded";
			return clan + ": " + a.Kind;
		}

		bool Read(const Json& o, const char* key, float& v, float lo, float hi, std::string& e)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_number() || it->get<float>() < lo || it->get<float>() > hi) { e = std::string("alerts: ") + key + ": expected a number in " + Num(lo, 0) + ".." + Num(hi, 0); return false; }
			v = it->get<float>();
			return true;
		}
	}

	std::string LoadAlertRules(const std::string& json, AlertRules& live)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("alerts");
		if (section == doc.end()) return {};
		if (!section->is_object()) return "alerts: expected an object";
		AlertRules r = live;
		std::string e;
		for (const auto& [key, v] : section->items())
			if (key != "_doc" && key != "hungerDays" && key != "hungerOnSurvival" && key != "graceDays" && key != "winterWithinDays" && key != "warDays" && key != "eventDays") return "alerts: unknown key '" + key + "'";
		if (!Read(*section, "hungerDays", r.HungerDays, 0.0f, 100.0f, e) || !Read(*section, "winterWithinDays", r.WinterWithinDays, 0.0f, 1000.0f, e)
			|| !Read(*section, "warDays", r.WarDays, 0.0f, 1000.0f, e) || !Read(*section, "eventDays", r.EventDays, 0.0f, 1000.0f, e)
			|| !Read(*section, "graceDays", r.GraceDays, 0.0f, 1000.0f, e)) return e;
		if (const auto it = section->find("hungerOnSurvival"); it != section->end())
		{
			if (!it->is_boolean()) return "alerts: hungerOnSurvival: expected true or false";
			r.HungerOnSurvival = it->get<bool>();
		}
		live = r;
		return {};
	}

	std::string AlertRulesToJson(const AlertRules& r)
	{
		Json o = Json::object();
		o["hungerDays"] = r.HungerDays;
		o["hungerOnSurvival"] = r.HungerOnSurvival;
		o["graceDays"] = r.GraceDays;
		o["winterWithinDays"] = r.WinterWithinDays;
		o["warDays"] = r.WarDays;
		o["eventDays"] = r.EventDays;
		return o.dump();
	}

	const char* AlertLevelName(AlertLevel level) { return level == AlertLevel::Critical ? "critical" : "important"; }

	const std::vector<std::string>& AlertKinds()
	{
		static const std::vector<std::string> kinds = { "hunger", "winter", "war", "extinct", "inventor", "split" };
		return kinds;
	}

	AlertInput AlertInputOf(const ClanWorld& world, int sinceEvent)
	{
		AlertInput in;
		in.Now = world.Time();
		for (const auto& c : world.All())
		{
			if (!c || !c->Folk) continue;
			const Tribe& t = *c->Folk;
			if (in.Clans.empty()) { in.Now = t.WorldSeconds(); in.DaySeconds = t.Config.Needs.DaySeconds; }   // (the clans share one clock)
			ClanAlertState s;
			s.Clan = c->Id;
			s.Name = c->Name;
			for (const TribeMember& m : t.Members()) (m.Dead ? s.Dead : s.Alive) += 1;
			if (s.Alive > 0)
			{
				s.FoodDays = t.FoodDays();
				s.Survival = t.MaslowTier() == "survival";
				s.Winter = ForecastWinter(t.Config.Seasons, t.WorldSeconds(), t.Config.Needs.DaySeconds, s.FoodDays);
			}
			s.Camp = t.Config.Economy.Camp;
			in.Clans.push_back(std::move(s));
		}
		for (const HistoryEvent& e : world.Past().Events())
			if (e.Id > sinceEvent) in.Events.push_back(&e);
		in.ClanOfMember = [&world](const std::string& name) { const Clan* c = world.ClanOf(name); return c ? static_cast<int>(c->Id) : -1; };
		in.ClanName = [&world](int id) { const Clan* c = world.Find(static_cast<ClanId>(id)); return c ? c->Name : std::string(); };
		return in;
	}

	std::vector<Alert> WorldAlerts(const AlertInput& in, const AlertRules& rules)
	{
		std::vector<Alert> out;
		auto nameOf = [&](int id) -> std::string
		{
			for (const ClanAlertState& c : in.Clans) if (c.Clan == id) return c.Name;
			return in.ClanName ? in.ClanName(id) : std::string();
		};
		// ---- the conditions of each clan
		for (const ClanAlertState& c : in.Clans)
		{
			Alert a;
			a.Clan = c.Clan;
			a.ClanName = c.Name;
			a.Where = c.Camp;
			a.HasWhere = true;
			a.Since = a.Last = in.Now;
			a.Level = AlertLevel::Critical;
			if (c.Alive == 0)
			{
				if (c.Dead > 0)
				{
					Alert x = a;
					x.Kind = "extinct";
					x.Key = "extinct:" + std::to_string(c.Clan);
					x.Value = static_cast<float>(c.Dead);
					out.push_back(std::move(x));
				}
				continue;
			}
			const bool grace = in.Now < static_cast<double>(rules.GraceDays) * static_cast<double>(std::max(1.0f, in.DaySeconds));   // (the world's first hours)
			if (!grace && (c.FoodDays < rules.HungerDays || (rules.HungerOnSurvival && c.Survival)))
			{
				Alert x = a;
				x.Kind = "hunger";
				x.Key = "hunger:" + std::to_string(c.Clan);
				x.Value = c.FoodDays;
				x.Value2 = static_cast<float>(c.Alive);
				out.push_back(std::move(x));
			}
			if (!c.Winter.Covered() && (c.Winter.In || c.Winter.ToWinter <= rules.WinterWithinDays))
			{
				Alert x = a;
				x.Kind = "winter";
				x.Key = "winter:" + std::to_string(c.Clan);
				x.Value = c.Winter.Short;
				x.Value2 = c.Winter.FoodDays;
				x.Value3 = c.Winter.In ? 0.0f : std::max(c.Winter.ToWinter, 0.01f);
				out.push_back(std::move(x));
			}
		}
		// ---- the events since the last look
		for (const HistoryEvent* e : in.Events)
		{
			if (!e) continue;
			Alert a;
			a.Since = a.Last = e->At;
			a.Where = e->Where;
			a.HasWhere = e->HasWhere;
			a.Event = e->Id;
			a.What = e->Text;
			if (e->Kind == "discovery" && e->Clan >= 0)
			{
				a.Kind = "inventor";
				a.Key = "inventor:" + std::to_string(e->Clan);   // (one per clan: the discoveries add up, the last is named)
				a.Level = AlertLevel::Important;
				a.Clan = e->Clan;
				a.Who = e->Who;
				a.Value = 1.0f;
			}
			else if (e->Kind == "found" && e->Clan >= 0)
			{
				a.Kind = "split";
				a.Key = "split:#" + std::to_string(e->Id);
				a.Level = AlertLevel::Important;
				a.Clan = e->Clan;
				a.Other = e->Other;
			}
			else
			{
				// a raid between clans (the diplomacy's: the raider and the raided), or a member killed or wounded by another clan's
				int attacker = -1, victim = -1;
				if (e->Kind == "raid" && e->Clan >= 0 && e->Other >= 0) { attacker = e->Clan; victim = e->Other; }
				else if ((e->Kind == "killed" || e->Kind == "wounded") && e->Clan >= 0 && !e->Who.empty() && in.ClanOfMember) { attacker = in.ClanOfMember(e->Who); victim = e->Clan; }
				if (attacker < 0 || victim < 0 || attacker == victim) continue;
				a.Kind = "war";
				a.Key = "war:" + std::to_string(std::min(attacker, victim)) + ":" + std::to_string(std::max(attacker, victim));
				a.Level = AlertLevel::Critical;
				a.Clan = attacker;
				a.Other = victim;
				a.Value = 1.0f;
				a.Who = e->Who;
				if (!a.HasWhere)   // (a raid of the diplomacy has no place: the camp of the raided clan)
					for (const ClanAlertState& c : in.Clans) if (c.Clan == victim) { a.Where = c.Camp; a.HasWhere = true; }
			}
			// the clashes of one war, or the discoveries of one clan, are one alert: its count goes up, the last one says who and where
			if (Counted(a.Kind))
				if (auto same = std::find_if(out.begin(), out.end(), [&](const Alert& x) { return x.Key == a.Key; }); same != out.end())
				{
					a.Value += same->Value;
					a.Since = same->Since;
					*same = a;
					continue;
				}
			out.push_back(std::move(a));
		}
		for (Alert& a : out)
		{
			if (a.ClanName.empty()) a.ClanName = nameOf(a.Clan);
			if (a.Other >= 0 && a.OtherName.empty()) a.OtherName = nameOf(a.Other);
			a.Text = TextOf(a);
		}
		std::stable_sort(out.begin(), out.end(), [](const Alert& a, const Alert& b)
		{
			if (a.Level != b.Level) return a.Level == AlertLevel::Critical;
			return a.Clan < b.Clan;
		});
		return out;
	}

	// ------------------------------------------------------------------ the board
	std::vector<std::string> AlertBoard::Update(const std::vector<Alert>& found, double now, const AlertRules& rules, float daySeconds)
	{
		std::vector<std::string> raised;
		const double day = std::max(1.0, static_cast<double>(daySeconds));
		std::vector<std::string> holding;   // the conditions that hold in this look
		for (const Alert& f : found)
		{
			if (IsCondition(f.Kind)) holding.push_back(f.Key);
			auto it = std::find_if(m_Alerts.begin(), m_Alerts.end(), [&](const Alert& a) { return a.Key == f.Key; });
			if (it == m_Alerts.end())
			{
				Alert a = f;
				if (IsCondition(a.Kind)) a.Since = a.Last = now;
				m_Alerts.push_back(std::move(a));
				++m_Raised[f.Kind];
				raised.push_back(f.Key);
				continue;
			}
			const double since = it->Since;
			const float count = it->Value;
			const std::string clanName = it->ClanName, otherName = it->OtherName;
			*it = f;
			it->Since = since;
			if (it->ClanName.empty()) it->ClanName = clanName;   // (a look that could not name them keeps the names it had)
			if (it->OtherName.empty()) it->OtherName = otherName;
			if (Counted(f.Kind))
			{
				it->Value = count + f.Value;   // (each look brings only its new clashes or discoveries) ...
				m_Dismissed.erase(f.Key);      // ... and a new one shows the alert again, dismissed or not
			}
			if (IsCondition(f.Kind)) it->Last = now;
			it->Text = TextOf(*it);
		}
		// what no longer holds goes: a condition at once (it may come back, and then it is raised again), an event's alert after its days
		for (auto it = m_Alerts.begin(); it != m_Alerts.end();)
		{
			bool gone = false;
			if (IsCondition(it->Kind)) gone = std::find(holding.begin(), holding.end(), it->Key) == holding.end();
			else if (it->Kind == "war") gone = now - it->Last > static_cast<double>(rules.WarDays) * day;
			else gone = now - it->Last > static_cast<double>(rules.EventDays) * day;   // (an inventor's: after the clan's last discovery)
			if (gone) { m_Dismissed.erase(it->Key); it = m_Alerts.erase(it); }
			else ++it;
		}
		return raised;
	}

	std::vector<std::string> AlertBoard::Look(const ClanWorld& world, const AlertRules& rules)
	{
		AlertInput in = AlertInputOf(world, m_SeenEvent);
		if (!world.Past().Events().empty()) m_SeenEvent = std::max(m_SeenEvent, world.Past().Events().back().Id);
		return Update(WorldAlerts(in, rules), in.Now, rules, in.DaySeconds);
	}

	void AlertBoard::SkipHistory(const ClanWorld& world)
	{
		m_SeenEvent = world.Past().Events().empty() ? 0 : world.Past().Events().back().Id;
	}

	std::vector<const Alert*> AlertBoard::Shown() const
	{
		std::vector<const Alert*> out;
		for (const Alert& a : m_Alerts)
			if (!m_Dismissed.count(a.Key)) out.push_back(&a);
		std::stable_sort(out.begin(), out.end(), [](const Alert* a, const Alert* b)
		{
			if (a->Level != b->Level) return a->Level == AlertLevel::Critical;
			return a->Since > b->Since;
		});
		return out;
	}

	bool AlertBoard::Dismiss(int n)
	{
		const std::vector<const Alert*> shown = Shown();
		if (n < 1 || n > static_cast<int>(shown.size())) return false;
		m_Dismissed[shown[static_cast<size_t>(n - 1)]->Key] = true;
		return true;
	}

	bool AlertBoard::DismissKey(const std::string& key)
	{
		for (const Alert& a : m_Alerts)
			if (a.Key == key) { m_Dismissed[key] = true; return true; }
		return false;
	}

	void AlertBoard::Clear()
	{
		m_Alerts.clear();
		m_Dismissed.clear();
		m_Raised.clear();
		m_SeenEvent = 0;
	}

	std::string AlertBoard::RaisedLine() const
	{
		std::string s;
		for (const std::string& k : AlertKinds())
		{
			const auto it = m_Raised.find(k);
			s += (s.empty() ? "" : ", ") + k + " " + std::to_string(it == m_Raised.end() ? 0 : it->second);
		}
		return s;
	}

	std::string AlertBoard::Text() const
	{
		const std::vector<const Alert*> shown = Shown();
		if (shown.empty()) return "no alerts";
		std::string s;
		int n = 0;
		for (const Alert* a : shown) s += (s.empty() ? "" : "\n") + std::to_string(++n) + ". " + AlertLevelName(a->Level) + " " + a->Kind + ": " + a->Text;
		return s;
	}
}
