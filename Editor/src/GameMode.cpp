// H15: the game mode (`play`, `Editor --play`, play.txt). One command makes the world, the clans and the player's body, a member of clan 0; then the
// keys of the "controls" section (assets/data/player.json) act through `game <action>` commands (the same a script or a test uses), the game camera
// follows the player (H19) and a small interface over the viewport shows the needs, the condition (H14: health, stamina, wounds), what the player
// carries, its job, its standing, the clock, what the interact key would do and the news of the clan; Tab opens the clan (members, store, board,
// knowledge, other clans). The rules of what the player does are the tribe's (GB/Game/TribePlayer.cpp): this file only reads keys and the scene.
// See docs/JUGAR.md.
#include "EditorApp.h"
#include "GB/Input.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		std::string LowerCase(std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		std::string Underscored(std::string s)
		{
			for (char& c : s) if (c == ' ') c = '_';
			return s;
		}

		std::string Spaced(std::string s)
		{
			for (char& c : s) if (c == '_') c = ' ';
			return s;
		}

		std::string Fixed(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.0f", static_cast<double>(v));
			return b;
		}

		// A key of Dear ImGui by its name ("E", "Tab", "F5", "Equal"); ImGuiKey_None when there is none.
		ImGuiKey KeyNamed(const std::string& name)
		{
			const std::string want = LowerCase(name);
			for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
				if (LowerCase(ImGui::GetKeyName(static_cast<ImGuiKey>(k))) == want) return static_cast<ImGuiKey>(k);
			return ImGuiKey_None;
		}

		const char* SeasonText(Season s)
		{
			switch (s)
			{
			case Season::Summer: return "summer";
			case Season::Autumn: return "autumn";
			case Season::Winter: return "winter";
			default: return "spring";
			}
		}

		// What the clan's log says that a player wants to hear: what concerns it, and the big events of the clan.
		bool Newsworthy(const std::string& line, const std::string& player)
		{
			if (line.find(" " + player + " ") != std::string::npos || line.find(player + " (") != std::string::npos) return true;
			for (const char* k : { "dies", "is born", "raid", "attacked", "killed", "takes up the trade", "tradition", "has done what", "agrees", "lights the campfire", "finishes" })
				if (line.find(k) != std::string::npos) return true;
			return false;
		}

		// Wood for the campfire (anything tagged Item.Resource.Wood: the rule of Stoke, as the tribe reads it).
		bool HasFuel(const ItemLibrary& items, const Inventory& inv)
		{
			for (const ItemSlot& s : inv.Stacks())
				if (const ItemDef* d = items.Find(s.Id); d && s.Count > 0)
					for (const std::string& t : d->Tags) if (t == "Item.Resource.Wood" || t.rfind("Item.Resource.Wood.", 0) == 0) return true;
			return false;
		}

		constexpr const char* kClanNames[] = { "Blue", "Red", "Green", "Gold", "Ash" };
	}

	void EditorApp::GameNote(const std::string& text)
	{
		if (text.empty()) return;
		m_Game.Notes.push_back({ text, m_Game.Clock });
		while (m_Game.Notes.size() > 30) m_Game.Notes.pop_front();
	}

	// ------------------------------------------------------------------ play: a new game
	CommandResult EditorApp::PlayCommand(const std::vector<std::string>& a)
	{
		if (!a.empty() && (a[0] == "off" || a[0] == "stop"))
		{
			if (!m_Game.Active) return { true, "not playing" };
			m_Game.Active = false;
			m_Game.Paused = false;
			m_Game.Menu = false;
			if (m_Game.HasPanels)
			{
				const GameModeState::SavedPanels& p = m_Game.Panels;
				m_Panels.Hierarchy = p.Hierarchy; m_Panels.Inspector = p.Inspector; m_Panels.Properties = p.Properties; m_Panels.World = p.World; m_Panels.Stats = p.Stats;
				m_Panels.Console = p.Console; m_Panels.Assets = p.Assets; m_Panels.Model = p.Model; m_Panels.Animation = p.Animation; m_Panels.Abilities = p.Abilities;
				m_Panels.Tribe = p.Tribe; m_Panels.WorldState = p.WorldState; m_Panels.Tests = p.Tests; m_Panels.QA = p.QA;
			}
			// the editor gets the keys and the view back: the player's body stays in the world, but nobody steers it and the free camera starts over it
			const glm::vec3 where = m_Player.IsActive() ? m_Player.GetPosition() : glm::vec3(m_Tribe.Config.Economy.Camp.x, 0.0f, m_Tribe.Config.Economy.Camp.y);
			if (m_Game.SavedPitchNear >= 0.0f) { m_GameView.Params.Overview.PitchNearDegrees = m_Game.SavedPitchNear; m_GameView.Params.Overview.PitchFarDegrees = m_Game.SavedPitchFar; }
			m_GameView.QeTurns = true;
			m_Player.SetTurnWithQE(true);
			if (m_GameView.Active()) m_GameView.Command({ "off" }, m_Actors, m_Player, m_World, m_Camera);
			m_Player.Release();
			SetCamMode(CamMode::Free);
			m_Orbit.Target = where;
			return { true, "back to the editor (the world and the clans stay; `play resume` goes back to the game, `play` starts a new one)" };
		}
		if (!a.empty() && (a[0] == "status" || a[0] == "hud")) return GameCommand({ "status" });
		if (!a.empty() && a[0] == "resume")   // back into the game left with Esc or `play off`: the same world, clans and player (nothing is generated)
		{
			if (m_Game.Active) return { true, "already playing" };
			const std::string name = m_Game.Player.empty() ? m_Controls.Game.Name : m_Game.Player;
			if (!m_Tribe.FindMember(name) || !m_Actors.Find(name)) return { false, "there is no game to go back to: `play` starts one" };
			const std::string model = m_Game.Model;
			m_Player.Control(name);
			Execute("camera game follow");
			Execute("visuals on");
			m_Game = GameModeState{ true, name, model };
			m_Game.AutoQuality = false;
			m_GameView.QeTurns = false;
			m_Player.SetTurnWithQE(false);
			m_Game.Panels = { m_Panels.Hierarchy, m_Panels.Inspector, m_Panels.Properties, m_Panels.World, m_Panels.Stats, m_Panels.Console, m_Panels.Assets, m_Panels.Model,
				m_Panels.Animation, m_Panels.Abilities, m_Panels.Tribe, m_Panels.WorldState, m_Panels.Tests, m_Panels.QA };
			m_Game.HasPanels = true;
			m_Panels.Hierarchy = m_Panels.Inspector = m_Panels.Properties = m_Panels.World = m_Panels.Stats = m_Panels.Assets = m_Panels.Model = false;
			m_Panels.Animation = m_Panels.Abilities = m_Panels.Tribe = m_Panels.WorldState = m_Panels.Tests = m_Panels.QA = false;
			m_Panels.Viewport = true;
			m_Game.LogSeen = m_Tribe.Log().size();
			GameNote("Back in the game.");
			return { true, "back in the game" };
		}
		GameSetup setup = m_Controls.Game;
		std::vector<int> numbers;
		for (const std::string& w : a)
		{
			if (w == "temperate" || w == "alpine" || w == "desert") setup.World = w;
			else if (PlayerMode::IsFbx(w) || w == "fox" || w == "cesium") setup.Model = w;
			else numbers.push_back(std::atoi(w.c_str()));
		}
		if (numbers.size() > 0) setup.Seed = numbers[0];
		if (numbers.size() > 1) setup.Members = std::max(1, numbers[1]);
		if (numbers.size() > 2) setup.Clans = std::clamp(numbers[2], 1, 1 + static_cast<int>(std::size(kClanNames)));

		// the picture: on a weak GPU the minimum preset (H19); "auto" starts low and drops to minimum when a frame takes too long
		m_Game.AutoQuality = setup.Quality == "auto";
		m_Game.Lowered = false;
		SetQuality(m_Game.AutoQuality ? "low" : setup.Quality);

		CommandResult r = Execute("generate " + setup.World + " " + std::to_string(setup.Seed));
		if (!r.Ok) return r;
		r = Execute("resources world " + std::to_string(setup.Resources));
		if (!r.Ok) return r;
		// the characters: the X Bot of packs/full (beside the working directory or the repository), else the fallback
		std::string model = setup.Model;
		if (PlayerMode::IsFbx(model))
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const fs::path asGiven(model), beside = fs::path(m_AssetRoot) / ".." / model;
			if (fs::exists(asGiven, ec)) model = fs::absolute(asGiven, ec).generic_string();
			else if (fs::exists(beside, ec)) model = fs::weakly_canonical(beside, ec).generic_string();
			else model = setup.Fallback;
		}
		m_Game.Model = model;
		r = Execute("tribe build \"" + model + "\" " + std::to_string(setup.Members));
		if (!r.Ok) return r;
		std::string clans;
		for (int i = 1; i < setup.Clans; ++i)
		{
			const CommandResult c = Execute(std::string("clan new ") + kClanNames[i - 1] + " " + std::to_string(setup.Members) + " \"" + model + "\"");
			clans += c.Ok ? std::string(", the ") + kClanNames[i - 1] : std::string();
		}
		// the player: an actor beside the camp, a member of clan 0 whom the brain never drives
		const std::string name = setup.Name;
		if (m_Actors.Find(name)) Execute("actor remove " + name);
		const glm::vec2 at = m_Tribe.Config.Economy.Camp + glm::vec2(2.5f, 2.5f);
		r = Execute("actor spawn " + name + " \"" + model + "\" " + std::to_string(at.x) + " " + std::to_string(at.y) + " player");
		if (!r.Ok) return r;
		m_TribeBodies.push_back(MakeTribeBody(name));
		if (!m_Tribe.AddPlayer(name, m_TribeBodies.back().get())) return { false, "the clan already has a member called " + name };
		m_Player.Control(name);
		Execute("camera game follow");
		Execute("visuals on");   // the visual layer: the player's props, sets and work montages are chosen like an NPC's (TribeVisuals)

		m_Game = GameModeState{ true, name, model };
		m_Game.AutoQuality = setup.Quality == "auto";
		m_GameView.QeTurns = false;      // E interacts: only the arrows turn the camera
		m_Player.SetTurnWithQE(false);
		m_Game.Panels ={ m_Panels.Hierarchy, m_Panels.Inspector, m_Panels.Properties, m_Panels.World, m_Panels.Stats, m_Panels.Console, m_Panels.Assets, m_Panels.Model,
			m_Panels.Animation, m_Panels.Abilities, m_Panels.Tribe, m_Panels.WorldState, m_Panels.Tests, m_Panels.QA };
		m_Game.HasPanels = true;
		m_Panels.Hierarchy = m_Panels.Inspector = m_Panels.Properties = m_Panels.World = m_Panels.Stats = m_Panels.Assets = m_Panels.Model = false;
		m_Panels.Animation = m_Panels.Abilities = m_Panels.Tribe = m_Panels.WorldState = m_Panels.Tests = m_Panels.QA = false;
		m_Panels.Viewport = true;
		m_FocusTribe = 0;
		m_TimeScale = std::max(1, setup.TimeScale);
		m_Game.LogSeen = m_Tribe.Log().size();
		const std::string help = m_Controls.Keys.count("help") ? m_Controls.Keys.at("help") : std::string("help");
		GameNote("You are " + name + ", a " + m_Tribe.FindMember(name)->Role + " of the clan. Gather, bring to the store, take jobs, help. [" + help + "] help");
		return { true, "playing: " + setup.World + " " + std::to_string(setup.Seed) + ", " + name + " in a clan of " + std::to_string(setup.Members) + clans
			+ " (" + (PlayerMode::IsFbx(model) ? std::filesystem::path(model).filename().string() : model) + "); game status | game help" };
	}

	// ------------------------------------------------------------------ what the interact key would do
	namespace
	{
		struct Candidate { float Distance = 1e30f; std::string Label, Command; };
	}

	void EditorApp::RefreshPrompt()
	{
		m_Game.Prompt.clear();
		m_Game.PromptCommand.clear();
		const Actor* me = m_Actors.Find(m_Game.Player);
		const TribeMember* member = m_Tribe.FindMember(m_Game.Player);
		if (!me || !member || member->Dead) return;
		const glm::vec3 p3 = me->GetPosition();
		const glm::vec2 pos(p3.x, p3.z);
		const PlayerRules& rules = m_Tribe.Config.Player;
		Candidate best;
		auto offer = [&](float d, const std::string& label, const std::string& command) { if (d < best.Distance) best = { d, label, command }; };
		// someone fighting the player first
		if (const std::vector<std::string> foes = m_Combat.AttackersOf(m_Game.Player); !foes.empty()) { m_Game.Prompt = "Fight back " + foes.front(); m_Game.PromptCommand = "game attack " + foes.front(); return; }
		// the members of the clan within reach
		for (const TribeMember& o : m_Tribe.Members())
		{
			if (o.Dead || o.Name == m_Game.Player || !o.Body) continue;
			const float d = glm::distance(o.Body->Position(), pos);
			if (d <= rules.Reach) offer(d + 0.5f, "Talk to " + o.Name, "game talk " + o.Name);   // (a node at the same distance comes first)
		}
		// resource nodes, the campfire, wells and construction sites within reach
		const EntityWorld& world = m_SceneEditor.World();
		for (const auto& [id, e] : world.All())
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(e.Name);
			if (!asc) continue;
			const bool node = asc->HasTag("Resource.Node"), fire = e.Name == m_Tribe.Config.FireName, well = asc->HasTag("Interactive.Well"), site = asc->HasTag("Building.Site");
			if (!node && !fire && !well && !site) continue;
			const glm::vec4 w = world.WorldMatrix(id)[3];
			const float d = glm::distance(glm::vec2(w.x, w.z), pos);
			if (d > rules.Reach + (site ? 2.0f : 1.0f)) continue;
			if (node && ResourceSystem::Amount(*asc) > 0)
			{
				std::string ability = "Gather";
				for (const auto& [kind, def] : m_NodeDefs)
					if (asc->HasTag("Resource.Node." + kind)) { if (!def.Ability.empty()) ability = def.Ability; break; }
				offer(d, ability + " " + e.Name, "game use " + Underscored(e.Name));
			}
			else if (fire) offer(d, "Feed the campfire", "game use " + Underscored(e.Name));
			else if (well) offer(d, "Draw water at " + e.Name, "game use " + Underscored(e.Name));
			else if (site) offer(d, "Build " + e.Name, "game use " + Underscored(e.Name));
		}
		if (best.Command.empty() && glm::distance(pos, m_Tribe.Config.Economy.Camp) <= rules.CampReach)
		{
			int carried = 0;
			for (const ItemSlot& s : me->Bag().Stacks())
				if (const ItemDef* d = m_Items.Find(s.Id); !d || d->Category != "tool") carried += s.Count;
			if (carried > 0) best = { 0.0f, "Leave " + std::to_string(carried) + " things in the store", "game deposit" };
		}
		if (best.Command.empty())
			for (const auto& c : m_Clans.All())
				if (c->Folk && c->Folk != &m_Tribe && glm::distance(pos, c->Folk->Config.Economy.Camp) <= rules.VisitRadius)
					best = { 0.0f, "Trade with the " + c->Name, "game trade " + c->Name };
		m_Game.Prompt = best.Label;
		m_Game.PromptCommand = best.Command;
	}

	// ------------------------------------------------------------------ each frame
	void EditorApp::StepGame(float dt)
	{
		if (!m_Game.Active) return;
		m_Game.Clock += static_cast<double>(dt);
		++m_Game.Frames;
		// a weak GPU: the minimum preset as soon as frames are slow (after the first second of a new world)
		if (m_Game.AutoQuality && !m_Game.Lowered && m_Game.Frames > 30 && m_FrameMsSmooth > m_Controls.Game.AutoMs)
		{
			m_Game.Lowered = true;
			SetQuality("minimum");
			GameNote("Slow frames (" + Fixed(m_FrameMsSmooth) + " ms): quality minimum");
		}
		const TribeMember* me = m_Tribe.FindMember(m_Game.Player);
		Actor* actor = m_Actors.Find(m_Game.Player);
		if (!me || !actor) return;
		if (me->Dead && !m_Game.Over) { m_Game.Over = true; GameNote("You died of " + me->Cause + ". The game is over: load a save or play again."); }
		// after a load (or anything that took it away) the player gets its body and the camera back
		if (m_Player.ControlledName() != m_Game.Player) m_Player.Control(m_Game.Player);
		if (!m_GameView.Active() && m_Game.View == "follow") m_GameView.Command({ "follow" }, m_Actors, m_Player, m_World, m_Camera);
		// knocked down in a fight and alive (it woke at the camp): it stands up after a moment
		if (actor->Ragdolled() && !me->Dead)
		{
			if (m_Game.DownAt < 0.0) m_Game.DownAt = m_Game.Clock;
			else if (m_Game.Clock - m_Game.DownAt > 2.0) { actor->GetUp(); m_Game.DownAt = -1.0; }
		}
		// the ability it was sent to use has ended: the clan hears of it
		if (!m_Game.PendingKind.empty() && !actor->TasksRunning())
		{
			const bool failed = actor->TasksFailed();
			int units = 1;
			if (m_Game.PendingKind == "gather") units = actor->Bag().Count(m_Game.PendingItem) - m_Game.PendingBefore;
			if (!failed && units > 0 && m_Game.PendingKind != "water") GameNote(m_Tribe.PlayerWorked(m_Game.Player, m_Game.PendingKind, m_Game.PendingKey, static_cast<float>(units)).Text);
			else if (!failed && m_Game.PendingKind == "water") GameNote("Water in your bag: drink it when thirsty");
			else if (failed) GameNote("Could not: " + actor->TaskStatus());
			m_Game.PendingKind.clear();
			m_Tribe.SetPlayerActivity(m_Game.Player, std::string());
		}
		// walking away from a fight (a key held with the foe out of reach) breaks it off: a chase of something faster never traps the player
		if (float gap = 0.0f; m_Tribe.PlayerFighting(m_Game.Player, &gap) && gap > 0.5f && !ImGui::GetIO().WantTextInput && (Input::IsKeyDown(Key::W) || Input::IsKeyDown(Key::A) || Input::IsKeyDown(Key::S) || Input::IsKeyDown(Key::D)))
		{
			m_Tribe.PlayerStop(m_Game.Player);
			GameNote("You break off the fight");
		}
		if (me->Job == "sleep" && m_Player.GetSpeed() > 0.5f) m_Tribe.PlayerSleep(m_Game.Player, false);
		// news from the clan's log
		const std::vector<std::string>& log = m_Tribe.Log();
		if (log.size() < m_Game.LogSeen) m_Game.LogSeen = log.size();   // (the log is a ring: its oldest quarter went)
		for (size_t i = m_Game.LogSeen; i < log.size(); ++i)
			if (Newsworthy(log[i], m_Game.Player))
			{
				const size_t cut = log[i].find_first_not_of(" 0123456789.");
				GameNote(cut == std::string::npos ? log[i] : log[i].substr(cut));
			}
		m_Game.LogSeen = log.size();
		if (m_Game.Clock - m_Game.PromptAt > 0.25) { m_Game.PromptAt = m_Game.Clock; RefreshPrompt(); }
	}

	// The keys of the "controls" section, read while the ImGui frame is being built (from DrawViewport): each runs `game <action>`.
	void EditorApp::GameKeys()
	{
		if (!m_Game.Active || ImGui::GetIO().WantTextInput) return;
		for (const auto& [action, key] : m_Controls.Keys)
		{
			const ImGuiKey k = KeyNamed(key);
			// from above the character is not at hand: its keys (E, Z...) do nothing, the camera's and the clock's do
			if (m_Game.View == "top" && (action == "interact" || action == "eat" || action == "drink" || action == "deposit" || action == "sleep" || action == "claim" || action == "attack" || action == "block" || action == "inventory" || action == "people")) continue;
			if (k != ImGuiKey_None && ImGui::IsKeyPressed(k, false)) m_UiCommands.push_back("game " + action);
		}
	}

	// ------------------------------------------------------------------ the state the interface shows, as lines (also `game status`)
	std::vector<std::string> EditorApp::GameHud() const
	{
		std::vector<std::string> out;
		const TribeMember* me = m_Tribe.FindMember(m_Game.Player);
		if (!m_Game.Active || !me) { out.push_back("not playing (play [temperate|alpine|desert] [seed] [members] [clans])"); return out; }
		const Tribe& t = m_Tribe;
		out.push_back("player: " + me->Name + " (" + me->Role + ")" + (me->Dead ? ", dead" : "") + "; standing " + Fixed(t.Standing(me->Name)) + " of " + Fixed(t.Config.Player.StandingMax)
			+ (t.Leads(me->Name) ? ", you lead the clan" : ""));
		std::string needs = "needs:";
		const std::map<std::string, float> levels = t.NeedLevels(*me);
		for (const std::string& n : m_Controls.Hud.Needs)
			if (const auto it = levels.find(n); it != levels.end()) needs += " " + n + " " + Fixed(it->second);
		out.push_back(needs);
		if (const std::string c = t.PlayerCondition(me->Name); !c.empty()) out.push_back("condition: " + c);
		std::string carries, tools;
		if (me->Body)
			for (const ItemSlot& s : me->Body->Bag().Stacks())
			{
				const ItemDef* d = m_Items.Find(s.Id);
				std::string& to = d && d->Category == "tool" ? tools : carries;
				to += (to.empty() ? "" : ", ") + s.Id + (s.Count > 1 ? " x" + std::to_string(s.Count) : std::string());
			}
		out.push_back("carries: " + (carries.empty() ? std::string("nothing") : carries) + (tools.empty() ? std::string() : "; tools: " + tools));
		const Job* job = t.Board().ClaimOf(me->Name);
		std::string jobLine = "job: " + (job ? job->Id + " (" + job->Why + ")" : std::string("none"));
		if (job)
			for (const JobClaim& c : job->Claims) if (c.Member == me->Name) jobLine += ", done " + Fixed(c.Done);
		out.push_back(jobLine);
		if (me->Job == "sleep") out.push_back("asleep (move to wake up)");
		if (m_Controls.Hud.Clock)
		{
			const float hour = t.Hour();
			char clock[96];
			std::snprintf(clock, sizeof clock, "time: day %d, %02d:%02d, %s; speed x%d%s", static_cast<int>((t.WorldSeconds() / std::max(1.0, static_cast<double>(t.Config.Needs.DaySeconds))) + static_cast<double>(t.Config.Needs.StartHour) / 24.0) + 1,
				static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f), SeasonText(t.CurrentSeason()), m_TimeScale, m_Game.Paused ? ", paused" : "");
			out.push_back(clock);
		}
		const std::string interact = m_Controls.Keys.count("interact") ? m_Controls.Keys.at("interact") : std::string("interact");
		if (m_Controls.Hud.Prompt && !m_Game.Prompt.empty()) out.push_back("prompt: [" + interact + "] " + m_Game.Prompt);
		const int shown = m_Controls.Hud.Notifications;
		int n = 0;
		for (auto it = m_Game.Notes.rbegin(); it != m_Game.Notes.rend() && n < shown; ++it, ++n) out.push_back("news: " + it->Text);
		if (m_Game.Over) out.push_back("GAME OVER");
		return out;
	}

	// ------------------------------------------------------------------ game <action>
	CommandResult EditorApp::GameCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status" || sub == "hud")
		{
			std::string s;
			for (const std::string& line : GameHud()) s += (s.empty() ? "" : " | ") + line;
			return { true, s };
		}
		if (sub == "help")
		{
			m_Game.Help = !m_Game.Help;
			std::string s = "keys:";
			for (const auto& [action, key] : m_Controls.Keys) s += " " + key + "=" + action;
			return { true, s + "; WASD walk, Shift runs, the mouse turns the camera; more: game talk|give|teach|learn|marry|propose|craft|attack|block|flee|trade <...>" };
		}
		if (!m_Game.Active) return { false, "not playing: play [temperate|alpine|desert] [seed] [members] [clans]" };
		const std::string& me = m_Game.Player;
		auto act = [&](const Tribe::PlayerAct& r) { GameNote(r.Text); return CommandResult{ r.Ok, r.Text }; };
		auto arg = [&](size_t i, const std::string& fallback = std::string()) { return a.size() > i ? a[i] : fallback; };
		auto count = [&](size_t i, int fallback) { return a.size() > i ? std::max(1, std::atoi(a[i].c_str())) : fallback; };
		// the clock of the game: pause, speed, save and load work even when the game is over
		if (sub == "pause") { m_Game.Paused = !m_Game.Paused; return { true, m_Game.Paused ? "paused" : "running" }; }
		if (sub == "resume") { m_Game.Paused = false; return { true, "running" }; }
		if (sub == "faster" || sub == "slower" || sub == "speed")
		{
			static const int steps[] = { 1, 2, 4, 8, 16, 20 };
			int i = 0;
			while (i < 5 && steps[i] < m_TimeScale) ++i;
			if (sub == "faster") i = std::min(5, i + (steps[i] == m_TimeScale ? 1 : 0));
			else if (sub == "slower") i = std::max(0, i - 1);
			m_TimeScale = sub == "speed" ? std::clamp(count(1, 1), 1, 20) : steps[i];
			return { true, "speed x" + std::to_string(m_TimeScale) };
		}
		if (sub == "save") { const CommandResult r = Execute("save " + arg(1, "play")); GameNote(r.Message); return r; }
		if (sub == "load") { const CommandResult r = Execute("load " + arg(1, "play")); GameNote(r.Message); m_Game.Over = false; m_Game.LogSeen = m_Tribe.Log().size(); return r; }
		if (sub == "clan" || sub == "jobs")
		{
			m_Game.ClanPanel = !m_Game.ClanPanel;
			std::string s = sub == "jobs" ? m_Tribe.JobsReport() : m_Tribe.PlayerReport(me) + "\n" + m_Tribe.Report();
			for (char& c : s) if (c == '\n') c = '|';
			return { true, s };
		}
		// Esc: closes what is open (the clan sheet, the help), else leaves the game for the editor
		if (sub == "quit")
		{
			if (m_Tribe.PlayerFighting(m_Game.Player)) return GameCommand({ "stop" });   // (Esc first breaks off a fight)
			if (m_Game.ClanPanel || m_Game.Help || m_Game.Inventory || m_Game.People || m_Game.Build) { m_Game.ClanPanel = m_Game.Help = m_Game.Inventory = m_Game.People = m_Game.Build = false; return { true, "closed" }; }
			m_Game.Menu = !m_Game.Menu;   // (Esc again closes it; the editor is one of its buttons, not a slip of the finger)
			return { true, m_Game.Menu ? "menu" : "menu closed" };
		}
		// I, T, B: the bag, the people around and the buildings the clan can raise (panels over the game; every button of them runs a `game` command)
		if (sub == "menu") { m_Game.Menu = !m_Game.Menu; return { true, m_Game.Menu ? "menu" : "menu closed" }; }
		if (sub == "inventory") { m_Game.Inventory = !m_Game.Inventory; return { true, m_Game.Inventory ? "bag open" : "bag closed" }; }
		if (sub == "people") { m_Game.People = !m_Game.People; return { true, m_Game.People ? "people open" : "people closed" }; }
		if (sub == "build" && a.size() == 1) { m_Game.Build = !m_Game.Build; return { true, m_Game.Build ? "buildings open" : "buildings closed" }; }
		// V: the view of the player (behind it) or the overview from above (WASD pans, Q/E turn, Z/X zoom) — `game view [top|follow]`
		if (sub == "view")
		{
			const std::string want = a.size() > 1 ? a[1] : (m_Game.View == "top" ? "follow" : "top");
			if (want == "top")
			{
				if (m_Game.SavedPitchNear < 0.0f) { m_Game.SavedPitchNear = m_GameView.Params.Overview.PitchNearDegrees; m_Game.SavedPitchFar = m_GameView.Params.Overview.PitchFarDegrees; }
				m_GameView.Params.Overview.PitchNearDegrees = 78.0f;
				m_GameView.Params.Overview.PitchFarDegrees = 86.0f;
				const glm::vec3 p = m_Player.GetPosition();
				const CommandResult r = m_GameView.Command({ "overview", std::to_string(p.x), std::to_string(p.z), "45" }, m_Actors, m_Player, m_World, m_Camera);
				if (!r.Ok) return r;
				m_Game.View = "top";
				return { true, "view from above (WASD pan, Q/E turn, Z/X zoom, V returns to the player)" };
			}
			if (want != "follow") return { false, "game view [top|follow]" };
			if (m_Game.SavedPitchNear >= 0.0f)
			{
				m_GameView.Params.Overview.PitchNearDegrees = m_Game.SavedPitchNear;
				m_GameView.Params.Overview.PitchFarDegrees = m_Game.SavedPitchFar;
				m_Game.SavedPitchNear = m_Game.SavedPitchFar = -1.0f;
			}
			m_Game.View = "follow";
			return m_GameView.Command({ "follow" }, m_Actors, m_Player, m_World, m_Camera);
		}
		if (m_Game.Over) return { false, "the game is over: game load, or play again" };
		if (sub == "interact")
		{
			RefreshPrompt();
			if (m_Game.PromptCommand.empty()) return { false, "nothing to do here" };
			return Execute(m_Game.PromptCommand);
		}
		if (sub == "use" && a.size() > 1)
		{
			const std::string target = Spaced(a[1]);
			const AbilitySystemComponent* asc = m_Gameplay.Find(target);
			Actor* actor = m_Actors.Find(me);
			if (!asc || !actor) return { false, "nothing called " + target };
			std::string tasks, error;
			m_Game.PendingKind.clear();
			if (asc->HasTag("Resource.Node"))
			{
				std::string ability = "Gather", item;
				for (const auto& [kind, def] : m_NodeDefs)
					if (asc->HasTag("Resource.Node." + kind)) { if (!def.Ability.empty()) ability = def.Ability; item = def.Item; m_Game.PendingKey = kind; break; }
				for (int i = 0; i < 3; ++i) tasks += std::string(i ? " | " : "") + "ability " + ability + " " + Underscored(target) + " | wait 1";
				m_Game.PendingKind = "gather";
				m_Game.PendingItem = item;
				m_Game.PendingBefore = actor->Bag().Count(item);
			}
			else if (target == m_Tribe.Config.FireName)
			{
				if (!HasFuel(m_Items, actor->Bag()))
				{
					const Tribe::PlayerAct took = m_Tribe.PlayerTake(me, "wood", 1);
					if (!took.Ok) return { false, "you need wood or a branch for the fire (the store has none, or you are not at the camp)" };
				}
				tasks = "ability Stoke " + Underscored(target) + " | wait 1";
				m_Game.PendingKind = "stoke";
				m_Game.PendingKey = target;
			}
			else if (asc->HasTag("Interactive.Well"))
			{
				tasks = "ability DrawWater " + Underscored(target) + " | wait 1";
				m_Game.PendingKind = "water";
				m_Game.PendingKey = target;
			}
			else if (asc->HasTag("Building.Site"))
			{
				tasks = "ability Build " + Underscored(target) + " | wait 0.5 | ability Build " + Underscored(target) + " | wait 0.5";
				m_Game.PendingKind = "build";
				m_Game.PendingKey = target;
			}
			else
			{
				if (!m_Player.Interact(target, "", error)) return { false, error };
				return { true, "going to " + target };
			}
			if (!m_Player.RunTasks(tasks, error)) { m_Game.PendingKind.clear(); return { false, error }; }
			m_Tribe.SetPlayerActivity(me, m_Game.PendingKind == "water" ? std::string("drink") : m_Game.PendingKind, target);   // what its body does, as an NPC's job (the visuals read it)
			return { true, "going to " + target };
		}
		if (sub == "eat") return act(m_Tribe.PlayerEat(me));
		if (sub == "drink") return act(m_Tribe.PlayerDrink(me));
		if (sub == "deposit") return act(m_Tribe.PlayerDeposit(me, arg(1), a.size() > 2 ? count(2, 1) : 0));
		if (sub == "take" && a.size() > 1) return act(m_Tribe.PlayerTake(me, a[1], count(2, 1)));
		if (sub == "sleep") { const TribeMember* m = m_Tribe.FindMember(me); return act(m_Tribe.PlayerSleep(me, !(m && m->Job == "sleep"))); }
		if (sub == "wake") return act(m_Tribe.PlayerSleep(me, false));
		if (sub == "claim") return act(m_Tribe.PlayerClaim(me, a.size() > 1 ? a[1] + (a.size() > 2 ? " " + a[2] : std::string()) : std::string()));
		if (sub == "release") return act(m_Tribe.PlayerRelease(me));
		if (sub == "give" && a.size() > 2) return act(m_Tribe.PlayerGive(me, a[1], a[2], count(3, 1)));
		if (sub == "talk")
		{
			std::string who = arg(1);
			if (who.empty() && m_Game.PromptCommand.rfind("game talk ", 0) == 0) who = m_Game.PromptCommand.substr(10);
			return act(m_Tribe.PlayerTalk(me, who));
		}
		if (sub == "teach" && a.size() > 1) return act(m_Tribe.PlayerTeach(me, a[1], arg(2)));
		if (sub == "learn" && a.size() > 1) return act(m_Tribe.PlayerLearn(me, a[1], arg(2)));
		if (sub == "marry" && a.size() > 1) return act(m_Tribe.PlayerMarry(me, a[1]));
		if (sub == "craft" && a.size() > 1) return act(m_Tribe.PlayerCraft(me, a[1]));
		if (sub == "work" && a.size() > 2) return act(m_Tribe.PlayerWorked(me, a[1], a[2], static_cast<float>(count(3, 1))));
		if (sub == "propose" && a.size() > 2)
		{
			glm::vec2 at(0.0f);
			if (a[1] == "build")
				if (const Actor* actor = m_Actors.Find(me)) { const glm::vec3 p = actor->GetPosition(); at = { p.x, p.z }; }   // where the player stands
			const CommandResult r = act(m_Tribe.Propose(me, a[1], a[2], a.size() > 3 ? count(3, 1) : 0, at));
			return { r.Ok || r.Message.find(": rejected") != std::string::npos, r.Message };   // (the clan saying no is an answer, not a failed command)
		}
		if (sub == "attack")
		{
			const Tribe::PlayerAct r = m_Tribe.PlayerAttack(me, arg(1));
			return act(r);   // (the player closes on its foe by itself, running: Tribe::StepPlayer; a key or `game stop` breaks it off)
		}
		if (sub == "stop")
		{
			const Tribe::PlayerAct r = m_Tribe.PlayerStop(me);
			if (Actor* actor = m_Actors.Find(me); actor && r.Ok) actor->ReleaseOrders();
			return act(r);
		}
		if (sub == "block") return act(m_Tribe.PlayerBlock(me));
		if (sub == "flee") { const CommandResult r = act(m_Tribe.PlayerFlee(me)); if (r.Ok) m_Player.MoveTo(m_Tribe.Config.Economy.Camp, true); return r; }
		if ((sub == "trade" || sub == "visit") && a.size() > 1)
		{
			const Clan* c = m_Clans.Find(a[1]);
			if (!c) return { false, "no clan '" + a[1] + "' (clan list)" };
			std::string outcome;
			const std::string why = m_Clans.PlayerVisit(me, c->Id, sub, &outcome);
			GameNote(why.empty() ? outcome : why);
			return { why.empty(), why.empty() ? outcome : why };
		}
		return { false, "game status|help|interact|use <thing>|eat|drink|deposit [item n]|take <item> [n]|sleep|wake|claim [job]|release|give <member> <item> [n]|talk [member]|"
			"teach|learn <member> [tech]|marry <member>|propose gather|make <item> [n]|propose build <building>|craft <recipe>|attack [foe]|block|flee|trade|visit <clan>|"
			"clan|jobs|pause|resume|faster|slower|speed <n>|save|load [slot]" };
	}

	// ------------------------------------------------------------------ the interface over the viewport
	void EditorApp::DrawGameHud(float ox, float oy, float w, float h)
	{
		GameKeys();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const TribeMember* me = m_Tribe.FindMember(m_Game.Player);
		if (!me) return;
		const ImU32 text = IM_COL32(240, 240, 235, 235), shade = IM_COL32(0, 0, 0, 120);
		auto label = [&](float x, float y, const std::string& s, ImU32 color)
		{
			dl->AddText(ImVec2(x + 1.0f, y + 1.0f), shade, s.c_str());
			dl->AddText(ImVec2(x, y), color, s.c_str());
		};
		// the needs as bars, top left
		float y = oy + 28.0f;
		const std::map<std::string, float> levels = m_Tribe.NeedLevels(*me);
		for (const std::string& n : m_Controls.Hud.Needs)
		{
			const auto it = levels.find(n);
			if (it == levels.end()) continue;
			const float v = std::clamp(it->second / 100.0f, 0.0f, 1.0f);
			const ImU32 bar = v > 0.5f ? IM_COL32(90, 200, 110, 220) : v > 0.25f ? IM_COL32(230, 190, 60, 220) : IM_COL32(230, 70, 60, 230);
			dl->AddRectFilled(ImVec2(ox + 10.0f, y), ImVec2(ox + 170.0f, y + 12.0f), IM_COL32(20, 20, 20, 150), 3.0f);
			dl->AddRectFilled(ImVec2(ox + 10.0f, y), ImVec2(ox + 10.0f + 160.0f * v, y + 12.0f), bar, 3.0f);
			label(ox + 176.0f, y - 2.0f, n, text);
			y += 16.0f;
		}
		// the other lines of the state below them; the news on the right; the prompt at the bottom
		for (const std::string& line : GameHud())
		{
			if (line.rfind("needs:", 0) == 0 || line.rfind("news:", 0) == 0 || line.rfind("prompt:", 0) == 0) continue;
			label(ox + 10.0f, y, line, line == "GAME OVER" ? IM_COL32(255, 80, 70, 255) : text);
			y += 16.0f;
		}
		float ny = oy + h - 60.0f;
		const int shown = m_Controls.Hud.Notifications;
		int n = 0;
		for (auto it = m_Game.Notes.rbegin(); it != m_Game.Notes.rend() && n < shown; ++it, ++n)
		{
			if (m_Game.Clock - it->At > static_cast<double>(m_Controls.Hud.NotifySeconds)) break;
			const ImVec2 size = ImGui::CalcTextSize(it->Text.c_str());
			label(ox + w - size.x - 12.0f, ny, it->Text, IM_COL32(220, 225, 255, 230));
			ny -= 16.0f;
		}
		if (m_Controls.Hud.Prompt && !m_Game.Prompt.empty())
		{
			const std::string interact = m_Controls.Keys.count("interact") ? m_Controls.Keys.at("interact") : std::string("?");
			const std::string s = "[" + interact + "] " + m_Game.Prompt;
			const ImVec2 size = ImGui::CalcTextSize(s.c_str());
			dl->AddRectFilled(ImVec2(ox + (w - size.x) * 0.5f - 8.0f, oy + h - 34.0f), ImVec2(ox + (w + size.x) * 0.5f + 8.0f, oy + h - 12.0f), IM_COL32(0, 0, 0, 140), 4.0f);
			label(ox + (w - size.x) * 0.5f, oy + h - 31.0f, s, IM_COL32(255, 230, 140, 255));
		}
		if (m_Game.Paused) label(ox + w * 0.5f - 30.0f, oy + h * 0.4f, "PAUSED", IM_COL32(255, 255, 255, 255));

		// the clan, on its key
		if (m_Game.ClanPanel)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + 30.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 640.0f), std::min(h - 60.0f, 460.0f)), ImGuiCond_Appearing);
			if (ImGui::Begin("Clan##game", &m_Game.ClanPanel))
			{
				ImGui::TextWrapped("%s", m_Tribe.PlayerReport(m_Game.Player).c_str());
				if (ImGui::CollapsingHeader("Members", ImGuiTreeNodeFlags_DefaultOpen) && ImGui::BeginTable("members", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					for (const TribeMember& o : m_Tribe.Members())
					{
						if (o.Name == m_Game.Player) continue;
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(o.Name.c_str());
						ImGui::TableNextColumn(); ImGui::TextUnformatted(o.Role.c_str());
						ImGui::TableNextColumn(); ImGui::TextUnformatted(o.Dead ? ("dead (" + o.Cause + ")").c_str() : (o.Job + " " + o.Detail).c_str());
						ImGui::TableNextColumn(); ImGui::Text("%+.2f", static_cast<double>(m_Tribe.People().Affinity(m_Tribe.Config.Society, o.Name, m_Game.Player)));
					}
					ImGui::EndTable();
				}
				if (ImGui::CollapsingHeader("Job board", ImGuiTreeNodeFlags_DefaultOpen))
					for (const Job* j : m_Tribe.Board().Ordered())
					{
						ImGui::PushID(j->Id.c_str());
						if (ImGui::SmallButton("take")) m_UiCommands.push_back("game claim " + j->Id);
						ImGui::SameLine();
						ImGui::Text("%s (%d of %d) - %s", j->Id.c_str(), static_cast<int>(j->Claims.size()), j->Slots, j->Why.c_str());
						ImGui::PopID();
					}
				if (ImGui::CollapsingHeader("Store"))
					for (const ItemSlot& s : m_Tribe.Store().Stacks()) ImGui::Text("%s x%d", s.Id.c_str(), s.Count);
				if (ImGui::CollapsingHeader("Knowledge")) ImGui::TextWrapped("%s", m_Tribe.Knowledge().ClanStatus().c_str());
				if (ImGui::CollapsingHeader("Proposals")) ImGui::TextWrapped("%s", m_Tribe.ProposalsReport().empty() ? "none yet (game propose gather wood 10 | build shelter)" : m_Tribe.ProposalsReport().c_str());
				if (m_Clans.Count() > 1 && ImGui::CollapsingHeader("Other clans")) ImGui::TextWrapped("%s", m_Clans.Report().c_str());
			}
			ImGui::End();
		}
		DrawGamePanels(ox, oy, w, h);
		if (m_Game.Help)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w - 20.0f, oy + 30.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
			if (ImGui::Begin("Help##game", &m_Game.Help, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextUnformatted("WASD walk, Shift runs, drag the mouse to look");
				for (const auto& [action, key] : m_Controls.Keys) ImGui::Text("%-6s %s", key.c_str(), action.c_str());
				ImGui::TextUnformatted("I bag - T people - B buildings - X attack - Q block - the buttons at the bottom do the same");
				ImGui::TextUnformatted("Console: game talk|give|teach|learn|marry|propose|craft|attack|trade ...");
			}
			ImGui::End();
		}
	}

	void EditorApp::RegisterGameCommands()
	{
		CommandRunner& c = m_Commands;
		c.Register("play", "play [temperate|alpine|desert] [seed] [members] [clans] | off | status: H15's game: a world, clans placed by the site chooser, and you as a member of clan 0 (the X Bot of packs/full, or the fox); the keys are in the \"controls\" section (player.json); see docs/JUGAR.md",
			[this](const std::vector<std::string>& a) { return PlayCommand(a); });
		c.Register("game", "game <action>: what the keys of the game do (status, interact, eat, drink, deposit, take, sleep, claim, give, talk, teach, learn, marry, propose, craft, attack, block, flee, trade, clan, jobs, pause, faster, slower, save, load, help)",
			[this](const std::vector<std::string>& a) { return GameCommand(a); });
	}
}
