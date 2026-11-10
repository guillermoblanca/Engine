// The panels of the game mode that make the actions reachable without the console: an action bar at the bottom (every button runs a `game` command, the
// same the keys run) and the clan book on the right, in three groups (docs/JUGAR.md, "La interfaz"): Me (the bag, the people around), the Clan (the
// village, the job board, the homes, what to build) and the World (the other clans, the map, the technologies, the story). Nothing here decides: each
// button queues `game <action>`, so a script plays the same game headless (GameMode.cpp). Every word the player reads goes through Lang (Lang.h).
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <optional>

namespace GB::Editor
{
	namespace
	{
		// The key of an action as the data names it ("" when it has none), for the label of a button.
		std::string KeyOf(const std::map<std::string, std::string>& keys, const char* action)
		{
			const auto it = keys.find(action);
			return it == keys.end() ? std::string() : it->second;
		}
	}

	// The sheets of the clan book, by group: `Action` is the `game` command that opens it (and its flag), `KeyAction` the action whose key opens it.
	const std::vector<BookSheet>& BookSheets()
	{
		static const std::vector<BookSheet> sheets = {
			{ "inventory", "me", "Bag", "inventory" }, { "people", "me", "People", "people" },
			{ "village", "clan", "Village", "clan" }, { "jobs", "clan", "Job board", "jobs" }, { "homes", "clan", "Homes", "homes" }, { "build", "clan", "Build", "build" },
			{ "clans", "world", "Clans", "clans" }, { "map", "world", "Map", "map" }, { "story", "world", "Story", "story" },
			{ "help", "keys", "Keys", "help" },
		};
		return sheets;
	}

	bool* EditorApp::BookFlag(const std::string& sheet)
	{
		if (sheet == "inventory") return &m_Game.Inventory;
		if (sheet == "people") return &m_Game.People;
		if (sheet == "village") return &m_Game.Village;
		if (sheet == "jobs") return &m_Game.ClanPanel;
		if (sheet == "homes") return &m_Game.Homes;
		if (sheet == "build") return &m_Game.Build;
		if (sheet == "clans") return &m_Game.Clans;
		if (sheet == "map") return &m_Game.Map;
		if (sheet == "tech") return &m_Game.Tech;
		if (sheet == "story") return &m_Game.Story;
		if (sheet == "help") return &m_Game.Help;
		return nullptr;
	}

