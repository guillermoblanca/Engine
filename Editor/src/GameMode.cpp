// H15: the game mode (`play`, `Editor --play`, play.txt). One command makes the world, the clans and the player's body, a member of clan 0; then the
// keys of the "controls" section (assets/data/player.json) act through `game <action>` commands (the same a script or a test uses), the game camera
// follows the player (H19) and a small interface over the viewport shows the needs, the condition (H14: health, stamina, wounds), what the player
// carries, its job, its standing, the clock, what the interact key would do and the news of the clan; Tab opens the clan (members, store, board,
// knowledge, other clans). The rules of what the player does are the tribe's (GB/Game/TribePlayer.cpp): this file only reads keys and the scene.
// See docs/JUGAR.md.
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include "GB/Input/InputSource.h"
#include "GB/Log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
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

		// The day of the game, from 1 (the clock of the HUD and its text).
		int DayOf(const Tribe& t)
		{
			return static_cast<int>((t.WorldSeconds() / std::max(1.0, static_cast<double>(t.Config.Needs.DaySeconds))) + static_cast<double>(t.Config.Needs.StartHour) / 24.0) + 1;
		}

		// What kind of thing a line of the clan's or the clans' log is for the story (GameSheets.cpp), "" when it is not one: "birth", "death",
		// "build", "know", "clans" (a new clan, a raid, a pact, a marriage between clans) or "fight".
		std::string StoryKind(const std::string& line)
		{
			if (line.find("is born") != std::string::npos) return "birth";
			if (line.find("dies") != std::string::npos || line.find("killed") != std::string::npos) return "death";
			if (line.find(" stands: ") != std::string::npos) return "build";
			if (line.find("discovers ") != std::string::npos || line.find(" enters the ") != std::string::npos) return "know";
			if (line.find("raid") != std::string::npos || line.find("attacked") != std::string::npos) return "fight";
			for (const char* k : { "leave to found", "alliance", "truce", "are hostile", "are allied", "marries", "first meet" })
				if (line.find(k) != std::string::npos) return "clans";
			return {};
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

	std::string EditorApp::Speak(const std::string& intent, const std::string& speaker, const HistoryEvent& e, const Recollection& r, float opinion) const
	{
		if (intent != "tell") return {};
		std::string view = "so it goes";
		if (r.How == "did" && opinion > 0.3f) view = "proud of it";
		else if (r.How == "suffered" && opinion < -0.3f) view = "it still hurts";
		else if (opinion > 0.3f) view = "a good thing";
		else if (opinion < -0.3f) view = "a bad business";
		const std::string told = r.From.empty() ? speaker + " tells you: \"" : speaker + " tells you what " + r.From + " told: \"";
		return told + e.Text + "\" - " + view;
	}

	std::string EditorApp::TellNews(const std::string& speaker, const std::string& listener)
	{
		WorldHistory& h = m_Clans.Past();
		const int id = h.Topic(speaker, listener);
		const HistoryEvent* e = id ? h.Find(id) : nullptr;
		const Recollection* r = e ? h.MemoryOf(speaker, id) : nullptr;
		if (!e || !r) return {};
		const Clan* c = m_Clans.ClanOf(speaker);
		const std::string line = Speak("tell", speaker, *e, *r, Opinion(m_Values, c ? c->Id : -1, speaker, *e, r->Feeling));
		h.Tell(speaker, listener, id, PlayTribe().WorldSeconds());
		return line;
	}

	int EditorApp::ClanIdOf(const std::string& member) const
	{
		const Clan* c = m_Clans.ClanOf(member);
		return c ? c->Id : (m_Tribe.FindMember(member) ? 0 : -1);
	}

	std::string EditorApp::AskLine(const Request& r) const
	{
		if (r.Kind == "bring" || r.Kind == "food") return r.Giver + " asks you to bring " + std::to_string(r.Count - r.Given) + " " + r.Item + ": " + r.Why;
		if (r.Kind == "find") return r.Giver + " asks you to find " + r.Target + ": " + r.Why;
		if (r.Kind == "teach") return r.Giver + " asks you to teach " + r.Item + ": " + r.Why;
		if (r.Kind == "message") return r.Giver + " asks you to talk to the " + r.Target + ": " + r.Why;
		return r.Giver + " asks you for help: " + r.Why;
	}

	void EditorApp::RequestDone(int id)
	{
		const Request* r = m_Clans.Requests().Find(id);
		if (!r) return;
		const RequestRules& rules = m_Clans.Requests().Rules;
		const Clan* c = m_Clans.Find(r->GiverClan);
		Tribe* t = c && c->Folk ? c->Folk : &PlayTribe();
		const std::string me = m_Game.Player;
		// the giver is grateful (a bond, when the player is of its clan), the clan thinks better of the player, and it is a deed people will tell
		if (t->FindMember(me)) t->People().Adjust(t->Config.Society, r->Giver, me, rules.Bond);
		PlayTribe().Credit(me, "gift", rules.Standing);
		std::string text = me + " does what " + r->Giver + " asked: " + r->Why;
		const int ev = t->Happen("help", me, r->Giver, text);
		if (ev && !t->FindMember(me)) m_Clans.Past().Remember(me, ev, "did", m_Clans.Past().FeelingOf("help", "did"));
		GameNote(r->Giver + " thanks you: " + r->Why);
	}

	std::string EditorApp::TalkRequest(const std::string& who)
	{
		RequestBoard& board = m_Clans.Requests();
		const std::string me = m_Game.Player;
		const int clan = ClanIdOf(who);
		// talking to someone may be what a request asked (a message to its clan, a relative found)
		std::string said;
		for (int id : board.Talked(me, who, clan)) { RequestDone(id); said = "done"; }
		if (!said.empty()) return said;
		if (const Request* open = board.OpenBy(who)) return open->Taker == me ? AskLine(*open) : std::string();
		const Clan* c = m_Clans.Find(clan);
		const Tribe& t = c && c->Folk ? *c->Folk : PlayTribe();
		const TribeMember* npc = t.FindMember(who);
		if (!npc) return {};
		Request r = board.ProblemOf(m_Clans, t, clan, *npc, me, t.WorldSeconds(), static_cast<double>(t.Config.Needs.DaySeconds));
		if (r.Kind.empty()) return {};
		r.Taker = me;
		return AskLine(board.Add(std::move(r)));
	}

	std::vector<EditorApp::HeirOption> EditorApp::Heirs(const std::string& dead) const
	{
		std::vector<HeirOption> out;
		std::set<std::string> seen = { dead };
		auto add = [&](const std::string& name, const std::string& tie, bool lineage)
		{
			if (name.empty() || seen.count(name)) return;
			const Clan* c = m_Clans.ClanOf(name);
			const Tribe* t = c && c->Folk ? c->Folk : m_Tribe.FindMember(name) ? &m_Tribe : nullptr;
			const TribeMember* m = t ? t->FindMember(name) : nullptr;
			if (!m || m->Dead || !m->Body || !m_Actors.Find(name)) return;
			seen.insert(name);
			out.push_back({ name, c ? c->Name : std::string(), tie, lineage });
		};
		const Clan* home = m_Clans.ClanOf(dead);
		const Tribe& t = home && home->Folk ? *home->Folk : m_Tribe;
		const std::map<std::string, Person>& people = t.People().People();
		// its lineage: children, then grandchildren (all its descendants, by generations)
		std::vector<std::string> gen = { dead };
		for (int depth = 0; depth < 4 && !gen.empty(); ++depth)
		{
			std::vector<std::string> next;
			for (const auto& [name, p] : people)
				if (std::find(gen.begin(), gen.end(), p.Mother) != gen.end() || std::find(gen.begin(), gen.end(), p.Father) != gen.end()) next.push_back(name);
			for (const std::string& n : next) add(n, depth == 0 ? "child" : "grandchild", true);
			gen = next;
		}
		// its kin: the partner, brothers and sisters, the parents
		if (const auto it = people.find(dead); it != people.end())
		{
			const Person& p = it->second;
			add(p.Partner, "partner", false);
			for (const auto& [name, o] : people)
				if ((!p.Mother.empty() && o.Mother == p.Mother) || (!p.Father.empty() && o.Father == p.Father)) add(name, "sibling", false);
			add(p.Mother, "parent", false);
			add(p.Father, "parent", false);
		}
		// another lineage: its clan, then the other clans
		for (const TribeMember& m : t.Members()) add(m.Name, "clan", false);
		for (const auto& c : m_Clans.All())
			if (c->Folk && c->Folk != &t)
				for (const TribeMember& m : c->Folk->Members()) add(m.Name, "other clan", false);
		return out;
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
			if (!PlayTribe().FindMember(name) || !m_Actors.Find(name)) return { false, "there is no game to go back to: `play` starts one" };
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
			m_Game.LogSeen = PlayTribe().Log().size();
			m_Game.ClanLogSeen = m_Clans.Log().size();
			GameNote("Back in the game.");
			return { true, "back in the game" };
		}
		const SpawnBatch crew(*this);
		GameSetup setup = m_Controls.Game;
		// T60: a quality given at launch (`--quality=medium`) is kept by the game: neither player.json's preset nor "auto" (which starts low and
		// drops to minimum on slow frames) overrides what was asked for explicitly. Without `--quality`, player.json decides as before.
		if (!m_LaunchQuality.empty()) setup.Quality = m_LaunchQuality;
		// `play here`: into the world as it is (a terrain graph's map, the villages of `village`), as one more member of the first clan
		const bool here = !a.empty() && a[0] == "here";
		if (here && (!m_World.IsBuilt() || m_Tribe.Members().empty())) return { false, "play here needs a world with a clan (terrain_graph preset ... ; village)" };
		std::vector<int> numbers;
		for (const std::string& w : (here ? std::vector<std::string>() : a))
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

		CommandResult r;
		if (!here)
		{
			r = Execute("generate " + setup.World + " " + std::to_string(setup.Seed));
			if (!r.Ok) return r;
			FinishWorldBuild();   // the clans and the player go on the new world, not on the one it replaces
			r = Execute("resources world " + std::to_string(setup.Resources));
			if (!r.Ok) return r;
		}
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
		if (here) model = m_TribeModel;   // (the clan's own look)
		m_Game.Model = model;
		if (!here)
		{
			r = Execute("tribe build \"" + model + "\" " + std::to_string(setup.Members));
			if (!r.Ok) return r;
		}
		std::string clans;
		for (int i = 1; i < (here ? 1 : setup.Clans); ++i)
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
		m_Game.ClanLogSeen = m_Clans.Log().size();
		m_Game.History.clear();
		const std::string help = m_Controls.Keys.count("help") ? m_Controls.Keys.at("help") : std::string("help");
		GameNote("You are " + name + ", a " + m_Tribe.FindMember(name)->Role + " of the clan. Gather, bring to the store, take jobs, help. [" + help + "] help");
		if (here) return { true, "playing here: " + name + " in the first clan, of " + std::to_string(m_Tribe.Members().size()) + "; game status | game help" };
		return { true, "playing: " + setup.World + " " + std::to_string(setup.Seed) + ", " + name + " in a clan of " + std::to_string(setup.Members) + clans
			+ " (" + (PlayerMode::IsFbx(model) ? std::filesystem::path(model).filename().string() : model) + "); game status | game help" };
	}

	// ------------------------------------------------------------------ what the interact key would do
	namespace
	{
		struct Candidate { float Distance = 1e30f; std::string Label, Command; glm::vec3 At{ 0.0f }; bool HasAt = false; };
	}

	void EditorApp::RefreshPrompt()
	{
		m_Game.Prompt.clear();
		m_Game.HasPromptPos = false;
		m_Game.PromptCommand.clear();
		const Actor* me = m_Actors.Find(m_Game.Player);
		const TribeMember* member = PlayTribe().FindMember(m_Game.Player);
		if (!me || !member || member->Dead) return;
		const glm::vec3 p3 = me->GetPosition();
		const glm::vec2 pos(p3.x, p3.z);
		const PlayerRules& rules = PlayTribe().Config.Player;
		Candidate best;
		auto offer = [&](float d, const std::string& label, const std::string& command, const glm::vec3* at = nullptr) { if (d < best.Distance) { best = { d, label, command }; best.HasAt = at != nullptr; if (at) best.At = *at; } };
		// someone fighting the player first
		if (const std::vector<std::string> foes = m_Combat.AttackersOf(m_Game.Player); !foes.empty()) { m_Game.Prompt = "Fight back " + foes.front(); m_Game.PromptCommand = "game attack " + foes.front(); return; }
		// the members of the clan within reach
		for (const TribeMember& o : PlayTribe().Members())
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
			const bool node = asc->HasTag("Resource.Node"), fire = e.Name == PlayTribe().Config.FireName, well = asc->HasTag("Interactive.Well"), site = asc->HasTag("Building.Site");
			if (!node && !fire && !well && !site) continue;
			const glm::vec4 w = world.WorldMatrix(id)[3];
			const float d = glm::distance(glm::vec2(w.x, w.z), pos);
			if (d > rules.Reach + (site ? 2.0f : 1.0f)) continue;
			const glm::vec3 at3(w.x, w.y, w.z);
			if (node && ResourceSystem::Amount(*asc) > 0)
			{
				std::string ability = "Gather";
				for (const auto& [kind, def] : m_NodeDefs)
					if (asc->HasTag("Resource.Node." + kind)) { if (!def.Ability.empty()) ability = def.Ability; break; }
				offer(d, ability + " " + e.Name, "game use " + Underscored(e.Name), &at3);
			}
			else if (fire) offer(d, "Feed the campfire", "game use " + Underscored(e.Name), &at3);
			else if (well) offer(d, "Draw water at " + e.Name, "game use " + Underscored(e.Name), &at3);
			else if (site)
			{
				// a phase without all its materials cannot be worked (Build fails "needs Building.Ready" and the player stood there): bring what it
				// lacks if the bag has some, else say that it needs materials (`game use` tells which)
				const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs);
				const SiteState st = def ? ReadSite(*asc, *def) : SiteState();
				bool carries = false;
				for (const auto& [item, n] : st.Missing)
				{
					carries = carries || me->Bag().Count(item) > 0;
					if (const auto sub = st.Substitutes.find(item); sub != st.Substitutes.end())
						for (const std::string& other : sub->second) carries = carries || me->Bag().Count(other) > 0;
				}
				if (!def || st.Ready) offer(d, "Build " + e.Name, "game use " + Underscored(e.Name), &at3);
				else if (carries) offer(d, "Bring materials to " + e.Name, "game use " + Underscored(e.Name), &at3);
				else offer(d, e.Name + " needs materials", "game use " + Underscored(e.Name), &at3);
			}
		}
		if (best.Command.empty() && glm::distance(pos, PlayTribe().Config.Economy.Camp) <= rules.CampReach)
		{
			int carried = 0;
			for (const ItemSlot& s : me->Bag().Stacks())
				if (const ItemDef* d = m_Items.Find(s.Id); !d || d->Category != "tool") carried += s.Count;
			if (carried > 0) best = { 0.0f, "Leave " + std::to_string(carried) + " things in the store", "game deposit" };
		}
		if (best.Command.empty())
			for (const auto& c : m_Clans.All())
				if (c->Folk && c->Folk != &PlayTribe() && glm::distance(pos, c->Folk->Config.Economy.Camp) <= rules.VisitRadius)
					best = { 0.0f, "Trade with the " + c->Name, "game trade " + c->Name };
		m_Game.Prompt = best.Label;
		m_Game.PromptCommand = best.Command;
		m_Game.PromptPos = best.At;
		m_Game.HasPromptPos = best.HasAt;
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
		const TribeMember* me = PlayTribe().FindMember(m_Game.Player);
		Actor* actor = m_Actors.Find(m_Game.Player);
		if (!me || !actor) return;
		actor->KeysTakeOver = true;   // (a person plays it: a key drops a chase or a trip to a node)
		// V3: a life ends; the game goes on with an heir the player chooses (its lineage first), and is over only when nobody is left
		if (me->Dead && !m_Game.Over && !m_Game.Heir)
		{
			// V6: between two lives the world leaps the years of its era (its data); then the heir is chosen among those living after them
			// (the dead's grandchildren born in those years are its lineage), who lived them and remember them
			if (const float years = PlayTribe().EraLeap(); years > 0.0f)
			{
				std::string told = std::to_string(static_cast<int>(years)) + " years pass";
				for (const std::string& l : m_Clans.Leap(years)) told += " | " + l;
				SyncNewGameplay();   // (the newborns' bodies)
				GB_CLIENT_INFO("{}", told);
				GameNote(std::to_string(static_cast<int>(years)) + " years pass.");
			}
			if (Heirs(m_Game.Player).empty()) { m_Game.Over = true; GameNote("You died of " + PlayTribe().FindMember(m_Game.Player)->Cause + " and nobody is left to carry on. The game is over: load a save or play again."); }
			else { m_Game.Heir = true; m_Game.Dead = m_Game.Player; GameNote("You died of " + PlayTribe().FindMember(m_Game.Player)->Cause + ". Choose who carries on: game heir"); }
			return;   // (the leap's births moved the members: `me` and the actor are not to be used any more this frame)
		}
		// V2: running to a person (game approach): the way is set again every second, as it moves, until the person is within reach
		if (!m_Game.Approach.empty() && !me->Dead)
		{
			const Clan* c = m_Clans.ClanOf(m_Game.Approach);
			const Tribe* t = c && c->Folk ? c->Folk : PlayTribe().FindMember(m_Game.Approach) ? &PlayTribe() : nullptr;
			const TribeMember* o = t ? t->FindMember(m_Game.Approach) : nullptr;
			const glm::vec2 mine(actor->GetPosition().x, actor->GetPosition().z);
			if (!o || o->Dead || !o->Body) m_Game.Approach.clear();
			else if (glm::distance(t->Position(*o), mine) <= PlayTribe().Config.Player.Reach - 0.5f) { if (actor->TasksRunning()) actor->StopTasks(); }   // (it stays by the person until it talks to it or cancels)
			else if (m_Game.ApproachAt < 0.0 || m_Game.Clock - m_Game.ApproachAt >= 1.0)
			{
				m_Game.ApproachAt = m_Game.Clock;
				const glm::vec2 at = t->Position(*o);
				std::string error;
				actor->RunTasks("goto " + std::to_string(at.x) + " " + std::to_string(at.y) + " run", error);
			}
		}
		for (int id : m_Clans.Requests().Expire(PlayTribe().WorldSeconds()))   // V3: what was asked and not done in time is let go
			if (const Request* r = m_Clans.Requests().Find(id); r && r->Taker == m_Game.Player) GameNote(r->Giver + " no longer counts on you: " + r->Why);
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
			if (!failed && units > 0 && m_Game.PendingKind != "water") GameNote(PlayTribe().PlayerWorked(m_Game.Player, m_Game.PendingKind, m_Game.PendingKey, static_cast<float>(units)).Text);
			else if (!failed && m_Game.PendingKind == "water") GameNote("Water in your bag: drink it when thirsty");
			else if (failed) GameNote("Could not: " + actor->TaskStatus());
			m_Game.PendingKind.clear();
			PlayTribe().SetPlayerActivity(m_Game.Player, std::string());
		}
		// walking away from a fight (a key held with the foe out of reach) breaks it off: a chase of something faster never traps the player
		if (float gap = 0.0f; PlayTribe().PlayerFighting(m_Game.Player, &gap) && gap > 0.5f && !ImGui::GetIO().WantTextInput && (GetInput().IsKeyDown(Key::W) || GetInput().IsKeyDown(Key::A) || GetInput().IsKeyDown(Key::S) || GetInput().IsKeyDown(Key::D)))
		{
			PlayTribe().PlayerStop(m_Game.Player);
			GameNote("You break off the fight");
		}
		if (me->Job == "sleep" && m_Player.GetSpeed() > 0.5f) PlayTribe().PlayerSleep(m_Game.Player, false);
		// news from the clan's log
		const std::vector<std::string>& log = PlayTribe().Log();
		if (log.size() < m_Game.LogSeen) m_Game.LogSeen = log.size();   // (the log is a ring: its oldest quarter went)
		auto remember = [&](const std::string& line)   // the story keeps what mattered, with its day (the oldest go after 400)
		{
			const std::string kind = StoryKind(line);
			if (kind.empty()) return;
			const size_t cut = line.find_first_not_of(" 0123456789.");
			m_Game.History.push_back({ cut == std::string::npos ? line : line.substr(cut), kind, DayOf(PlayTribe()) });
			while (m_Game.History.size() > 400) m_Game.History.pop_front();
		};
		for (size_t i = m_Game.LogSeen; i < log.size(); ++i)
		{
			if (Newsworthy(log[i], m_Game.Player))
			{
				const size_t cut = log[i].find_first_not_of(" 0123456789.");
				GameNote(cut == std::string::npos ? log[i] : log[i].substr(cut));
			}
			remember(log[i]);
		}
		m_Game.LogSeen = log.size();
		const std::vector<std::string>& between = m_Clans.Log();   // (what passes between the clans: new clans, raids, pacts)
		if (between.size() < m_Game.ClanLogSeen) m_Game.ClanLogSeen = between.size();
		for (size_t i = m_Game.ClanLogSeen; i < between.size(); ++i) remember(between[i]);
		m_Game.ClanLogSeen = between.size();
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
		const TribeMember* me = PlayTribe().FindMember(m_Game.Player);
		if (!m_Game.Active || !me) { out.push_back("not playing (play [temperate|alpine|desert] [seed] [members] [clans])"); return out; }
		const Tribe& t = PlayTribe();
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
			std::snprintf(clock, sizeof clock, "time: day %d, %02d:%02d, %s; speed x%d%s", DayOf(t),
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
		if (sub == "load")
		{
			const CommandResult r = Execute("load " + arg(1, "play"));
			GameNote(r.Message);
			m_Game.Over = false;
			// V0: the save says who is played (a member marked as a player in any clan); the character may not be the one the game started with
			if (r.Ok && !PlayTribe().FindMember(m_Game.Player))
				for (const auto& c : m_Clans.All())
					if (c->Folk)
						if (const TribeMember* p = c->Folk->PlayerMember(); p && m_Actors.Find(p->Name)) { m_Game.Player = p->Name; m_Player.Control(p->Name); break; }
			m_Game.LogSeen = PlayTribe().Log().size();
			return r;
		}
		// V0: take over another person, of this clan or any other: the character left goes back to its clan's brain, the new one is a person's
		if (sub == "become" && a.size() > 1)
		{
			std::string name = a[1];
			if (name == "clan" && a.size() > 2)   // `game become clan <id or name>`: the first living member of that clan with a body
			{
				const Clan* c = m_Clans.Find(a[2]);
				if (!c || !c->Folk) return { false, "there is no clan " + a[2] };
				name.clear();
				for (const TribeMember& o : c->Folk->Members())
					if (!o.Dead && o.Body && o.Name != m_Game.Player && m_Actors.Find(o.Name)) { name = o.Name; break; }
				if (name.empty()) return { false, "nobody of " + c->Name + " can be taken over" };
			}
			Tribe* to = m_Tribe.FindMember(name) ? &m_Tribe : nullptr;
			if (!to)
				if (const Clan* c = m_Clans.ClanOf(name)) to = c->Folk;
			const TribeMember* m = to ? to->FindMember(name) : nullptr;
			if (!m) return { false, "there is nobody called " + name };
			if (m->Dead) return { false, name + " is dead" };
			if (!m->Body || !m_Actors.Find(name)) return { false, name + " has no body in the world" };
			if (name == m_Game.Player) return { true, "you are already " + name };
			if (!m_Game.PendingKind.empty()) GameCommand({ "cancel" });
			Tribe& from = PlayTribe();
			from.SetPlayerActivity(m_Game.Player, std::string());
			from.SetPlayer(m_Game.Player, false);
			to->SetPlayer(name, true);
			m_Game.Player = name;
			m_Player.Control(name);
			m_Game.LogSeen = to->Log().size();
			m_Game.History.clear();
			const Clan* c = m_Clans.ClanOf(name);
			const std::string clan = c ? c->Name : std::string("the clan");
			GameNote("You are " + name + ", a " + m->Role + " of " + clan + ".");
			return { true, "you are " + name + " of " + clan };
		}
		if (sub == "clan" || sub == "jobs")
		{
			bool& sheet = sub == "clan" ? m_Game.Village : m_Game.ClanPanel;   // (Tab: the village at a glance; J: the job board)
			sheet = !sheet;
			std::string s = sub == "jobs" ? PlayTribe().JobsReport() : PlayTribe().PlayerReport(me) + "\n" + PlayTribe().Report();
			for (char& c : s) if (c == '\n') c = '|';
			return { true, s };
		}
		// Esc: closes what is open (the clan sheet, the help), else leaves the game for the editor
		if (sub == "quit")
		{
			if (PlayTribe().PlayerFighting(m_Game.Player)) return GameCommand({ "stop" });   // (Esc first breaks off a fight)
			if (!m_Game.PendingKind.empty() && m_Player.TasksRunning()) return GameCommand({ "cancel" });   // (then what the player is doing)
			if (m_Game.TechTree) { m_Game.TechTree = false; return { true, "closed" }; }   // (the tree first: it is over everything)
			if (m_Game.ClanPanel || m_Game.Help || m_Game.Inventory || m_Game.People || m_Game.Build || m_Game.Console || m_Game.Village || m_Game.Homes || m_Game.Clans || m_Game.Tech || m_Game.Map || m_Game.Story)
			{
				m_Game.ClanPanel = m_Game.Help = m_Game.Inventory = m_Game.People = m_Game.Build = m_Game.Console = false;
				m_Game.Village = m_Game.Homes = m_Game.Clans = m_Game.Tech = m_Game.Map = m_Game.Story = false;
				return { true, "closed" };
			}
			m_Game.Menu = !m_Game.Menu;   // (Esc again closes it; the editor is one of its buttons, not a slip of the finger)
			return { true, m_Game.Menu ? "menu" : "menu closed" };
		}
		// I, T, B: the bag, the people around and the buildings the clan can raise (panels over the game; every button of them runs a `game` command)
		// the clan book by its groups (the buttons of the action bar): the sheet last open in the group, else its first; again, the book closes
		if (sub == "book")
		{
			const std::string group = arg(1, "clan");
			std::string first, last = m_Game.BookLast.count(group) ? m_Game.BookLast[group] : std::string();
			bool open = false;
			for (const BookSheet& s : BookSheets())
			{
				if (s.Group != group) continue;
				if (first.empty()) first = s.Action;
				open = open || m_Game.BookTab == s.Action;
			}
			if (first.empty()) return { false, "game book me|clan|world" };
			for (const BookSheet& s : BookSheets()) *BookFlag(s.Action) = false;
			std::string sheet = last.empty() ? first : last;
			// `game book <group> <sheet>`: that sheet of the group, open even if the group was
			if (a.size() > 2)
			{
				bool found = false;
				for (const BookSheet& s : BookSheets()) found = found || (s.Group == group && s.Action == a[2]);
				if (!found) return { false, "no sheet " + a[2] + " in " + group };
				sheet = a[2];
			}
			else if (open) return { true, "book closed" };
			*BookFlag(sheet) = true;
			return { true, "book: " + sheet };
		}
		// what the selection card shows (as a left click picks it): a member, a node, a building or a site by its name; none clears it
		if (sub == "select")
		{
			std::string name;
			for (size_t i = 1; i < a.size(); ++i) name += (name.empty() ? "" : " ") + a[i];
			if (name.empty() || name == "none") { m_Game.Selected.clear(); return { true, "nothing selected" }; }
			if (!m_Gameplay.Find(name)) return { false, "nothing called '" + name + "' to select" };
			m_Game.Selected = name;
			return { true, "selected " + name };
		}
		if (sub == "menu") { m_Game.Menu = !m_Game.Menu; return { true, m_Game.Menu ? "menu" : "menu closed" }; }
		if (sub == "console") { m_Game.Console = !m_Game.Console; return { true, m_Game.Console ? "console open" : "console closed" }; }
		if (sub == "inventory") { m_Game.Inventory = !m_Game.Inventory; return { true, m_Game.Inventory ? "bag open" : "bag closed" }; }
		if (sub == "people") { m_Game.People = !m_Game.People; return { true, m_Game.People ? "people open" : "people closed" }; }
		if (sub == "build" && a.size() == 1) { m_Game.Build = !m_Game.Build; return { true, m_Game.Build ? "buildings open" : "buildings closed" }; }
		// U, Y, O, K, N: the sheets that look at the village, its households, the other clans, the technologies and the story (GameSheets.cpp)
		if (sub == "village") { m_Game.Village = !m_Game.Village; return { true, m_Game.Village ? "village open" : "village closed" }; }
		if (sub == "homes") { m_Game.Homes = !m_Game.Homes; return { true, m_Game.Homes ? "homes open" : "homes closed" }; }
		if (sub == "clans") { m_Game.Clans = !m_Game.Clans; return { true, m_Game.Clans ? "clans open" : "clans closed" }; }
		// K: the tree of knowledge over the game (the book's Knowledge sheet still has the list); `game research <tech>|none`: the clan's aim
		if ((sub == "tech" || sub == "tree") && a.size() > 1)   // `game tech <tech>`: the tree open on that technology's card
		{
			m_Game.TechTree = true;
			m_Game.TreePick = a[1];
			return { true, "tree of knowledge open on " + a[1] };
		}
		if (sub == "tech" || sub == "tree") { m_Game.TechTree = !m_Game.TechTree; return { true, m_Game.TechTree ? "tree of knowledge open" : "tree of knowledge closed" }; }
		if (sub == "research")
		{
			std::string tech;
			for (size_t i = 1; i < a.size(); ++i) tech += (tech.empty() ? "" : " ") + a[i];
			return act(PlayTribe().PlayerResearch(me, tech));
		}
		if (sub == "story")
		{
			m_Game.Story = !m_Game.Story;
			std::string s = std::string(m_Game.Story ? "story open: " : "story closed: ") + std::to_string(m_Game.History.size()) + " events";
			for (size_t i = m_Game.History.size() > 3 ? m_Game.History.size() - 3 : 0; i < m_Game.History.size(); ++i)
				s += " | day " + std::to_string(m_Game.History[i].Day) + " " + m_Game.History[i].Kind + ": " + m_Game.History[i].Text;
			return { true, s };
		}
		// M: the map sheet, and the areas painted on the ground with it; `game map <on|off|territory|home|zones|productivity [item]>` sets them
		if (sub == "map")
		{
			if (a.size() == 1)
			{
				m_Game.Map = !m_Game.Map;
				m_MapAreas.On = m_Game.Map;
				return { true, m_Game.Map ? "map open" : "map closed" };
			}
			m_Game.Map = a[1] != "off";   // (the areas are painted while the map sheet is open: GamePanels.cpp)
			return MapAreasCommand(std::vector<std::string>(a.begin() + 1, a.end()));
		}
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
		// V3: who carries on after the character's death: `game heir` lists them (its lineage first), `game heir <name>` takes that person
		if (sub == "heir")
		{
			const std::string dead = m_Game.Heir ? m_Game.Dead : m_Game.Player;
			const std::vector<HeirOption> heirs = Heirs(dead);
			if (a.size() < 2)
			{
				std::string s = heirs.empty() ? std::string("nobody can carry on") : std::string(heirs.front().Lineage ? "your lineage:" : "your lineage ends: choose someone of another lineage:");
				for (size_t i = 0; i < heirs.size() && i < 12; ++i) s += " " + heirs[i].Name + " (" + heirs[i].Tie + (heirs[i].Clan.empty() ? "" : ", " + heirs[i].Clan) + ")";
				return { true, s };
			}
			const auto pick = std::find_if(heirs.begin(), heirs.end(), [&](const HeirOption& h) { return h.Name == a[1] || (a[1] == "first" && &h == &heirs.front()); });
			if (pick == heirs.end()) return { false, a[1] + " cannot carry on (game heir lists who can)" };
			const std::string name = pick->Name;
			// the heir grew up hearing the dead tell its life: it remembers the weightiest of it
			WorldHistory& h = m_Clans.Past();
			for (const HistoryEvent* e : h.Recall(dead, 12)) h.Tell(dead, name, e->Id, PlayTribe().WorldSeconds());
			m_Game.Heir = false;
			m_Game.Over = false;
			const CommandResult r = GameCommand({ "become", name });
			if (r.Ok) GameNote("You carry on as " + name + " (" + pick->Tie + "), who remembers what " + dead + " told.");
			return r;
		}
		if (sub == "leap")   // V6: the world lives some years in aggregate now (the years of the era when none are given)
		{
			const float years = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : PlayTribe().EraLeap();
			if (years <= 0.0f) return { false, "usage: game leap <years> (this era leaps none)" };
			std::string s = std::to_string(static_cast<int>(years)) + " years pass";
			for (const std::string& l : m_Clans.Leap(years)) s += " | " + l;
			SyncNewGameplay();
			return { true, s };
		}
		if (sub == "die")   // (for tests and trying: the character dies now of old age, or of the cause given)
		{
			const std::string cause = arg(1, "OldAge");
			return PlayTribe().Kill(me, cause) ? CommandResult{ true, me + " dies of " + cause } : CommandResult{ false, "you cannot die now" };
		}
		if (sub == "requests") return { true, m_Clans.Requests().Report(me) };
		if (m_Game.Heir) return { false, "choose who carries on first: game heir" };
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
			else if (target == PlayTribe().Config.FireName)
			{
				if (!HasFuel(m_Items, actor->Bag()))
				{
					const Tribe::PlayerAct took = PlayTribe().PlayerTake(me, "wood", 1);
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
				// its phase lacks materials: what the bag has of them goes in (substitutes too); still short, it says what is missing and nothing is queued
				if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs); def && !ReadSite(*asc, *def).Ready)
				{
					AbilitySystemComponent* site = m_Gameplay.Find(target);
					int brought = 0;
					const SiteState before = ReadSite(*site, *def);
					for (const auto& [item, n] : before.Missing)
					{
						std::vector<std::string> kinds{ item };
						if (const auto alt = before.Substitutes.find(item); alt != before.Substitutes.end()) kinds.insert(kinds.end(), alt->second.begin(), alt->second.end());
						for (const std::string& k : kinds)
							if (const int have = actor->Bag().Count(k); have > 0)
							{
								const int took = DeliverToSite(*site, *def, k, have);
								actor->Bag().Remove(k, took);
								brought += took;
							}
					}
					const SiteState after = ReadSite(*site, *def);
					if (!after.Ready)
					{
						std::string lacks;
						for (const auto& [item, n] : after.Missing) lacks += (lacks.empty() ? "" : ", ") + item + " x" + std::to_string(n);
						return { brought > 0, (brought > 0 ? "you bring " + std::to_string(brought) + " to " + target + "; " : std::string()) + target + " still lacks " + lacks
							+ " (take it from the store, gather it, or let the clan bring it)" };
					}
				}
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
			PlayTribe().SetPlayerActivity(me, m_Game.PendingKind == "water" ? std::string("drink") : m_Game.PendingKind, target);   // what its body does, as an NPC's job (the visuals read it)
			return { true, "going to " + target };
		}
		if (sub == "eat") return act(PlayTribe().PlayerEat(me));
		if (sub == "drink") return act(PlayTribe().PlayerDrink(me));
		if (sub == "deposit") return act(PlayTribe().PlayerDeposit(me, arg(1), a.size() > 2 ? count(2, 1) : 0));
		if (sub == "take" && a.size() > 1) return act(PlayTribe().PlayerTake(me, a[1], count(2, 1)));
		if (sub == "sleep") { const TribeMember* m = PlayTribe().FindMember(me); return act(PlayTribe().PlayerSleep(me, !(m && m->Job == "sleep"))); }
		if (sub == "wake") return act(PlayTribe().PlayerSleep(me, false));
		if (sub == "claim")
		{
			std::string job;
			for (size_t i = 1; i < a.size(); ++i) job += (job.empty() ? "" : " ") + a[i];
			return act(PlayTribe().PlayerClaim(me, job));
		}
		if (sub == "release") return act(PlayTribe().PlayerRelease(me));
		if (sub == "give" && a.size() > 2)
		{
			const int had = m_Actors.Find(me) ? m_Actors.Find(me)->Bag().Count(a[2]) : 0;
			const CommandResult r = act(PlayTribe().PlayerGive(me, a[1], a[2], count(3, 1)));
			const int now = m_Actors.Find(me) ? m_Actors.Find(me)->Bag().Count(a[2]) : 0;
			if (r.Ok)   // V3: a gift may be what someone asked for
				for (int id : m_Clans.Requests().Gave(me, a[1], ClanIdOf(a[1]), a[2], had - now, PlayTribe().IsFood(a[2]))) RequestDone(id);
			return r;
		}
		// V2: walks the character to a person of any clan (it runs to where that person is now; a script and the "go to" of the interface)
		if (sub == "approach" && a.size() > 1)
		{
			const Clan* c = m_Clans.ClanOf(a[1]);
			const Tribe* t = c ? c->Folk : PlayTribe().FindMember(a[1]) ? &PlayTribe() : nullptr;
			const TribeMember* o = t ? t->FindMember(a[1]) : nullptr;
			if (!o || o->Dead || !o->Body) return { false, "nobody called " + a[1] };
			m_Game.Approach = a[1];   // (StepGame follows it until it is within reach)
			m_Game.ApproachAt = -1.0;
			const Actor* mine = m_Actors.Find(me);
			return { true, "you go to " + a[1] + (mine ? " (" + std::to_string(static_cast<int>(glm::distance(t->Position(*o), glm::vec2(mine->GetPosition().x, mine->GetPosition().z)))) + " m)" : std::string()) };
		}
		// V3: a conversation has a state: a person listens to the player a few times in a while (PatienceTimes in PatienceSeconds of game), and
		// then it has no time and minds being pressed. `game ask <who> about <person>`: what it knows of someone; `game ask <who> where <item>`:
		// where its clan finds it; `game flatter <who>`: kind words (a bond while it is patient, a cold shoulder when it is not).
		auto patient = [&](const std::string& who) -> bool
		{
			constexpr int kTimes = 4;
			constexpr double kSeconds = 120.0;
			const double now = PlayTribe().WorldSeconds();
			GameModeState::Patience& p = m_Game.Talks[who];
			if (now - p.Since > kSeconds) { p.Times = 0; p.Since = now; }
			return ++p.Times <= kTimes;
		};
		auto someone = [&](const std::string& who, Tribe*& t) -> const TribeMember*
		{
			const Clan* c = m_Clans.ClanOf(who);
			t = c && c->Folk ? c->Folk : PlayTribe().FindMember(who) ? &PlayTribe() : nullptr;
			const TribeMember* o = t ? t->FindMember(who) : nullptr;
			const Actor* mine = m_Actors.Find(me);
			if (!o || o->Dead || !o->Body || !mine) return nullptr;
			return glm::distance(t->Position(*o), glm::vec2(mine->GetPosition().x, mine->GetPosition().z)) <= PlayTribe().Config.Player.Reach + 1.0f ? o : nullptr;
		};
		if ((sub == "ask" && a.size() > 3) || (sub == "flatter" && a.size() > 1))
		{
			Tribe* t = nullptr;
			const TribeMember* o = someone(a[1], t);
			if (!o) return { false, a[1] + " is not within reach" };
			if (!patient(a[1]))
			{
				if (t->FindMember(me)) t->People().Adjust(t->Config.Society, a[1], me, -0.01f);
				const std::string text = a[1] + " has no time for you now";
				GameNote(text);
				return { false, text };
			}
			std::string text;
			if (sub == "flatter")
			{
				if (t->FindMember(me)) t->People().Adjust(t->Config.Society, a[1], me, 0.02f);
				text = a[1] + " is pleased by your words";
			}
			else if (a[2] == "about")
			{
				WorldHistory& h = m_Clans.Past();
				const int id = h.Topic(a[1], me, a[3]);
				const HistoryEvent* e = id ? h.Find(id) : nullptr;
				const Recollection* r = e ? h.MemoryOf(a[1], id) : nullptr;
				if (e && r)
				{
					text = Speak("tell", a[1], *e, *r, Opinion(m_Values, ClanIdOf(a[1]), a[1], *e, r->Feeling));
					h.Tell(a[1], me, id, PlayTribe().WorldSeconds());
				}
				else text = a[1] + " knows nothing more of " + a[3];
			}
			else if (a[2] == "where")
			{
				const std::vector<Tribe::ProductivityCell> cells = t->ProductivityMap(a[3]);
				const auto best = std::max_element(cells.begin(), cells.end(), [](const Tribe::ProductivityCell& x, const Tribe::ProductivityCell& y) { return x.Value < y.Value; });
				const Actor* mine = m_Actors.Find(me);
				if (best == cells.end() || !mine) text = a[1] + " does not know where to find " + a[3];
				else
				{
					const glm::vec2 from(mine->GetPosition().x, mine->GetPosition().z), d = best->Pos - from;
					static const char* kDirs[] = { "east", "north-east", "north", "north-west", "west", "south-west", "south", "south-east" };
					const int dir = static_cast<int>(std::lround(std::atan2(-d.y, d.x) / (3.14159265f / 4.0f)) + 8) % 8;
					text = a[1] + " says there is " + a[3] + " about " + std::to_string(static_cast<int>(glm::length(d))) + " m to the " + kDirs[dir];
				}
			}
			else return { false, "game ask <who> about <person> | game ask <who> where <item>" };
			GameNote(text);
			return { true, text };
		}
		if (sub == "talk")
		{
			std::string who = arg(1);
			if (who.empty() && m_Game.PromptCommand.rfind("game talk ", 0) == 0) who = m_Game.PromptCommand.substr(10);
			if (who == m_Game.Approach) m_Game.Approach.clear();   // (the walk to this person ends with the talk)
			// V2: someone of another clan (a visit) has no bond with the clan to grow, but it talks: it tells what it knows
			const Clan* theirs = who.empty() ? nullptr : m_Clans.ClanOf(who);
			if (theirs && theirs->Folk != &PlayTribe())
			{
				const TribeMember* o = theirs->Folk->FindMember(who);
				const Actor* mine = m_Actors.Find(me);
				if (!o || o->Dead || !o->Body || !mine) return { false, "nobody called " + who };
				if (glm::distance(theirs->Folk->Position(*o), glm::vec2(mine->GetPosition().x, mine->GetPosition().z)) > PlayTribe().Config.Player.Reach + 1.0f) return { false, who + " is too far away" };
				const std::string news = TellNews(who, me);
				const std::string text = news.empty() ? who + " of the " + theirs->Name + " has nothing new to tell you" : news;
				GameNote(text);
				const std::string ask = TalkRequest(who);   // V3: a request, a reminder, or what a request asked done
				if (!ask.empty() && ask != "done") GameNote(ask);
				return { true, text + (ask.empty() || ask == "done" ? std::string() : "; " + ask) };
			}
			if (!who.empty() && !patient(who)) { const std::string text = who + " has no time for you now"; GameNote(text); return { false, text }; }
			CommandResult r = act(PlayTribe().PlayerTalk(me, who));
			if (r.Ok)
				if (const std::string news = TellNews(who, me); !news.empty()) { GameNote(news); r.Message += "; " + news; }   // V2: a real fact
			if (r.Ok)
				if (const std::string ask = TalkRequest(who); !ask.empty())   // V3: a request, a reminder, or what a request asked done
				{
					if (ask != "done") { GameNote(ask); r.Message += "; " + ask; }
				}
			return r;
		}
		if (sub == "teach" && a.size() > 1)
		{
			const CommandResult r = act(PlayTribe().PlayerTeach(me, a[1], arg(2)));
			if (r.Ok) for (int id : m_Clans.Requests().Taught(me, a[1], arg(2))) RequestDone(id);   // V3
			return r;
		}
		if (sub == "learn" && a.size() > 1) return act(PlayTribe().PlayerLearn(me, a[1], arg(2)));
		if (sub == "marry" && a.size() > 1) return act(PlayTribe().PlayerMarry(me, a[1]));
		if (sub == "craft" && a.size() > 1) return act(PlayTribe().PlayerCraft(me, a[1]));
		if (sub == "work" && a.size() > 2) return act(PlayTribe().PlayerWorked(me, a[1], a[2], static_cast<float>(count(3, 1))));
		if (sub == "propose" && a.size() > 2)
		{
			glm::vec2 at(0.0f);
			if (a[1] == "build")
				if (const Actor* actor = m_Actors.Find(me)) { const glm::vec3 p = actor->GetPosition(); at = { p.x, p.z }; }   // where the player stands
			const bool force = a.back() == "force";   // (`game propose build <building> force`: the site opens even if the clan says no)
			const CommandResult r = act(PlayTribe().Propose(me, a[1], a[2], a.size() > 3 && a[3] != "force" ? count(3, 1) : 0, at, force));
			return { r.Ok || r.Message.find(": rejected") != std::string::npos, r.Message };   // (the clan saying no is an answer, not a failed command)
		}
		if (sub == "attack")
		{
			const Tribe::PlayerAct r = PlayTribe().PlayerAttack(me, arg(1));
			return act(r);   // (the player closes on its foe by itself, running: Tribe::StepPlayer; a key or `game stop` breaks it off)
		}
		// `game cancel`: the action under way (a gather, a build, the fire, the well) stops where it is; what it got so far counts
		if (sub == "cancel" || (sub == "stop" && !PlayTribe().PlayerFighting(me)))
		{
			m_Game.Approach.clear();
			if (!m_Player.TasksRunning()) return { false, "you are not doing anything" };
			m_Player.StopTasks();
			if (Actor* actor = m_Actors.Find(me)) actor->ReleaseOrders();
			return { true, "you stop " + (m_Game.PendingKind.empty() ? std::string("what you were doing") : m_Game.PendingKind) };
		}
		if (sub == "stop")
		{
			const Tribe::PlayerAct r = PlayTribe().PlayerStop(me);
			if (Actor* actor = m_Actors.Find(me); actor && r.Ok) actor->ReleaseOrders();
			return act(r);
		}
		if (sub == "block") return act(PlayTribe().PlayerBlock(me));
		if (sub == "flee") { const CommandResult r = act(PlayTribe().PlayerFlee(me)); if (r.Ok) m_Player.MoveTo(PlayTribe().Config.Economy.Camp, true); return r; }
		if ((sub == "trade" || sub == "visit") && a.size() > 1)
		{
			const Clan* c = m_Clans.Find(a[1]);
			if (!c) return { false, "no clan '" + a[1] + "' (clan list)" };
			std::string outcome;
			const std::string why = m_Clans.PlayerVisit(me, c->Id, sub, &outcome);
			GameNote(why.empty() ? outcome : why);
			return { why.empty(), why.empty() ? outcome : why };
		}
		return { false, "game status|help|village|homes|clans|tech|map [on|off|territory|home|zones|productivity <item>]|story|interact|use <thing>|eat|drink|deposit [item n]|take <item> [n]|sleep|wake|claim [job]|release|cancel|give <member> <item> [n]|talk [member]|"
			"teach|learn <member> [tech]|marry <member>|propose gather|make <item> [n]|propose build <building>|craft <recipe>|attack [foe]|block|flee|trade|visit <clan>|"
			"clan|jobs|tech|research <tech>|select <name>|book me|clan|world|console|pause|resume|faster|slower|speed <n>|save|load [slot]" };
	}

	// ------------------------------------------------------------------ the interface over the viewport
	// Where each part goes (docs/JUGAR.md, "La interfaz"): who you are and your needs top left, the clock in the middle, the news on the right, the
	// prompt of the interact key just above the action bar at the bottom, the selection under the needs. Every part sits on a panel of hide
	// (GameTheme): bare text over a sunny meadow or snow cannot be read. What the windows measured last frame (HudBottom, BarTop) keeps them apart.
	void EditorApp::DrawGameHud(float ox, float oy, float w, float h)
	{
		GameKeys();
		const TribeMember* me = PlayTribe().FindMember(m_Game.Player);
		if (!me) return;
		namespace T = GameTheme;
		const T::Scope theme;   // the game's look for everything below: the panels (DrawGamePanels), the selection and the console too
		const Lang& L = m_Lang;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const Tribe& t = PlayTribe();
		auto keyOf = [&](const char* action) { const auto it = m_Controls.Keys.find(action); return it == m_Controls.Keys.end() ? std::string() : L.Key(it->second); };
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
		// a meter: an icon, a bar with its marks, the number and the name; the tooltip says what moves it
		auto meter = [&](const std::string& id, const std::string& name, const std::string& icon, float v01, ImU32 color, const std::string& value, const std::string& tip, std::initializer_list<float> marks)
		{
			ImGui::PushID(id.c_str());
			ImGui::AlignTextToFramePadding();
			if (!Icon(icon, 18.0f)) ImGui::Dummy(ImVec2(18.0f, 18.0f));
			ImGui::SameLine();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float bw = 150.0f, row = 18.0f, y0 = at.y + (row - T::BarH) * 0.5f;
			ImGui::InvisibleButton("bar", ImVec2(bw, row));
			const bool hovered = ImGui::IsItemHovered();
			ImDrawList* wl = ImGui::GetWindowDrawList();
			wl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideRaised, 3.0f);
			wl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw * std::clamp(v01, 0.0f, 1.0f), y0 + T::BarH), color, 3.0f);
			for (const float m : marks) wl->AddLine(ImVec2(at.x + bw * m, y0 - 2.0f), ImVec2(at.x + bw * m, y0 + T::BarH + 2.0f), T::Bone, 1.0f);
			wl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideLine, 3.0f);
			ImGui::SameLine();
			ImGui::Text("%4s", value.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", name.c_str());
			if (hovered || ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
			ImGui::PopID();
		};
		// an item of the bag: its icon (or its first letters) and the count in the corner; the name on hover
		auto item = [&](const ItemSlot& s)
		{
			const ItemDef* d = m_Items.Find(s.Id);
			const std::string name = Lang::Cap(L.Name("item", s.Id, d && !d->Name.empty() ? d->Name : std::string()));
			ImGui::PushID(s.Id.c_str());
			if (!Icon(s.Id, 26.0f)) ImGui::Button(name.substr(0, 3).c_str(), ImVec2(26.0f, 26.0f));
			const ImVec2 max = ImGui::GetItemRectMax();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s x%d", name.c_str(), s.Count);
			if (s.Count > 1)
			{
				const std::string n = std::to_string(s.Count);
				const ImVec2 size = ImGui::CalcTextSize(n.c_str());
				ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - size.x, max.y - size.y + 1.0f), T::Shade, n.c_str());
				ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - size.x - 1.0f, max.y - size.y), T::Bone, n.c_str());
			}
			ImGui::PopID();
			ImGui::SameLine();
		};

		// ---- top left: who you are, your needs, what you carry and your job
		ImGui::SetNextWindowPos(ImVec2(ox + T::Margin, oy + T::Margin));
		if (ImGui::Begin("Vitals##game", nullptr, fixed))
		{
			ImGui::TextUnformatted(me->Name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s%s", L.Name("role", me->Role).c_str(), t.Leads(me->Name) ? (" - " + L.Text("leads the clan")).c_str() : "");
			const PlayerRules& rules = t.Config.Player;
			const float most = std::max(rules.StandingMax, 1.0f);
			meter("standing", L.Text("Standing"), "rite", t.Standing(me->Name) / most, T::Ochre, L.Num(t.Standing(me->Name), 0),
				L.Text("Your standing in the clan (of {0}). Bringing things to the store, jobs of the board, gifts and lessons raise it; it fades a little every day. At {1} (the mark) you lead: the clan follows your proposals.",
					L.Num(most, 0), L.Num(rules.Leader, 0)), { rules.Leader / most });
			ImGui::Separator();
			static const std::map<std::string, std::pair<const char*, const char*>> kNeeds = {
				{ "Hunger", { "eat", "Food. Eat [{0}]: what you carry or, at the camp, from the store." } },
				{ "Thirst", { "drink", "Water. Drink [{0}] the water you carry, or draw it at a well [{1}]." } },
				{ "Rest", { "sleep", "Rest. Sleep [{0}]: in your bed if the clan has a shelter, else by the fire." } },
				{ "Warmth", { "warm", "Warmth. Stay by the campfire or in a shelter; keep the fire fed [{1}]." } },
				{ "Health", { "care", "Health. Hunger, thirst, cold and wounds take it; rest and food bring it back." } },
			};
			const std::map<std::string, float> levels = t.NeedLevels(*me);
			for (const std::string& n : m_Controls.Hud.Needs)
			{
				const auto it = levels.find(n);
				if (it == levels.end()) continue;
				const auto known = kNeeds.find(n);
				const float v = std::clamp(it->second / 100.0f, 0.0f, 1.0f);
				const char* action = n == "Hunger" ? "eat" : n == "Thirst" ? "drink" : n == "Rest" ? "sleep" : "interact";
				const std::string tip = known == kNeeds.end() ? n : L.Text(known->second.second, keyOf(action), keyOf("interact"));
				meter(n, Lang::Cap(L.Name("need", n, n)), known == kNeeds.end() ? std::string() : known->second.first, v, T::Level(v), L.Num(it->second, 0),
					tip + "\n" + L.Text("Below the mark (25) it is a danger."), { 0.25f });
			}
			if (const std::string c = t.PlayerCondition(me->Name); !c.empty() && c != "health 100, stamina 100")
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c.find("wound") != std::string::npos || c.find("fight") != std::string::npos ? T::Warn : T::Bone), "%s", L.Line(c).c_str());
			ImGui::Separator();
			std::vector<const ItemSlot*> carried, tools;
			if (me->Body)
				for (const ItemSlot& s : me->Body->Bag().Stacks())
					if (s.Count > 0) { const ItemDef* d = m_Items.Find(s.Id); (d && d->Category == "tool" ? tools : carried).push_back(&s); }
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", L.Text("Carries").c_str());
			ImGui::SameLine();
			if (carried.empty()) { ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("%s", L.Text("nothing").c_str()); }
			else { for (const ItemSlot* s : carried) item(*s); ImGui::NewLine(); }
			if (!tools.empty())
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%s", L.Text("Tools").c_str());
				ImGui::SameLine();
				for (const ItemSlot* s : tools) item(*s);
				ImGui::NewLine();
			}
			// the job of the board you hold: what it is, what you have done of it, and a button to leave it
			if (const Job* job = t.Board().ClaimOf(me->Name))
			{
				const std::string kind = job->Id.substr(0, job->Id.find(' '));
				ImGui::AlignTextToFramePadding();
				if (Icon(kind == "deliver" ? "build" : kind, 18.0f)) ImGui::SameLine();
				float done = 0.0f;
				for (const JobClaim& c : job->Claims) if (c.Member == me->Name) done = c.Done;
				ImGui::TextUnformatted(L.Text("Job: {0}", JobName(*job)).c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::Cap(L.Line(job->Why)).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("({0} done)", L.Num(done, done == std::floor(done) ? 0 : 1)).c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton((L.Text("Leave it") + "###release").c_str())) run("game release");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Give the job back to the board").c_str());
			}
			else ImGui::TextDisabled("%s", L.Text("No job: [{0}] takes the most urgent one", keyOf("claim")).c_str());
			if (me->Job == "sleep") ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Asleep (walk to wake up)").c_str());
			m_Game.HudBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			m_Game.HudRight = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x;
		}
		ImGui::End();

		// ---- top middle: the clock and the speed, which the buttons change too
		if (m_Controls.Hud.Clock)
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, oy + T::Margin), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
			if (ImGui::Begin("Clock##game", nullptr, fixed))
			{
				const float hour = t.Hour();
				char time[16];
				std::snprintf(time, sizeof time, "%02d:%02d", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f));
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted((L.Text("Day {0}", DayOf(t)) + "   " + time + "   " + Lang::Cap(L.Name("season", SeasonText(t.CurrentSeason())))).c_str());
				ImGui::SameLine(0.0f, 14.0f);
				if (ImGui::Button(m_Game.Paused ? " > ###pause" : " || ###pause")) run("game pause");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text(m_Game.Paused ? "Resume" : "Pause").c_str(), keyOf("pause").c_str());
				ImGui::SameLine();
				if (ImGui::Button(" - ###slower")) run("game slower");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text("Slower").c_str(), keyOf("slower").c_str());
				ImGui::SameLine();
				ImGui::Text("x%d", m_TimeScale);
				ImGui::SameLine();
				if (ImGui::Button(" + ###faster")) run("game faster");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s [%s]", L.Text("Faster").c_str(), keyOf("faster").c_str());
				m_Game.ClockBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
			}
			ImGui::End();
		}

		// ---- right: the news, newest on top, each on its card; what can kill or hurt (a death, a raid, an attack) has a red edge, what is about you
		// an ochre one; a line fades out in its last moments. With the clan book open they go left of it, under the clock. What the player has just
		// done itself (taking from the store, a job, leaving things) only flashes for a moment, a line said twice is shown once with its count, and
		// the board's echo of a proposal ("proposal #n ...") is left to the proposal's own line. (Drawn only: the notes and `game status` keep them all.)
		{
			static const char* kDanger[] = { "dies", "died", "killed", "raid", "attack", "GAME OVER" };
			const float wrap = std::min(380.0f, w * 0.3f), right = ox + w - T::Margin - m_Game.BookWidth;
			float ny = m_Game.BookWidth > 0.0f ? std::max(oy + T::Margin, m_Game.ClockBottom + T::Gap) : oy + T::Margin;
			const std::string mine = m_Game.Player + " (";
			auto routine = [&](const std::string& s)
			{
				if (s.rfind("took ", 0) == 0 || s.rfind("you take the job", 0) == 0) return true;
				if (s.rfind(mine, 0) != 0) return false;
				for (const char* k : { " takes ", " leaves ", " carries ", " goes to ", " heads for " }) if (s.find(k) != std::string::npos) return true;
				return false;
			};
			struct Shown { std::string Text, English; float Alpha; int Count; };
			std::vector<Shown> shown;
			for (auto it = m_Game.Notes.rbegin(); it != m_Game.Notes.rend() && static_cast<int>(shown.size()) < m_Controls.Hud.Notifications; ++it)
			{
				const double age = m_Game.Clock - it->At;
				const double life = routine(it->Text) ? 2.5 : static_cast<double>(m_Controls.Hud.NotifySeconds);
				if (age > static_cast<double>(m_Controls.Hud.NotifySeconds)) break;
				if (age > life || it->Text.rfind("proposal #", 0) == 0) continue;
				const std::string text = Lang::Cap(L.Line(it->Text));
				bool same = false;
				for (Shown& s : shown) if (s.Text == text) { ++s.Count; same = true; }
				if (same) continue;
				shown.push_back({ text, it->Text, std::clamp(static_cast<float>(life - age) / 1.5f, 0.0f, 1.0f), 1 });
			}
			for (const Shown& s : shown)
			{
				ImU32 accent = 0;
				for (const char* k : kDanger) if (s.English.find(k) != std::string::npos) accent = T::Bad;
				if (!accent && s.English.find(m_Game.Player) != std::string::npos) accent = T::Ochre;
				const std::string text = s.Count > 1 ? s.Text + "  x" + std::to_string(s.Count) : s.Text;
				const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
				const ImVec2 min(right - size.x - 2.0f * T::Pad - 4.0f, ny), max(right, ny + size.y + 2.0f * 6.0f);
				T::Card(dl, min, max, accent, s.Alpha);
				dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + T::Pad + 4.0f, min.y + 6.0f), T::Faded(T::News, s.Alpha), text.c_str(), nullptr, wrap);
				ny = max.y + 4.0f;
			}
		}

		// ---- the action under way: what, how far along (the steps of its task list) and a button to stop it (Esc does the same); above the prompt
		if (!m_Game.PendingKind.empty() && m_Player.TasksRunning())
		{
			int step = 0, steps = 0;
			const std::string status = m_Player.TaskStatus();   // "2/6 ability Gather ..."
			if (const size_t slash = status.find('/'); slash != std::string::npos && slash > 0)
			{
				step = std::atoi(status.c_str());
				steps = std::atoi(status.c_str() + slash + 1);
			}
			if (steps <= 0) { step = 0; steps = 1; }
			const float v = std::clamp(static_cast<float>(step - 1) / static_cast<float>(steps), 0.0f, 1.0f);
			const std::string& k = m_Game.PendingKind;
			const std::string what = k == "gather" ? L.Text("Gathering {0}", L.Name("item", m_Game.PendingItem)) : k == "build" ? L.Text("Building {0}", L.Entity(m_Game.PendingKey))
				: k == "stoke" ? L.Text("Feeding the campfire") : k == "water" ? L.Text("Drawing water") : L.Name("doing", k);
			const float barTop = m_Game.BarTop > 0.0f ? m_Game.BarTop : oy + h - 70.0f;
			const float above = barTop - 10.0f - (m_Controls.Hud.Prompt && !m_Game.Prompt.empty() ? 32.0f + 8.0f : 0.0f);
			ImGui::SetNextWindowPos(ImVec2(ox + w * 0.5f, above), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
			if (ImGui::Begin("###action", nullptr, fixed))
			{
				ImGui::AlignTextToFramePadding();
				if (Icon(k == "water" ? "drink" : k == "stoke" ? "stoke" : k == "build" ? "build" : m_Game.PendingItem, 20.0f)) ImGui::SameLine();
				ImGui::TextUnformatted(what.c_str());
				ImGui::SameLine();
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const float bw = 140.0f, row = ImGui::GetFrameHeight(), y0 = at.y + (row - T::BarH) * 0.5f;
				ImGui::Dummy(ImVec2(bw, row));
				ImDrawList* wl = ImGui::GetWindowDrawList();
				wl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideRaised, 3.0f);
				wl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + bw * v, y0 + T::BarH), T::Ochre, 3.0f);
				wl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + bw, y0 + T::BarH), T::HideLine, 3.0f);
				ImGui::SameLine();
				ImGui::Text("%d/%d", std::max(step, 1), steps);
				ImGui::SameLine();
				if (ImGui::Button((L.Text("Cancel") + " [" + L.Key("Escape") + "]###cancel").c_str())) run("game cancel");
			}
			ImGui::End();
		}

		// ---- the prompt of the interact key: just above the action bar, and its key over the thing in the world
		if (m_Controls.Hud.Prompt && !m_Game.Prompt.empty())
		{
			const std::string key = keyOf("interact").empty() ? std::string("?") : keyOf("interact");
			const std::string prompt = L.Line(m_Game.Prompt);
			const ImVec2 size = ImGui::CalcTextSize(prompt.c_str()), keySize = ImGui::CalcTextSize(key.c_str());
			const float cardW = keySize.x + 10.0f + 8.0f + size.x + 2.0f * 10.0f, cardH = 32.0f;
			const float barTop = m_Game.BarTop > 0.0f ? m_Game.BarTop : oy + h - 70.0f;
			const ImVec2 min(ox + (w - cardW) * 0.5f, barTop - 10.0f - cardH), max(min.x + cardW, min.y + cardH);
			T::Card(dl, min, max);
			const float kw = T::KeyCap(dl, ImVec2(min.x + 10.0f, min.y + 6.0f), key.c_str(), 20.0f);
			dl->AddText(ImVec2(min.x + 10.0f + kw + 8.0f, min.y + (cardH - size.y) * 0.5f), T::Bone, prompt.c_str());
			glm::vec2 at;
			const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
			if (m_Game.HasPromptPos && ProjectToScreen(viewProj, m_Game.PromptPos + glm::vec3(0.0f, 1.4f, 0.0f), glm::vec2(w, h), at))
				T::KeyCap(dl, ImVec2(ox + at.x - (keySize.x + 10.0f) * 0.5f, oy + at.y - 10.0f), key.c_str(), 20.0f);
		}

		// ---- from above: where you are (a ring and "YOU" over your character), so the overview never loses you among the trees
		if (m_Game.View == "top" && !m_ClanOverlay)   // (the clan overlay labels you itself: "YOU - <name>")
			if (const Actor* actor = m_Actors.Find(m_Game.Player))
			{
				glm::vec2 at;
				const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
				if (ProjectToScreen(viewProj, actor->GetPosition() + glm::vec3(0.0f, 2.0f, 0.0f), glm::vec2(w, h), at))
				{
					const ImVec2 c(ox + at.x, oy + at.y);
					const std::string you = L.Text("YOU");
					dl->AddCircle(c, 16.0f, T::Shade, 32, 6.0f);
					dl->AddCircle(c, 16.0f, T::Ochre, 32, 3.0f);
					const ImVec2 size = ImGui::CalcTextSize(you.c_str());
					T::Card(dl, ImVec2(c.x - size.x * 0.5f - 6.0f, c.y - 40.0f), ImVec2(c.x + size.x * 0.5f + 6.0f, c.y - 40.0f + size.y + 6.0f), 0);
					dl->AddText(ImVec2(c.x - size.x * 0.5f, c.y - 37.0f), T::Ochre, you.c_str());
				}
			}

		// ---- paused or over: the world dims and a card says so (the pause menu has its own card)
		if ((m_Game.Paused && !m_Game.Menu) || m_Game.Over)
		{
			dl->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 80));
			const std::string title = L.Text(m_Game.Over ? "GAME OVER" : "Paused");
			const std::string sub = m_Game.Over ? L.Text("game load brings back the last save; play starts again") : L.Text("[{0}] or the clock's button resumes", keyOf("pause"));
			const ImVec2 ts = ImGui::CalcTextSize(title.c_str()), ss = ImGui::CalcTextSize(sub.c_str());
			const float cw = std::max(ts.x * 2.0f, ss.x) + 40.0f, ch = ts.y * 2.0f + ss.y + 36.0f;
			const ImVec2 min(ox + (w - cw) * 0.5f, oy + h * 0.32f), max(min.x + cw, min.y + ch);
			T::Card(dl, min, max, m_Game.Over ? T::Bad : T::Ochre);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 2.0f, ImVec2(min.x + (cw - ts.x * 2.0f) * 0.5f, min.y + 14.0f), m_Game.Over ? T::Bad : T::Bone, title.c_str());
			dl->AddText(ImVec2(min.x + (cw - ss.x) * 0.5f, max.y - ss.y - 14.0f), T::BoneDim, sub.c_str());
		}

		DrawGamePanels(ox, oy, w, h);
		DrawTechTree(PlayTribe(), true, ox, oy, w, h, m_Game.TechTree, m_Game.TreePick);
		DrawGameConsole(ox, oy, w, h);
	}

	// The keys' sheet of the clan book (H, `game help`): every key with what it does, in the player's words.
	void EditorApp::DrawKeysSheet()
	{
		const Lang& L = m_Lang;
		ImGui::TextUnformatted(L.Text("WASD to walk, Shift to run, drag the mouse to look around").c_str());
		ImGui::Spacing();
		if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			for (const auto& [action, key] : m_Controls.Keys)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Ochre), "%s", L.Key(key).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Lang::Cap(L.Name("action", action)).c_str());
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", L.Text("The buttons at the bottom do the same. The console ({0}) takes the rest: game talk, give, teach, learn, marry, propose, craft, trade...",
			m_Controls.Keys.count("console") ? L.Key(m_Controls.Keys.at("console")) : std::string("game console")).c_str());
		ImGui::PopStyleColor();
	}

	// The console inside the game: the editor's panels are not drawn while playing, so the log, the command line and the frame's numbers come here.
	void EditorApp::DrawGameConsole(float ox, float oy, float w, float h)
	{
		if (!m_Game.Console) return;
		// under the clock, between the needs and the clan book (the left is the needs', the right the book's)
		const float left = std::max(ox + 12.0f, m_Game.HudRight + 12.0f), top = std::max(oy + 12.0f, m_Game.ClockBottom + 6.0f);
		ImGui::SetNextWindowPos(ImVec2(left, top), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(std::clamp(ox + w - 12.0f - m_Game.BookWidth - left, 240.0f, 760.0f), h * 0.42f), ImGuiCond_Always);
		if (ImGui::Begin((m_Lang.Text("Console") + "###console_game").c_str(), &m_Game.Console, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings))
		{
			const bool appearing = ImGui::IsWindowAppearing();
			const std::string key = m_Controls.Keys.count("console") ? m_Controls.Keys.at("console") : std::string();
			char fps[48];
			std::snprintf(fps, sizeof fps, "%.0f fps   %ux%u", 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)), m_ViewportW, m_ViewportH);
			ImGui::TextDisabled("%s   %s", fps, m_Lang.Text("({0} or Esc closes it; the commands are in English)", key.empty() ? std::string("game console") : m_Lang.Key(key)).c_str());
			if (m_ShowRenderStats) ImGui::TextDisabled("%s", RenderStatsLine().c_str());
			DrawConsoleBody();
			if (appearing) ImGui::SetKeyboardFocusHere(-1);   // (the command line, the body's last item)
			// while the command line has the keys the game's are off (WantTextInput): its own key and Esc still close it, without leaving the key's character typed
			const ImGuiKey toggle = KeyNamed(key);
			if (!appearing && ImGui::GetIO().WantTextInput && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (toggle != ImGuiKey_None && ImGui::IsKeyPressed(toggle, false))))
			{
				m_Game.Console = false;
				m_ConsoleInput[0] = '\0';
			}
		}
		ImGui::End();
	}

	void EditorApp::RegisterGameCommands()
	{
		CommandRunner& c = m_Commands;
		// T38: the bindings of the named actions the player's character and the cameras read (assets/data/input.json)
		if (const std::string error = m_Player.LoadActions(m_AssetRoot); !error.empty()) GB_CLIENT_WARN("input not loaded: {}", error);
		c.Register("play", "play [temperate|alpine|desert] [seed] [members] [clans] | here | off | status: (here: into the world as it is, in its first clan) H15's game: a world, clans placed by the site chooser, and you as a member of clan 0 (the X Bot of packs/full, or the fox); the keys are in the \"controls\" section (player.json); see docs/JUGAR.md",
			[this](const std::vector<std::string>& a) { return PlayCommand(a); });
		c.Register("lang", "lang [es|en] | check: the language of the game's interface (assets/ui/lang/<code>.json; the commands, logs and `game status` stay English); check: how much of the clan's log its phrases cover",
			[this](const std::vector<std::string>& a) -> CommandResult
			{
				if (!a.empty() && a[0] == "missing")
				{
					std::string s;
					for (const std::string& m : m_Lang.Missing()) s += "\n  " + m;
					return { true, std::to_string(m_Lang.Missing().size()) + " texts of the interface without translation" + s };
				}
				if (!a.empty() && a[0] == "check")
				{
					int lines = 0, covered = 0;
					std::string missing;
					for (const std::string& raw : PlayTribe().Log())
					{
						const size_t cut = raw.find_first_not_of(" 0123456789.");
						const std::string line = cut == std::string::npos ? raw : raw.substr(cut);
						++lines;
						if (m_Lang.Covers(line)) ++covered;
						else if (missing.size() < 1500) missing += "\n  " + line;
					}
					return { true, "lang " + m_Lang.Code() + ": " + std::to_string(covered) + " of " + std::to_string(lines) + " lines of the clan's log covered" + (missing.empty() ? std::string() : "; not covered:" + missing) };
				}
				if (!a.empty()) m_Lang.Load(m_AssetRoot + "/ui/lang", a[0]);
				return { true, "lang " + m_Lang.Code() + (m_Lang.On() || m_Lang.Code() == "en" ? std::string() : " (not found: English)") };
			});
		c.Register("observer", "observer [on|off|toggle|status|next|prev|<clan id or name>|sheet <members|board|store|buildings|knowledge|relations|log>]: the free mode's observer: the clans one by one with the game's look over the viewport (a card to pick the clan, its book on the right); the clan is the one the Tribe panel shows",
			[this](const std::vector<std::string>& a) { return ObserverCommand(a); });
		c.Register("game", "game <action>: what the keys of the game do (status, village, homes, clans, tech, map, story, interact, eat, drink, deposit, take, sleep, claim, give, talk, teach, learn, marry, propose, craft, attack, block, flee, trade, clan, jobs, pause, faster, slower, save, load, help)",
			[this](const std::vector<std::string>& a) { return GameCommand(a); });
	}
}
