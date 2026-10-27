// The editor's side of the Gameplay Ability System: the `garden` and `ability` commands and the Abilities panel.
#include "EditorApp.h"
#include "GB/Log.h"
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		std::string Quoted(const std::string& s) { return s.find(' ') == std::string::npos ? s : "\"" + s + "\""; }

		std::string Join(const std::vector<std::string>& v, const char* sep = ", ")
		{
			std::string s;
			for (const std::string& x : v) s += (s.empty() ? "" : sep) + x;
			return s;
		}

		std::string Number(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.4g", static_cast<double>(v));
			return b;
		}
	}

	void EditorApp::SyncGameplay()
	{
		m_Gameplay.Load(m_SceneEditor.World());
		m_Garden.Bind(m_Gameplay, &m_SceneEditor.World());
		m_Garden.SyncVisuals();
		BindResources();
		m_Actors.PrepareAbilities();   // the new systems get the player's character at once (a script can activate in this same frame)
	}

	void EditorApp::BindResources()
	{
		m_Resources.Bind(m_Gameplay, m_NodeDefs, [this](const std::string& who, const std::string& item, int count)
		{
			if (Actor* actor = m_Actors.Find(who)) actor->Bag().Add(m_Items, item, count);
		});
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
	}

	void EditorApp::StepResources()
	{
		for (const std::string& name : m_Actors.Names())
			if (AbilitySystemComponent* asc = m_Gameplay.Find(name))
				ResourceSystem::SyncTools(*asc, m_Actors.Find(name)->Bag(), m_Items);
		m_Resources.SyncVisuals(m_Gameplay, m_SceneEditor.World());
	}

	CommandResult EditorApp::ResourcesCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			glm::vec3 origin = m_Orbit.Target;
			if (a.size() > 2) { origin.x = static_cast<float>(std::atof(a[1].c_str())); origin.z = static_cast<float>(std::atof(a[2].c_str())); }
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;
			const ResourceLayout layout = BuildResources(m_SceneEditor.World(), origin, m_NodeDefs);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			return { true, std::to_string(layout.Nodes.size()) + " resource nodes built at (" + std::to_string(static_cast<int>(origin.x)) + ", " + std::to_string(static_cast<int>(origin.z))
				+ "). Gather with: ability activate Gather \"berry_bush 1\" (Chop needs an axe, Mine a pickaxe: actor bag <name> craft stone_axe workbench)" };
		}
		std::string s;
		for (const std::string& n : m_Gameplay.Names())
			if (const AbilitySystemComponent* node = m_Gameplay.Find(n); node && node->HasTag("Resource.Node"))
				s += n + ": " + std::to_string(ResourceSystem::Amount(*node)) + (node->HasTag("State.Depleted") ? " (depleted)" : "") + "; ";
		if (s.empty()) return { false, "no resource nodes in the scene (resources build)" };
		return { true, s };
	}

	CommandResult EditorApp::GardenCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "build")
		{
			glm::vec3 origin = m_Orbit.Target;
			if (a.size() > 2) { origin.x = static_cast<float>(std::atof(a[1].c_str())); origin.z = static_cast<float>(std::atof(a[2].c_str())); }
			origin.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(origin.x, origin.z) : 0.0f;
			const GardenLayout layout = BuildGarden(m_SceneEditor.World(), origin);
			m_SceneEditor.Select(NullEntity);
			SyncGameplay();
			m_SceneEditor.Commit();
			std::ostringstream message;   // (a fixed buffer with names of any length: gcc rejects it as a possible truncation)
			message << std::fixed << std::setprecision(1) << "garden built at (" << origin.x << ", " << origin.y << ", " << origin.z << "): " << layout.Plots.size()
				<< " plots, a tree spot, a fruit tree, a cow, " << layout.Boxes.size() << " boxes, a wheelbarrow, the " << layout.Player << " and the " << layout.Npc
				<< ". Start the player (player <character.fbx>) after it to walk around the props, then: ability activate Dig \"Plot 1\"";
			return { true, message.str() };
		}
		if (sub == "status")
		{
			std::string s;
			for (const std::string& n : m_Gameplay.Names())
			{
				const AbilitySystemComponent* e = m_Gameplay.Find(n);
				std::string st = GardenSystem::SoilState(*e);
				if (st.empty()) st = GardenSystem::TreeState(*e);
				if (st.empty()) continue;
				s += n + ": " + st + (e->HasTag("State.Wet") ? " (wet)" : "") + (e->Attributes().Has("Fruit") ? ", fruit " + Number(e->Attribute("Fruit")) : "") + "\n";
			}
			if (s.empty()) return { false, "no garden in the scene (garden build)" };
			return { true, s + m_Gameplay.Describe(m_AbilityActor) };
		}
		return { false, "usage: garden build [x z] | garden status" };
	}

	CommandResult EditorApp::AbilityCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "list" : a[0];
		auto find = [this](const std::string& name, std::string& error) -> AbilitySystemComponent*
		{
			AbilitySystemComponent* e = m_Gameplay.Find(name);
			if (!e) error = "no ability system called '" + name + "' (garden build, or give an entity with a gameplay component). Have: " + Join(m_Gameplay.Names());
			return e;
		};
		std::string error;
		if (sub == "sync")
		{
			SyncGameplay();
			return { true, std::to_string(m_Gameplay.Count()) + " ability systems from the scene" };
		}
		if (sub == "store")
		{
			const int n = m_Gameplay.Store(m_SceneEditor.World());
			m_SceneEditor.Commit();
			return { true, "state of " + std::to_string(n) + " ability systems written into the scene's entities" };
		}
		if (sub == "list" || sub == "status")
		{
			AbilitySystemComponent* e = find(a.size() > 1 ? a[1] : m_AbilityActor, error);
			if (!e) return { false, error };
			std::string s;
			if (sub == "list")
			{
				for (const std::string& n : e->Granted())
				{
					const AbilityDef* d = m_Gameplay.Library.Ability(n);
					std::string state = e->IsActive(n) ? "running" : e->CooldownRemaining(n) > 0.0f ? "cooldown " + Number(e->CooldownRemaining(n)) + " s" : "ready";
					s += n + " [" + state + "]" + (d && !d->Note.empty() ? " - " + d->Note : "") + "\n";
				}
				return { true, s.empty() ? e->Name() + " has no abilities" : e->Name() + ":\n" + s };
			}
			s = m_Gameplay.Describe(e->Name()) + "\ntags: " + Join(e->Tags().Names()) + "\neffects: ";
			for (const ActiveEffect& fx : e->ActiveEffects()) s += fx.Def.Name + (fx.Def.Policy == EffectPolicy::Duration ? "(" + Number(fx.Remaining) + " s) " : " ");
			s += "\nrunning: " + Join(e->ActiveAbilities());
			if (!e->LastError().empty()) s += "\nlast failure: " + e->LastError();
			return { true, s };
		}
		if (sub == "grant")
		{
			if (a.size() < 2) return { false, "usage: ability grant <ability> [entity]" };
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			return e->Grant(a[1]) ? CommandResult{ true, e->Name() + " can now " + a[1] } : CommandResult{ false, "unknown ability '" + a[1] + "'" };
		}
		if (sub == "apply")
		{
			if (a.size() < 2) return { false, "usage: ability apply <effect> [entity]" };
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			return e->ApplyEffect(a[1]) ? CommandResult{ true, "applied " + a[1] + " to " + e->Name() } : CommandResult{ false, "unknown effect '" + a[1] + "'" };
		}
		if (sub == "activate")
		{
			if (a.size() < 2) return { false, "usage: ability activate <ability> [target entity] [actor]" };
			const std::string target = a.size() > 2 ? a[2] : "";
			const std::string actor = a.size() > 3 ? a[3] : m_AbilityActor;
			AbilitySystemComponent* e = find(actor, error);
			if (!e) return { false, error };
			if (!target.empty() && !m_Gameplay.Find(target)) return { false, "no target called '" + target + "'" };
			std::string why;
			const bool ok = m_Actors.Find(actor) ? m_Actors.Find(actor)->UseAbility(a[1], target, why) : e->Activate(a[1], target.empty() ? nullptr : m_Gameplay.Find(target), &why, target);
			return ok ? CommandResult{ true, actor + ": " + a[1] + (target.empty() ? "" : " on " + target) + (e->Runtime() ? "" : " (no character: runs on timers)") } : CommandResult{ false, actor + " cannot " + a[1] + ": " + why };
		}
		if (sub == "cancel")
		{
			const std::string which = a.size() > 1 ? a[1] : "all";
			AbilitySystemComponent* e = find(a.size() > 2 ? a[2] : m_AbilityActor, error);
			if (!e) return { false, error };
			if (which == "all") { e->CancelAll(); return { true, "cancelled everything " + e->Name() + " was doing" }; }
			return e->Cancel(which) ? CommandResult{ true, "cancelled " + which } : CommandResult{ false, which + " is not running" };
		}
		return { false, "usage: ability list|status [entity] | grant <ability> [entity] | activate <ability> [target] [actor] | cancel [ability] [actor] | apply <effect> [entity] | sync | store" };
	}

	void EditorApp::DrawAbilitiesPanel()
	{
		if (ImGui::Begin("Abilities", &m_Panels.Abilities))
		{
			const std::vector<std::string> names = m_Gameplay.Names();
			if (names.empty())
			{
				ImGui::TextWrapped("No ability systems in the scene. Build the vegetable garden with: garden build");
				if (ImGui::Button("Build the garden")) m_UiCommands.push_back("garden build");
				ImGui::End();
				return;
			}
			if (ImGui::BeginCombo("Entity", m_AbilityActor.c_str()))
			{
				for (const std::string& n : names)
					if (ImGui::Selectable(n.c_str(), n == m_AbilityActor)) m_AbilityActor = n;
				ImGui::EndCombo();
			}
			AbilitySystemComponent* asc = m_Gameplay.Find(m_AbilityActor);
			if (!asc) { ImGui::TextUnformatted("(gone)"); ImGui::End(); return; }
			ImGui::TextDisabled("%s", asc->Runtime() ? "driving the player's character" : "no character: abilities run on timers");

			static char target[96] = "Plot 1";
			ImGui::InputText("Target", target, sizeof target);
			if (ImGui::CollapsingHeader("Abilities", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const std::string& n : asc->Granted())
				{
					const AbilityDef* d = m_Gameplay.Library.Ability(n);
					const bool running = asc->IsActive(n);
					const float cooldown = asc->CooldownRemaining(n);
					std::string why;
					const bool can = asc->CanActivate(n, d && d->NeedsTarget ? m_Gameplay.Find(target) : nullptr, &why);
					ImGui::PushID(n.c_str());
					if (running)
					{
						if (ImGui::SmallButton("Cancel")) m_UiCommands.push_back("ability cancel " + n + " " + Quoted(asc->Name()));
					}
					else
					{
						ImGui::BeginDisabled(!can);
						if (ImGui::SmallButton("Use")) m_UiCommands.push_back("ability activate " + n + " " + (d && d->NeedsTarget ? Quoted(target) : std::string("\"\"")) + " " + Quoted(asc->Name()));
						ImGui::EndDisabled();
					}
					ImGui::SameLine();
					ImGui::Text("%s", n.c_str());
					ImGui::SameLine();
					if (running) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "running");
					else if (cooldown > 0.0f) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "cooldown %.1f s", static_cast<double>(cooldown));
					else if (can) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "ready");
					else ImGui::TextDisabled("%s", why.c_str());
					if (d && !d->Note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d->Note.c_str());
					ImGui::PopID();
				}
			}
			if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen))
				for (const AttributeSet::Attribute& at : asc->Attributes().All())
				{
					const float span = at.Max - at.Min;
					char label[64];
					std::snprintf(label, sizeof label, "%s %.4g / %.4g", at.Name.c_str(), static_cast<double>(at.Current), static_cast<double>(at.Max));
					ImGui::ProgressBar(span > 0.0f && span < 1e6f ? (at.Current - at.Min) / span : 0.0f, ImVec2(-1.0f, 0.0f), label);
				}
			if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen))
				for (const auto& [tag, count] : asc->Tags().All())
					ImGui::BulletText("%s%s", tag.c_str(), count > 1 ? (" x" + std::to_string(count)).c_str() : "");
			if (ImGui::CollapsingHeader("Effects and cooldowns", ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const ActiveEffect& fx : asc->ActiveEffects())
				{
					if (fx.Def.Policy == EffectPolicy::Duration) ImGui::BulletText("%s: %.1f s left", fx.Def.Name.c_str(), static_cast<double>(fx.Remaining));
					else ImGui::BulletText("%s%s", fx.Def.Name.c_str(), fx.Def.Period > 0.0f ? (" (every " + Number(fx.Def.Period) + " s)").c_str() : "");
				}
				if (asc->ActiveEffects().empty()) ImGui::TextDisabled("none");
			}
			if (!asc->LastError().empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "last failure: %s", asc->LastError().c_str());
			if (ImGui::CollapsingHeader("Garden"))
			{
				for (const std::string& n : names)
				{
					const AbilitySystemComponent* e = m_Gameplay.Find(n);
					std::string st = GardenSystem::SoilState(*e);
					if (st.empty()) st = GardenSystem::TreeState(*e);
					if (st.empty() && !e->HasTag("State.Cow")) continue;
					ImGui::Text("%s: %s%s", n.c_str(), st.empty() ? "cow" : st.c_str(), e->HasTag("State.Wet") ? " (wet)" : "");
				}
			}
		}
		ImGui::End();
	}
}