	void EditorApp::DrawGamePanels(float ox, float oy, float w, float h)
	{
		const TribeMember* me = PlayTribe().FindMember(m_Game.Player);
		const Actor* actor = m_Actors.Find(m_Game.Player);
		if (!me || !actor) return;
		if (m_Game.Heir) DrawHeirChoice();   // V3: a life ended: who carries on
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const glm::vec3 p3 = actor->GetPosition();
		const glm::vec2 pos(p3.x, p3.z);
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto itemName = [&](const std::string& id) { const ItemDef* d = m_Items.Find(id); return Lang::Cap(L.Name("item", id, d && !d->Name.empty() ? d->Name : std::string())); };
		auto dimText = [&](const std::string& s) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s", s.c_str()); ImGui::PopStyleColor(); };

		// ---- the action bar, at the bottom in the middle: an icon per action with its key in the corner, in groups (living, work, the clan book and
		// the view). The book's three buttons open its groups. From above the character is not at hand, so its actions are greyed with the reason.
		{
			struct Entry { const char* Label; const char* Action; const char* Command; const char* Icon; int Group; bool Body; const char* Tip; };
			static const Entry entries[] = {
				{ "Eat", "eat", "game eat", "eat", 0, true, "Eat what you carry or, at the camp, from the store" },
				{ "Drink", "drink", "game drink", "drink", 0, true, "Drink the water you carry" },
				{ "Sleep", "sleep", "game sleep", "sleep", 0, true, "Sleep (in your bed if there is a shelter)" },
				{ "Interact", "interact", "game interact", "collect", 1, true, "Do what the prompt says (pick up, talk, build, feed the fire...)" },
				{ "Leave", "deposit", "game deposit", "deposit", 1, true, "Leave what you carry in the store (at the camp)" },
				{ "Attack", "attack", "game attack", "fight", 1, true, "Attack the nearest foe (right mouse on an animal or foe does it too); Esc, right mouse on the ground or walking away breaks it off" },
				{ "Block", "block", "game block", "club", 1, true, "Raise your guard" },
				{ "Me", "inventory", "game book me", "leather_bag", 2, false, "Your bag, your recipes and the people around you" },
				{ "Clan", "clan", "game book clan", "campfire", 2, false, "The village, the job board, the homes and what to build" },
				{ "World", "clans", "game book world", "goto", 2, false, "The other clans, the map and the clan's story" },
				{ "Knowledge", "tech", "game tech", "tech.fire_making", 2, false, "The tree of knowledge: what the clan knows, what it is working out and what to seek next" },
				{ "Camera", "view", "game view", "explore", 3, false, "Behind you / from above" },
				{ "Help", "help", "game help", "", 3, false, "The keys" },
			};
			const float size = 36.0f;
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + h - T::Margin), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
			if (ImGui::Begin("Actions##game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking))
			{
				const bool fromAbove = m_Game.View == "top";
				const std::string openGroup = [&]() { for (const BookSheet& s : BookSheets()) if (s.Action == m_Game.BookTab) return std::string(s.Group); return std::string(); }();
				int group = -1;
				for (const Entry& e : entries)
				{
					if (group >= 0)
					{
						ImGui::SameLine(0.0f, e.Group != group ? 16.0f : 4.0f);
						if (e.Group != group)
						{
							const ImVec2 at = ImGui::GetCursorScreenPos();
							ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x - 9.0f, at.y + 4.0f), ImVec2(at.x - 9.0f, at.y + size + 8.0f), T::HideLine, 1.0f);
						}
					}
					group = e.Group;
					const std::string key = m_Lang.Key(KeyOf(m_Controls.Keys, e.Action)), label = L.Text(e.Label);
					const bool off = fromAbove && e.Body;
					// the book's button of the group that is open looks pressed
					const std::string command = e.Command;
					const bool open = e.Group == 2 && (command.rfind("game book ", 0) == 0 ? command.substr(10) == openGroup : m_Game.TechTree);
					if (open) ImGui::PushStyleColor(ImGuiCol_Button, T::HideActive);
					ImGui::BeginDisabled(off);
					glm::vec2 uv0, uv1;   // (an action without an icon in the atlas gets its name on the button)
					const bool pressed = *e.Icon && IconUv(e.Icon, uv0, uv1) ? IconButton(e.Command, e.Icon, size) : ImGui::Button((label + "###" + e.Command).c_str(), ImVec2(size + 8.0f, size + 8.0f));
					ImGui::EndDisabled();
					if (open) ImGui::PopStyleColor();
					if (pressed) run(e.Command);
					if (!key.empty())
					{
						const ImVec2 max = ImGui::GetItemRectMax(), ks = ImGui::CalcTextSize(key.c_str());
						T::KeyCap(ImGui::GetWindowDrawList(), ImVec2(max.x - ks.x - 10.0f - 1.0f, max.y - 15.0f), key.c_str(), 14.0f);
					}
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					{
						std::string tip = label + (key.empty() ? std::string() : " [" + key + "]") + "\n" + L.Text(e.Tip);
						if (e.Group == 2)   // the sheets of the group, with their keys
							for (const BookSheet& s : BookSheets())
								if (std::string(e.Command).substr(10) == s.Group)
								{
									const std::string k = m_Lang.Key(KeyOf(m_Controls.Keys, s.KeyAction));
									tip += "\n  " + L.Text(s.Label) + (k.empty() ? std::string() : " [" + k + "]");
								}
						if (off) tip += "\n" + L.Text("Not from above: [V] goes back to your character");
						ImGui::SetTooltip("%s", tip.c_str());
					}
				}
				m_Game.BarTop = ImGui::GetWindowPos().y;
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
						const std::string picked = PickSelectable(ray);   // a character, a node or a building (SelectionView.cpp)
						if (button == 0) m_Game.Selected = picked;   // (a click on nothing clears it)
						else if (m_Game.View != "top")
						{
							const bool node = !picked.empty() && PickNode(ray) == picked;
							const bool foe = !picked.empty() && !node && picked != m_Game.Player && !PlayTribe().FindMember(picked);
							if (PlayTribe().PlayerFighting(m_Game.Player) && !foe) run("game stop");
							else run(foe ? "game attack " + picked : std::string("game attack"));
						}
					}
					px = -1.0f;
				}
			}
		}

		// ---- the selection: a character, a node or a building (SelectionView.cpp, the same card as in the free mode)
		DrawSelection(ox, oy, w, h, m_Game.Selected);

		// ---- the menu (Esc with nothing open): the game waits and the world dims; leaving for the editor is a button, and `play resume` (or its button)
		// comes back
		if (m_Game.Menu)
		{
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 110));
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + h * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(260.0f, 0.0f), ImGuiCond_Always);
			if (ImGui::Begin((L.Text("Paused") + "###paused_game").c_str(), &m_Game.Menu, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
			{
				const ImVec2 full(-FLT_MIN, 30.0f);
				{
					const GameTheme::PrimaryScope primary;
					if (ImGui::Button((L.Text("Resume") + "###resume").c_str(), full)) run("game menu");
				}
				if (ImGui::Button((L.Text("Save") + " [" + m_Lang.Key(KeyOf(m_Controls.Keys, "save")) + "]###save").c_str(), full)) run("game save");
				if (ImGui::Button((L.Text("Load") + " [" + m_Lang.Key(KeyOf(m_Controls.Keys, "load")) + "]###load").c_str(), full)) run("game load");
				if (ImGui::Button((L.Text("Keys") + "###keys").c_str(), full)) { run("game menu"); run("game help"); }
				// T49: the graphics presets (`game quality`: applied at once and kept in the player's settings file, T88). The one chosen is ochre on a darker hide and
				// framed, and the line under them says in words what is in use, so the colour is never the only sign.
				ImGui::Separator();
				ImGui::TextDisabled("%s", L.Text("Graphics").c_str());
				{
					struct Choice { const char* Id; const char* Level; const char* Label; const char* Tip; };
					std::vector<Choice> choices;
					for (const GraphicsPreset& p : GraphicsPresets()) choices.push_back({ p.Id, p.Level, p.Label, p.Tip });
					choices.push_back({ "auto", "auto", "Auto", "Starts like Medium without ambient occlusion and drops to Low as soon as frames are slow" });
					const float gap = ImGui::GetStyle().ItemSpacing.x;
					const float each = (ImGui::GetContentRegionAvail().x - gap * static_cast<float>(choices.size() - 1)) / static_cast<float>(choices.size());
					for (size_t i = 0; i < choices.size(); ++i)
					{
						const Choice& c = choices[i];
						const bool chosen = m_Controls.Game.Quality == c.Level;
						if (i > 0) ImGui::SameLine();
						if (chosen) { ImGui::PushStyleColor(ImGuiCol_Button, GameTheme::HideActive); ImGui::PushStyleColor(ImGuiCol_Text, GameTheme::Ochre); }
						if (ImGui::Button((L.Text(c.Label) + "###quality_" + c.Id).c_str(), ImVec2(each, 26.0f))) run(std::string("game quality ") + c.Id);
						if (chosen)
						{
							ImGui::PopStyleColor(2);
							ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), GameTheme::Ochre, 4.0f);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(c.Tip).c_str());
					}
					// what is in use now: a preset, auto (lowered or not) or a level given at launch that is none of the three
					const GraphicsPreset* now = PresetOfLevel(m_QualityLevel);
					std::string inUse = now ? L.Text(now->Label) : L.Text(m_LaunchQuality.empty() ? "Custom" : "Custom (given at launch)");
					if (m_Game.AutoQuality) inUse = m_Game.Lowered && now ? L.Text("Auto") + " (" + inUse + ")" : L.Text("Auto");   // (auto starts above low, at a level of its own)
					ImGui::TextDisabled("%s", L.Text("In use: {0}", inUse).c_str());
				}
				ImGui::Separator();
				if (ImGui::Button((L.Text("Back to the editor") + "###editor").c_str(), full)) run("play off");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The world and the clans stay: `play resume` (or the editor's button) comes back here").c_str());
			}
			ImGui::End();
		}

		// ---- the sheets that act for the player (the ones that look at the clan and the world are in GameSheets.cpp)
		// the bag (I): what you carry (its icons), what you can make with it (each recipe with what it asks and what you have), the clan's store
		auto bagSheet = [&]()
		{
			const Inventory& bag = actor->Bag();
			const bool atCamp = glm::distance(pos, PlayTribe().Config.Economy.Camp) <= PlayTribe().Config.Player.CampReach;
			// V2: what people know of you: how many have heard of what you did, and how they speak of it
			{
				const WorldHistory::Reputation rep = m_Clans.Past().ReputationOf(m_Game.Player);
				const char* how = rep.Deeds == 0 ? "nobody tells anything of you yet" : rep.Score > 0.25f ? "they speak well of you" : rep.Score < -0.25f ? "they speak ill of you" : "they speak of you, neither well nor ill";
				ImGui::TextUnformatted(L.Text("Known by {0} people: {1}", rep.Known, L.Text(how)).c_str());
				ImGui::Separator();
			}
			// V3: what people asked of you (the troubles they told you when you talked to them)
			if (const std::vector<const Request*> asked = m_Clans.Requests().OpenFor(m_Game.Player); !asked.empty())
			{
				ImGui::TextUnformatted(L.Text("Asked of you").c_str());
				for (const Request* r : asked)
				{
					ImGui::Bullet();
					ImGui::SameLine();
					ImGui::TextWrapped("%s", Lang::Cap(L.Line(AskLine(*r))).c_str());
				}
				ImGui::Separator();
			}
			ImGui::TextUnformatted(L.Text("You carry").c_str());
			if (bag.Stacks().empty()) ImGui::TextDisabled("%s", L.Text("nothing").c_str());
			else DrawItems(bag);
			bool food = false, water = false;
			for (const ItemSlot& s : bag.Stacks()) { food = food || (s.Count > 0 && PlayTribe().IsFood(s.Id)); water = water || (s.Count > 0 && s.Id == "water"); }
			ImGui::BeginDisabled(!food && !atCamp);
			if (ImGui::Button((L.Text("Eat") + "###eat").c_str())) run("game eat");
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(!water);
			if (ImGui::Button((L.Text("Drink") + "###drink").c_str())) run("game drink");
			ImGui::EndDisabled();
			if (!water && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("You carry no water: draw it at a well").c_str());
			ImGui::SameLine();
			ImGui::BeginDisabled(!atCamp || bag.Stacks().empty());
			if (ImGui::Button((L.Text("Leave in the store") + "###deposit").c_str())) run("game deposit");
			ImGui::EndDisabled();
			if (!atCamp && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("The store is at the camp").c_str());

			ImGui::Spacing();
			ImGui::SeparatorText(L.Text("What you can make").c_str());
			static bool all = false;   // (the sheet's own view: the recipes you cannot make yet too)
			ImGui::Checkbox((L.Text("Also the ones you cannot make yet") + "###all").c_str(), &all);
			int shown = 0;
			for (const std::string& id : m_Recipes.Ids())
			{
				const Recipe* r = m_Recipes.Find(id);
				if (!r) continue;
				bool can = true;
				for (const auto& [item, n] : r->Inputs) can = can && bag.Count(item) + (atCamp ? PlayTribe().Store().Count(item) : 0) >= n;
				if (!can && !all) continue;
				++shown;
				ImGui::PushID(id.c_str());
				if (!Icon(r->Output, 26.0f)) ImGui::Dummy(ImVec2(26.0f, 26.0f));
				ImGui::SameLine();
				ImGui::BeginGroup();
				ImGui::Text("%s x%d", itemName(r->Output).c_str(), r->OutputCount);
				// what it asks: an icon and "have/need" for each, red when short
				bool first = true;
				for (const auto& [item, n] : r->Inputs)
				{
					if (!first) ImGui::SameLine(0.0f, 10.0f);
					first = false;
					const int have = bag.Count(item) + (atCamp ? PlayTribe().Store().Count(item) : 0);
					if (Icon(item, 16.0f)) ImGui::SameLine(0.0f, 3.0f);
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(have >= n ? T::BoneDim : T::Bad), "%d/%d", have, n);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", itemName(item).c_str());
				}
				if (!r->Station.empty())
				{
					ImGui::SameLine(0.0f, 10.0f);
					ImGui::TextDisabled("%s", L.Text("at the {0}", L.Name("station", r->Station)).c_str());
				}
				ImGui::EndGroup();
				ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 12.0f, ImGui::GetWindowWidth() - 100.0f));
				ImGui::BeginDisabled(!can);
				{
					std::optional<GameTheme::PrimaryScope> primary;
					if (can) primary.emplace();
					if (ImGui::Button((L.Text("Make") + "###make").c_str(), ImVec2(84.0f, 0.0f))) run("game craft " + id);
				}
				ImGui::EndDisabled();
				if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("%s", L.Text(atCamp ? "You do not have all it asks (the store counts at the camp)" : "You do not have all it asks: at the camp the store counts too").c_str());
				ImGui::PopID();
			}
			if (shown == 0) ImGui::TextDisabled("%s", L.Text("nothing with what you have: tick the box to see what each recipe asks").c_str());

			ImGui::Spacing();
			ImGui::SeparatorText(L.Text("The clan's store").c_str());
			if (!atCamp) ImGui::TextDisabled("%s", L.Text("You must be at the camp to take from it").c_str());
			int col = 0;
			std::vector<std::pair<std::string, int>> kinds;   // (a kind in two stacks is one line)
			for (const ItemSlot& s : PlayTribe().Store().Stacks())
			{
				if (s.Count <= 0) continue;
				auto it = std::find_if(kinds.begin(), kinds.end(), [&](const auto& k) { return k.first == s.Id; });
				if (it == kinds.end()) kinds.push_back({ s.Id, s.Count });
				else it->second += s.Count;
			}
			for (const auto& [id, count] : kinds)
			{
				const ItemSlot s{ id, count };
				ImGui::PushID(s.Id.c_str());
				if (ImGui::GetWindowWidth() > 460.0f && col++ % 2) ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f + 8.0f);   // (two columns when they fit)
				if (!Icon(s.Id, 20.0f)) ImGui::Dummy(ImVec2(20.0f, 20.0f));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s %d", itemName(s.Id).c_str(), s.Count);
				ImGui::SameLine();
				ImGui::BeginDisabled(!atCamp);
				if (ImGui::SmallButton((L.Text("take 1") + "###take").c_str())) run("game take " + s.Id + " 1");
				ImGui::EndDisabled();
				ImGui::PopID();
			}
		};
		// the people around (T): one hint when nobody is within reach, the clan's members by distance (what each does and what it feels for you),
		// and what you can do with the ones at hand; the other clans, to trade with at their camp
		auto peopleSheet = [&]()
		{
			struct Row { const TribeMember* M; float D; };
			std::vector<Row> rows;
			for (const TribeMember& o : PlayTribe().Members())
				if (!o.Dead && o.Body && o.Name != m_Game.Player) rows.push_back({ &o, glm::distance(o.Body->Position(), pos) });
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.D < b.D; });
			const float reach = PlayTribe().Config.Player.Reach + 1.0f;
			std::string gift;   // the first thing you carry that is not a tool: what "give" hands over
			for (const ItemSlot& s : actor->Bag().Stacks())
				if (const ItemDef* d = m_Items.Find(s.Id); s.Count > 0 && d && d->Category != "tool") { gift = s.Id; break; }
			if (rows.empty()) ImGui::TextDisabled("%s", L.Text("nobody else in the clan is alive").c_str());
			else if (rows.front().D > reach)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("Nobody within reach ({0} m). Nearest: {1}, {2} m away.", static_cast<int>(reach),
					rows.front().M->Name, static_cast<int>(rows.front().D)).c_str());
			for (const Row& r : rows)
			{
				const bool near = r.D <= reach;
				const float bond = PlayTribe().People().Affinity(PlayTribe().Config.Society, r.M->Name, m_Game.Player);
				ImGui::PushID(r.M->Name.c_str());
				ImGui::Separator();
				ImGui::TextUnformatted(r.M->Name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s, %s", L.Name("role", r.M->Role).c_str(), L.Text("{0} m", static_cast<int>(r.D)).c_str());
				ImGui::SameLine();
				const char* feel = bond > 0.5f ? "loves you" : bond > 0.2f ? "likes you" : bond > 0.05f ? "knows you a little" : bond < -0.2f ? "dislikes you" : "hardly knows you";
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(bond < -0.2f ? T::Bad : bond > 0.2f ? T::Good : T::BoneDim), "%s", L.Text(feel).c_str());
				if (ImGui::IsItemHovered())
				{
					char value[16];
					std::snprintf(value, sizeof value, "%+.2f", static_cast<double>(bond));
					ImGui::SetTooltip("%s", L.Text("Bond {0}: gifts, talks and lessons raise it; with a strong bond you can marry", std::string(value)).c_str());
				}
				const std::string doing = L.Doing(r.M->Job, r.M->Detail);
				dimText(doing);
				// V2: what it values most (its clan's culture and its own way), the two values it holds most strongly
				{
					const Clan* clan = m_Clans.ClanOf(r.M->Name);
					std::vector<std::pair<std::string, int>> vals;
					for (const auto& [name, v] : PersonValues(m_Values, clan ? clan->Id : -1, r.M->Name)) vals.push_back({ name, v });
					std::stable_sort(vals.begin(), vals.end(), [](const auto& x, const auto& y) { return std::abs(x.second) > std::abs(y.second); });
					std::string line;
					for (size_t i = 0; i < vals.size() && i < 2; ++i)
						if (std::abs(vals[i].second) >= 15) line += (line.empty() ? "" : ", ") + L.Text(vals[i].second > 0 ? "values {0}" : "scorns {0}", L.Name("value", vals[i].first));
					if (!line.empty()) dimText(Lang::Cap(line));
				}
				if (near)
				{
					if (ImGui::SmallButton((L.Text("Talk") + "###talk").c_str())) run("game talk " + r.M->Name);
					ImGui::SameLine();
					ImGui::BeginDisabled(gift.empty());
					if (ImGui::SmallButton((gift.empty() ? L.Text("Give") : L.Text("Give {0}", itemName(gift))).append("###give").c_str())) run("game give " + r.M->Name + " " + gift + " 1");
					ImGui::EndDisabled();
					if (gift.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("You carry nothing to give").c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton((L.Text("Teach") + "###teach").c_str())) run("game teach " + r.M->Name);
					ImGui::SameLine();
					if (ImGui::SmallButton((L.Text("Learn") + "###learn").c_str())) run("game learn " + r.M->Name);
					ImGui::SameLine();
					if (ImGui::SmallButton((L.Text("Marry") + "###marry").c_str())) run("game marry " + r.M->Name);
				}
				ImGui::PopID();
			}
			if (m_Clans.Count() > 1)
			{
				ImGui::Spacing();
				ImGui::SeparatorText(L.Text("Other clans").c_str());
				for (const auto& c : m_Clans.All())
				{
					if (!c->Folk || c->Folk == &PlayTribe()) continue;
					ImGui::PushID(c->Name.c_str());
					const float d = glm::distance(pos, c->Folk->Config.Economy.Camp);
					const bool there = d <= PlayTribe().Config.Player.VisitRadius;
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(L.Name("clan", c->Name, c->Name).c_str());
					ImGui::SameLine();
					if (there)
					{
						if (ImGui::SmallButton((L.Text("Trade") + "###trade").c_str())) run("game trade " + c->Name);
						ImGui::SameLine();
						if (ImGui::SmallButton((L.Text("Visit") + "###visit").c_str())) run("game visit " + c->Name);
					}
					else ImGui::TextDisabled("%s", L.Text("go to their camp ({0} m) to trade or visit", static_cast<int>(d)).c_str());
					ImGui::PopID();
				}
			}
		};
		// the buildings (B): what the clan can raise (first) and what it cannot yet (greyed, with the reason), proposed to the clan where you stand;
		// then the sites the clan has open, each with its phase and what it lacks
		auto buildSheet = [&]()
		{
			dimText(L.Text("Stand where you want it and propose it: the clan votes (your standing counts). If it says no you can build it all the same."));
			// the vote is the same for every building (it weighs who asks, not what): foreseen once
			const Tribe::ProposalForecast vote = PlayTribe().ForeseeProposal(m_Game.Player, "build", std::string());
			std::string against;
			for (const std::string& n : vote.Against) against += (against.empty() ? "" : ", ") + n;
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(vote.Accepted ? T::Good : T::Warn));
			ImGui::TextWrapped("%s", (vote.Accepted ? L.Text("The clan would say yes: {0} of {1} for.", vote.Yes, vote.Voters)
				: L.Text("The clan would say no: {0} of {1} for; against: {2}.", vote.Yes, vote.Voters, against)).c_str());
			ImGui::PopStyleColor();
			if (!vote.Accepted && !vote.Leader)
				dimText(L.Text("Your standing is {0}; at {1} you lead and the clan follows you more readily.", L.Num(vote.Standing, 0), L.Num(PlayTribe().Config.Player.Leader, 0)));
			for (const bool knownPass : { true, false })
				for (const auto& [id, def] : m_Buildings.Defs)
				{
					const bool known = PlayTribe().CanBuild(id);
					if (known != knownPass) continue;
					ImGui::PushID(id.c_str());
					ImGui::Separator();
					const std::string name = Lang::Cap(L.Name("building", id, def.Name));
					if (known) ImGui::TextUnformatted(name.c_str());
					else
					{
						// V4: not of the clan's ways (its culture) or not known yet (its technology): it says which
						std::string ways;
						for (const auto& [value, threshold] : def.Values)
						{
							const auto it = PlayTribe().Culture().find(value);
							const int have = it != PlayTribe().Culture().end() ? it->second : 0;
							if (it == PlayTribe().Culture().end() || (threshold > 0 ? have < threshold : have > threshold))
								ways = L.Text(threshold > 0 ? "(your clan does not value {0} enough)" : "(your clan values {0} too much)", L.Name("value", value));
						}
						ImGui::TextDisabled("%s  %s", name.c_str(), ways.empty() ? L.Text("(not known yet)").c_str() : ways.c_str());
					}
					// what it does, a bullet per effect; what it asks
					for (const std::string& line : BuildingDoes(def))
					{
						ImGui::Bullet();
						ImGui::SameLine();
						if (known) ImGui::TextWrapped("%s", line.c_str());
						else dimText(line);
					}
					std::string needs;
					for (const auto& [item, n] : def.TotalNeeds()) needs += (needs.empty() ? "" : ", ") + std::to_string(n) + " " + L.Name("item", item);
					std::string phases;
					for (const BuildingPhase& ph : def.Phases) phases += (phases.empty() ? "" : ", ") + L.Name("phase", ph.Name);
					dimText(L.Text("Needs {0}; {1} s of work.", needs, static_cast<int>(def.TotalWork())) + (phases.empty() ? std::string() : " " + L.Text("Phases: {0}.", phases)));
					if (!known)
					{
						ImGui::BeginDisabled();
						ImGui::Button((L.Text("Propose here") + "###propose").c_str());
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("The clan does not know how yet: it needs the knowledge that opens it").c_str());
					}
					else if (vote.Accepted)
					{
						const GameTheme::PrimaryScope primary;
						if (ImGui::Button((L.Text("Propose here") + "###propose").c_str())) run("game propose build " + id);
					}
					else
					{
						if (ImGui::Button((L.Text("Propose here") + "###propose").c_str())) run("game propose build " + id);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan would say no: the proposal stays in its record").c_str());
						ImGui::SameLine();
						const GameTheme::PrimaryScope primary;
						if (ImGui::Button((L.Text("Build it all the same") + "###force").c_str())) run("game propose build " + id + " force");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The site opens where you stand even though the clan says no; it costs you nothing").c_str());
					}
					ImGui::PopID();
				}
			ImGui::Spacing();
			ImGui::SeparatorText(L.Text("The clan's sites").c_str());
			const auto sites = PlayTribe().SettlementSites();
			if (sites.empty()) ImGui::TextDisabled("%s", L.Text("no sites yet").c_str());
			for (const Tribe::SettlementSite& s : sites)
			{
				ImGui::PushID(s.Name.c_str());
				ImGui::TextUnformatted(L.Entity(s.Name).c_str());
				ImGui::SameLine();
				if (s.State.Done) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("stands").c_str());
				else
				{
					ImGui::TextDisabled("%s", L.Text("{0} ({1} of {2})", L.Name("phase", s.State.PhaseName), s.State.Phase + 1, s.State.Phases).c_str());
					ImGui::ProgressBar(std::clamp(s.State.Progress, 0.0f, 1.0f), ImVec2(-FLT_MIN, 6.0f), "");
					std::string missing;
					for (const auto& [item, n] : s.State.Missing) if (n > 0) missing += (missing.empty() ? "" : ", ") + std::to_string(n) + " " + L.Name("item", item);
					if (!missing.empty()) dimText(L.Text("lacks {0}", missing));
				}
				ImGui::PopID();
			}
		};

		// ---- the clan book: one panel on the right, from the top to the action bar. Its three groups on top (Me, Clan, World), the sheets of the open
		// group as tabs under them, and "?" for the keys. A sheet's key (or its `game` command) opens the book on it; the key of the open sheet closes the
		// book and another's turns to it; a click on a group or a tab turns too. Only one sheet is open at a time.
		{
			const std::vector<BookSheet>& sheets = BookSheets();
			bool any = false;
			std::string want;   // a sheet just opened by its key or command
			for (const BookSheet& s : sheets)
			{
				const bool* flag = BookFlag(s.Action);
				any = any || *flag;
				if (*flag && m_Game.BookTab != s.Action) want = s.Action;
			}
			m_Game.BookWidth = 0.0f;
			if (!any) m_Game.BookTab.clear();
			else
			{
				const std::string current = want.empty() ? m_Game.BookTab : want;
				std::string group = "clan";
				for (const BookSheet& s : sheets) if (current == s.Action) group = s.Group;
				const float bw = std::clamp(w * 0.36f, 380.0f, 560.0f), top = oy + T::Margin;
				const float bottom = (m_Game.BarTop > 0.0f ? m_Game.BarTop : oy + h - 70.0f) - T::Margin;
				ImGui::SetNextWindowPos(ImVec2(ox + w - T::Margin, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(bw, std::max(bottom - top, 160.0f)), ImGuiCond_Always);
				bool open = true;
				std::string shown;
				if (ImGui::Begin((L.Text("Clan book") + "###clanbook").c_str(), &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
				{
					// the groups: three wide buttons and the keys
					static const char* kGroups[][2] = { { "me", "Me" }, { "clan", "Clan" }, { "world", "World" } };
					const float gw = (ImGui::GetContentRegionAvail().x - 34.0f - 2.0f * 4.0f) / 3.0f;
					for (int i = 0; i < 3; ++i)
					{
						if (i) ImGui::SameLine(0.0f, 4.0f);
						const bool on = group == kGroups[i][0];
						std::optional<GameTheme::PrimaryScope> primary;
						if (on) primary.emplace();
						if (ImGui::Button((L.Text(kGroups[i][1]) + "###group" + kGroups[i][0]).c_str(), ImVec2(gw, 26.0f)) && !on) run(std::string("game book ") + kGroups[i][0]);
					}
					ImGui::SameLine(0.0f, 4.0f);
					{
						std::optional<GameTheme::PrimaryScope> primary;
						if (group == "keys") primary.emplace();
						if (ImGui::Button("?###keys", ImVec2(30.0f, 26.0f)) && group != "keys") run("game help");
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text("Keys").c_str(), m_Lang.Key(KeyOf(m_Controls.Keys, "help")).c_str());
					if (ImGui::BeginTabBar("sheets"))
					{
						for (const BookSheet& s : sheets)
						{
							if (group != s.Group) continue;
							const std::string key = m_Lang.Key(KeyOf(m_Controls.Keys, s.KeyAction));
							const std::string label = L.Text(s.Label) + (key.empty() ? std::string() : " [" + key + "]") + "###" + s.Action;
							if (!ImGui::BeginTabItem(label.c_str(), nullptr, want == s.Action ? ImGuiTabItemFlags_SetSelected : 0)) continue;
							shown = s.Action;
							ImGui::BeginChild("sheet");
							if (shown == "inventory") bagSheet();
							else if (shown == "people") peopleSheet();
							else if (shown == "village") DrawVillageSheet(PlayTribe());
							else if (shown == "jobs") DrawBoardSheet(PlayTribe(), true);
							else if (shown == "homes") DrawHomesSheet(PlayTribe(), true);
							else if (shown == "build") buildSheet();
							else if (shown == "clans") DrawClansSheet(PlayTribe());
							else if (shown == "map") DrawMapSheet();
							else if (shown == "tech") DrawTechSheet(PlayTribe());
							else if (shown == "story") DrawStorySheet();
							else DrawKeysSheet();
							ImGui::EndChild();
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				}
				ImGui::End();
				m_Game.BookWidth = bw + T::Margin;
				// one sheet open: the one a key asked for, else the one shown (a click on a tab); the X closes the book
				const std::string now = !open ? std::string() : !want.empty() ? want : shown.empty() ? m_Game.BookTab : shown;
				for (const BookSheet& s : sheets) *BookFlag(s.Action) = !now.empty() && now == s.Action;
				m_Game.BookTab = now;
				for (const BookSheet& s : sheets) if (now == s.Action) m_Game.BookLast[s.Group] = now;
			}
			m_MapAreas.On = m_Game.Map;   // the areas are painted on the ground while the map sheet is open
		}
	}

	// V3: the character died: the player chooses who carries on, its descendants first (its lineage); when there are none, anyone of another
	// lineage, of its clan or of another clan. Each button is `game heir <name>`.
	void EditorApp::DrawHeirChoice()
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin((L.Text("A life ends") + "###heir").c_str(), nullptr, flags))
		{
			const std::vector<HeirOption> heirs = Heirs(m_Game.Dead);
			const TribeMember* dead = nullptr;
			for (const auto& c : m_Clans.All()) if (c->Folk && !dead) dead = c->Folk->FindMember(m_Game.Dead);
			ImGui::TextWrapped("%s", L.Text("{0} has died ({1}). Who carries on?", m_Game.Dead, dead ? L.Name("cause", dead->Cause) : std::string()).c_str());
			if (heirs.empty() || !heirs.front().Lineage)
				{ ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre)); ImGui::TextWrapped("%s", L.Text("Your lineage ends here: you may carry on with someone of another lineage.").c_str()); ImGui::PopStyleColor(); }
			ImGui::Separator();
			for (size_t i = 0; i < heirs.size() && i < 10; ++i)
			{
				const HeirOption& h = heirs[i];
				ImGui::PushID(h.Name.c_str());
				if (ImGui::Button((L.Text("Carry on as {0}", h.Name) + "###go").c_str())) m_UiCommands.push_back("game heir " + h.Name);
				ImGui::SameLine();
				ImGui::TextDisabled("%s", (L.Name("tie", h.Tie) + (h.Clan.empty() ? std::string() : ", " + L.Name("clan", h.Clan, h.Clan))).c_str());
				ImGui::PopID();
			}
		}
		ImGui::End();
	}
}
