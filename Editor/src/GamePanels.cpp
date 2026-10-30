// The panels of the game mode that make the actions reachable without the console: an action bar at the bottom (every button runs a `game` command, the
// same the keys run), the bag with the recipes and the store (I), the people around with what can be done with them (T) and the buildings the clan can
// raise (B). Nothing here decides: each button queues `game <action>`, so a script plays the same game headless (GameMode.cpp, docs/JUGAR.md).
#include "EditorApp.h"
#include "SceneEditor.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		std::string Needs(const std::map<std::string, int>& needs)
		{
			std::string s;
			for (const auto& [item, n] : needs) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + item;
			return s;
		}

		// The key of an action as the data names it ("" when it has none), for the label of a button.
		std::string KeyOf(const std::map<std::string, std::string>& keys, const char* action)
		{
			const auto it = keys.find(action);
			return it == keys.end() ? std::string() : it->second;
		}
	}

	void EditorApp::DrawGamePanels(float ox, float oy, float w, float h)
	{
		const TribeMember* me = m_Tribe.FindMember(m_Game.Player);
		const Actor* actor = m_Actors.Find(m_Game.Player);
		if (!me || !actor) return;
		const glm::vec3 p3 = actor->GetPosition();
		const glm::vec2 pos(p3.x, p3.z);
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };

		// ---- the action bar: what can be done, with its key
		{
			struct Entry { const char* Label; const char* Action; const char* Command; const char* Tip; };
			static const Entry entries[] = {
				{ "Interact", "interact", "game interact", "Do what the prompt says (pick up, talk, build, feed the fire...)" },
				{ "Eat", "eat", "game eat", "Eat what you carry or, at the camp, from the store" },
				{ "Drink", "drink", "game drink", "Drink the water you carry" },
				{ "Leave", "deposit", "game deposit", "Leave what you carry in the store (at the camp)" },
				{ "Bag", "inventory", "game inventory", "Your bag, the recipes and the store" },
				{ "People", "people", "game people", "Talk, give, teach or marry the people near you" },
				{ "Build", "build", "game build", "The buildings your clan can raise" },
				{ "Attack", "attack", "game attack", "Attack the nearest foe (right mouse on an animal or foe does it too); Esc, right mouse on the ground or walking away breaks it off" },
				{ "Block", "block", "game block", "Raise your guard" },
				{ "Camera", "view", "game view", "Behind you / from above" },
				{ "Clan", "clan", "game clan", "Members, job board, store, knowledge and the other clans" },
				{ "Sleep", "sleep", "game sleep", "Sleep (in your bed if there is a shelter)" },
				{ "Help", "help", "game help", "The keys" },
			};
			ImGui::SetNextWindowPos(ImVec2(ox + 10.0f, oy + h - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
			ImGui::SetNextWindowBgAlpha(0.55f);
			if (ImGui::Begin("Actions##game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar))
			{
				bool first = true;
				for (const Entry& e : entries)
				{
					if (!first) ImGui::SameLine();
					first = false;
					const std::string key = KeyOf(m_Controls.Keys, e.Action);
					const std::string text = key.empty() ? std::string(e.Label) : std::string(e.Label) + " [" + key + "]";
					if (ImGui::Button(text.c_str())) run(e.Command);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.Tip);
				}
			}
			ImGui::End();
		}

		// ---- the left button picks a character or a node (the character wins, as in the editor); the panel below shows what it is.
		// The right button attacks: a click on someone who is not of the clan (an animal, a stranger) goes for it, one on the ground for the nearest foe;
		// a click on the ground while fighting, Esc, `game stop` or walking away breaks it off. (A right drag turns the camera: only a click counts.)
		{
			ImGuiIO& io = ImGui::GetIO();
			const glm::vec2 mouse(io.MousePos.x - ox, io.MousePos.y - oy);
			for (int button = 0; button < 2; ++button)
			{
				float& px = button == 0 ? m_Game.PressX : m_Game.PressRX;
				float& py = button == 0 ? m_Game.PressY : m_Game.PressRY;
				if (m_ViewportHovered && ImGui::IsMouseClicked(button)) { px = mouse.x; py = mouse.y; }
				else if (px >= 0.0f && ImGui::IsMouseReleased(button))
				{
					if (m_ViewportHovered && glm::distance(mouse, glm::vec2(px, py)) < 4.0f && m_Game.View != "top-none")
					{
						const Ray ray = ScreenRay(m_Camera, mouse / glm::vec2(w, h), w / h);
						std::string picked = PickActor(ray);
						if (picked.empty()) picked = PickNode(ray);
						if (picked.empty())
							if (const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.Pick(ray)); e && m_Gameplay.Find(e->Name)) picked = e->Name;
						if (button == 0) m_Game.Selected = picked;   // (a click on nothing clears it)
						else if (m_Game.View != "top")
						{
							const bool node = !picked.empty() && PickNode(ray) == picked;
							const bool foe = !picked.empty() && !node && picked != m_Game.Player && !m_Tribe.FindMember(picked);
							if (m_Tribe.PlayerFighting(m_Game.Player) && !foe) run("game stop");
							else run(foe ? "game attack " + picked : std::string("game attack"));
						}
					}
					px = -1.0f;
				}
			}
		}

		// ---- the selection: a character (needs, condition, trade, skills, bag as a grid) or a node (what it gives, how much is left, its tags)
		if (!m_Game.Selected.empty())
		{
			const std::string name = m_Game.Selected;
			const AbilitySystemComponent* asc = m_Gameplay.Find(name);
			const TribeMember* who = m_Tribe.FindMember(name);
			bool open = true;
			ImGui::SetNextWindowPos(ImVec2(ox + w - 12.0f, oy + h - 12.0f), ImGuiCond_Appearing, ImVec2(1.0f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 330.0f), std::min(h - 80.0f, 420.0f)), ImGuiCond_Appearing);
			if (!asc) m_Game.Selected.clear();
			else if (ImGui::Begin(("Selection: " + name + "##sel").c_str(), &open))
			{
				if (who)
				{
					ImGui::Text("%s - %s%s", who->Name.c_str(), who->Role.c_str(), who->Dead ? " (dead)" : "");
					ImGui::TextDisabled("%s %s", who->Job.c_str(), who->Detail.c_str());
					if (who->Name != m_Game.Player)
						ImGui::TextDisabled("feels %+.2f about you", static_cast<double>(m_Tribe.People().Affinity(m_Tribe.Config.Society, who->Name, m_Game.Player)));
					const std::map<std::string, float> levels = m_Tribe.NeedLevels(*who);
					for (const std::string& n : m_Controls.Hud.Needs)
					{
						const auto it = levels.find(n);
						if (it == levels.end()) continue;
						const float v = std::clamp(it->second / 100.0f, 0.0f, 1.0f);
						ImGui::ProgressBar(v, ImVec2(150.0f, 10.0f), "");
						ImGui::SameLine();
						ImGui::TextUnformatted(n.c_str());
					}
					if (ImGui::CollapsingHeader("Skills", ImGuiTreeNodeFlags_DefaultOpen))
						for (const auto& [family, level] : who->Skills) ImGui::Text("%-12s %.1f", family.c_str(), static_cast<double>(level));
					if (const Actor* a = m_Actors.Find(name))
					{
						ImGui::TextUnformatted("Bag");
						const Inventory& bag = a->Bag();
						// the bag as a grid of slots: the first letters of the item and its count, the full name when the mouse is over it
						const int cols = 6, slots = std::max(bag.Slots(), static_cast<int>(bag.Stacks().size()));
						for (int i = 0; i < slots; ++i)
						{
							ImGui::PushID(i);
							if (i % cols) ImGui::SameLine();
							if (i < static_cast<int>(bag.Stacks().size()))
							{
								const ItemSlot& s = bag.Stacks()[static_cast<size_t>(i)];
								const ItemDef* d = m_Items.Find(s.Id);
								char label[32];
								std::snprintf(label, sizeof label, "%.3s\n%d", (d && !d->Name.empty() ? d->Name : s.Id).c_str(), s.Count);
								ImGui::Button(label, ImVec2(42.0f, 38.0f));
								if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s x%d%s", (d && !d->Name.empty() ? d->Name : s.Id).c_str(), s.Count, d ? ("  (" + d->Category + ")").c_str() : "");
							}
							else { ImGui::BeginDisabled(); ImGui::Button("-", ImVec2(42.0f, 38.0f)); ImGui::EndDisabled(); }
							ImGui::PopID();
						}
					}
				}
				else
				{
					// a node (or any other thing with abilities): what it is, what it gives, how much is left
					const std::string kindTag = "Resource.Node.";
					std::string kind;
					for (const auto& [id, def] : m_NodeDefs) if (asc->HasTag(kindTag + id)) { kind = id; break; }
					ImGui::TextUnformatted(name.c_str());
					if (!kind.empty())
					{
						const ResourceNodeDef& def = m_NodeDefs.at(kind);
						ImGui::TextDisabled("%s", kind.c_str());
						ImGui::Text("left: %d of %d", ResourceSystem::Amount(*asc), def.Capacity);
						ImGui::Text("gives: %s x%d per go", def.Item.c_str(), def.PerGather);
						ImGui::Text("needs: %s", def.RequiresTag.empty() ? "bare hands" : def.RequiresTag.c_str());
						ImGui::Text("regrows: %s", def.RegenSeconds > 0.0f ? ("+" + std::to_string(def.Regen) + " every " + std::to_string(static_cast<int>(def.RegenSeconds)) + " s").c_str() : "never");
						if (!def.Ability.empty()) ImGui::Text("ability: %s", def.Ability.c_str());
					}
					if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen))
						for (const auto& at : asc->Attributes().All()) ImGui::Text("%-14s %.1f / %.1f", at.Name.c_str(), static_cast<double>(at.Current), static_cast<double>(at.Max));
					if (ImGui::CollapsingHeader("State")) ImGui::TextWrapped("%s", m_Gameplay.Describe(name).c_str());
				}
			}
			ImGui::End();
			if (!open) m_Game.Selected.clear();
		}

		// ---- the menu (Esc with nothing open): the game waits; leaving for the editor is a button, and `play resume` (or its button) comes back
		if (m_Game.Menu)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + h * 0.35f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			if (ImGui::Begin("Paused##game", &m_Game.Menu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
			{
				if (ImGui::Button("Resume", ImVec2(220.0f, 0.0f))) run("game menu");
				if (ImGui::Button("Save (F5)", ImVec2(220.0f, 0.0f))) run("game save");
				if (ImGui::Button("Load (F9)", ImVec2(220.0f, 0.0f))) run("game load");
				if (ImGui::Button("Keys", ImVec2(220.0f, 0.0f))) run("game help");
				if (ImGui::Button("Back to the editor", ImVec2(220.0f, 0.0f))) run("play off");
				ImGui::TextDisabled("In the editor, `play resume` (or the button) returns here.");
			}
			ImGui::End();
		}

		// ---- the bag (I): what you carry, the recipes you can make, the store
		if (m_Game.Inventory)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + 20.0f, oy + 30.0f), ImGuiCond_Appearing);
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 420.0f), std::min(h - 120.0f, 480.0f)), ImGuiCond_Appearing);
			if (ImGui::Begin("Bag##game", &m_Game.Inventory))
			{
				const Inventory& bag = actor->Bag();
				ImGui::TextUnformatted("You carry");
				bool any = false;
				for (const ItemSlot& s : bag.Stacks())
				{
					if (s.Count <= 0) continue;
					any = true;
					const ItemDef* d = m_Items.Find(s.Id);
					const bool tool = d && d->Category == "tool";
					ImGui::BulletText("%s x%d%s", (d && !d->Name.empty() ? d->Name : s.Id).c_str(), s.Count, tool ? "  (tool)" : "");
					if (!tool && m_Tribe.IsFood(s.Id)) { ImGui::SameLine(); ImGui::PushID(s.Id.c_str()); if (ImGui::SmallButton("eat")) run("game eat"); ImGui::PopID(); }
				}
				if (!any) ImGui::TextDisabled("nothing");
				if (ImGui::Button("Leave in the store")) run("game deposit");
				ImGui::SameLine();
				if (ImGui::Button("Drink")) run("game drink");

				if (ImGui::CollapsingHeader("Craft", ImGuiTreeNodeFlags_DefaultOpen))
				{
					const bool atCamp = glm::distance(pos, m_Tribe.Config.Economy.Camp) <= m_Tribe.Config.Player.CampReach;
					for (const std::string& id : m_Recipes.Ids())
					{
						const Recipe* r = m_Recipes.Find(id);
						if (!r) continue;
						bool can = true;
						for (const auto& [item, n] : r->Inputs) can = can && bag.Count(item) + (atCamp ? m_Tribe.Store().Count(item) : 0) >= n;
						if (!can && !ImGui::GetIO().KeyShift) continue;   // (Shift shows the ones you cannot make yet)
						ImGui::PushID(id.c_str());
						ImGui::BeginDisabled(!can);
						if (ImGui::SmallButton("make")) run("game craft " + id);
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::Text("%s x%d  <-  %s%s", r->Output.c_str(), r->OutputCount, Needs(r->Inputs).c_str(), r->Station.empty() ? "" : ("  at " + r->Station).c_str());
						ImGui::PopID();
					}
					ImGui::TextDisabled("hold Shift to see what you cannot make yet");
				}
				if (ImGui::CollapsingHeader("Store of the clan"))
				{
					if (glm::distance(pos, m_Tribe.Config.Economy.Camp) > m_Tribe.Config.Player.CampReach) ImGui::TextDisabled("(you must be at the camp to take from it)");
					for (const ItemSlot& s : m_Tribe.Store().Stacks())
					{
						if (s.Count <= 0) continue;
						ImGui::PushID(s.Id.c_str());
						if (ImGui::SmallButton("take 1")) run("game take " + s.Id + " 1");
						ImGui::SameLine();
						ImGui::Text("%s x%d", s.Id.c_str(), s.Count);
						ImGui::PopID();
					}
				}
			}
			ImGui::End();
		}

		// ---- the people around (T): the clan's members by distance, with what can be done with each one
		if (m_Game.People)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w - 20.0f, oy + 30.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 520.0f), std::min(h - 120.0f, 420.0f)), ImGuiCond_Appearing);
			if (ImGui::Begin("People##game", &m_Game.People))
			{
				struct Row { const TribeMember* M; float D; };
				std::vector<Row> rows;
				for (const TribeMember& o : m_Tribe.Members())
					if (!o.Dead && o.Body && o.Name != m_Game.Player) rows.push_back({ &o, glm::distance(o.Body->Position(), pos) });
				std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.D < b.D; });
				const float reach = m_Tribe.Config.Player.Reach;
				std::string gift;   // the first thing you carry that is not a tool: what "give" hands over
				for (const ItemSlot& s : actor->Bag().Stacks())
					if (const ItemDef* d = m_Items.Find(s.Id); s.Count > 0 && d && d->Category != "tool") { gift = s.Id; break; }
				if (rows.empty()) ImGui::TextDisabled("nobody else in the clan is alive");
				for (const Row& r : rows)
				{
					const bool near = r.D <= reach + 1.0f;
					ImGui::PushID(r.M->Name.c_str());
					ImGui::Text("%s (%s)  %.0f m  %s %s", r.M->Name.c_str(), r.M->Role.c_str(), static_cast<double>(r.D), r.M->Job.c_str(), r.M->Detail.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%+.2f", static_cast<double>(m_Tribe.People().Affinity(m_Tribe.Config.Society, r.M->Name, m_Game.Player)));
					ImGui::BeginDisabled(!near);
					if (ImGui::SmallButton("talk")) run("game talk " + r.M->Name);
					ImGui::SameLine();
					ImGui::BeginDisabled(gift.empty());
					if (ImGui::SmallButton(gift.empty() ? "give" : ("give " + gift).c_str())) run("game give " + r.M->Name + " " + gift + " 1");
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::SmallButton("teach")) run("game teach " + r.M->Name);
					ImGui::SameLine();
					if (ImGui::SmallButton("learn")) run("game learn " + r.M->Name);
					ImGui::SameLine();
					if (ImGui::SmallButton("marry")) run("game marry " + r.M->Name);
					ImGui::EndDisabled();
					if (!near) { ImGui::SameLine(); ImGui::TextDisabled("(come closer)"); }
					ImGui::PopID();
					ImGui::Separator();
				}
				if (m_Clans.Count() > 1)
				{
					ImGui::TextUnformatted("Other clans");
					for (const auto& c : m_Clans.All())
					{
						if (!c->Folk || c->Folk == &m_Tribe) continue;
						ImGui::PushID(c->Name.c_str());
						const bool there = glm::distance(pos, c->Folk->Config.Economy.Camp) <= m_Tribe.Config.Player.VisitRadius;
						ImGui::Text("%s", c->Name.c_str());
						ImGui::SameLine();
						ImGui::BeginDisabled(!there);
						if (ImGui::SmallButton("trade")) run("game trade " + c->Name);
						ImGui::SameLine();
						if (ImGui::SmallButton("visit")) run("game visit " + c->Name);
						ImGui::EndDisabled();
						if (!there) { ImGui::SameLine(); ImGui::TextDisabled("(go to its camp)"); }
						ImGui::PopID();
					}
				}
			}
			ImGui::End();
		}

		// ---- the buildings (B): what the clan can raise, proposed to the clan where you stand
		if (m_Game.Build)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + 30.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 560.0f), std::min(h - 120.0f, 440.0f)), ImGuiCond_Appearing);
			if (ImGui::Begin("Buildings##game", &m_Game.Build))
			{
				ImGui::TextWrapped("Stand where you want it and propose it: the clan votes (your standing counts). The sites it starts show on the board and in the Clan sheet.");
				for (const auto& [id, def] : m_Buildings.Defs)
				{
					const bool known = m_Tribe.CanBuild(id);
					ImGui::PushID(id.c_str());
					ImGui::BeginDisabled(!known);
					if (ImGui::Button("Propose here")) run("game propose build " + id);
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::Text("%s%s", def.Name.c_str(), known ? "" : "  (the clan does not know how yet)");
					std::string gives;
					if (def.Shelter > 0) gives += std::to_string(def.Shelter) + " beds ";
					if (!def.Store.empty() || def.StoreCap > 0) gives += "store ";
					if (!def.Station.empty()) gives += "station:" + def.Station + " ";
					if (def.Warmth > 0.0f && def.Shelter == 0) gives += "warmth ";
					if (def.Healing > 0.0f) gives += "healing ";
					if (def.Defense > 0.0f) gives += "defence ";
					if (def.Culture > 0.0f) gives += "meeting place ";
					ImGui::TextDisabled("   gives: %s| needs: %s | work: %.0f s", gives.c_str(), Needs(def.TotalNeeds()).c_str(), static_cast<double>(def.TotalWork()));
					ImGui::PopID();
				}
				if (ImGui::CollapsingHeader("Sites and buildings of the clan", ImGuiTreeNodeFlags_DefaultOpen)) ImGui::TextWrapped("%s", m_Tribe.SettlementReport().c_str());
			}
			ImGui::End();
		}
	}
}
