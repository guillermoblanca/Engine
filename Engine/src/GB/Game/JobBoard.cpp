#include "GB/Game/JobBoard.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		double Rounded(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.35f is written as 0.35

		// The numbers of each part, by their JSON key (all of them 0 or more).
		struct SkillField { const char* Key; float SkillRules::*Field; };
		constexpr SkillField kSkillFields[] = { { "gain", &SkillRules::Gain }, { "decayPerDay", &SkillRules::DecayPerDay }, { "max", &SkillRules::Max },
			{ "yield", &SkillRules::Yield }, { "speed", &SkillRules::Speed }, { "weight", &SkillRules::Weight }, { "profession", &SkillRules::Profession } };
		struct JobField { const char* Key; float JobRules::*Field; };
		constexpr JobField kJobFields[] = { { "every", &JobRules::Every }, { "bonus", &JobRules::Bonus }, { "slotUnits", &JobRules::SlotUnits },
			{ "claimSeconds", &JobRules::ClaimSeconds }, { "ageBoost", &JobRules::AgeBoost }, { "ageSeconds", &JobRules::AgeSeconds }, { "maxAge", &JobRules::MaxAge },
			{ "minScore", &JobRules::MinScore }, { "switch", &JobRules::Switch }, { "fireWood", &JobRules::FireWood } };
		struct RoleField { const char* Key; float RoleRules::*Field; };
		constexpr RoleField kRoleFields[] = { { "review", &RoleRules::Review }, { "inertia", &RoleRules::Inertia }, { "skillSwitch", &RoleRules::SkillSwitch },
			{ "fireDemand", &RoleRules::FireDemand } };

		// Reads the numbers of `o` into `into` through the table; an unknown key or a negative number is a mistake.
		template <typename T, typename F, size_t N>
		std::string ReadNumbers(const Json& o, const char* part, const F (&fields)[N], T& into, std::initializer_list<const char*> others)
		{
			for (const auto& [key, v] : o.items())
			{
				if (std::find_if(others.begin(), others.end(), [&](const char* k) { return key == k; }) != others.end()) continue;
				const F* f = std::find_if(std::begin(fields), std::end(fields), [&](const F& x) { return key == x.Key; });
				if (f == std::end(fields)) return std::string("work: ") + part + " has no " + key;
				if (!v.is_number() || v.get<float>() < 0.0f) return std::string("work: ") + part + " " + key + " must be a number of 0 or more";
				into.*(f->Field) = v.get<float>();
			}
			return {};
		}

		std::string ReadMap(const Json& o, const char* part, const char* key, std::map<std::string, float>& out)
		{
			const auto m = o.find(key);
			if (m == o.end()) return {};
			if (!m->is_object()) return std::string("work: ") + part + " " + key + " must be an object of name -> number";
			out.clear();
			for (const auto& [k, v] : m->items())
			{
				if (!v.is_number() || v.get<float>() < 0.0f) return std::string("work: ") + part + " " + key + ": " + k + " must be a number of 0 or more";
				out[k] = v.get<float>();
			}
			return {};
		}

		Json WriteMap(const std::map<std::string, float>& m)
		{
			Json o = Json::object();
			for (const auto& [k, v] : m) o[k] = Rounded(v);
			return o;
		}
	}

	WorkRules DefaultWorkRules()
	{
		WorkRules w;
		w.Names = { "Ana", "Biel", "Cleo", "Dan", "Eli", "Fran", "Gala", "Hugo", "Iris", "Joel", "Kira", "Leo", "Mara", "Nil", "Olga", "Pau", "Rita", "Saul", "Tea", "Unai" };
		w.Skills = DefaultSkillRules();
		w.Jobs.Priority = { { "gather", 1.0f }, { "craft", 1.4f }, { "stoke", 1.6f }, { "fire", 1.2f }, { "build", 1.3f }, { "deliver", 1.1f } };   // (H8: the construction sites)
		w.Jobs.Upkeep = { { "fiber", 1.0f } };
		return w;
	}

	std::string MemberName(const WorkRules& rules, size_t n)
	{
		if (rules.Names.empty()) return "Member" + std::to_string(n + 1);
		const std::string& base = rules.Names[n % rules.Names.size()];
		const size_t round = n / rules.Names.size();
		return round == 0 ? base : base + std::to_string(round + 1);
	}

	std::string LoadWorkRules(const std::string& json, WorkRules& live)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("work");
		if (section == j.end()) return {};
		if (!section->is_object()) return "work: expected an object";
		const Json& w = *section;
		WorkRules rules = live;
		for (const auto& [key, v] : w.items())
			if (key != "names" && key != "skills" && key != "jobs" && key != "roles") return "work: unknown part " + key;
		if (const auto n = w.find("names"); n != w.end())
		{
			if (!n->is_array()) return "work: names must be an array of names";
			rules.Names.clear();
			for (const Json& name : *n)
			{
				if (!name.is_string() || name.get<std::string>().empty()) return "work: every name must be a non-empty string";
				const std::string s = name.get<std::string>();
				if (s.find_first_of(" \t\"|") != std::string::npos) return "work: the name '" + s + "' has a space, a quote or a bar (it is an actor's name in commands)";
				if (std::find(rules.Names.begin(), rules.Names.end(), s) != rules.Names.end()) return "work: the name " + s + " is twice";
				rules.Names.push_back(s);
			}
		}
		if (const auto s = w.find("skills"); s != w.end())
		{
			if (!s->is_object()) return "work: skills must be an object";
			rules.Skills = SkillRules();
			if (std::string e = ReadNumbers(*s, "skills", kSkillFields, rules.Skills, { "families", "practice" }); !e.empty()) return e;
			if (const auto f = s->find("families"); f != s->end())
			{
				if (!f->is_object()) return "work: skills families must be an object of key -> family";
				for (const auto& [key, v] : f->items())
				{
					if (!v.is_string() || v.get<std::string>().empty()) return "work: the family of " + key + " must be a name";
					rules.Skills.Families[key] = v.get<std::string>();
				}
			}
			if (std::string e = ReadMap(*s, "skills", "practice", rules.Skills.Practice); !e.empty()) return e;
			if (rules.Skills.Max <= 0.0f) return "work: skills max must be above 0";
		}
		if (const auto jb = w.find("jobs"); jb != w.end())
		{
			if (!jb->is_object()) return "work: jobs must be an object";
			rules.Jobs = JobRules();
			if (std::string e = ReadNumbers(*jb, "jobs", kJobFields, rules.Jobs, { "priority", "upkeep", "maxSlots" }); !e.empty()) return e;
			if (const auto m = jb->find("maxSlots"); m != jb->end())
			{
				if (!m->is_number_integer() || m->get<int>() < 1) return "work: jobs maxSlots must be a whole number of 1 or more";
				rules.Jobs.MaxSlots = m->get<int>();
			}
			if (std::string e = ReadMap(*jb, "jobs", "priority", rules.Jobs.Priority); !e.empty()) return e;
			if (std::string e = ReadMap(*jb, "jobs", "upkeep", rules.Jobs.Upkeep); !e.empty()) return e;
			if (rules.Jobs.Every <= 0.0f || rules.Jobs.SlotUnits <= 0.0f || rules.Jobs.AgeSeconds <= 0.0f) return "work: jobs every, slotUnits and ageSeconds must be above 0";
		}
		if (const auto r = w.find("roles"); r != w.end())
		{
			if (!r->is_object()) return "work: roles must be an object";
			rules.Roles = RoleRules();
			if (std::string e = ReadNumbers(*r, "roles", kRoleFields, rules.Roles, { "minClan" }); !e.empty()) return e;
			if (const auto m = r->find("minClan"); m != r->end())
			{
				if (!m->is_number_integer() || m->get<int>() < 1) return "work: roles minClan must be a whole number of 1 or more";
				rules.Roles.MinClan = m->get<int>();
			}
			if (rules.Roles.Review <= 0.0f) return "work: roles review must be above 0";
		}
		live = std::move(rules);
		return {};
	}

	std::string LoadWorkRulesFile(const std::string& path, WorkRules& rules)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadWorkRules(ss.str(), rules);
		return error.empty() ? error : path + ": " + error;
	}

	std::string WorkRulesToJson(const WorkRules& rules)
	{
		Json out = Json::object();
		out["names"] = rules.Names;
		Json s = Json::object();
		for (const SkillField& f : kSkillFields) s[f.Key] = Rounded(rules.Skills.*(f.Field));
		Json families = Json::object();
		for (const auto& [k, v] : rules.Skills.Families) families[k] = v;
		s["families"] = families;
		s["practice"] = WriteMap(rules.Skills.Practice);
		out["skills"] = s;
		Json jb = Json::object();
		for (const JobField& f : kJobFields) jb[f.Key] = Rounded(rules.Jobs.*(f.Field));
		jb["maxSlots"] = rules.Jobs.MaxSlots;
		jb["priority"] = WriteMap(rules.Jobs.Priority);
		jb["upkeep"] = WriteMap(rules.Jobs.Upkeep);
		out["jobs"] = jb;
		Json r = Json::object();
		for (const RoleField& f : kRoleFields) r[f.Key] = Rounded(rules.Roles.*(f.Field));
		r["minClan"] = rules.Roles.MinClan;
		out["roles"] = r;
		return out.dump(2);
	}

	// ------------------------------------------------------------------ the board
	void JobBoard::Begin()
	{
		m_Posted.clear();
	}

	Job& JobBoard::Post(const std::string& kind, const std::string& target, const std::string& family, float amount, float base, int slots, const std::string& why, double now, const JobRules& rules)
	{
		const std::string id = kind + " " + target;
		auto it = m_Jobs.find(id);
		if (it == m_Jobs.end())
		{
			Job fresh;
			fresh.Id = id;
			fresh.Kind = kind;
			fresh.Target = target;
			fresh.Posted = now;
			fresh.Waiting = now;
			it = m_Jobs.emplace(id, fresh).first;
		}
		Job& job = it->second;
		job.Family = family;
		job.Amount = amount;
		job.Base = base;
		job.Slots = std::max(1, slots);
		job.Why = why;
		if (job.Claims.empty() && job.Waiting < 0.0) job.Waiting = now;   // its last claimant left: it waits from now
		// nobody on it: it rises with the wait (a job no one wants does not stay at the bottom for good)
		const float age = job.Claims.empty() ? std::min(rules.MaxAge, rules.AgeBoost * static_cast<float>(now - job.Waiting) / rules.AgeSeconds) : 0.0f;
		job.Priority = base * (1.0f + std::max(0.0f, age));
		m_Posted[id] = true;
		return job;
	}

	std::vector<std::string> JobBoard::End(double now, const JobRules& rules)
	{
		std::vector<std::string> gone;
		for (auto it = m_Jobs.begin(); it != m_Jobs.end();)
		{
			Job& job = it->second;
			if (!m_Posted.count(it->first))   // the need is met: its claims end with it
			{
				for (const JobClaim& c : job.Claims) gone.push_back(c.Member + " " + job.Id);
				it = m_Jobs.erase(it);
				continue;
			}
			for (size_t i = 0; i < job.Claims.size();)
			{
				const JobClaim& c = job.Claims[i];
				const bool lapsed = now - c.Progress > static_cast<double>(rules.ClaimSeconds);
				const bool over = static_cast<int>(i) >= job.Slots;   // fewer slots than before: the last ones to claim go
				if (lapsed || over)
				{
					gone.push_back(c.Member + " " + job.Id);
					job.Claims.erase(job.Claims.begin() + static_cast<std::ptrdiff_t>(i));
				}
				else ++i;
			}
			if (job.Claims.empty() && job.Waiting < 0.0) job.Waiting = now;
			++it;
		}
		m_Posted.clear();
		return gone;
	}

	const Job* JobBoard::Find(const std::string& id) const
	{
		const auto it = m_Jobs.find(id);
		return it == m_Jobs.end() ? nullptr : &it->second;
	}

	const Job* JobBoard::ClaimOf(const std::string& member) const
	{
		for (const auto& [id, job] : m_Jobs)
			for (const JobClaim& c : job.Claims)
				if (c.Member == member) return &job;
		return nullptr;
	}

	bool JobBoard::Claim(const std::string& id, const std::string& member, double now)
	{
		const auto it = m_Jobs.find(id);
		if (it == m_Jobs.end()) return false;
		if (const Job* held = ClaimOf(member); held && held->Id == id) return true;
		if (!HasRoom(it->second)) return false;
		Release(member);
		it->second.Claims.push_back({ member, now, now, 0.0f });
		it->second.Waiting = -1.0;   // somebody is on it: it stops ageing
		it->second.Priority = it->second.Base;
		return true;
	}

	void JobBoard::Release(const std::string& member)
	{
		for (auto& [id, job] : m_Jobs)
		{
			const auto before = job.Claims.size();
			job.Claims.erase(std::remove_if(job.Claims.begin(), job.Claims.end(), [&](const JobClaim& c) { return c.Member == member; }), job.Claims.end());
			if (job.Claims.size() != before && job.Claims.empty()) job.Waiting = -1.0;   // set to the time of the next End: it waits again from then
		}
	}

	void JobBoard::Progress(const std::string& member, float units, double now)
	{
		for (auto& [id, job] : m_Jobs)
			for (JobClaim& c : job.Claims)
				if (c.Member == member) { c.Progress = now; c.Done += units; }
	}

	std::vector<const Job*> JobBoard::Ordered() const
	{
		std::vector<const Job*> out;
		for (const auto& [id, job] : m_Jobs) out.push_back(&job);
		std::stable_sort(out.begin(), out.end(), [](const Job* a, const Job* b) { return a->Priority > b->Priority; });
		return out;
	}

	std::string JobBoard::Report() const
	{
		std::string s;
		for (const Job* job : Ordered())
		{
			char head[96];
			std::snprintf(head, sizeof head, " x%.0f (p %.2f, %zu/%d): ", static_cast<double>(job->Amount), static_cast<double>(job->Priority), job->Claims.size(), job->Slots);
			std::string who;
			for (const JobClaim& c : job->Claims) who += (who.empty() ? "" : ", ") + c.Member;
			s += job->Id + head + (who.empty() ? "nobody" : who) + (job->Why.empty() ? "" : " | " + job->Why) + "\n";
		}
		return s.empty() ? "the board is empty: the clan needs nothing now\n" : s;
	}

	std::string JobBoard::ToJson() const
	{
		Json jobs = Json::array();
		for (const auto& [id, job] : m_Jobs)
		{
			Json claims = Json::array();
			for (const JobClaim& c : job.Claims) claims.push_back(Json{ { "member", c.Member }, { "since", c.Since }, { "progress", c.Progress }, { "done", c.Done } });
			jobs.push_back(Json{ { "kind", job.Kind }, { "target", job.Target }, { "family", job.Family }, { "why", job.Why }, { "amount", job.Amount }, { "base", job.Base },
				{ "priority", job.Priority }, { "slots", job.Slots }, { "posted", job.Posted }, { "waiting", job.Waiting }, { "claims", claims } });
		}
		return jobs.dump();
	}

	bool JobBoard::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_array()) return false;
		std::map<std::string, Job> read;
		for (const Json& o : j)
		{
			if (!o.is_object() || !o.contains("kind") || !o["kind"].is_string() || !o.contains("target") || !o["target"].is_string()) return false;
			Job job;
			job.Kind = o["kind"].get<std::string>();
			job.Target = o["target"].get<std::string>();
			job.Id = job.Kind + " " + job.Target;
			job.Family = o.value("family", std::string());
			job.Why = o.value("why", std::string());
			job.Amount = o.value("amount", 0.0f);
			job.Base = o.value("base", 1.0f);
			job.Priority = o.value("priority", job.Base);
			job.Slots = std::max(1, o.value("slots", 1));
			job.Posted = o.value("posted", 0.0);
			job.Waiting = o.value("waiting", 0.0);
			if (o.contains("claims") && o["claims"].is_array())
				for (const Json& c : o["claims"])
				{
					if (!c.is_object() || !c.contains("member") || !c["member"].is_string()) return false;
					job.Claims.push_back({ c["member"].get<std::string>(), c.value("since", 0.0), c.value("progress", 0.0), c.value("done", 0.0f) });
				}
			read[job.Id] = std::move(job);
		}
		m_Jobs = std::move(read);
		m_Posted.clear();
		return true;
	}
}
