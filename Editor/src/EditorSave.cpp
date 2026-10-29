// The editor's side of saved games (H4): the host that maps the scene to a save, the commands save / load / saves / quicksave / quickload /
// autosave / replay, the fixed simulation tick they rely on and the File > Save game / Load game menus. The format lives in GB/Save.
#include "EditorApp.h"
#include "GB/Game/SceneIO.h"
#include "GB/Gameplay/WorldResources.h"
#include "GB/Log.h"
#include "GB/World/WorldGen.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <imgui.h>
#include <set>
#include <sstream>

namespace GB::Editor
{
	namespace fs = std::filesystem;
	using namespace GB::Save;

	namespace
	{
		constexpr double kSimDt = 1.0 / 60.0;
		constexpr const char* kUserSection = "user-entities";
		constexpr const char* kTribeSection = "ai";
		constexpr const char* kEcologySection = "ecology";          // where each rabbit was heading and the counters
		constexpr const char* kCombatSection = "combat";            // H14: the fights, the wounds and morale, the hostility table and the grievances
		constexpr const char* kInteractivesSection = "interactives";   // the timers of the fires and the wells
		constexpr const char* kSeasonSection = "season-look";          // the weather, snow and leaf colour the season wrote
		constexpr const char* kClansSection = "clans";                 // H13: the clans besides the first (their tribes), relations, territory, meetings
		constexpr const char* kKnowledgeSection = "knowledge-clan";    // H11: what the clan knows, what is half learnt, the tries (each member's own is in its actor)

		// `wild`: the density of the resource nodes spread over the world (`resources world`), 0 when there are none; they are part of the baseline.
		WorldParams ParamsOf(const WorldDesc& d, bool generated, float wild)
		{
			WorldParams p;
			p.Generated = generated;
			p.GeneratorVersion = d.GeneratorVersion;
			p.WildResources = generated ? wild : 0.0f;
			p.Type = d.Type == WorldType::Alpine ? "alpine" : d.Type == WorldType::Desert ? "desert" : "temperate";
			p.Seed = d.Seed;
			p.Size = d.Size;
			p.Resolution = d.Resolution;
			p.River = d.River;
			p.Lake = d.Lake;
			p.ForestAmount = d.ForestAmount;
			p.TreeBudget = d.TreeBudget;
			p.RockBudget = d.RockBudget;
			p.Tiles = d.Tiles;
			return p;
		}

		WorldDesc DescOf(const WorldParams& p)
		{
			WorldDesc d;
			d.Type = p.Type == "alpine" ? WorldType::Alpine : p.Type == "desert" ? WorldType::Desert : WorldType::Temperate;
			d.Seed = p.Seed;
			d.GeneratorVersion = p.GeneratorVersion;
			d.Size = p.Size;
			d.Resolution = p.Resolution;
			d.River = p.River;
			d.Lake = p.Lake;
			d.ForestAmount = p.ForestAmount;
			d.TreeBudget = p.TreeBudget;
			d.Tiles = p.Tiles;
			d.RockBudget = p.RockBudget;
			return d;
		}

		// Everything of `src` except where it sits in the hierarchy.
		void CopyData(Entity& dst, const Entity& src)
		{
			const Entity keep = dst;
			dst = src;
			dst.Id = keep.Id;
			dst.Parent = keep.Parent;
			dst.Children = keep.Children;
		}

		// Orders that change the simulation: the ones a replay records.
		bool IsSimCommand(const std::string& word)
		{
			static const std::set<std::string> names = { "garden", "resources", "ability", "add", "delete", "duplicate", "move", "rotate", "scale", "color", "emissive",
				"light", "drop", "set", "time", "weather", "entity", "select", "generate", "actor", "ecology", "interactives", "tribe", "content", "knowledge" };
			return names.count(word) != 0;
		}

		std::string ReplayPath(const std::string& arg)
		{
			return arg.find('.') != std::string::npos || arg.find('/') != std::string::npos ? arg : "replays/" + arg + ".gbreplay";
		}

