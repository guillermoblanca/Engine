// The state of a clan's social life (GB/Game/Social.h): what it is, how it compares, and how it is saved. The rules and their JSON are in
// SocialRules.cpp; what the tribe does with them in TribeSocial.cpp.
#include "GB/Game/Social.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	float SocialRoll(uint32_t seed, const std::string& name, int n)
	{
		uint32_t h = 2166136261u ^ (seed * 16777619u);
		for (const char c : name) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
		h ^= static_cast<uint32_t>(n) * 2654435761u;
		h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
		return static_cast<float>(h % 100003u) / 100003.0f;
	}

	float SocialFactor(const SocialEffects& effects, const std::string& kind, float value)
	{
		const auto it = effects.find(kind);
		return it == effects.end() ? 1.0f : std::max(0.05f, 1.0f + it->second * (2.0f * value - 1.0f));
	}

	std::string SocialState::Dominant() const
	{
		std::string best;
		float top = -1.0f;
		for (const auto& [id, v] : Beliefs) if (v > top) { top = v; best = id; }   // (ties: the first by id)
		return best;
	}

	float SocialState::Similarity(const std::map<std::string, float>& a, const std::map<std::string, float>& b)
	{
		if (a.empty() || b.empty()) return 0.5f;
		float sum = 0.0f;
		int n = 0;
		for (const auto& [id, v] : a)
		{
			const auto o = b.find(id);
			sum += std::abs(v - (o == b.end() ? 0.0f : o->second));
			++n;
		}
		for (const auto& [id, v] : b) if (!a.count(id)) { sum += v; ++n; }
		return n ? std::clamp(1.0f - sum / static_cast<float>(n), 0.0f, 1.0f) : 0.5f;
	}

	bool SocialState::Holds(const SocialRules& rules, const std::string& taboo) const
	{
		const auto t = rules.Beliefs.Taboos.find(taboo);
		if (t == rules.Beliefs.Taboos.end()) return false;
		const auto b = Beliefs.find(t->second.System);
		return b != Beliefs.end() && b->second >= rules.Beliefs.HoldAbove;
	}

	// ---- saving: only what is not the default is written; an older save (no "social") loads as a fresh state
	namespace
	{
		double R(float v) { return static_cast<double>(v); }   // (exact: a loaded game goes on step by step as the saved one)
		Json Vec(const glm::vec2& v) { return Json::array({ R(v.x), R(v.y) }); }
		bool ReadVec(const Json& j, glm::vec2& v)
		{
			if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return false;
			v = { j[0].get<float>(), j[1].get<float>() };
			return true;
		}
	}

	std::string SocialState::ToJson() const
	{
		Json j = Json::object();
		Json members = Json::object();
		for (const auto& [name, m] : Members)
		{
			Json o = { { "mood", R(m.Mood) }, { "grief", R(m.Grief) }, { "lastTalk", m.LastTalk }, { "talks", m.Talks }, { "rites", m.Rites }, { "led", m.Led }, { "kills", m.Kills } };
			if (!m.Diet.empty()) o["diet"] = m.Diet;
			if (!m.Talked.empty()) o["talked"] = m.Talked;
			members[name] = o;
		}
		j["members"] = members;
		Json agenda = Json::array();
		for (const PendingRite& p : Agenda) agenda.push_back({ { "rite", p.Rite }, { "subject", p.Subject }, { "due", p.Due }, { "tries", p.Tries } });
		j["agenda"] = agenda;
		if (Rite.Active())
			j["rite"] = { { "rite", Rite.Rite }, { "subject", Rite.Subject }, { "leader", Rite.Leader }, { "place", Vec(Rite.Place) }, { "start", Rite.Start }, { "hold", Rite.Hold },
				{ "called", Rite.Called }, { "humble", Rite.Humble } };
		j["history"] = History;
		Json beliefs = Json::object();
		for (const auto& [id, v] : Beliefs) beliefs[id] = R(v);
		j["beliefs"] = beliefs;
		if (HasSacred) j["sacred"] = Vec(Sacred);
		j["shaman"] = Shaman;
		Json snares = Json::array();
		for (const Snare& s : Snares)
			snares.push_back({ { "name", s.Name }, { "trap", s.Trap }, { "owner", s.Owner }, { "pos", Vec(s.Pos) }, { "set", s.Set }, { "caught", s.Caught } });
		j["snares"] = snares;
		j["counts"] = Counts;
		j["years"] = Years;
		j["recurring"] = Recurring;
		j["dayTimer"] = DayTimer;
		j["day"] = Day;
		j["deathsToday"] = DeathsToday;
		j["stepTimer"] = R(StepTimer);
		j["serial"] = Serial;
		return j.dump();
	}

	bool SocialState::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		SocialState s;
		try
		{
			if (j.contains("members"))
				for (const auto& [name, o] : j["members"].items())
				{
					SocialMember m;
					m.Mood = std::clamp(o.value("mood", 0.5f), 0.0f, 1.0f);
					m.Grief = std::clamp(o.value("grief", 0.0f), 0.0f, 1.0f);
					m.LastTalk = o.value("lastTalk", -1e9);
					m.Talks = o.value("talks", 0);
					m.Rites = o.value("rites", 0);
					m.Led = o.value("led", 0);
					m.Kills = o.value("kills", 0);
					if (o.contains("diet")) m.Diet = o["diet"].get<std::vector<std::string>>();
					if (o.contains("talked")) m.Talked = o["talked"].get<std::map<std::string, double>>();
					s.Members[name] = std::move(m);
				}
			if (j.contains("agenda"))
				for (const Json& p : j["agenda"])
					s.Agenda.push_back({ p.at("rite").get<std::string>(), p.value("subject", std::string()), p.value("due", 0.0), p.value("tries", 0) });
			if (j.contains("rite"))
			{
				const Json& r = j["rite"];
				s.Rite.Rite = r.at("rite").get<std::string>();
				s.Rite.Subject = r.value("subject", std::string());
				s.Rite.Leader = r.value("leader", std::string());
				if (!ReadVec(r.at("place"), s.Rite.Place)) return false;
				s.Rite.Start = r.value("start", 0.0);
				s.Rite.Hold = r.value("hold", -1.0);
				if (r.contains("called")) s.Rite.Called = r["called"].get<std::vector<std::string>>();
				s.Rite.Humble = r.value("humble", false);
			}
			if (j.contains("history")) s.History = j["history"].get<std::vector<std::string>>();
			if (j.contains("beliefs"))
				for (const auto& [id, v] : j["beliefs"].items()) s.Beliefs[id] = std::clamp(v.get<float>(), 0.0f, 1.0f);
			if (j.contains("sacred")) { if (!ReadVec(j["sacred"], s.Sacred)) return false; s.HasSacred = true; }
			s.Shaman = j.value("shaman", std::string());
			if (j.contains("snares"))
				for (const Json& o : j["snares"])
				{
					Snare n;
					n.Name = o.at("name").get<std::string>();
					n.Trap = o.at("trap").get<std::string>();
					n.Owner = o.value("owner", std::string());
					if (!ReadVec(o.at("pos"), n.Pos)) return false;
					n.Set = o.value("set", 0.0);
					n.Caught = o.value("caught", std::string());
					s.Snares.push_back(std::move(n));
				}
			if (j.contains("counts")) s.Counts = j["counts"].get<std::map<std::string, int>>();
			if (j.contains("years")) s.Years = j["years"].get<std::map<std::string, int>>();
			if (j.contains("recurring")) s.Recurring = j["recurring"].get<std::map<std::string, double>>();
			s.DayTimer = j.value("dayTimer", 0.0);
			s.Day = j.value("day", 0);
			s.DeathsToday = j.value("deathsToday", 0);
			s.StepTimer = j.value("stepTimer", 0.0f);
			s.Serial = j.value("serial", 0);
		}
		catch (const Json::exception&)
		{
			return false;
		}
		*this = std::move(s);
		return true;
	}
}
