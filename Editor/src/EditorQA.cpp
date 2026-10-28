// Simulation QA in the editor (H20): the `qa` command and the QA panel. Both use the table of invariants of GB/Game/SimQA.h, the same one the seed
// sweep of the simulation runs (tests/sim_sweep_test.cpp): `qa sim` runs the current scene headless for days in a few seconds, the live monitor checks
// the scene as it runs and the panel lists what broke. See docs/QA_SIMULACION.md.
#include "EditorApp.h"
#include "GB/Game/SimQA.h"
#include "GB/Log.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <regex>

namespace GB::Editor
{
	namespace
	{
		constexpr double kDaySeconds = 1200.0;   // NeedsConfig::DaySeconds
		constexpr uint64_t kQaEvery = 30;        // ticks between two live checks (half a simulated second)
	}

	// What the checks see of the live editor: the tribe, the ability systems and the scene, and the ground the actors walk (their host's world:
	// terrain, water and the static obstacles of trees, rocks and bodies).
	SimView EditorApp::QaView() const
	{
		SimView v;
		v.Clan = &m_Tribe;
		v.Gameplay = &m_Gameplay;
		v.Entities = &m_SceneEditor.World();
		v.Clock = static_cast<double>(m_SimTick) / 60.0;
		if (m_World.IsBuilt())
		{
			const ActorWorld* actors = &m_Actors;
			v.Ground = [actors](const glm::vec2& p) { return SimGroundAt(actors->Host().World, p); };
		}
		return v;
	}

	// Called once per simulation tick (EditorSaveHost::Step): every half second of simulation the live monitor checks the scene.
	void EditorApp::StepQa()
	{
		if (m_Tribe.Members().empty() || m_SimTick % kQaEvery != 0) return;
		const SimView v = QaView();
		m_SimQA.Sample(v);
		if (m_SimQA.Step(v) > 0 && !m_QaWarned)
		{
			m_QaWarned = true;   // once: the panel keeps the rest
			GB_CLIENT_WARN("QA: an invariant of the simulation broke ({}); see the QA panel or `qa status`", m_SimQA.Findings().empty() ? std::string() : m_SimQA.Findings().back().Check);
		}
	}