		std::string Kilobytes(uintmax_t bytes)
		{
			std::ostringstream os;   // (not snprintf: gcc rejects a fixed buffer for a double as a possible truncation)
			os << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0;
			return os.str();
		}
	}

	// ------------------------------------------------------------------ the host
	WorldParams EditorSaveHost::World() const { return ParamsOf(m_App.m_WorldDesc, m_App.m_World.IsBuilt(), m_App.m_WildResources); }

	ClockState EditorSaveHost::Clock() const
	{
		ClockState c;
		c.Tick = m_App.m_SimTick;
		c.Dt = kSimDt;
		c.Hours = m_App.m_Tod.Hours;
		c.DayLengthSeconds = 0.0f;
		c.TimeOfDay = m_App.m_Tod.Enabled;
		return c;
	}

	WeatherState EditorSaveHost::Weather() const
	{
		WeatherState w;
		w.Preset = m_App.m_Props.Get("weather.preset");
		if (w.Preset.empty()) w.Preset = "clear";
		return w;
	}

	const GameplayWorld* EditorSaveHost::Gameplay() const { return &m_App.m_Gameplay; }

	std::vector<ActorState> EditorSaveHost::Actors() const
	{
		std::vector<ActorState> out = m_Actors ? m_Actors->CaptureActors() : std::vector<ActorState>();
		for (ActorState& a : out)   // H11: what a tribe member knows ("tech.<id>"), from its own clan (H13); empty for the rest
		{
			const Clan* c = m_App.m_Clans.ClanOf(a.Name);
			if (!c) continue;
			a.Knowledge = c->Folk->Knowledge().Names(a.Name);
			// an abstract member (far from the camera, H6) walks as its agent while its actor waits where it was left: the save keeps where the member is
			for (const TribeMember& m : c->Folk->Members())
				if (m.Name == a.Name && m.Abstract && !m.Dead)
				{
					const glm::vec2 p = m.Agent.Position();
					a.Position = { p.x, m_App.m_World.IsBuilt() ? m_App.m_World.GetTerrain().GetHeight(p.x, p.y) : a.Position.y, p.y };
				}
		}
		return out;
	}

	bool EditorSaveHost::BuildBaseline(const WorldParams& world, EntityWorld& out, std::string& error) const
	{
		out = EntityWorld();
		if (!world.Generated) return true;
		if (m_HaveBaseline && m_BaselineKey == world)
		{
			out = m_Baseline;
			return true;
		}
		SceneEditor scratch;   // the same code that fills the live scene numbers the generated objects
		// The wild resource nodes (`resources world <density>`) come from the same seed, so they belong to the baseline too: the save then keeps only the
		// nodes that changed (gathered, depleted, moved), not the few hundred of them. They are planned with the node data loaded now (ForgetBaseline
		// when it changes); data that plans other spots is a drift of the baseline, reported like any other.
		auto populate = [&](const GeneratedWorld& generated)
		{
			scratch.PopulateEnvironment(generated.Trees, generated.Rocks);
			if (world.WildResources > 0.0f)
			{
				const std::vector<ResourceSpot> spots = PlanWorldResources(ResourceGroundOf(generated), m_App.m_NodeDefs, generated.Desc.Seed, world.WildResources);
				BuildWorldResources(scratch.World(), spots, m_App.m_NodeDefs);   // its root is tagged "world", like the groups of trees and rocks
			}
		};
		if (m_App.m_World.IsBuilt() && ParamsOf(m_App.m_WorldDesc, true, world.WildResources) == world)
			populate(m_App.m_World.World());
		else
		{
			if (world.Type != "temperate" && world.Type != "alpine" && world.Type != "desert") { error = "unknown world type '" + world.Type + "'"; return false; }
			if (world.GeneratorVersion < kWorldGeneratorV1 || world.GeneratorVersion > kWorldGeneratorLatest)
			{
				error = "the world was made by generator version " + std::to_string(world.GeneratorVersion) + " (this one knows 1 to " + std::to_string(kWorldGeneratorLatest) + ")";
				return false;
			}
			// No GL here: only the heights, biomes and placements are needed, not the mesh (WorldGenerator::GenerateData).
			populate(WorldGenerator::GenerateData(DescOf(world)));
		}
		m_Baseline = scratch.World();
		m_BaselineKey = world;
		m_HaveBaseline = true;
		out = m_Baseline;
		return true;
	}

