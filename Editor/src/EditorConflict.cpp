// The editor's side of H14: the fights of the world in the simulation step (a character of the scene that dies in one falls as a ragdoll),
// the grievances that fade with the days, the `combat`, `hostile` and `raid` commands, and the "combat" section of a save. The fights live in
// EditorApp::m_Combat (shared by the tribe and the animals), the hostility in m_Hostility until the clans of H13 give their own.
#include "EditorApp.h"
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace GB::Editor
{
	namespace
	{
		std::string OneLine(std::string s)
		{
			while (!s.empty() && s.back() == '\n') s.pop_back();
			for (char& c : s) if (c == '\n') c = '|';   // one line: the console shows the first line of a message
			return s;
		}
	}

	void EditorApp::StepCombat()
	{
		const float day = m_Tribe.Config.Needs.DaySeconds;
		// every clan's tribe fights in the same world, under its clan's name, with the hostility of the diplomacy (H13; the table for the rest)
		m_ClanHostility.Bind(&m_Clans, &m_Hostility);
		for (const auto& c : m_Clans.All())
		{
			if (!c->Folk) continue;
			if (c->Folk->Combat() != &m_Combat || c->Folk->Hostility() != &m_ClanHostility) c->Folk->SetCombat(&m_Combat, &m_ClanHostility);
			if (c->Folk->ClanName() != c->Name && !c->Name.empty()) c->Folk->SetClanName(c->Name);
		}
		m_Combat.Update(static_cast<float>(1.0 / 60.0), day);
		// a character of the scene killed in a fight falls as a ragdoll (an animal or a stand-in just has the state)
		for (const CombatEvent& e : m_Combat.EventsSince(m_CombatSeq))
		{
			m_CombatSeq = e.Seq;
			if (e.Kind != "death") continue;
			if (Actor* actor = m_Actors.Find(e.Other)) actor->StartRagdoll();
		}
		// anyone dead of any cause (hunger, thirst, cold, wounds) falls as a ragdoll too, once, and leaves a cross on the map
		for (const auto& c : m_Clans.All())
		{
			if (!c->Folk) continue;
			for (const TribeMember& m : c->Folk->Members())
			{
				if (!m.Dead || m_Graves.count(m.Name)) continue;
				Actor* actor = m_Actors.Find(m.Name);
				if (!actor) continue;
				const glm::vec3 p = actor->GetPosition();
				m_Graves[m.Name] = { m.Name, m.Cause, { p.x, p.z } };
				if (!actor->Ragdolled()) actor->StartRagdoll();
			}
		}
		for (auto g = m_Graves.begin(); g != m_Graves.end();)   // (a new world or a loaded game forgets the crosses of the old one)
		{
			bool dead = false;
			for (const auto& c : m_Clans.All()) if (c->Folk) for (const TribeMember& m : c->Folk->Members()) dead = dead || (m.Dead && m.Name == g->first);
			g = dead ? std::next(g) : m_Graves.erase(g);
		}
		// grievances fade, day by day of the needs' clock
		const double today = std::floor(m_Combat.Time() / std::max(1.0f, day));
		if (today > m_HostilityDay) { m_Hostility.ForgetPerDay = m_Combat.Rules.Raids.Forget; m_Hostility.Forget(static_cast<float>(today - m_HostilityDay)); m_HostilityDay = today; }
	}

	bool EditorApp::RestoreCombat(const std::string& json)
	{
		const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		if (j.contains("fights") && !m_Combat.FromJson(j["fights"].dump())) return false;
		if (j.contains("hostility") && !m_Hostility.FromJson(j["hostility"].dump())) return false;
		m_CombatSeq = m_Combat.LastSeq();
		m_HostilityDay = std::floor(m_Combat.Time() / std::max(1.0f, m_Tribe.Config.Needs.DaySeconds));
		return true;
	}

	CommandResult EditorApp::CombatCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status")
		{
			if (a.size() > 1)
			{
				const std::string s = m_Combat.Status(a[1]);
				return { !s.empty(), s.empty() ? "'" + a[1] + "' is not in any fight (combat fight joins it)" : s };
			}
			std::string s = m_Combat.Report();
			for (const auto& c : m_Clans.All()) if (c->Folk && !c->Folk->Members().empty()) s += c->Folk->ConflictReport();
			if (const std::string problems = CheckCombat(m_Combat.Rules, m_Items); !problems.empty()) s += "content: " + problems;
			return { true, OneLine(s) };
		}
		if (sub == "log")
		{
			const size_t n = a.size() > 1 ? static_cast<size_t>(std::max(1, std::atoi(a[1].c_str()))) : 12;
			const std::vector<CombatEvent>& events = m_Combat.Events();
			std::string s;
			for (size_t i = events.size() > n ? events.size() - n : 0; i < events.size(); ++i)
			{
				const CombatEvent& e = events[i];
				char b[64];
				std::snprintf(b, sizeof b, "%.1f ", e.Time);
				s += b + e.Who + " " + e.Kind + " " + e.Other + (e.Text.empty() ? "" : " (" + e.Text + ")");
				if (e.Amount > 0.0f) { std::snprintf(b, sizeof b, " %.1f", static_cast<double>(e.Amount)); s += b; }
				s += "\n";
			}
			return { true, s.empty() ? "nothing has happened in any fight" : OneLine(s) };
		}
		// the others work on characters with an ability system: they join the fights as they are (a member of the tribe with its clan's side)
		auto join = [&](const std::string& name) -> Combatant*
		{
			if (Combatant* c = m_Combat.Find(name)) return c;
			if (Combatant* c = m_Combat.JoinAnimal(name)) return c;
			for (const auto& c : m_Clans.All())
				if (c->Folk) for (const TribeMember& m : c->Folk->Members()) if (m.Name == name) return m_Combat.Join(name, c->Folk->ClanName());
			return m_Combat.Join(name, name, !m_Actors.Find(name));   // anyone else is a side of its own (moved here unless it is an actor)
		};
		if (sub == "hurt" && a.size() > 2)
		{
			if (!join(a[1])) return { false, "'" + a[1] + "' has no ability system" };
			const float severity = a.size() > 3 ? static_cast<float>(std::atof(a[3].c_str())) : 0.0f;
			const bool died = m_Combat.Hurt(a[1], static_cast<float>(std::atof(a[2].c_str())), "cut", severity, "the editor");
			return { true, died ? a[1] + " dies" : m_Combat.Status(a[1]) };
		}
		if ((sub == "fight" || sub == "flee") && a.size() > 2)
		{
			if (!join(a[1]) || !join(a[2])) return { false, "both need an ability system" };
			const bool ok = sub == "fight" ? m_Combat.Engage(a[1], a[2]) : m_Combat.Flee(a[1], a[2]);
			return { ok, ok ? m_Combat.Status(a[1]) : "cannot (one of them is dead)" };
		}
		if (sub == "stop" && a.size() > 1)
		{
			m_Combat.Release(a[1]);
			return { true, m_Combat.Status(a[1]) };
		}
		return { false, "usage: combat status [name] | log [n] | hurt <name> <health> [severity] | fight <a> <b> | flee <a> <b> | stop <name>" };
	}

	CommandResult EditorApp::HostileCommand(const std::vector<std::string>& a)
	{
		if (a.empty()) return { true, OneLine(m_ClanHostility.Report() + m_Hostility.Report()) };

		if (a.size() < 2) return { false, "usage: hostile [a b [0-1]]" };
		const float value = a.size() > 2 ? static_cast<float>(std::atof(a[2].c_str())) : 1.0f;
		m_Hostility.Set(a[0], a[1], value);
		char b[96];
		std::snprintf(b, sizeof b, "%s and %s are hostile %.2f to each other", a[0].c_str(), a[1].c_str(), static_cast<double>(value));
		return { true, b };
	}

	CommandResult EditorApp::RaidCommand(const std::vector<std::string>& a)
	{
		Tribe& tribe = ActiveTribe();   // (the clan `clan select` chose)
		if (tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		if (a.empty() || a[0] == "status") return { true, OneLine(tribe.ConflictReport() + m_ClanHostility.Report()) };
		std::string why = "the editor sends it";
		if (!tribe.StartRaid(a[0], &why)) return { false, "no raid: " + why };
		return { true, tribe.ClanName() + " raids " + a[0] };
	}
}