	CommandResult EditorApp::QaCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "sim")
		{
			if (!m_World.IsBuilt()) return { false, "generate a world first (generate temperate 5)" };
			const double days = a.size() > 1 ? std::max(0.05, std::atof(a[1].c_str())) : 1.0;
			SimSetup setup;
			setup.World = &m_World.World();
			setup.Items = &m_Items;
			setup.Recipes = &m_Recipes;
			setup.Nodes = &m_NodeDefs;
			setup.Config = &m_Tribe.Config;
			setup.Techs = &m_Techs;
			SyncRegions();
			if (!m_Regions.Empty()) setup.Regions = &m_Regions;
			if (m_Items.Ids().empty()) setup.Items = nullptr;   // nothing loaded yet: the built-in era 1
			EntityWorld scene = m_SceneEditor.World();
			bool nodes = false;
			for (const std::string& n : m_Gameplay.Names()) if (const AbilitySystemComponent* s = m_Gameplay.Find(n); s && s->HasTag("Resource.Node")) nodes = true;
			if (!m_Tribe.Members().empty())
			{
				// the current tribe: its members (their needs travel with their ability systems), its camp and its store; memory and decisions start afresh
				for (const TribeMember& m : m_Tribe.Members())
					if (!m.Dead)
					{
						setup.Roster.emplace_back(m.Name, m.Role);
						if (m.Body) setup.Bags[m.Name] = m.Body->Bag().ToJson();   // their tools go with them
					}
				setup.HasCamp = true;
				setup.Camp = m_Tribe.Config.Camp;
				setup.Store = m_Tribe.Store().ToJson();
			}
			else setup.Members = a.size() > 2 ? std::max(1, std::min(60, std::atoi(a[2].c_str()))) : 4;
			if (nodes || !m_Tribe.Members().empty())
			{
				m_Gameplay.Store(scene);   // the live state of the nodes and the members, not what was last saved
				setup.Scene = &scene;
			}
			const auto t0 = std::chrono::steady_clock::now();
			SimRun run(setup);
			if (!run.Ok()) return { false, "qa sim: " + run.Error() };
			run.Run(days * kDaySeconds);
			run.Finish();
			const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			char head[200];
			std::snprintf(head, sizeof head, "qa sim %.2g day(s), %d member(s), %zu nodes%s: %d violation(s) in %.1f s | ", days, run.Monitor().Metrics().Members, run.Nodes(),
				setup.Scene ? " (the current scene)" : " (the world's own resources)", run.Monitor().Violations(), took);
			m_QaLastSim = head + run.Monitor().Metrics().Line();
			m_QaLastFindings = run.Monitor().Describe(40);
			m_Panels.QA = true;
			m_FocusQA = 20;
			std::string text = m_QaLastSim;
			if (!m_QaLastFindings.empty()) { std::string lines = m_QaLastFindings; for (char& c : lines) if (c == '\n') c = '|'; text += " || " + lines; }
			return { run.Monitor().Violations() == 0, text };
		}
		if (sub == "check")
		{
			if (m_Tribe.Members().empty()) return { false, "no tribe to check (tribe build)" };
			SimMonitor once;
			once.Step(QaView());
			if (once.Violations() == 0) return { true, "qa check: every invariant holds (" + std::to_string(SimInvariants().size()) + " checked)" };
			std::string lines = once.Describe(20);
			for (char& c : lines) if (c == '\n') c = '|';
			return { false, "qa check: " + std::to_string(once.Violations()) + " violation(s): " + lines };
		}
		if (sub == "reset") { m_SimQA.Reset(); m_QaWarned = false; return { true, "the live QA monitor starts again" }; }
		if (sub == "expect")
		{
			// qa expect "<command>" <regex>: runs the command and fails unless its answer matches (the demos of the milestones assert with it)
			if (a.size() < 3) return { false, "usage: qa expect \"<command>\" <regex>" };
			std::string pattern;
			for (size_t i = 2; i < a.size(); ++i) pattern += (i > 2 ? " " : "") + a[i];
			const CommandResult r = Execute(a[1]);
			try
			{
				if (std::regex_search(r.Message, std::regex(pattern))) return { true, "qa expect: '" + a[1] + "' says /" + pattern + "/" };
			}
			catch (const std::regex_error&) { return { false, "qa expect: bad regex /" + pattern + "/" }; }
			std::string said = r.Message.size() > 300 ? r.Message.substr(r.Message.size() - 300) : r.Message;
			return { false, "qa expect: '" + a[1] + "' does not say /" + pattern + "/ (it said: " + said + ")" };
		}
		if (sub != "status") return { false, "usage: qa sim [days] [members] | check | status | reset | expect \"<command>\" <regex>" };
		SimMonitor& mon = m_SimQA;
		mon.Finish(QaView());
		std::string s = "qa: " + std::to_string(mon.Steps()) + " live checks, " + std::to_string(mon.Violations()) + " violation(s)";
		for (const auto& [check, n] : mon.CountByCheck()) if (n) s += " | " + check + " " + std::to_string(n);
		if (!m_Tribe.Members().empty()) s += " | abstract " + std::to_string(m_Tribe.AbstractCount()) + "/" + std::to_string(m_Tribe.Members().size()) + " | " + mon.Metrics().Line();
		return { true, s };
	}

	void EditorApp::DrawQaPanel()
	{
		if (m_FocusQA > 0) { ImGui::SetNextWindowFocus(); --m_FocusQA; }
		if (ImGui::Begin("QA", &m_Panels.QA))
		{
			ImGui::TextWrapped("Invariants of the simulation (the table the seed sweep runs), checked every half second of simulation while a tribe lives.");
			if (ImGui::Button("Check now")) m_UiCommands.push_back("qa check");
			ImGui::SameLine();
			if (ImGui::Button("Reset")) m_UiCommands.push_back("qa reset");
			ImGui::SameLine();
			if (ImGui::Button("Run 1 day headless")) m_UiCommands.push_back("qa sim 1");
			ImGui::Text("%d live checks, %d violation(s)", m_SimQA.Steps(), m_SimQA.Violations());
			const std::map<std::string, int> counts = m_SimQA.CountByCheck();
			if (ImGui::BeginTable("invariants", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, 80.0f);
				ImGui::TableSetupColumn("Seen", ImGuiTableColumnFlags_WidthFixed, 45.0f);
				ImGui::TableSetupColumn("Promise");
				ImGui::TableHeadersRow();
				for (const SimInvariant& inv : SimInvariants())
				{
					const auto it = counts.find(inv.Name);
					const int n = it == counts.end() ? 0 : it->second;
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextColored(n ? ImVec4(1.0f, 0.4f, 0.35f, 1.0f) : ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "%s", inv.Name.c_str());
					ImGui::TableNextColumn();
					ImGui::Text("%d", n);
					ImGui::TableNextColumn();
					ImGui::TextWrapped("%s", inv.Promise.c_str());
				}
				ImGui::EndTable();
			}
			if (!m_SimQA.Findings().empty() && ImGui::CollapsingHeader("Violations", ImGuiTreeNodeFlags_DefaultOpen))
				for (const SimFinding& f : m_SimQA.Findings())
					ImGui::TextWrapped("[%s] %s: %s (x%d, %.0f-%.0f s)", f.Check.c_str(), f.Subject.c_str(), f.What.c_str(), f.Count, f.First, f.Last);
			if (!m_Tribe.Members().empty() && ImGui::CollapsingHeader("Metrics (live)", ImGuiTreeNodeFlags_DefaultOpen))
			{
				m_SimQA.Finish(QaView());
				ImGui::TextWrapped("%s", m_SimQA.Metrics().Line().c_str());
			}
			if (!m_QaLastSim.empty() && ImGui::CollapsingHeader("Last qa sim", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::TextWrapped("%s", m_QaLastSim.c_str());
				if (!m_QaLastFindings.empty()) ImGui::TextWrapped("%s", m_QaLastFindings.c_str());
			}
		}
		ImGui::End();
	}
}