	void EditorSaveHost::Split(const EntityWorld& baseline, EntityWorld& env, EntityWorld& user) const
	{
		const EntityWorld& live = m_App.m_SceneEditor.World();
		env = baseline;
		std::map<std::string, EntityId> baseRoots;
		std::map<std::pair<EntityId, std::string>, EntityId> baseChildren;
		for (const auto& [id, e] : baseline.All())
		{
			if (e.Parent == NullEntity) baseRoots[e.Name] = id;
			else baseChildren[{ e.Parent, e.Name }] = id;
		}
		std::map<EntityId, EntityId> canonical;   // live id -> the baseline's id for it
		std::vector<EntityId> pending;
		for (EntityId root : live.Roots())
		{
			const Entity* e = live.Find(root);
			auto it = baseRoots.find(e->Name);
			if (!e->HasTag("world") || it == baseRoots.end()) continue;
			canonical[root] = it->second;
			pending.push_back(root);
		}
		while (!pending.empty())
		{
			const EntityId id = pending.back();
			pending.pop_back();
			for (EntityId child : live.Find(id)->Children)
			{
				auto it = baseChildren.find({ canonical[id], live.Find(child)->Name });
				if (it == baseChildren.end()) continue;
				canonical[child] = it->second;
				pending.push_back(child);
			}
		}
		std::set<EntityId> kept;
		for (const auto& [liveId, baseId] : canonical)
		{
			CopyData(*env.Find(baseId), *live.Find(liveId));
			kept.insert(baseId);
		}
		std::vector<EntityId> gone;   // generated objects that are not in the scene any more
		for (const auto& [id, e] : baseline.All())
			if (!kept.count(id) && (e.Parent == NullEntity || kept.count(e.Parent))) gone.push_back(id);
		for (EntityId id : gone) env.Destroy(id);

		user = EntityWorld();
		std::map<EntityId, EntityId> renumbered;
		EntityId next = 1;
		for (const auto& [id, e] : live.All())
			if (!canonical.count(id))
			{
				Entity* d = user.Insert(next, e.Name);
				CopyData(*d, e);
				renumbered[id] = next++;
			}
		for (const auto& [id, e] : live.All())
		{
			auto it = renumbered.find(id);
			if (it == renumbered.end() || e.Parent == NullEntity) continue;
			auto parent = renumbered.find(e.Parent);
			if (parent != renumbered.end()) user.Link(it->second, parent->second);
			else user.Find(it->second)->Transform.FromMatrix(live.WorldMatrix(id));   // its parent is a generated object: it stands on its own
		}
	}

	const EntityWorld& EditorSaveHost::Entities() const
	{
		const WorldParams p = World();
		EntityWorld baseline;
		std::string error;
		if (!p.Generated || !BuildBaseline(p, baseline, error))
		{
			m_View = m_App.m_SceneEditor.World();
			return m_View;
		}
		EntityWorld user;
		Split(baseline, m_View, user);
		return m_View;
	}

