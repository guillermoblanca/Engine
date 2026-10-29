#include "GB/Game/Conflict.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		std::string Parts(const std::vector<std::pair<std::string, float>>& factors, const char* prefix)
		{
			std::string s;
			char b[64];
			for (const auto& [name, v] : factors)
			{
				if (name.rfind(prefix, 0) != 0) continue;
				std::snprintf(b, sizeof b, "%s%s %.2f", s.empty() ? "" : ", ", name.c_str() + std::char_traits<char>::length(prefix), static_cast<double>(v));
				s += b;
			}
			return s;
		}
	}

	FightChoice FightOrFlight(const ThreatRules& t, const MoraleRules& morale, const ThreatView& v)
	{
		FightChoice c;
		auto add = [&](float& total, const char* name, float value) { total += value; c.Factors.emplace_back(name, value); };
		const float bold = std::clamp(v.Boldness, 0.0f, 1.0f), health = std::clamp(v.Health / 100.0f, 0.0f, 1.0f);
		const float odds = std::max(0.0f, v.Odds);
		add(c.Fight, "fight.bold", t.Bold * bold);
		add(c.Fight, "fight.health", t.Health * health);
		add(c.Fight, "fight.odds", t.Odds * std::min(odds / 2.0f, 1.0f));
		if (v.Kin) add(c.Fight, "fight.kin", t.Kin);
		if (v.Camp) add(c.Fight, "fight.camp", t.Camp);
		if (v.Camp && v.Defense > 0.0f) add(c.Fight, "fight.walls", t.Walls * v.Defense);
		add(c.Flee, "flee.fear", t.Fear * (1.0f - std::clamp(v.Safety / 100.0f, 0.0f, 1.0f)));
		add(c.Flee, "flee.timid", t.Timid * (1.0f - bold));
		if (odds < 1.0f) add(c.Flee, "flee.outnumbered", t.Odds * (1.0f - odds) * 1.5f);
		if (v.Health < morale.FleeHealth) add(c.Flee, "flee.hurt", 2.0f * t.Health * (1.0f - health));   // (badly hurt, running weighs more than all its health gave)

		if (v.FightCulture != 1.0f) { c.Fight *= v.FightCulture; c.Factors.emplace_back("fight.culture", v.FightCulture); }
		if (v.FleeCulture != 1.0f) { c.Flee *= v.FleeCulture; c.Factors.emplace_back("flee.culture", v.FleeCulture); }
		if (v.Child && !t.ChildrenFight) { c.Fight = 0.0f; c.Factors.emplace_back("fight.child", 0.0f); }
		return c;
	}

	std::string FightChoice::Why() const
	{
		char b[64];
		std::snprintf(b, sizeof b, "fight %.2f (", static_cast<double>(Fight));
		std::string s = b + Parts(Factors, "fight.") + ") ";
		s += Fights() ? "> " : "<= ";
		std::snprintf(b, sizeof b, "flee %.2f (", static_cast<double>(Flee));
		return s + b + Parts(Factors, "flee.") + ")";
	}

	RaidMotive WeighRaid(const RaidRules& r, float meanBoldness, float hunger, float grievance, float hostility, float defense)
	{
		RaidMotive m;
		m.Boldness = r.Bold * std::clamp(meanBoldness, 0.0f, 1.0f);
		m.Hunger = r.Hunger * std::clamp(hunger, 0.0f, 1.0f);
		m.Grievance = r.Grievance * std::max(0.0f, grievance);
		m.Hostility = r.Hostility * std::clamp(hostility, 0.0f, 1.0f);
		m.Defense = r.Defense * std::max(0.0f, defense);
		m.Score = m.Boldness + m.Hunger + m.Grievance + m.Hostility - m.Defense;
		return m;
	}

	std::string RaidMotive::Why() const
	{
		char b[200];
		std::snprintf(b, sizeof b, "motive %.2f (boldness %.2f, hunger %.2f, grievance %.2f, hostility %.2f", static_cast<double>(Score), static_cast<double>(Boldness),
			static_cast<double>(Hunger), static_cast<double>(Grievance), static_cast<double>(Hostility));
		std::string s = b;
		if (Defense > 0.0f)
		{
			std::snprintf(b, sizeof b, ", walls -%.2f", static_cast<double>(Defense));
			s += b;
		}
		return s + ")";
	}

	std::vector<std::string> ChooseRaidParty(const RaidRules& rules, std::vector<RaiderCandidate> candidates)
	{
		candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [&](const RaiderCandidate& c) { return c.Child || c.Busy || c.Health < rules.PartyHealth; }), candidates.end());
		std::sort(candidates.begin(), candidates.end(), [](const RaiderCandidate& a, const RaiderCandidate& b)
		{
			const float va = a.Boldness + a.Strength, vb = b.Boldness + b.Strength;
			return va != vb ? va > vb : a.Name < b.Name;
		});
		std::vector<std::string> party;
		for (const RaiderCandidate& c : candidates)
			if (static_cast<int>(party.size()) < rules.MaxParty) party.push_back(c.Name);
		if (static_cast<int>(party.size()) < rules.Party) party.clear();
		return party;
	}

	std::string ConflictState::ToJson() const
	{
		Json members = Json::object();
		for (const auto& [name, m] : Members)
			members[name] = Json{ { "threat", m.Threat }, { "seen", m.SeenAt }, { "alarmed", m.AlarmedAt }, { "goal", Json::array({ m.Goal.x, m.Goal.y }) },
				{ "ordered", m.Ordered }, { "care", m.CareUntil } };
		const RaidState& r = Raid;
		Json raid{ { "phase", r.Phase }, { "target", r.Target }, { "camp", Json::array({ r.TargetCamp.x, r.TargetCamp.y }) }, { "party", r.Party }, { "took", r.Took },
			{ "home", r.Home }, { "loot", r.Loot }, { "started", r.Started }, { "next", r.NextAllowed }, { "why", r.Why }, { "raids", r.Raids }, { "stolen", r.Stolen } };
		return Json{ { "raid", raid }, { "members", members }, { "seq", Seq }, { "timer", Timer }, { "raidTimer", RaidTimer }, { "hunted", Hunted },
			{ "counts", Json::array({ Fights, Flights, Kills, Deaths, Cares, Raided }) }, { "raidedBy", RaidedBy } }.dump();
	}

	bool ConflictState::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		ConflictState s;
		try
		{
			if (j.contains("raid"))
			{
				const Json& r = j["raid"];
				s.Raid.Phase = r.value("phase", std::string());
				s.Raid.Target = r.value("target", std::string());
				if (r.contains("camp")) s.Raid.TargetCamp = { r["camp"].at(0).get<float>(), r["camp"].at(1).get<float>() };
				if (r.contains("party")) s.Raid.Party = r["party"].get<std::vector<std::string>>();
				if (r.contains("took")) s.Raid.Took = r["took"].get<std::map<std::string, std::map<std::string, int>>>();
				if (r.contains("home")) s.Raid.Home = r["home"].get<std::map<std::string, bool>>();
				if (r.contains("loot")) s.Raid.Loot = r["loot"].get<std::map<std::string, int>>();
				s.Raid.Started = r.value("started", 0.0);
				s.Raid.NextAllowed = r.value("next", -1e9);
				s.Raid.Why = r.value("why", std::string());
				s.Raid.Raids = r.value("raids", 0);
				s.Raid.Stolen = r.value("stolen", 0);
				if (!s.Raid.Phase.empty() && s.Raid.Phase != "march" && s.Raid.Phase != "steal" && s.Raid.Phase != "retreat") return false;
			}
			if (j.contains("members"))
				for (const auto& [name, m] : j["members"].items())
				{
					MemberConflict c;
					c.Threat = m.value("threat", std::string());
					c.SeenAt = m.value("seen", -1e9);
					c.AlarmedAt = m.value("alarmed", -1e9);
					if (m.contains("goal")) c.Goal = { m["goal"].at(0).get<float>(), m["goal"].at(1).get<float>() };
					c.Ordered = m.value("ordered", false);
					c.CareUntil = m.value("care", -1e9);
					s.Members[name] = c;
				}
			s.Seq = j.value("seq", static_cast<uint64_t>(0));
			s.Timer = j.value("timer", 0.0f);
			s.RaidTimer = j.value("raidTimer", 0.0f);
			if (j.contains("hunted")) s.Hunted = j["hunted"].get<std::map<std::string, int>>();
			if (j.contains("counts") && j["counts"].size() == 6)
			{
				const Json& c = j["counts"];
				s.Fights = c[0].get<int>(); s.Flights = c[1].get<int>(); s.Kills = c[2].get<int>();
				s.Deaths = c[3].get<int>(); s.Cares = c[4].get<int>(); s.Raided = c[5].get<int>();
			}
			if (j.contains("raidedBy")) s.RaidedBy = j["raidedBy"].get<std::map<std::string, double>>();
		}
		catch (const Json::exception&) { return false; }
		*this = std::move(s);
		return true;
	}
}
