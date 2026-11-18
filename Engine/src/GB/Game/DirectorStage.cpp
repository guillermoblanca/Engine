#include "GB/Game/WorldDirector.h"
#include "GB/AI/Needs.h"
#include "GB/Game/Clans.h"
#include "GB/Game/History.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Ecology.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

// D48 (MODO_DIOS_DECISIONES.md §6.2 B and §6.3): the stages of the god's legacy. A stage is a list of generic goals in data (the top-level "stages"
// of assets/data/director.json); fulfilling Need of them wins it, and the player opens the next one. The director only READS the clans, their
// clock and the world's history here: nothing of the simulation changes, so a run without orders is the same with stages or without. Only with
// the stakes on (GB.exe's god's game, GBSim) are the goals looked at, as the defeat.
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
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }
		int Living(const Tribe& t)
		{
			int n = 0;
			for (const TribeMember& m : t.Members()) if (!m.Dead) ++n;
			return n;
		}
		int Starved(const Tribe& t)
		{
			int n = 0;
			for (const TribeMember& m : t.Members()) if (m.Dead && m.Cause == NeedName::Hunger) ++n;
			return n;
		}
		const char* kTypes[] = { "population", "tech", "techs", "believers", "signs_built", "no_hunger_winter", "peace", "building" };
		bool KnownType(const std::string& t) { return std::any_of(std::begin(kTypes), std::end(kTypes), [&](const char* k) { return t == k; }); }
		bool Int(const Json& o, const char* key, int& out, int lo, int hi, const std::string& where, std::string& e)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_number_integer() || it->get<int>() < lo || it->get<int>() > hi) { e = where + key + ": expected an integer in " + std::to_string(lo) + ".." + std::to_string(hi); return false; }
			out = it->get<int>();
			return true;
		}
		bool Known(const Json& o, std::initializer_list<const char*> keys, const std::string& where, std::string& e)
		{
			for (const auto& [k, v] : o.items())
				if (std::none_of(keys.begin(), keys.end(), [&](const char* known) { return k == known; })) { e = where + "unknown key '" + k + "'"; return false; }
			return true;
		}
		// the finished buildings of the id in a clan's settlement (0 when its defs do not have it)
		int BuiltOf(const Tribe& t, const std::string& id)
		{
			const std::map<std::string, BuildingDef>* defs = t.Buildings();
			if (!defs) return 0;
			const auto def = defs->find(id);
			if (def == defs->end()) return 0;
			int n = 0;
			for (const Tribe::SettlementSite& s : t.SettlementSites()) if (s.State.Done && s.Building == def->second.Name) ++n;
			return n;
		}
		// the target of a goal: what its Value climbs to
		int Target(const DirectorGoalDef& g) { return g.Type == "tech" ? g.Clans : g.Count; }
	}

	// { "stages": { "lookSeconds": s, "list": [ { "id", "name", "need", "draft", "goals": [ { "id", "type", "tech", "building", "count", "clans", "foodDays",
	//   "survival" } ] } ] } }. All or nothing: on a mistake it says why and the stages stay as they were.
	std::string ReadDirectorStages(const Json& s, DirectorStageRules& live)
	{
		const std::string w = "stages: ";
		if (!s.is_object()) return w + "expected an object";
		std::string e;
		if (!Known(s, { "_doc", "lookSeconds", "list" }, w, e)) return e;
		DirectorStageRules r = live;
		if (const auto it = s.find("lookSeconds"); it != s.end())
		{
			if (!it->is_number() || it->get<double>() < 1.0 || it->get<double>() > 1200.0) return w + "lookSeconds: expected a number in 1..1200";
			r.LookSeconds = it->get<float>();
		}
		if (const auto list = s.find("list"); list != s.end())
		{
			if (!list->is_array()) return w + "list: expected an array";
			r.List.clear();
			for (const Json& st : *list)
			{
				if (!st.is_object()) return w + "list: expected objects";
				DirectorStageDef d;
				d.Id = st.value("id", std::string());
				const std::string ws = w + (d.Id.empty() ? std::string("a stage") : d.Id) + ": ";
				if (!Known(st, { "_doc", "id", "name", "need", "draft", "goals" }, ws, e)) return e;
				if (d.Id.empty()) return ws + "id: required";
				for (const DirectorStageDef& o : r.List) if (o.Id == d.Id) return ws + "id: twice";
				d.Name = st.value("name", d.Id);
				if (const auto dr = st.find("draft"); dr != st.end()) { if (!dr->is_boolean()) return ws + "draft: expected true or false"; d.Draft = dr->get<bool>(); }
				const auto goals = st.find("goals");
				if (goals == st.end() || !goals->is_array() || goals->empty()) return ws + "goals: expected a list of goals";
				for (const Json& go : *goals)
				{
					if (!go.is_object()) return ws + "goals: expected objects";
					DirectorGoalDef g;
					g.Id = go.value("id", std::string());
					const std::string wg = ws + (g.Id.empty() ? std::string("a goal") : g.Id) + ": ";
					if (!Known(go, { "_doc", "id", "type", "tech", "building", "count", "clans", "foodDays", "survival" }, wg, e)) return e;
					if (g.Id.empty()) return wg + "id: required";
					for (const DirectorGoalDef& o : d.Goals) if (o.Id == g.Id) return wg + "id: twice";
					g.Type = go.value("type", std::string());
					if (!KnownType(g.Type)) return wg + "type: expected population, tech, techs, believers, signs_built, no_hunger_winter, peace or building";
					if (const auto t = go.find("tech"); t != go.end()) { if (!t->is_string()) return wg + "tech: expected a technology id"; g.Tech = t->get<std::string>(); }
					if (g.Type == "tech" && g.Tech.empty()) return wg + "tech: required by the type tech";
					if (const auto b = go.find("building"); b != go.end()) { if (!b->is_string() || b->get<std::string>().empty()) return wg + "building: expected a building id"; g.Building = b->get<std::string>(); }
					if (g.Type == "building" && g.Building.empty()) return wg + "building: required by the type building";
					if (!Int(go, "count", g.Count, 1, 100000, wg, e) || !Int(go, "clans", g.Clans, 1, 100, wg, e)) return e;
					if (const auto f = go.find("foodDays"); f != go.end())
					{
						if (!f->is_number() || f->get<double>() < 0.0 || f->get<double>() > 100.0) return wg + "foodDays: expected a number in 0..100";
						g.FoodDays = f->get<float>();
					}
					if (const auto v = go.find("survival"); v != go.end()) { if (!v->is_boolean()) return wg + "survival: expected true or false"; g.Survival = v->get<bool>(); }
					d.Goals.push_back(std::move(g));
				}
				if (!Int(st, "need", d.Need, 0, static_cast<int>(d.Goals.size()), ws, e)) return e;
				r.List.push_back(std::move(d));
			}
		}
		live = std::move(r);
		return {};
	}

	// ------------------------------------------------------------------ what the player sees
	const DirectorStageDef* WorldDirector::Stage() const
	{
		if (m_StagesEnded || m_Stage < 0 || m_Stage >= static_cast<int>(Rules.Stages.List.size())) return nullptr;
		return &Rules.Stages.List[static_cast<size_t>(m_Stage)];
	}

	DirectorGoalState WorldDirector::GoalOf(const std::string& goal) const
	{
		const auto it = m_Goals.find(goal);
		return it == m_Goals.end() ? DirectorGoalState{} : it->second;
	}

	int WorldDirector::GoalsDone() const
	{
		const DirectorStageDef* st = Stage();
		if (!st) return 0;
		int n = 0;
		for (const DirectorGoalDef& g : st->Goals) if (GoalOf(g.Id).Done) ++n;
		return n;
	}

	int WorldDirector::GoalsNeeded() const
	{
		const DirectorStageDef* st = Stage();
		if (!st) return 0;
		return st->Need > 0 ? st->Need : static_cast<int>(st->Goals.size());
	}

	// ------------------------------------------------------------------ looking at the goals
	void WorldDirector::OpenStage(int index, double now)
	{
		m_Stage = index;
		m_Goals.clear();
		m_StageSince = now;
		m_StageWonAt = -1.0;
		m_StageLook = -1.0;
		m_LastClash = -1.0;
		m_StageOpen = Stage() != nullptr;
		if (const DirectorStageDef* st = Stage())   // (a building goal the data names wrongly: said once, and it never counts)
			for (const DirectorGoalDef& g : st->Goals)
				if (!GoalBuildingKnown(g)) m_Book.push_back({ now, "stage", "world", "the goal " + g.Id + " asks for the building " + g.Building + ", which the clans do not have: it never counts", 0.0f });
		if (m_To.Clans)   // (peace counts from now: what was before is not this stage's)
			for (const HistoryEvent& e : m_To.Clans->Past().Events()) m_StageEvent = std::max(m_StageEvent, e.Id);
	}

	bool WorldDirector::Counted(int clan) const
	{
		return m_Colony ? clan == ColonyClan() : FaithOf(clan) >= Rules.MinFaith;   // T269 (C-4): the stages count the player's clan, with no filter of faith
	}

	void WorldDirector::LookGoal(const DirectorGoalDef& g, DirectorGoalState& s, double now)
	{
		const double day = static_cast<double>(Rules.DaySeconds);
		// the believing clans, in id order
		std::vector<const Clan*> believers;
		for (const auto& c : m_To.Clans->All())
			if (c->Folk && Living(*c->Folk) > 0 && Counted(c->Id)) believers.push_back(c.get());   // (T269: in the colony, the player's clan alone)
		// the Clans-th best of a number over the believing clans (and who leads it)
		auto nth = [&](auto value)
		{
			std::vector<std::pair<int, int>> v;   // (value, -clan id): the best first, ties to the lower id
			for (const Clan* c : believers) v.push_back({ value(*c), -c->Id });
			std::sort(v.begin(), v.end(), std::greater<>());
			s.Leader = v.empty() ? -1 : -v.front().second;
			const size_t k = static_cast<size_t>(std::max(1, g.Clans)) - 1;
			s.Value = k < v.size() ? v[k].first : 0;
		};
		if (g.Type == "population") nth([](const Clan& c) { return Living(*c.Folk); });
		else if (g.Type == "techs") nth([](const Clan& c) { return static_cast<int>(c.Folk->Knowledge().Clan().size()); });
		else if (g.Type == "tech")
		{
			s.Value = 0;
			s.Leader = -1;
			for (const Clan* c : believers)
				if (c->Folk->Knowledge().Clan().count(g.Tech)) { ++s.Value; if (s.Leader < 0) s.Leader = c->Id; }
		}
		else if (g.Type == "building")
		{
			nth([&](const Clan& c) { return BuiltOf(*c.Folk, g.Building); });
			if (s.Value == 0 && std::none_of(believers.begin(), believers.end(), [&](const Clan* c) { return BuiltOf(*c->Folk, g.Building) > 0; })) s.Leader = -1;   // (nobody has one yet)
		}
		else if (g.Type == "believers") s.Value = static_cast<int>(believers.size());
		else if (g.Type == "signs_built") s.Value = m_SignsBuilt;
		else if (g.Type == "peace")
		{
			const double from = std::max(m_StageSince, m_LastClash);
			s.Value = static_cast<int>(std::floor(std::max(0.0, now - from) / day));
		}
		else if (g.Type == "no_hunger_winter" && !m_To.Clans->All().empty() && m_To.Clans->All().front()->Folk)
		{
			const SeasonClock& clock = m_To.Clans->All().front()->Folk->Config.Seasons;
			const bool winter = clock.At(now) == Season::Winter;
			const double year = 4.0 * static_cast<double>(clock.SeasonSeconds);
			const int y = year > 0.0 ? static_cast<int>(std::floor(now / year)) : 0;
			if (winter)
			{
				if (s.Year != y)   // a winter begins to be watched: whole only when the look comes in its first look's time
				{
					s.Year = y;
					s.Whole = static_cast<double>(clock.Progress(now)) * static_cast<double>(clock.SeasonSeconds) <= 2.0 * static_cast<double>(Rules.Stages.LookSeconds) + 1.0;
					s.Hungry = false;
					s.Starved.clear();
					for (const auto& c : m_To.Clans->All()) if (c->Folk) s.Starved[c->Id] = Starved(*c->Folk);
				}
				for (const Clan* c : believers)
				{
					const Tribe& t = *c->Folk;
					const auto was = s.Starved.find(c->Id);
					const bool died = Starved(t) > (was == s.Starved.end() ? 0 : was->second);
					if (died || t.FoodDays() < g.FoodDays || (g.Survival && t.MaslowTier() == "survival")) { s.Hungry = true; s.Leader = c->Id; }
				}
			}
			else if (s.Year >= 0)   // the winter that was watched is over
			{
				if (s.Whole && !s.Hungry) ++s.Value;
				s.Year = -1;
				s.Whole = s.Hungry = false;
				s.Starved.clear();
			}
		}
		if (!s.Done && s.Value >= Target(g)) { s.Done = true; s.DoneAt = now; }
	}

	void WorldDirector::UpdateStage()
	{
		if (!m_Stakes || !m_Looked || !m_To.Clans || Defeated() || m_StagesEnded || Rules.Stages.List.empty()) return;
		const double now = Now();
		if (!m_StageOpen) OpenStage(m_Stage, now);
		if (StageWon() || !Stage()) return;   // (the stage waits for the player to move on: `director stage next`)
		// the clashes between clans since the last look (raids; a death or wound of a member by another clan's), for peace
		const std::vector<HistoryEvent>& past = m_To.Clans->Past().Events();
		for (auto it = past.rbegin(); it != past.rend() && it->Id > m_StageEvent; ++it)
		{
			bool clash = it->Kind == "raid" && it->Clan >= 0 && it->Other >= 0 && it->Clan != it->Other;
			if (!clash && (it->Kind == "killed" || it->Kind == "wounded") && it->Clan >= 0 && !it->Who.empty())
				for (const auto& c : m_To.Clans->All())
					if (c->Id != it->Clan && c->Folk && c->Folk->FindMember(it->Who)) { clash = true; break; }
			if (clash) m_LastClash = std::max(m_LastClash, it->At);
		}
		if (!past.empty()) m_StageEvent = std::max(m_StageEvent, past.back().Id);
		if (m_StageLook >= 0.0 && now - m_StageLook < static_cast<double>(Rules.Stages.LookSeconds)) return;
		m_StageLook = now;
		const DirectorStageDef& st = *Stage();
		for (const DirectorGoalDef& g : st.Goals) LookGoal(g, m_Goals[g.Id], now);
		if (GoalsDone() < GoalsNeeded()) return;
		m_StageWonAt = now;
		m_Book.push_back({ now, "stage", "world", "the stage " + std::to_string(StageNumber()) + " (" + st.Name + ") is fulfilled: " + std::to_string(GoalsDone()) + " of "
			+ std::to_string(st.Goals.size()) + " goals", 0.0f });   // (the book only, as the defeat: no row of telemetry, no event of the history)
	}

	// ------------------------------------------------------------------ orders
	std::string WorldDirector::StageText() const
	{
		const DirectorStageDef* st = Stage();
		if (!st) return Rules.Stages.List.empty() ? "director stage: no stages" : "director stage: every stage fulfilled";
		const double day = static_cast<double>(Rules.DaySeconds);
		std::string s = "director stage " + std::to_string(StageNumber()) + " of " + std::to_string(Rules.Stages.List.size()) + " (" + st->Id + ", " + st->Name + (st->Draft ? ", draft" : "")
			+ "): " + std::to_string(GoalsDone()) + " of " + std::to_string(st->Goals.size()) + " goals, needs " + std::to_string(GoalsNeeded())
			+ (StageWon() ? "; won on day " + Num(m_StageWonAt / day, 2) : std::string()) + (m_Stakes ? "" : " (no stakes: the goals do not count in this game)");
		for (const DirectorGoalDef& g : st->Goals)
		{
			const DirectorGoalState v = GoalOf(g.Id);
			const Clan* leader = v.Leader >= 0 && m_To.Clans ? m_To.Clans->Find(v.Leader) : nullptr;
			s += "\n[" + std::string(v.Done ? "x" : " ") + "] " + g.Id + " " + g.Type + (g.Tech.empty() ? std::string() : " " + g.Tech) + (g.Building.empty() ? std::string() : " " + g.Building)
				+ (GoalBuildingKnown(g) ? std::string() : " (unknown building: it never counts)") + ": " + std::to_string(v.Value) + "/" + std::to_string(Target(g))
				+ (leader ? " (" + NameOf(*leader) + ")" : std::string()) + (v.Done ? ", done on day " + Num(v.DoneAt / day, 2) : std::string())
				+ (g.Type == "no_hunger_winter" && v.Year >= 0 ? std::string(", in a winter") + (v.Hungry ? " with hunger" : v.Whole ? " with no hunger yet" : " (watched from its middle)") : std::string());
		}
		return s;
	}

	std::string WorldDirector::StageReport() const
	{
		const DirectorStageDef* st = Stage();
		const double day = static_cast<double>(Rules.DaySeconds);
		if (!st) return std::string("stage: ") + (Rules.Stages.List.empty() ? "none" : "every stage fulfilled");
		std::string s = "stage: " + std::to_string(StageNumber()) + ", goals " + std::to_string(GoalsDone()) + "/" + std::to_string(st->Goals.size()) + " (needs " + std::to_string(GoalsNeeded()) + "), "
			+ (StageWon() ? "won on day " + Num(m_StageWonAt / day, 2) : std::string("none")) + "\nstage goals:";
		for (const DirectorGoalDef& g : st->Goals)
		{
			const DirectorGoalState v = GoalOf(g.Id);
			s += " " + g.Id + " " + std::to_string(v.Value) + "/" + std::to_string(Target(g)) + (v.Done ? " (day " + Num(v.DoneAt / day, 1) + ")" : std::string()) + ";";
		}
		return s;
	}

	bool WorldDirector::GoalBuildingKnown(const DirectorGoalDef& g) const
	{
		if (g.Type != "building" || !m_To.Clans) return true;
		bool anyDefs = false;
		for (const auto& c : m_To.Clans->All())
			if (c->Folk && c->Folk->Buildings())
			{
				anyDefs = true;
				if (c->Folk->Buildings()->count(g.Building)) return true;
			}
		return !anyDefs;   // (clans with no buildings at all cannot tell)
	}

	// director stage | stage win (the sandbox: wins it now) | stage next (after a win: opens the next one)
	DirectorReply WorldDirector::StageCommand(const std::vector<std::string>& a)
	{
		if (a.size() < 2) return { true, StageText() };
		const double now = Now();
		if (a[1] == "win")
		{
			if (!m_Sandbox) return { false, "director stage win: only in the sandbox (director sandbox on)" };
			if (!Stage()) return { false, StageText() };
			if (StageWon()) return { false, "director stage win: the stage is fulfilled already" };
			if (!m_StageOpen) OpenStage(m_Stage, now);
			m_StageWonAt = now;
			m_Book.push_back({ now, "stage", "world", "the stage " + std::to_string(StageNumber()) + " (" + Stage()->Name + ") is fulfilled (the sandbox)", 0.0f });
			return { true, "director stage " + std::to_string(StageNumber()) + " fulfilled (the sandbox, day " + Num(now / static_cast<double>(Rules.DaySeconds), 2) + ")" };
		}
		if (a[1] == "next")   // [Go on]
		{
			if (!Stage() || !StageWon()) return { false, "director stage next: the stage is not fulfilled" };
			const int next = m_Stage + 1;
			if (next >= static_cast<int>(Rules.Stages.List.size()))
			{
				m_StagesEnded = true;
				m_StageWonAt = -1.0;
				m_Book.push_back({ now, "stage", "world", "every stage is fulfilled", 0.0f });
				return { true, "director stage: every stage fulfilled; the god keeps the world" };
			}
			OpenStage(next, now);
			m_Book.push_back({ now, "stage", "world", "the stage " + std::to_string(StageNumber()) + " (" + Stage()->Name + ") opens", 0.0f });
			return { true, "director stage " + std::to_string(StageNumber()) + " opens: " + Stage()->Name };
		}
		return { false, "usage: director stage [win|next]" };
	}

	// ------------------------------------------------------------------ saving
	std::string WorldDirector::StageToJson() const
	{
		if (!m_StageOpen && m_SignsBuilt == 0 && m_Stage == 0 && !m_StagesEnded) return {};
		Json j;
		j["index"] = m_Stage;
		j["id"] = Stage() ? Stage()->Id : std::string();
		j["open"] = m_StageOpen;
		j["since"] = m_StageSince;
		j["wonAt"] = m_StageWonAt;
		j["look"] = m_StageLook;
		j["ended"] = m_StagesEnded;
		j["event"] = m_StageEvent;
		j["lastClash"] = m_LastClash;
		j["signsBuilt"] = m_SignsBuilt;
		Json goals = Json::object();
		for (const auto& [id, s] : m_Goals)
		{
			Json starved = Json::object();
			for (const auto& [c, n] : s.Starved) starved[std::to_string(c)] = n;
			goals[id] = { { "done", s.Done }, { "value", s.Value }, { "leader", s.Leader }, { "doneAt", s.DoneAt }, { "year", s.Year }, { "whole", s.Whole }, { "hungry", s.Hungry }, { "starved", starved } };
		}
		j["goals"] = goals;
		return j.dump();
	}

	bool WorldDirector::StageFromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		m_Stage = j.value("index", 0);
		const std::string id = j.value("id", std::string());   // (the data may have moved the stages: the id wins)
		for (size_t i = 0; i < Rules.Stages.List.size(); ++i) if (!id.empty() && Rules.Stages.List[i].Id == id) m_Stage = static_cast<int>(i);
		m_StageOpen = j.value("open", false);
		m_StageSince = j.value("since", 0.0);
		m_StageWonAt = j.value("wonAt", -1.0);
		m_StageLook = j.value("look", -1.0);
		m_StagesEnded = j.value("ended", false);
		m_StageEvent = j.value("event", 0);
		m_LastClash = j.value("lastClash", -1.0);
		m_SignsBuilt = j.value("signsBuilt", 0);
		m_Goals.clear();
		if (const auto it = j.find("goals"); it != j.end() && it->is_object())
			for (const auto& [gid, g] : it->items())
			{
				DirectorGoalState s;
				s.Done = g.value("done", false);
				s.Value = g.value("value", 0);
				s.Leader = g.value("leader", -1);
				s.DoneAt = g.value("doneAt", -1.0);
				s.Year = g.value("year", -1);
				s.Whole = g.value("whole", false);
				s.Hungry = g.value("hungry", false);
				if (const auto st = g.find("starved"); st != g.end()) for (const auto& [k, v] : st->items()) s.Starved[std::stoi(k)] = v.get<int>();
				m_Goals[gid] = s;
			}
		return true;
	}
}