	std::vector<SectionBlob> EditorSaveHost::Sections() const
	{
		std::vector<SectionBlob> out;
		// H16: the turns of the plan budget are not saved: the run that goes on forgets them too, so it queues as the one loaded from this save will
		m_App.m_Actors.Navigation().ResetQueue();
		const WorldParams p = World();
		EntityWorld baseline;
		std::string error;
		if (p.Generated && BuildBaseline(p, baseline, error))
		{
			EntityWorld env, user;
			Split(baseline, env, user);
			m_App.m_Gameplay.Store(user);   // the characters and plots keep their live state in their components
			out.push_back({ kUserSection, 1, SaveSceneToString(user) });
		}
		if (m_App.m_Ecology.Count() > 0 || m_App.m_Ecology.Born() > 0 || m_App.m_Ecology.Starved() > 0) out.push_back({ kEcologySection, 1, m_App.m_Ecology.ToJson() });
		if (!m_App.m_Combat.All().empty() || !m_App.m_Hostility.Table().empty() || !m_App.m_Hostility.Grievances().empty())
			out.push_back({ kCombatSection, 1, "{\"fights\":" + m_App.m_Combat.ToJson() + ",\"hostility\":" + m_App.m_Hostility.ToJson() + "}" });
		if (const std::string timers = m_App.m_Interactives.ToJson(); timers != m_App.m_NoInteractiveTimers) out.push_back({ kInteractivesSection, 1, timers });
		if (m_App.m_SeasonVisuals)
		{
			SeasonLook look;
			look.Enabled = true;
			look.Weather = m_App.m_AutoWeather;
			look.Snow = m_App.m_AutoSnow;
			look.Leaf = m_App.m_AutoLeaf;
			out.push_back({ kSeasonSection, 1, look.ToJson() });
		}
		if (!m_App.m_Tribe.Members().empty()) out.push_back({ kTribeSection, 1, m_App.m_Tribe.ToJson() });   // store, fire, memory and decisions (the needs are attributes)
		if (!m_App.m_Tribe.Members().empty() && m_App.m_Tribe.Knowledge().Gated()) out.push_back({ kKnowledgeSection, 1, m_App.m_Tribe.Knowledge().ToJson() });
		if (m_App.m_Clans.Count() > 1) out.push_back({ kClansSection, 1, m_App.m_Clans.ToJson() });   // H13 (a game of one clan writes what it wrote before)
		return out;
	}

	std::string EditorSaveHost::Assemble(const LoadedGame& game, EntityWorld& out) const
	{
		out = game.Entities;
		if (!game.World.Generated) return {};
		for (const SectionBlob& s : game.Sections)
		{
			if (s.Name != kUserSection) continue;
			EntityWorld user;
			const SceneIOResult r = LoadSceneFromString(user, s.Json);
			if (!r.Ok) return "the objects of the save do not load: " + r.Error;
			std::map<EntityId, EntityId> map;
			for (const auto& [id, e] : user.All())
			{
				const EntityId fresh = out.NextId();
				Entity* d = out.Insert(fresh, e.Name);
				if (!d) return "the save cannot be placed in the scene (" + e.Name + ")";
				CopyData(*d, e);
				map[id] = fresh;
			}
			for (const auto& [id, e] : user.All())
				if (e.Parent != NullEntity && !out.Link(map[id], map[e.Parent])) return "bad parent in the objects of the save (" + e.Name + ")";
		}
		return {};
	}

	std::string EditorSaveHost::Validate(const LoadedGame& game)
	{
		LoadedGame check;
		const std::string why = Assemble(game, check.Entities);
		if (!why.empty()) return why;
		check.Gameplay = game.Gameplay;
		std::string error = ISaveHost::Validate(check);
		if (error.empty() && m_Actors) error = m_Actors->RestoreActors(game.Actors, true);
		return error;
	}

	void EditorSaveHost::Commit(LoadedGame&& game)
	{
		EditorApp& app = m_App;
		app.m_Actors.Navigation().ResetQueue();   // (H16: as at the capture, see Sections)
		// H14: the fighters of the run that ends go first. Left until after the ability systems are rebuilt, anything that arms them in between gave the
		// new systems the old run's weapons ("Attack.pickaxe" before "Attack.fists"), and a replay did not end identical
		app.m_Combat.Clear();
		if (game.World.Generated && (!app.m_World.IsBuilt() || ParamsOf(app.m_WorldDesc, true, game.World.WildResources) != game.World)) app.BuildWorld(DescOf(game.World));
		app.m_WildResources = game.World.Generated ? game.World.WildResources : 0.0f;   // the wild nodes themselves come in with the entities
		app.m_Player.Stop();
		EntityWorld scene;
		Assemble(game, scene);
		app.m_SceneEditor.ReplaceWorld(std::move(scene));
		RestoreGameplay(app.m_Gameplay, app.m_SceneEditor.World(), game.Gameplay);
		app.m_Garden.Bind(app.m_Gameplay, &app.m_SceneEditor.World());
		app.m_Garden.SyncVisuals();
		app.BindResources();
		app.m_Player.PrepareAbilities();
		app.m_SimTick = game.Clock.Tick;
		app.m_SimAccum = 0.0;
		app.m_Props.Set("tod.enabled", game.Clock.TimeOfDay ? "true" : "false");
		app.m_Props.Set("tod.hours", std::to_string(game.Clock.Hours));
		if (app.m_Props.Set("weather.preset", game.Weather.Preset).Ok) app.m_Weather->Snap();
		if (m_Actors)
		{
			// a game loaded into a world that does not have its characters (a new world, a replay after something else) makes them again from what they were
			for (const ActorState& a : game.Actors)
			{
				if (a.Kind.empty() || app.m_Actors.Find(a.Name)) continue;
				std::string error;
				if (!app.m_Player.Spawn(a.Name, a.Kind, app.m_World, app.m_SceneEditor, app.m_AssetRoot, a.Position, app.m_Steps.get(), error, false))
					GB_CLIENT_WARN("the actor {} of the save cannot be made again: {}", a.Name, error);
			}
			m_Actors->RestoreActors(game.Actors, false);
		}
		app.ClearKeptClans();   // H13: the clans of `clan new` go; a save with them makes them again below
		app.m_Tribe.Clear();
		app.m_TribeBodies.clear();
		for (const SectionBlob& s : game.Sections)
			if (s.Name == kTribeSection) app.RestoreTribe(s.Json);
		// H11: what each member knows travels with its actor, the rest in its section; a save from before knowledge leaves the technologies of the start
		for (const ActorState& a : game.Actors) app.m_Tribe.Knowledge().SetNames(a.Name, a.Knowledge);
		for (const SectionBlob& s : game.Sections)
			if (s.Name == kKnowledgeSection && !app.m_Tribe.Knowledge().FromJson(s.Json)) GB_CLIENT_WARN("the knowledge of the clan in the save could not be read: it starts again");
		for (const SectionBlob& s : game.Sections)
			if (s.Name == kClansSection) app.RestoreClans(s.Json);   // H13 (a save from before has none: its tribe is clan 0, alone)
		// what the systems keep apart from the components (a save from before these sections leaves them at their start)
		std::string ecology = "{\"born\":0,\"starved\":0,\"eaten\":0,\"walks\":[]}", interactives = app.m_NoInteractiveTimers;
		SeasonLook look;
		for (const SectionBlob& s : game.Sections)
		{
			if (s.Name == kEcologySection) ecology = s.Json;
			else if (s.Name == kInteractivesSection) interactives = s.Json;
			else if (s.Name == kSeasonSection && !look.FromJson(s.Json)) GB_CLIENT_WARN("the season look of the save could not be read: it starts again");
		}
		if (!app.m_Ecology.FromJson(ecology)) GB_CLIENT_WARN("the animals' state of the save could not be read: they choose again");
		// H14: the fights and the hostility (a save from before has neither: nobody fights, the table is the rules')
		app.m_Combat.Clear();
		app.m_Hostility.Clear();
		app.m_Hostility.FromRules(app.m_Combat.Rules);
		app.m_CombatSeq = 0;
		for (const SectionBlob& s : game.Sections)
			if (s.Name == kCombatSection && !app.RestoreCombat(s.Json)) GB_CLIENT_WARN("the fights of the save could not be read: nobody is fighting");
		if (!app.m_Interactives.FromJson(interactives)) GB_CLIENT_WARN("the timers of the interactives could not be read: they start again");
		app.m_SeasonVisuals = look.Enabled;
		app.m_AutoWeather = look.Weather;
		app.m_AutoSnow = look.Snow;
		app.m_AutoLeaf = look.Leaf;
		if (look.Enabled && look.Snow >= 0.0f) app.m_Props.Set("ground.snow", std::to_string(look.Snow));
		app.StepResources();
	}

	void EditorSaveHost::Step()
	{
		// The characters move in the fixed tick (not with the frame time) so a replay, a loaded game and a run at another frame rate go the same way. With a player
		// driving one they follow the frame, which reads the keyboard (EditorApp::OnUpdate).
		if (!m_App.m_Player.IsActive())
		{
			m_App.m_Actors.SetViewer(m_App.m_Camera.Position);   // H16: far from the camera the animation is cheaper (presentation only)
			m_App.m_Actors.PrepareAbilities();
			m_App.m_Actors.Update(static_cast<float>(kSimDt));
		}
		m_App.m_Gameplay.Update(static_cast<float>(kSimDt));
		m_App.m_Garden.Update(static_cast<float>(kSimDt));
		m_App.StepResources();
		m_App.StepWorld(static_cast<double>(m_App.m_SimTick) / 60.0);
		m_App.m_Interactives.Update(static_cast<float>(kSimDt), &m_App.m_SceneEditor.World());
		m_App.m_Buildings.Update(static_cast<float>(kSimDt), &m_App.m_SceneEditor.World());   // H8: the sites the tribe (or a load) made are picked up; their parts grow
		for (const auto& c : m_App.m_Clans.All()) c->Buildings = static_cast<float>(c->Folk->BuildingCount());   // H8: its finished buildings make its claims stronger (H13)
		m_App.StepCombat();   // H14: blows, wounds, the predators; after the animals moved, before the clans read what happened
		m_App.m_Clans.SetViewer({ m_App.m_Camera.Position.x, m_App.m_Camera.Position.z });   // far from the camera the members are abstract agents
		m_App.m_Clans.SetWorldClock(static_cast<double>(m_App.m_SimTick) / 60.0);
		m_App.SyncRegions();   // H12: the tribes know the world by regions (rebuilt only when the world or its wild nodes change)
		m_App.m_Clans.Update(static_cast<float>(kSimDt));   // H13: every clan (the first is m_Tribe), then what lies between them
		m_App.StepQa();   // the invariants of the simulation, every half second (QA panel)
		++m_App.m_SimTick;
	}

	bool EditorSaveHost::Command(const std::string& line, std::string& message)
	{
		const CommandResult r = m_App.Execute(line);
		message = r.Message;
		return r.Ok;
	}

	// ------------------------------------------------------------------ the editor
	void EditorApp::StepSimulation(float dt)
	{
		m_SimAccum += static_cast<double>(dt);
		int steps = 0;
		for (; m_SimAccum >= kSimDt && steps < 8; ++steps)
		{
			m_SaveHost->Step();
			m_SimAccum -= kSimDt;
		}
		if (steps == 8) m_SimAccum = 0.0;   // a long stall: do not try to catch up
		if (m_AutoSaver.Tick(static_cast<double>(dt)))
		{
			const CommandResult r = SaveSlot(AutoSaver::AutoSlot);
			if (!r.Ok) GB_CLIENT_WARN("autosave failed: {}", r.Message);
		}
	}

	bool EditorApp::RecordIfSim(const std::string& line)
	{
		const auto words = CommandRunner::Tokenize(line);
		if (words.empty()) return true;
		if (words[0] == "scene_load" || words[0] == "new_scene" || words[0] == "scene") return false;   // they depend on a file outside the replay
		if (IsSimCommand(words[0])) m_Recorder.Record(m_SimTick, line);
		return true;
	}

	CommandResult EditorApp::SaveSlot(const std::string& name)
	{
		if (!IsValidSlotName(name)) return { false, "'" + name + "' is not a name for a save (letters, digits, - _ .)" };
		CaptureResult c = CaptureGame(*m_SaveHost);
		for (const std::string& w : c.Warnings) GB_CLIENT_WARN("{}", w);
		const std::string path = SlotPath(m_SaveDir, name);
		const Result r = SaveGameToFile(c.Game, path);
		if (!r.Ok) return { false, r.Error };
		std::error_code ec;
		return { true, "saved '" + name + "' (" + std::to_string(c.Game.Entities.Size()) + " changed objects, tick " + std::to_string(c.Game.Clock.Tick) + ", " +
			Kilobytes(fs::file_size(path, ec)) + " KB) in " + path };
	}

	CommandResult EditorApp::LoadSlot(const std::string& name)
	{
		if (!IsValidSlotName(name)) return { false, "'" + name + "' is not the name of a save" };
		if (m_Recorder.Active()) return { false, "a replay is being recorded: replay stop <file> first" };
		SaveGame game;
		Result r = LoadSaveGameFromFile(SlotPath(m_SaveDir, name), game);
		if (!r.Ok) return { false, "cannot load '" + name + "': " + r.Error };
		std::vector<std::string> notes = r.Warnings;
		r = ApplyGame(*m_SaveHost, game);
		if (!r.Ok) return { false, "cannot load '" + name + "': " + r.Error + " (nothing changed)" };
		for (const std::string& w : notes) GB_CLIENT_WARN("{}", w);
		for (const std::string& w : r.Warnings) GB_CLIENT_WARN("{}", w);
		return { true, "loaded '" + name + "' (tick " + std::to_string(game.Clock.Tick) + ", format " + std::to_string(game.Format) + ")" };
	}

	CommandResult EditorApp::ReplayCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "" : a[0];
		if (sub == "record")
		{
			if (m_Recorder.Active()) return { false, "already recording" };
			const Result r = m_Recorder.Begin(*m_SaveHost);
			for (const std::string& w : r.Warnings) GB_CLIENT_WARN("{}", w);
			return { true, "recording from tick " + std::to_string(m_SimTick) + " (garden, ability, add, delete, move, set, time, weather... are kept; replay stop <file>)" };
		}
		if (sub == "stop")
		{
			if (!m_Recorder.Active()) return { false, "not recording (replay record)" };
			const size_t events = m_Recorder.EventCount();
			const Replay replay = m_Recorder.End(*m_SaveHost);
			const std::string path = ReplayPath(a.size() > 1 ? a[1] : "last");
			const Result r = WriteTextAtomic(path, ReplayToString(replay));
			if (!r.Ok) return { false, r.Error };
			return { true, "replay saved to " + path + ": " + std::to_string(events) + " orders over " + std::to_string(replay.Ticks) + " ticks (final fingerprint " + DigestToString(replay.Final.Total) + ")" };
		}
		if (sub == "play")
		{
			if (a.size() < 2) return { false, "usage: replay play <file>" };
			if (m_Recorder.Active()) return { false, "a replay is being recorded: replay stop <file> first" };
			std::string text;
			const std::string path = ReplayPath(a[1]);
			Result r = ReadTextFile(path, text);
			Replay replay;
			if (r.Ok) r = ParseReplay(text, replay);
			if (!r.Ok) return { false, r.Error };
			const ReplayRun run = PlayReplay(*m_SaveHost, replay);
			if (!run.Status.Ok) return { false, "the replay cannot start: " + run.Status.Error + " (nothing changed)" };
			std::string detail = std::to_string(run.TicksRun) + " ticks, fingerprint " + DigestToString(run.Actual.Total);
			if (run.FailedCommands > 0) detail += ", " + std::to_string(run.FailedCommands) + " orders failed";
			if (!replay.HasFinal) return { true, "replayed " + detail + " (no recorded end state to compare)" };
			if (run.Matches) return { true, "replay identical: " + detail };
			std::string parts;
			for (const std::string& p : run.Differing) parts += (parts.empty() ? "" : ", ") + p;
			return { false, "REPLAY DIFFERS in " + (parts.empty() ? std::string("the total") : parts) + ": " + detail + ", expected " + DigestToString(replay.Final.Total) };
		}
		return { false, "usage: replay record | replay stop [file] | replay play <file>" };
	}

	void EditorApp::RegisterSaveCommands()
	{
		m_SaveHost = std::make_unique<EditorSaveHost>(*this);
		m_SaveHost->SetActorSource(&m_Actors);   // the actors of the world are the ones that are saved and put back
		{
			// getenv without MSVC's deprecation warning
#if defined(_WIN32)
			char* value = nullptr;
			size_t size = 0;
			if (_dupenv_s(&value, &size, "GB_SAVE_DIR") == 0 && value && *value) m_SaveDir = value;
			std::free(value);
#else
			if (const char* dir = std::getenv("GB_SAVE_DIR")) m_SaveDir = dir;
#endif
		}
		CommandRunner& c = m_Commands;
		c.Register("save", "save <name>: saves the game (world seed, edits, garden, clock, weather) in " + m_SaveDir + "/<name>.gbsave", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: save <name>" };
			return SaveSlot(a[0]);
		});
		c.Register("load", "load <name>: loads a saved game; a damaged or incompatible one is refused and nothing changes", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: load <name>  (or load <model.glb>)" };
			// `load` was the model loader before there were saved games: a glTF path still goes there
			const fs::path given(a[0]);
			std::string ext = given.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (ext == ".glb" || ext == ".gltf") return m_Commands.Execute("load_model \"" + a[0] + "\"");
			return LoadSlot(a[0]);
		});
		c.Register("quicksave", "quicksave: saves to the 'quicksave' slot (F5)", [this](const std::vector<std::string>&) -> CommandResult { return SaveSlot(AutoSaver::QuickSlot); });
		c.Register("quickload", "quickload: loads the 'quicksave' slot (F9)", [this](const std::vector<std::string>&) -> CommandResult { return LoadSlot(AutoSaver::QuickSlot); });
		c.Register("saves", "saves: lists the saved games with their date and version", [this](const std::vector<std::string>&) -> CommandResult
		{
			const std::vector<SaveInfo> list = ListSaves(m_SaveDir);
			if (list.empty()) return { true, "no saved games in " + m_SaveDir };
			std::string s = std::to_string(list.size()) + " saved game(s) in " + m_SaveDir + ":";
			for (const SaveInfo& i : list)
			{
				s += "\n  " + i.Name + "  " + FormatTime(i.Modified) + "  ";
				if (i.Ok) s += "format " + std::to_string(i.Format) + (i.Format < kFormatVersion ? " (migrates)" : "") + "  tick " + std::to_string(i.Tick) + "  " + (i.WorldType.empty() ? "no generated world" : i.WorldType + " seed " + std::to_string(i.Seed)) + "  " + Kilobytes(i.Bytes) + " KB";
				else s += "DAMAGED: " + i.Error;
			}
			return { true, s };
		});
		c.Register("autosave", "autosave [on|off] [seconds]: saves to the 'autosave' slot on a timer (default every 300 s); without arguments, the state", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty())
			{
				if (a[0] != "on" && a[0] != "off") return { false, "usage: autosave [on|off] [seconds]" };
				const double seconds = a.size() > 1 ? std::atof(a[1].c_str()) : 0.0;
				if (a.size() > 1 && seconds < 1.0) return { false, "the interval is a number of seconds (1 or more)" };
				m_AutoSaver.Configure(a[0] == "on", seconds);
			}
			return { true, std::string("autosave ") + (m_AutoSaver.Enabled() ? "on" : "off") + ", every " + std::to_string(static_cast<int>(m_AutoSaver.Interval())) + " s" };
		});
		c.Register("replay", "replay record | replay stop [file] | replay play <file>: records the orders of a session and plays them again from the same start to check the end state", [this](const std::vector<std::string>& a) -> CommandResult { return ReplayCommand(a); });
	}

	void EditorApp::DrawSaveMenu()
	{
		if (ImGui::MenuItem("Save game (quicksave)", "F5")) Execute("quicksave");
		if (ImGui::BeginMenu("Save game as..."))
		{
			static char name[64] = "slot1";
			ImGui::InputText("##slotname", name, sizeof name);
			ImGui::SameLine();
			if (ImGui::Button("Save")) Execute(std::string("save ") + name);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Load game"))
		{
			const std::vector<SaveInfo> list = ListSaves(m_SaveDir);
			if (list.empty()) ImGui::TextDisabled("no saved games in %s", m_SaveDir.c_str());
			for (const SaveInfo& i : list)
			{
				const std::string label = i.Name + "   " + FormatTime(i.Modified);
				if (i.Ok) { if (ImGui::MenuItem(label.c_str())) Execute("load " + i.Name); }
				else ImGui::TextDisabled("%s (damaged)", i.Name.c_str());
			}
			ImGui::EndMenu();
		}
	}
}
