// G1/G2: the shell of GB.exe (GameMain.cpp starts the EditorApp with `--shell`; see GameShell.h). Nothing of the editor is drawn here: the start
// menu over the world, the god's screen (ObserverView.cpp, `observer screen`) or the player's game (GameMode.cpp) with a pause menu of their own,
// and the director's note box. Every button queues a `shell ...` command (or a `game`/`observer` one), so a script drives the same menu headless
// (Editor/tests/game_shell.txt). A game is recorded as a session folder, <saves>/sessions/<date>_<time>_<biome>_<seed>/ (see WriteSessionJson).
// Every word drawn goes through Lang (assets/ui/lang/es.json); the commands' answers, `shell status` and the session's files stay English.
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Gameplay/ContentPack.h"
#include "GB/Gameplay/ShippedContent.h"
#include "GB/Log.h"
#include "GB/Save/SaveFiles.h"
#include "GB/Save/SaveWorld.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#ifndef GB_GIT_HASH
#define GB_GIT_HASH "unknown"
#endif

namespace GB::Editor
{
	namespace
	{
		namespace fs = std::filesystem;
		using Json = nlohmann::json;
		constexpr const char* kBiomes[] = { "temperate", "alpine", "desert" };
		constexpr const char* kBiomeLabels[] = { "Temperate", "Alpine", "Desert" };

		// "2026-11-15 10:42:07" (separators: "-" and ":"), or a name for a folder ("2026-11-15_10-42-07")
		std::string Now(bool forFolder)
		{
			const std::time_t t = std::time(nullptr);
			std::tm local{};
#ifdef _WIN32
			localtime_s(&local, &t);
#else
			localtime_r(&t, &local);
#endif
			char text[32];
			std::strftime(text, sizeof text, forFolder ? "%Y-%m-%d_%H-%M-%S" : "%Y-%m-%d %H:%M:%S", &local);
			return text;
		}

		std::string LowerCase(std::string s)
		{
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		// A key of Dear ImGui by its name ("F8", "Escape"); ImGuiKey_None when there is none (the same lookup as the game's keys, GameMode.cpp).
		ImGuiKey KeyByName(const std::string& name)
		{
			const std::string want = LowerCase(name);
			for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
				if (LowerCase(ImGui::GetKeyName(static_cast<ImGuiKey>(k))) == want) return static_cast<ImGuiKey>(k);
			return ImGuiKey_None;
		}

		int BiomeIndex(const std::string& biome)
		{
			for (int i = 0; i < 3; ++i) if (biome == kBiomes[i]) return i;
			return -1;
		}

		// The god's file beside a save: <name>.gbsave -> <name>.god.json (what the save of the world does not keep: the camera, the speed, the clan
		// observed, and the counts the load list shows without reading the whole save).
		std::string GodPath(const std::string& save)
		{
			fs::path p(save);
			p.replace_extension(".god.json");
			return p.generic_string();
		}

		std::string Quoted(const std::string& s) { return "\"" + s + "\""; }

		// a big button of the menus, the whole width of the window
		bool MenuButton(const std::string& label, bool primary = false)
		{
			if (primary)
			{
				const GameTheme::PrimaryScope lit;
				return ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 34.0f));
			}
			return ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 34.0f));
		}

		// one of a row of choices: ochre on a darker hide and framed when chosen, so the colour is never the only sign
		bool Choice(const std::string& label, bool chosen, float width)
		{
			if (chosen) { ImGui::PushStyleColor(ImGuiCol_Button, GameTheme::HideActive); ImGui::PushStyleColor(ImGuiCol_Text, GameTheme::Ochre); }
			const bool pressed = ImGui::Button(label.c_str(), ImVec2(width, 28.0f));
			if (chosen)
			{
				ImGui::PopStyleColor(2);
				ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), GameTheme::Ochre, 4.0f);
			}
			return pressed;
		}
	}

	// ------------------------------------------------------------------ data
	std::string LoadSessionRules(const std::string& json, SessionRules& rules)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("session");
		if (section == j.end()) return {};
		if (!section->is_object()) return "session: expected an object";
		SessionRules read = rules;
		for (const auto& [key, v] : section->items())
		{
			if (key == "autosaveDays") { if (!v.is_number_integer() || v.get<int>() < 0) return "session.autosaveDays: expected a whole number of days, 0 or more"; read.AutosaveDays = v.get<int>(); }
			else if (key == "telemetry") { if (!v.is_boolean()) return "session.telemetry: expected true or false"; read.Telemetry = v.get<bool>(); }
			else if (key == "telemetryEvery") { if (!v.is_number() || v.get<double>() < 1.0) return "session.telemetryEvery: expected seconds, 1 or more"; read.TelemetryEvery = v.get<double>(); }
			else if (key == "keys")
			{
				if (!v.is_object()) return "session.keys: expected an object of key names";
				for (const auto& [action, k] : v.items())
				{
					if (action != "note" && action != "shot" && action != "quicksave" && action != "quickload") return "session.keys: unknown action '" + action + "' (note, shot, quicksave, quickload)";
					if (!k.is_string()) return "session.keys." + action + ": expected a key name";
					if (k.get<std::string>().empty()) read.Keys.erase(action);
					else read.Keys[action] = k.get<std::string>();
				}
			}
			else if (key != "_doc") return "session: unknown key '" + key + "'";
		}
		rules = std::move(read);
		return {};
	}

	std::string LoadGamePresets(const std::string& json, std::vector<GamePreset>& out)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("presets") || !j["presets"].is_array()) return "expected { \"presets\": [ ... ] }";
		std::vector<GamePreset> read;
		for (const Json& p : j["presets"])
		{
			if (!p.is_object() || !p.contains("id") || !p["id"].is_string()) return "a preset without an id";
			GamePreset g;
			g.Id = p["id"].get<std::string>();
			try
			{
				g.Label = p.value("label", g.Id);
				g.Tip = p.value("tip", std::string());
				g.Biome = p.value("biome", g.Biome);
				g.Seed = p.value("seed", g.Seed);
				g.Clans = p.value("clans", g.Clans);
				g.Members = p.value("members", g.Members);
				g.Speed = p.value("speed", g.Speed);
				g.Bare = p.value("bare", g.Bare);
			}
			catch (const std::exception& e) { return "preset " + g.Id + ": " + e.what(); }
			if (BiomeIndex(g.Biome) < 0) return "preset " + g.Id + ": unknown biome '" + g.Biome + "' (temperate, alpine, desert)";
			if (g.Clans < 1 || g.Clans > 6 || g.Members < 2 || g.Members > 40) return "preset " + g.Id + ": 1 to 6 clans of 2 to 40 members";
			read.push_back(std::move(g));
		}
		out = std::move(read);
		return {};
	}

	// ------------------------------------------------------------------ start
	void EditorApp::StartShell()
	{
		m_Shell.On = true;
		m_Shell.Now = ShellState::Screen::Menu;
		const std::string presets = m_AssetRoot + "/data/presets.json";
		std::ifstream in(presets, std::ios::binary);
		if (!in) GB_CLIENT_WARN("shell: {} not found: the new game has no presets", presets);
		else
		{
			std::stringstream ss;
			ss << in.rdbuf();
			if (const std::string error = LoadGamePresets(ss.str(), m_Shell.Presets); !error.empty()) GB_CLIENT_WARN("shell: {} ignored: {}", presets, error);
		}
		LoadUserPresets();
		ChooseShellPreset(m_Shell.Presets.empty() ? -1 : 0);
		// the world behind the menu waits; the window takes the whole screen unless asked not to (or hidden: a test, a capture)
		if (!Options().Hidden && !Options().HasFlag("windowed")) SetFullscreen(true);
		GB_CLIENT_INFO("GB shell: assets {}, data {}, saves {}", m_AssetRoot, ContentDir(), m_SaveDir);
	}

	// G5: the player's presets, <saves>/presets.json (the same format as assets/data/presets.json; the shipped file is never written). They go after
	// the shipped ones; one with the id of a shipped one is skipped (the shipped one wins).
	void EditorApp::LoadUserPresets()
	{
		m_Shell.Presets.erase(std::remove_if(m_Shell.Presets.begin(), m_Shell.Presets.end(), [](const GamePreset& p) { return p.User; }), m_Shell.Presets.end());
		const std::string path = (fs::path(m_SaveDir) / "presets.json").generic_string();
		std::ifstream in(path, std::ios::binary);
		if (!in) return;
		std::stringstream ss;
		ss << in.rdbuf();
		std::vector<GamePreset> mine;
		if (const std::string error = LoadGamePresets(ss.str(), mine); !error.empty()) { GB_CLIENT_WARN("shell: {} ignored: {}", path, error); return; }
		for (GamePreset& p : mine)
		{
			if (std::any_of(m_Shell.Presets.begin(), m_Shell.Presets.end(), [&](const GamePreset& q) { return q.Id == p.Id; })) continue;
			p.User = true;
			m_Shell.Presets.push_back(std::move(p));
		}
	}

	void EditorApp::ChooseShellPreset(int index)
	{
		if (index < 0 || index >= static_cast<int>(m_Shell.Presets.size())) { m_Shell.Preset = -1; return; }
		const GamePreset& p = m_Shell.Presets[static_cast<size_t>(index)];
		m_Shell.Preset = index;
		m_Shell.Biome = std::max(0, BiomeIndex(p.Biome)); m_Shell.Seed = p.Seed; m_Shell.Clans = p.Clans; m_Shell.Members = p.Members; m_Shell.Speed = p.Speed; m_Shell.Bare = p.Bare;
	}

	// G5: the form as it is, kept as a preset of the player's in <saves>/presets.json (a preset of his with the same id is replaced)
	CommandResult EditorApp::ShellPresetSave(const std::string& id, const std::string& label)
	{
		if (!Save::IsValidSlotName(id) || id == "custom") return { false, "'" + id + "' is not a name for a preset (letters, digits, - _ .; not custom)" };
		for (const GamePreset& p : m_Shell.Presets)
			if (p.Id == id && !p.User) return { false, "'" + id + "' is a shipped preset: choose another name" };
		const std::string path = (fs::path(m_SaveDir) / "presets.json").generic_string();
		Json doc = Json::object();
		if (std::ifstream in(path, std::ios::binary); in)
		{
			doc = Json::parse(in, nullptr, false);
			if (doc.is_discarded() || !doc.is_object()) doc = Json::object();
		}
		if (!doc.contains("presets") || !doc["presets"].is_array()) doc["presets"] = Json::array();
		doc["_doc"] = "G5: the presets the player saved from GB.exe's new game (custom). Same format as assets/data/presets.json.";
		Json& list = doc["presets"];
		for (auto it = list.begin(); it != list.end();)
			it = it->is_object() && it->value("id", std::string()) == id ? list.erase(it) : it + 1;
		const std::string biome = kBiomes[std::clamp(m_Shell.Biome, 0, 2)];
		char tip[160];
		std::snprintf(tip, sizeof tip, "%s %d, %d x %d%s, x%d", biome.c_str(), m_Shell.Seed, m_Shell.Clans, m_Shell.Members, m_Shell.Bare ? ", bare" : "", m_Shell.Speed);
		list.push_back({ { "id", id }, { "label", label.empty() ? id : label }, { "tip", tip }, { "biome", biome }, { "seed", m_Shell.Seed }, { "clans", m_Shell.Clans },
			{ "members", m_Shell.Members }, { "speed", m_Shell.Speed }, { "bare", m_Shell.Bare } });
		std::error_code ec;
		fs::create_directories(m_SaveDir, ec);
		std::ofstream out(path, std::ios::binary);
		if (!out) return { false, "cannot write " + path };
		out << doc.dump(2);
		out.close();
		LoadUserPresets();
		for (size_t i = 0; i < m_Shell.Presets.size(); ++i)
			if (m_Shell.Presets[i].Id == id) m_Shell.Preset = static_cast<int>(i);
		ShellToast("Preset saved");
		return { true, "preset " + id + " saved in " + path + " (" + tip + ")" };
	}

	void EditorApp::SetFullscreen(bool on)
	{
		GLFWwindow* window = GetWindow() ? GetWindow()->GetNative() : nullptr;
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
		if (!window || !mode) return;
		int mx = 0, my = 0;
		glfwGetMonitorPos(monitor, &mx, &my);
		// a window without a frame over the whole monitor (alt-tab and captures behave; no change of the display's mode)
		glfwSetWindowAttrib(window, GLFW_DECORATED, on ? GLFW_FALSE : GLFW_TRUE);
		if (on) glfwSetWindowMonitor(window, nullptr, mx, my, mode->width, mode->height, GLFW_DONT_CARE);
		else glfwSetWindowMonitor(window, nullptr, mx + 80, my + 80, std::min(1600, mode->width - 160), std::min(900, mode->height - 160), GLFW_DONT_CARE);
		m_Shell.Fullscreen = on;
	}

	bool EditorApp::ShellHoldsTime() const
	{
		return m_Shell.On && (m_Shell.Now == ShellState::Screen::Menu || m_Shell.Pause || m_Shell.NoteOpen || m_Shell.NoteOpenIn > 0 || !m_Shell.Pending.empty());
	}

	EditorApp::ShellCounts EditorApp::Counts() const
	{
		ShellCounts c;
		c.Day = GameDay();
		for (const auto& clan : m_Clans.All())
		{
			if (!clan->Folk) continue;
			int alive = 0;
			for (const TribeMember& m : clan->Folk->Members()) if (!m.Dead) ++alive;
			if (alive > 0) ++c.Clans;
			c.Alive += alive;
		}
		return c;
	}

	// ------------------------------------------------------------------ a new game
	bool EditorApp::ResetForNewGame(std::string& error)
	{
		// What a load does to the run that ends (EditorSaveHost::Commit), with an empty game: no objects of the user (the villages' buildings, the
		// sites), no actors, clans, history, requests, fights, animals or timers, and the clock at its start. The world is generated after it.
		if (m_Game.Active) Execute("play off");
		if (m_Telemetry.Active()) TelemetryCommand({ "stop" });
		Save::SaveGame empty;
		empty.World.Generated = false;
		empty.Baseline = Save::BaselineIdOf(EntityWorld());
		empty.Weather.Preset = "clear";
		const Save::Result r = Save::ApplyGame(*m_SaveHost, empty);
		if (!r.Ok) { error = "the old game could not be cleared: " + r.Error; return false; }
		m_Graves.clear();
		m_Game = GameModeState{};
		m_FreeSelected.clear();
		m_ActiveClan = 0;
		m_Observer = ObserverState{};
		m_QolDays.clear();
		m_QolDay = 0;
		m_Societies.clear();
		m_SocietiesAt = -1.0;
		return true;
	}

	CommandResult EditorApp::ShellNew(const std::string& biome, int seed, int clans, int members, bool bare, int speed, const std::string& preset)
	{
		if (BiomeIndex(biome) < 0) return { false, "unknown biome '" + biome + "' (temperate, alpine, desert)" };
		clans = std::clamp(clans, 1, 6);
		members = std::clamp(members, 2, 40);
		EndSession("new game");
		std::string error;
		if (!ResetForNewGame(error)) return { false, error };
		const auto start = std::chrono::steady_clock::now();
		CommandResult r = Execute("generate " + biome + " " + std::to_string(std::max(0, seed)));
		if (!r.Ok) return r;
		FinishWorldBuild();
		// the people: the X Bot of packs/full beside the assets (the package carries it), else CesiumMan
		std::error_code ec;
		const fs::path xbot = fs::path(m_AssetRoot) / ".." / "packs" / "full" / "X Bot.fbx";
		const std::string model = fs::exists(xbot, ec) ? fs::weakly_canonical(xbot, ec).generic_string() : std::string("cesium");
		r = Execute("village " + std::to_string(clans) + " " + std::to_string(members) + " " + Quoted(model) + (bare ? " bare" : ""));
		if (!r.Ok) return r;
		Execute("observer screen on");
		m_Observer.Paused = false;
		m_TimeScale = std::clamp(speed, 1, 20);
		m_ActiveClan = 0;
		// the camera over the first clan's camp, from a little above
		const glm::vec2 camp = m_Tribe.Config.Economy.Camp;
		m_Orbit.Target = { camp.x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(camp.x, camp.y) : 0.0f, camp.y };
		m_Orbit.Distance = 70.0f;
		m_Orbit.Pitch = 0.55f;
		m_Shell.Now = ShellState::Screen::God;
		m_Director.SetStakes(true);   // §6.2 A: the god's game can be lost (saved with it; the editor and the player's game never)
		m_Shell.Pause = false;
		m_Shell.Page = m_Shell.PausePage = "main";
		ShellState::Session& s = m_Shell.Run;
		s = ShellState::Session{};
		s.Biome = biome; s.Seed = static_cast<uint32_t>(std::max(0, seed)); s.Clans = clans; s.Members = members; s.Bare = bare; s.Speed = m_TimeScale; s.Preset = preset;
		BeginSession("god", "");
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		char text[64];
		std::snprintf(text, sizeof text, " (%.1f s)", took);
		return { true, "new god's game: " + biome + " " + std::to_string(seed) + ", " + std::to_string(clans) + " clans of " + std::to_string(members) + (bare ? ", bare" : "") + text
			+ "; session " + m_Shell.Run.Dir };
	}

	CommandResult EditorApp::ShellPlayer(const std::vector<std::string>& a)
	{
		EndSession("new game");
		std::string error;
		if (!ResetForNewGame(error)) return { false, error };
		std::string args;
		for (const std::string& w : a) args += " " + w;
		const CommandResult r = Execute("play" + args);
		if (!r.Ok) return r;
		m_Shell.Now = ShellState::Screen::Player;
		m_Director.SetStakes(false);   // §6.2 A: never in the player's game
		m_Shell.Pause = false;
		ShellState::Session& s = m_Shell.Run;
		s = ShellState::Session{};
		s.Biome = kBiomes[m_WorldDesc.Type == WorldType::Alpine ? 1 : m_WorldDesc.Type == WorldType::Desert ? 2 : 0];
		s.Seed = m_WorldDesc.Seed;
		s.Clans = Counts().Clans;
		s.Members = static_cast<int>(m_Tribe.Members().size());
		s.Speed = m_TimeScale;
		s.Preset = "player";
		BeginSession("player", "");
		return { true, r.Message + "; session " + m_Shell.Run.Dir };
	}

	// ------------------------------------------------------------------ saves
	CommandResult EditorApp::ShellSave(const std::string& path, const std::string& label)
	{
		std::error_code ec;
		fs::create_directories(fs::path(path).parent_path(), ec);
		const CommandResult r = SaveFile(path, label);
		if (!r.Ok) return r;
		const ShellCounts c = Counts();
		Json god;
		god["mode"] = m_Shell.Now == ShellState::Screen::Player ? "player" : "god";
		god["biome"] = m_Shell.Run.Biome.empty() ? std::string(kBiomes[m_WorldDesc.Type == WorldType::Alpine ? 1 : m_WorldDesc.Type == WorldType::Desert ? 2 : 0]) : m_Shell.Run.Biome;
		god["seed"] = m_WorldDesc.Seed;
		god["day"] = c.Day;
		god["clans"] = c.Clans;
		god["alive"] = c.Alive;
		god["speed"] = m_TimeScale;
		god["clan"] = m_ActiveClan;
		god["camera"] = { { "target", { m_Orbit.Target.x, m_Orbit.Target.y, m_Orbit.Target.z } }, { "yaw", m_Orbit.Yaw }, { "pitch", m_Orbit.Pitch }, { "distance", m_Orbit.Distance } };
		god["follow"] = m_GameView.Following();
		god["sheet"] = m_Observer.Tab;
		god["folded"] = m_Observer.Folded;
		god["session"] = m_Shell.Run.Dir;
		god["saved"] = Now(false);
		std::ofstream out(GodPath(path), std::ios::binary);
		if (out) out << god.dump(2);
		else GB_CLIENT_WARN("shell: the god's file {} could not be written: the save loads without its camera and speed", GodPath(path));
		if (m_Shell.Run.Active)
		{
			std::error_code rel;
			const fs::path inSession = fs::relative(path, m_Shell.Run.Dir, rel);
			m_Shell.Run.Saves.push_back(!rel && !inSession.empty() && inSession.generic_string().rfind("..", 0) != 0 ? inSession.generic_string() : path);
		}
		return r;
	}

	CommandResult EditorApp::ShellLoad(const std::string& given)
	{
		const std::string path = Save::IsValidSlotName(given) ? Save::SlotPath(m_SaveDir, given) : given;
		std::error_code ec;
		if (!fs::exists(path, ec)) return { false, "no save " + given };
		EndSession("load");
		if (m_Game.Active) Execute("play off");
		const CommandResult r = LoadFile(path, fs::path(path).stem().string());
		if (!r.Ok) return r;
		m_Game = GameModeState{};
		m_Observer = ObserverState{};
		m_QolDays.clear();
		m_QolDay = 0;
		m_FreeSelected.clear();
		Execute("observer screen on");
		m_Observer.Paused = false;
		m_Shell.Now = ShellState::Screen::God;
		m_Shell.Pause = false;
		m_Shell.Page = m_Shell.PausePage = "main";
		ShellState::Session& s = m_Shell.Run;
		s = ShellState::Session{};
		s.Biome = kBiomes[m_WorldDesc.Type == WorldType::Alpine ? 1 : m_WorldDesc.Type == WorldType::Desert ? 2 : 0];
		s.Seed = m_WorldDesc.Seed;
		s.Preset = "load";
		// what the save of the world does not keep: the god's camera, speed and clan (its file beside the save; an old save has none)
		std::string restored = "no god's file";
		std::ifstream in(GodPath(path), std::ios::binary);
		const Json god = in ? Json::parse(in, nullptr, false) : Json();
		if (god.is_object())
		{
			try
			{
				m_TimeScale = std::clamp(god.value("speed", 1), 1, 20);
				if (const int clan = god.value("clan", 0); m_Clans.Find(clan)) m_ActiveClan = clan;
				if (god.contains("camera") && god["camera"].is_object())
				{
					const Json& c = god["camera"];
					if (c.contains("target") && c["target"].is_array() && c["target"].size() == 3) m_Orbit.Target = { c["target"][0].get<float>(), c["target"][1].get<float>(), c["target"][2].get<float>() };
					m_Orbit.Yaw = c.value("yaw", m_Orbit.Yaw);
					m_Orbit.Pitch = c.value("pitch", m_Orbit.Pitch);
					m_Orbit.Distance = c.value("distance", m_Orbit.Distance);
				}
				if (const std::string follow = god.value("follow", std::string()); !follow.empty())
					if (const Tribe* t = ClanOfMember(follow); t && t->FindMember(follow) && !t->FindMember(follow)->Dead)
						m_GameView.Command({ "follow", follow }, m_Actors, m_Player, m_World, m_Camera);
				m_Observer.Folded = god.value("folded", true);
				s.Clans = god.value("clans", 0);
				restored = "camera, speed x" + std::to_string(m_TimeScale) + " and clan " + std::to_string(m_ActiveClan) + " restored";
			}
			catch (const std::exception& e) { restored = std::string("god's file unreadable: ") + e.what(); }
		}
		s.Speed = m_TimeScale;
		BeginSession("god", path);
		ShellToast("Game loaded");
		return { true, r.Message + "; " + restored + "; session " + m_Shell.Run.Dir };
	}

	CommandResult EditorApp::ShellToMenu()
	{
		EndSession("menu");
		if (m_Game.Active) Execute("play off");
		Execute("observer off");
		m_Shell.Now = ShellState::Screen::Menu;
		m_Shell.Pause = m_Shell.NoteOpen = false;
		m_Shell.NoteOpenIn = 0;
		m_Shell.Page = m_Shell.PausePage = "main";
		return { true, "main menu" };
	}

	// ------------------------------------------------------------------ the session (G2)
	void EditorApp::BeginSession(const std::string& mode, const std::string& loadedFrom)
	{
		ShellState::Session& s = m_Shell.Run;
		std::error_code ec;
		const std::string name = Now(true) + "_" + s.Biome + "_" + std::to_string(s.Seed);
		s.Dir = (fs::path(m_SaveDir) / "sessions" / name).generic_string();
		fs::create_directories(fs::path(s.Dir) / "shots", ec);
		fs::create_directories(fs::path(s.Dir) / "notes", ec);
		if (ec) { GB_CLIENT_WARN("shell: the session folder {} could not be made ({}): this game is not recorded", s.Dir, ec.message()); return; }
		s.Active = true;
		s.Mode = mode;
		s.LoadedFrom = loadedFrom;
		s.Started = Now(false);
		s.Clock = std::chrono::steady_clock::now();
		s.DayStart = GameDay();
		s.NextAutosaveDay = m_Shell.Rules.AutosaveDays > 0 ? s.DayStart + m_Shell.Rules.AutosaveDays : 0;
		// game.log: every line of the log from now until the session ends (the log's lock is held while a sink runs; ours guards the flush)
		{
			std::shared_ptr<ShellState::LogFile> log = m_Shell.Log;
			std::lock_guard<std::mutex> lock(log->Lock);
			log->Out.open(fs::path(s.Dir) / "game.log", std::ios::binary | std::ios::app);
			log->Flushed = std::chrono::steady_clock::now();
		}
		std::weak_ptr<ShellState::LogFile> weak = m_Shell.Log;
		m_Shell.Log->Sink = GB::Log::AddSink([weak](GB::Log::Channel, GB::Log::Level level, std::string_view text)
		{
			const std::shared_ptr<ShellState::LogFile> log = weak.lock();
			if (!log) return;
			std::lock_guard<std::mutex> lock(log->Lock);
			if (!log->Out) return;
			log->Out << text << "\n";
			if (level >= GB::Log::Level::Warn) log->Out.flush();   // (a warning or an error is on the disk at once: a crash may follow)
		});
		GB_CLIENT_INFO("session {}: {} game, {} {} (git {}), day {}{}", s.Dir, mode, s.Biome, s.Seed, GB_GIT_HASH, s.DayStart, loadedFrom.empty() ? std::string() : ", loaded from " + loadedFrom);
		if (m_Shell.Rules.Telemetry)
		{
			char every[32];
			std::snprintf(every, sizeof every, "%g", m_Shell.Rules.TelemetryEvery);
			const CommandResult t = TelemetryCommand({ "start", (fs::path(s.Dir) / "telemetry.db").generic_string(), every });
			s.Telemetry = t.Ok;
			if (!t.Ok) GB_CLIENT_WARN("session: no telemetry: {}", t.Message);
		}
		WriteSessionJson("playing");
	}

	void EditorApp::EndSession(const std::string& why)
	{
		ShellState::Session& s = m_Shell.Run;
		if (!s.Active) return;
		// the save on leaving: the world as the director left it
		if (Counts().Clans > 0)
		{
			const CommandResult r = ShellSave((fs::path(s.Dir) / "exit.gbsave").generic_string(), "exit");
			if (!r.Ok) GB_CLIENT_WARN("session: the save on leaving failed: {}", r.Message);
		}
		if (m_Telemetry.Active())
		{
			const CommandResult t = TelemetryCommand({ "status" });
			GB_CLIENT_INFO("session: {}", t.Message);
			TelemetryCommand({ "stop" });
		}
		WriteSessionJson(why);
		GB_CLIENT_INFO("session {} ends ({})", s.Dir, why);
		if (m_Shell.Log->Sink) { GB::Log::RemoveSink(m_Shell.Log->Sink); m_Shell.Log->Sink = 0; }
		{
			std::lock_guard<std::mutex> lock(m_Shell.Log->Lock);
			m_Shell.Log->Out.close();
		}
		s.Active = false;
	}

	// session.json: what reproduces the game (the build, the start, the data's folder) and what happened in it (days, saves, notes), written at the
	// start, at each autosave and at the end (a crash leaves the last one).
	void EditorApp::WriteSessionJson(const std::string& state)
	{
		const ShellState::Session& s = m_Shell.Run;
		if (s.Dir.empty()) return;
		const ShellCounts c = Counts();
		Json j;
		j["format"] = 1;
		j["state"] = state;
		j["version"] = { { "git", GB_GIT_HASH }, { "engine", Save::kEngineVersion }, { "built", std::string(__DATE__) + " " + __TIME__ } };
		j["mode"] = s.Mode;
		j["started"] = s.Started;
		j["updated"] = Now(false);
		j["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - s.Clock).count();
		j["setup"] = { { "preset", s.Preset }, { "biome", s.Biome }, { "seed", s.Seed }, { "clans", s.Clans }, { "members", s.Members }, { "bare", s.Bare }, { "speed", s.Speed } };
		// G5: where the start came from: a shipped preset, one of the player's (<saves>/presets.json), the custom form, a load or the player's mode
		const auto chosen = std::find_if(m_Shell.Presets.begin(), m_Shell.Presets.end(), [&](const GamePreset& p) { return p.Id == s.Preset; });
		j["setup"]["source"] = chosen != m_Shell.Presets.end() ? (chosen->User ? "user preset" : "preset") : s.Preset == "custom" ? "custom" : s.Preset;
		if (chosen != m_Shell.Presets.end()) j["setup"]["label"] = chosen->Label;
		j["loadedFrom"] = s.LoadedFrom;
		j["days"] = { { "start", s.DayStart }, { "now", c.Day } };
		j["now"] = { { "clans", c.Clans }, { "alive", c.Alive }, { "speed", m_TimeScale }, { "tick", m_SimTick } };
		j["autosaveDays"] = m_Shell.Rules.AutosaveDays;
		j["saves"] = s.Saves;
		j["notes"] = s.Notes;
		j["shots"] = s.Shots;
		j["telemetry"] = s.Telemetry ? Json{ { "file", "telemetry.db" }, { "every", m_Shell.Rules.TelemetryEvery } } : Json();
		j["quality"] = m_QualityLevel;
		j["window"] = { GetWindow() ? GetWindow()->GetWidth() : 0u, GetWindow() ? GetWindow()->GetHeight() : 0u };
		j["assets"] = m_AssetRoot;
		std::ofstream out(fs::path(s.Dir) / "session.json", std::ios::binary);
		if (out) out << j.dump(2);
	}

	void EditorApp::ShellTick()
	{
		ShellState::Session& s = m_Shell.Run;
		if (!s.Active) return;
		if (s.NextAutosaveDay > 0 && GameDay() >= s.NextAutosaveDay)
		{
			const int day = GameDay();
			const CommandResult r = ShellSave((fs::path(s.Dir) / ("autosave_d" + std::to_string(day) + ".gbsave")).generic_string(), "autosave");
			if (!r.Ok) GB_CLIENT_WARN("session: the autosave of day {} failed: {}", day, r.Message);
			s.NextAutosaveDay = day + m_Shell.Rules.AutosaveDays;
			WriteSessionJson("playing");
		}
		const auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(m_Shell.Log->Lock);
		if (now - m_Shell.Log->Flushed > std::chrono::seconds(1)) { m_Shell.Log->Out.flush(); m_Shell.Log->Flushed = now; }
	}

	// ------------------------------------------------------------------ notes and captures
	CommandResult EditorApp::ShellShot()
	{
		ShellState::Session& s = m_Shell.Run;
		const fs::path dir = s.Active ? fs::path(s.Dir) / "shots" : fs::path(m_SaveDir) / "shots";
		std::error_code ec;
		fs::create_directories(dir, ec);
		++s.Shots;
		const std::string path = (dir / ("shot_" + std::to_string(s.Shots) + "_d" + std::to_string(GameDay()) + ".png")).generic_string();
		m_PendingUiShot = path;   // (the whole window at the end of this frame: the toast comes on the next one)
		ShellToast("Capture saved");
		return { true, "capture " + path };
	}

	CommandResult EditorApp::ShellNote(const std::string& text)
	{
		ShellState::Session& s = m_Shell.Run;
		if (!s.Active) return { false, "no session: start or load a game first" };
		++s.Notes;
		std::string shot = m_Shell.NoteShot;
		if (shot.empty())   // (a note given by a command: its capture is this frame)
		{
			shot = (fs::path(s.Dir) / "notes" / ("note_" + std::to_string(s.Notes) + "_d" + std::to_string(GameDay()) + ".png")).generic_string();
			m_PendingUiShot = shot;
		}
		m_Shell.NoteShot.clear();
		const float hour = m_Tribe.Hour();
		const glm::vec3 eye = m_Camera.Position, at = m_Orbit.Target;
		const Clan* clan = m_Clans.Find(m_ActiveClan);
		const std::string follow = m_GameView.Following();
		char head[512];
		std::snprintf(head, sizeof head, "## Note %d: day %d, %02d:%02d\n\n- Camera at (%.1f, %.1f, %.1f) looking at (%.1f, %.1f, %.1f), %.0f m\n- Clan observed: %d %s; following: %s; speed x%d%s\n",
			s.Notes, GameDay(), static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f), static_cast<double>(eye.x), static_cast<double>(eye.y), static_cast<double>(eye.z),
			static_cast<double>(at.x), static_cast<double>(at.y), static_cast<double>(at.z), static_cast<double>(glm::distance(eye, at)), m_ActiveClan, clan ? clan->Name.c_str() : "",
			follow.empty() ? "nobody" : follow.c_str(), m_TimeScale, m_Shell.Now == ShellState::Screen::Player ? ("; playing " + m_Game.Player).c_str() : "");
		std::error_code ec;
		const fs::path rel = fs::relative(shot, s.Dir, ec);
		const bool first = !fs::exists(fs::path(s.Dir) / "notes.md", ec);
		std::ofstream out(fs::path(s.Dir) / "notes.md", std::ios::binary | std::ios::app);
		if (!out) return { false, "cannot write " + s.Dir + "/notes.md" };
		if (first) out << "# Director's notes\n\nSession " << fs::path(s.Dir).filename().generic_string() << " (" << s.Mode << ", " << s.Biome << " " << s.Seed << ", git " << GB_GIT_HASH << ")\n\n";
		out << head << "- Capture: " << (ec ? shot : rel.generic_string()) << "\n\n" << text << "\n\n";
		GB_CLIENT_INFO("note {} (day {}): {}", s.Notes, GameDay(), text);
		WriteSessionJson("playing");
		ShellToast("Note saved");
		return { true, "note " + std::to_string(s.Notes) + " in " + s.Dir + "/notes.md" };
	}

	void EditorApp::ShellToast(const std::string& english) { ShellToastShown(m_Lang.Text(english)); }

	void EditorApp::ShellToastShown(const std::string& shown)
	{
		m_Shell.Toast = shown;   // (kept translated: a toast of the hand is built from names, no English key to look up when drawn)
		m_Shell.ToastAt = ImGui::GetTime();
	}

	// ------------------------------------------------------------------ commands
	std::string EditorApp::ShellStatus()
	{
		const ShellCounts c = Counts();
		const char* screen = m_Shell.Now == ShellState::Screen::Menu ? "menu" : m_Shell.Now == ShellState::Screen::God ? "god" : "player";
		std::string s = std::string("shell: ") + screen + ", page " + (m_Shell.Now == ShellState::Screen::Menu ? m_Shell.Page : m_Shell.PausePage) + ", pause " + (m_Shell.Pause ? "on" : "off")
			+ ", note " + (m_Shell.NoteOpen ? "open" : "closed") + "; observer screen " + (m_Observer.Screen ? "on" : "off") + ", editor menus hidden";
		s += "; day " + std::to_string(c.Day) + ", clans " + std::to_string(c.Clans) + ", alive " + std::to_string(c.Alive) + "; speed x" + std::to_string(m_TimeScale) + ", clan " + std::to_string(m_ActiveClan);
		s += "; session " + (m_Shell.Run.Active ? m_Shell.Run.Dir + " (" + std::to_string(m_Shell.Run.Notes) + " notes, " + std::to_string(m_Shell.Run.Saves.size()) + " saves" + (m_Shell.Run.Telemetry ? ", telemetry" : "") + ")" : std::string("none"));
		return s;
	}

	CommandResult EditorApp::ShellCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status") return { true, ShellStatus() };
		if (!m_Shell.On && sub != "presets") return { false, "the shell is GB.exe's (GB.exe, or Editor --shell)" };
		const bool playing = m_Shell.Now != ShellState::Screen::Menu;
		if (sub == "presets")
		{
			std::string s = std::to_string(m_Shell.Presets.size()) + " preset(s):";
			for (const GamePreset& p : m_Shell.Presets) s += " " + p.Id + " (" + p.Biome + " " + std::to_string(p.Seed) + ", " + std::to_string(p.Clans) + "x" + std::to_string(p.Members) + (p.Bare ? ", bare" : "") + (p.User ? ", user" : "") + ")";
			const bool custom = m_Shell.Preset < 0 || m_Shell.Preset >= static_cast<int>(m_Shell.Presets.size());
			s += "; chosen " + (custom ? std::string("custom") : m_Shell.Presets[static_cast<size_t>(m_Shell.Preset)].Id);
			return { true, s };
		}
		if (sub == "console")   // the console of commands over a game (the `console` key of controls.keys): for testing, never on the screen by itself
		{
			if (m_Shell.Now == ShellState::Screen::Menu) return { false, "the console is a game's (start or load one)" };
			if (m_Shell.Now == ShellState::Screen::Player) return Execute("game console");
			m_Game.Console = a.size() > 1 ? a[1] == "on" : !m_Game.Console;
			return { true, m_Game.Console ? "console open" : "console closed" };
		}
		if (sub == "page")   // the start menu's pages, or the pause menu's in a game
		{
			const std::string page = a.size() > 1 ? a[1] : "main";
			if (page != "main" && page != "new" && page != "load" && page != "settings") return { false, "shell page main|new|load|settings" };
			if (playing) { if (page == "new") return { false, "a new game starts from the main menu" }; m_Shell.PausePage = page; m_Shell.Pause = true; }
			else m_Shell.Page = page;
			return { true, "page " + page };
		}
		if (sub == "preset")   // G5: shell preset select <id|custom> | save <id> [label words]: the new game's form, as its buttons
		{
			const std::string what = a.size() > 1 ? a[1] : "";
			if (what == "select" && a.size() > 2)
			{
				if (a[2] == "custom") { ChooseShellPreset(-1); return { true, "custom game" }; }
				for (size_t i = 0; i < m_Shell.Presets.size(); ++i)
					if (m_Shell.Presets[i].Id == a[2]) { ChooseShellPreset(static_cast<int>(i)); return { true, "preset " + a[2] }; }
				return { false, "no preset " + a[2] + " (shell presets)" };
			}
			if (what == "save" && a.size() > 2)
			{
				std::string label;
				for (size_t i = 3; i < a.size(); ++i) label += (i > 3 ? " " : "") + a[i];
				return ShellPresetSave(a[2], label);
			}
			return { false, "shell preset select <id|custom> | save <id> [label]" };
		}
		if (sub == "new")   // shell new [preset <id> | custom] [temperate|alpine|desert] [seed|random] [clans] [members] [bare|village] [speed <n>]
		{
			std::string biome = kBiomes[std::clamp(m_Shell.Biome, 0, 2)], preset = "custom";
			int seed = m_Shell.Seed, clans = m_Shell.Clans, members = m_Shell.Members, speed = m_Shell.Speed;
			bool bare = m_Shell.Bare;
			std::vector<int> numbers;
			for (size_t i = 1; i < a.size(); ++i)
			{
				if (a[i] == "preset" && i + 1 < a.size())
				{
					const auto it = std::find_if(m_Shell.Presets.begin(), m_Shell.Presets.end(), [&](const GamePreset& p) { return p.Id == a[i + 1]; });
					if (it == m_Shell.Presets.end()) return { false, "no preset " + a[i + 1] + " (shell presets)" };
					biome = it->Biome; seed = it->Seed; clans = it->Clans; members = it->Members; speed = it->Speed; bare = it->Bare; preset = it->Id;
					++i;
				}
				else if (a[i] == "custom") preset = "custom";   // (G5: the form's values, whatever preset was chosen)
				else if (a[i] == "speed" && i + 1 < a.size()) speed = std::atoi(a[++i].c_str());
				else if (a[i] == "bare") bare = true;
				else if (a[i] == "village") bare = false;
				else if (a[i] == "random") { static std::mt19937 dice{ std::random_device{}() }; numbers.push_back(static_cast<int>(dice() % 100000u)); }   // (in the seed's place)
				else if (BiomeIndex(a[i]) >= 0) biome = a[i];
				else numbers.push_back(std::atoi(a[i].c_str()));
			}
			if (numbers.size() > 0) seed = numbers[0];
			if (numbers.size() > 1) clans = numbers[1];
			if (numbers.size() > 2) members = numbers[2];
			return ShellNew(biome, seed, clans, members, bare, speed, preset);
		}
		if (sub == "player") return ShellPlayer(std::vector<std::string>(a.begin() + 1, a.end()));
		if (sub == "load")
		{
			if (a.size() < 2) return { false, "shell load <slot|path of a .gbsave>" };
			return ShellLoad(a[1]);
		}
		if (sub == "quickload") return ShellLoad(Save::AutoSaver::QuickSlot);
		if (sub == "menu") return ShellToMenu();
		if (sub == "fullscreen")
		{
			SetFullscreen(a.size() > 1 ? a[1] == "on" : !m_Shell.Fullscreen);
			return { true, std::string("full screen ") + (m_Shell.Fullscreen ? "on" : "off") };
		}
		if (sub == "quit")
		{
			EndSession("quit");
			Close();
			return { true, "bye" };
		}
		// shell mark / shell same: the day, the clans and the living now, and whether they are still the same (a save and its load: the test)
		if (sub == "mark")
		{
			const ShellCounts c = Counts();
			m_Shell.MarkDay = c.Day; m_Shell.MarkClans = c.Clans; m_Shell.MarkAlive = c.Alive;
			return { true, "marked: day " + std::to_string(c.Day) + ", clans " + std::to_string(c.Clans) + ", alive " + std::to_string(c.Alive) };
		}
		if (sub == "same")
		{
			const ShellCounts c = Counts();
			const std::string now = "day " + std::to_string(c.Day) + ", clans " + std::to_string(c.Clans) + ", alive " + std::to_string(c.Alive);
			const std::string then = "day " + std::to_string(m_Shell.MarkDay) + ", clans " + std::to_string(m_Shell.MarkClans) + ", alive " + std::to_string(m_Shell.MarkAlive);
			if (c.Day != m_Shell.MarkDay || c.Clans != m_Shell.MarkClans || c.Alive != m_Shell.MarkAlive) return { false, "shell differs: " + now + " (marked " + then + ")" };
			return { true, "shell same: " + now };
		}
		if (!playing) return { false, "not in a game: shell new ... | shell load <save>" };
		if (sub == "pause") { m_Shell.Pause = a.size() > 1 ? a[1] == "on" : !m_Shell.Pause; m_Shell.PausePage = "main"; return { true, m_Shell.Pause ? "pause menu" : "running" }; }
		if (sub == "resume") { m_Shell.Pause = false; m_Shell.PausePage = "main"; return { true, "running" }; }
		if (sub == "save")
		{
			const ShellCounts c = Counts();
			const std::string name = a.size() > 1 ? a[1] : m_Shell.Run.Biome + "-" + std::to_string(m_WorldDesc.Seed) + "-day" + std::to_string(c.Day);
			if (!Save::IsValidSlotName(name)) return { false, "'" + name + "' is not a name for a save (letters, digits, - _ .)" };
			const CommandResult r = ShellSave(Save::SlotPath(m_SaveDir, name), name);
			ShellToast(r.Ok ? "Game saved" : "Could not save");
			return r;
		}
		if (sub == "quicksave")
		{
			const CommandResult r = ShellSave(Save::SlotPath(m_SaveDir, Save::AutoSaver::QuickSlot), Save::AutoSaver::QuickSlot);
			ShellToast(r.Ok ? "Game saved" : "Could not save");
			return r;
		}
		if (sub == "shot") return ShellShot();
		if (sub == "note")
		{
			const std::string what = a.size() > 1 ? a[1] : "begin";
			if (what == "begin")   // F8: the capture of what is on the screen now, then the box (next frame)
			{
				if (!m_Shell.Run.Active) return { false, "no session" };
				m_Shell.NoteShot = (fs::path(m_Shell.Run.Dir) / "notes" / ("note_" + std::to_string(m_Shell.Run.Notes + 1) + "_d" + std::to_string(GameDay()) + ".png")).generic_string();
				m_PendingUiShot = m_Shell.NoteShot;
				m_Shell.NoteText[0] = 0;
				m_Shell.NoteOpenIn = 1;
				return { true, "note: capture " + m_Shell.NoteShot + "; write it and save (shell note save)" };
			}
			if (what == "save")
			{
				const std::string text = m_Shell.NoteText;
				m_Shell.NoteOpen = false;
				m_Shell.NoteOpenIn = 0;
				return ShellNote(text.empty() ? std::string("(no text)") : text);
			}
			if (what == "cancel")
			{
				std::error_code ec;
				if (!m_Shell.NoteShot.empty()) fs::remove(m_Shell.NoteShot, ec);
				m_Shell.NoteShot.clear();
				m_Shell.NoteOpen = false;
				m_Shell.NoteOpenIn = 0;
				return { true, "note cancelled" };
			}
			if (what == "text" && a.size() > 2)   // a whole note from a script: the capture is this frame
			{
				std::string text;
				for (size_t i = 2; i < a.size(); ++i) text += (i > 2 ? " " : "") + a[i];
				m_Shell.NoteShot.clear();
				return ShellNote(text);
			}
			return { false, "shell note [begin|save|cancel|text <words>]" };
		}
		return { false, "shell status|presets|preset select <id|custom>|preset save <id> [label]|page <main|new|load|settings>|new [preset <id>|custom] [biome] [seed|random] [clans] [members] [bare|village] [speed <n>]|console [on|off]|player [play's args]|save [name]|load <save>|quicksave|quickload|pause [on|off]|resume|note [begin|save|cancel|text <words>]|shot|mark|same|fullscreen [on|off]|menu|quit" };
	}

	void EditorApp::RegisterShellCommands()
	{
		m_Commands.Register("shell", "shell status|presets|preset|page|new|console|player|save|load|quicksave|quickload|pause|resume|note|shot|mark|same|fullscreen|menu|quit: GB.exe's start menu, the god's game and its session folder (G1/G2, GameShell.cpp); the menus' buttons run these",
			[this](const std::vector<std::string>& a) { return ShellCommand(a); });
	}

	// ------------------------------------------------------------------ keys
	void EditorApp::ShellKeys()
	{
		if (ImGui::GetIO().WantTextInput) return;
		auto pressed = [&](const std::map<std::string, std::string>& keys, const char* action)
		{
			const auto it = keys.find(action);
			const ImGuiKey k = it == keys.end() ? ImGuiKey_None : KeyByName(it->second);
			return k != ImGuiKey_None && ImGui::IsKeyPressed(k, false);
		};
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		if (m_Shell.Now == ShellState::Screen::Menu)
		{
			if (pressed(m_Controls.Keys, "quit") && m_Shell.Page != "main") run("shell page main");
			return;
		}
		if (pressed(m_Shell.Rules.Keys, "note") && !m_Shell.NoteOpen) run("shell note begin");
		if (pressed(m_Shell.Rules.Keys, "shot")) run("shell shot");
		if (m_Shell.Now != ShellState::Screen::God) return;   // (the player's game has its own Esc, F5, F9 and console: GameKeys)
		if (pressed(m_Controls.Keys, "console") && !m_Shell.Pause && !m_Shell.NoteOpen) run("shell console");   // (T233: the commands, by key only)
		if (pressed(m_Controls.Keys, "quit"))
		{
			if (m_Shell.NoteOpen) run("shell note cancel");
			else if (m_Game.Console) run("shell console off");
			else if (m_Shell.Pause) run(m_Shell.PausePage != "main" ? "shell page main" : "shell resume");
			else if (!m_Hand.Building.empty() || !m_Hand.Menu.empty()) run("director hand off");   // (T201: Esc lets go of the ghost or the hand's menu first)
			else if (m_Observer.Tree) run("observer tree");   // (Esc closes the tree of knowledge first, as in the game)
			else run("shell pause on");
		}
		if (m_Shell.Pause || m_Shell.NoteOpen) return;
		if (pressed(m_Shell.Rules.Keys, "quicksave")) run("shell quicksave");
		if (pressed(m_Shell.Rules.Keys, "quickload")) run("shell quickload");
	}

	// ------------------------------------------------------------------ drawing
	void EditorApp::DrawShell()
	{
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		// a long order (a new world) runs two frames after it was asked for: the card that says so is on the screen meanwhile
		if (!m_Shell.Pending.empty() && --m_Shell.PendingFrames <= 0) { m_UiCommands.push_back(m_Shell.Pending); m_Shell.Pending.clear(); }
		ShellKeys();
		if (m_Shell.Now == ShellState::Screen::Menu)
		{
			// the world behind the menu, turning slowly and dimmed; nothing of it takes the mouse
			m_ViewportW = static_cast<uint32_t>(std::max(vp->Size.x, 64.0f));
			m_ViewportH = static_cast<uint32_t>(std::max(vp->Size.y, 64.0f));
			m_ViewportHovered = false;
			m_Orbit.Yaw += m_FrameDt * 0.03f;
			ImDrawList* bg = ImGui::GetBackgroundDrawList();
			bg->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_ViewportFb->GetColorTexture())), vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), ImVec2(0, 1), ImVec2(1, 0));
			bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(12, 8, 5, 150));
			const GameTheme::Scope theme;
			DrawShellMenu();
			DrawShellOverlay();
		}
		else
		{
			ShellTick();
			// (the god's screen is the observer's: should anything have turned it off, the shell puts it back; never the editor)
			if (m_Shell.Now == ShellState::Screen::God && !m_Observer.Screen && !m_Game.Active) Execute("observer screen on");
			if (m_Shell.Now == ShellState::Screen::God && !m_Shell.Pause && !m_Shell.NoteOpen) ObserverKeys();
			DrawGameScreen();
			const GameTheme::Scope theme;
			if (m_Shell.Now == ShellState::Screen::God && m_Game.Console)
			{   // T233: the console of commands over the god's screen (the `console` key or `shell console`), under the clan card and left of the book
				const ImGuiViewport* gv = ImGui::GetMainViewport();
				m_Game.HudRight = 0.0f;
				m_Game.ClockBottom = m_Observer.CardBottom;
				m_Game.BookWidth = m_Observer.BookWidth;
				DrawGameConsole(gv->Pos.x, gv->Pos.y, gv->Size.x, gv->Size.y);
			}
			DrawShellPause();
			DrawShellNote();
			DrawShellOverlay();
		}
		for (const std::string& line : m_UiCommands) Execute(line);
		m_UiCommands.clear();
	}

	void EditorApp::DrawShellMenu()
	{
		const Lang& L = m_Lang;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const float width = m_Shell.Page == "load" ? std::min(760.0f, vp->Size.x - 40.0f) : m_Shell.Page == "new" ? 540.0f : 420.0f;
		ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
		if (ImGui::Begin("GB##shell_menu", nullptr, flags))
		{
			// the title: the name big, and what the game is under it
			ImGui::SetWindowFontScale(3.0f);
			const char* title = "GB";
			ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(title).x) * 0.5f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Ochre), "%s", title);
			ImGui::SetWindowFontScale(1.0f);
			const std::string sub = L.Text("Clans from the Stone Age to the arrival of AI");
			ImGui::SetCursorPosX((ImGui::GetWindowSize().x - ImGui::CalcTextSize(sub.c_str()).x) * 0.5f);
			ImGui::TextDisabled("%s", sub.c_str());
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			if (m_Shell.Page == "new") DrawShellNewPage();
			else if (m_Shell.Page == "load") DrawShellLoadPage(false);
			else if (m_Shell.Page == "settings") DrawShellSettingsPage();
			else
			{
				if (MenuButton(L.Text("New game (god mode)") + "###new", true)) run("shell page new");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Watch the clans live from above: a new world with its biome, seed, clans and people").c_str());
				if (MenuButton(L.Text("Load game") + "###load")) run("shell page load");
				if (MenuButton(L.Text("Settings") + "###settings")) run("shell page settings");
				if (MenuButton(L.Text("Quit") + "###quit")) run("shell quit");
				ImGui::Spacing();
				ImGui::Separator();
				// the player's mode stays as a way to test, apart from the game (decision 8 of MODO_DIOS_UX: the god's mode is the game)
				ImGui::PushStyleColor(ImGuiCol_Text, GameTheme::BoneDim);
				if (ImGui::Button((L.Text("Player mode (tests)") + "###player").c_str(), ImVec2(-FLT_MIN, 24.0f))) { m_Shell.Pending = "shell player"; m_Shell.PendingFrames = 2; }
				ImGui::PopStyleColor();
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Walk as a member of a clan, as in the editor's play mode: for testing, not the game").c_str());
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		// the keys of a game, at the foot of the screen
		const std::string keys = L.Text("In a game: [Esc] menu  [{0}] save  [{1}] load  [{2}] director's note  [{3}] capture", L.Key(m_Shell.Rules.Keys["quicksave"]),
			L.Key(m_Shell.Rules.Keys["quickload"]), L.Key(m_Shell.Rules.Keys["note"]), L.Key(m_Shell.Rules.Keys["shot"]));
		const ImVec2 size = ImGui::CalcTextSize(keys.c_str());
		ImGui::GetForegroundDrawList()->AddText(ImVec2(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + vp->Size.y - size.y - 14.0f), GameTheme::BoneDim, keys.c_str());
	}

	void EditorApp::DrawShellNewPage()
	{
		const Lang& L = m_Lang;
		ImGui::TextUnformatted(L.Text("New game").c_str());
		ImGui::Spacing();
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const bool custom = m_Shell.Preset < 0 || m_Shell.Preset >= static_cast<int>(m_Shell.Presets.size());
		// G5: a row of choices, at most four a row: the shipped presets (assets/data/presets.json), "Custom" (the form), then the player's own
		// (<saves>/presets.json, under "Yours"). Choosing a preset fills the form; "Custom" keeps what it holds, so a preset can be the start of one.
		auto row = [&](const std::vector<int>& items, bool withCustom)
		{
			const size_t n = items.size() + (withCustom ? 1u : 0u);
			for (size_t k = 0; k < n; k += 4)
			{
				const size_t inRow = std::min<size_t>(4, n - k);
				const float each = (ImGui::GetContentRegionAvail().x - gap * static_cast<float>(inRow - 1)) / static_cast<float>(inRow);
				for (size_t j = 0; j < inRow; ++j)
				{
					if (j > 0) ImGui::SameLine();
					const size_t at = k + j;
					if (at == items.size())   // the custom choice, last
					{
						if (Choice(L.Text("Custom") + "###custom", custom, each)) m_UiCommands.push_back("shell preset select custom");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Your own game: biome, seed, clans, people and how they start").c_str());
						continue;
					}
					const int i = items[at];
					const GamePreset& p = m_Shell.Presets[static_cast<size_t>(i)];
					if (Choice((p.User ? p.Label : L.Text(p.Label)) + "###preset" + p.Id, m_Shell.Preset == i, each)) m_UiCommands.push_back("shell preset select " + p.Id);
				}
			}
		};
		std::vector<int> shipped, mine;
		for (size_t i = 0; i < m_Shell.Presets.size(); ++i) (m_Shell.Presets[i].User ? mine : shipped).push_back(static_cast<int>(i));
		ImGui::TextDisabled("%s", L.Text("Start").c_str());
		row(shipped, true);
		if (!mine.empty())
		{
			ImGui::TextDisabled("%s", L.Text("Yours").c_str());
			row(mine, false);
		}
		ImGui::Spacing();
		// what the chosen start is: the preset's words (a shipped one's tip) and its numbers in a line; the form only for a custom game
		auto summary = [&]()
		{
			const std::string start = L.Text(m_Shell.Bare ? "nothing built" : "villages standing");
			return L.Text("{0}, seed {1}; {2} clans of {3} people, {4}; speed x{5}", L.Text(kBiomeLabels[std::clamp(m_Shell.Biome, 0, 2)]), m_Shell.Seed, m_Shell.Clans,
				m_Shell.Members, start, m_Shell.Speed);
		};
		if (!custom)
		{
			const GamePreset& p = m_Shell.Presets[static_cast<size_t>(m_Shell.Preset)];
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
			if (!p.User && !p.Tip.empty()) ImGui::TextUnformatted(L.Text(p.Tip).c_str());
			ImGui::PushStyleColor(ImGuiCol_Text, GameTheme::BoneDim);
			ImGui::TextUnformatted(summary().c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
			ImGui::TextDisabled("%s", L.Text("Custom changes it: the form keeps these values").c_str());
		}
		else
		{
			ImGui::TextDisabled("%s", L.Text("Biome").c_str());
			{
				const float each = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
				for (int i = 0; i < 3; ++i)
				{
					if (i > 0) ImGui::SameLine();
					if (Choice(L.Text(kBiomeLabels[i]) + "###biome" + kBiomes[i], m_Shell.Biome == i, each)) m_Shell.Biome = i;
				}
			}
			ImGui::TextDisabled("%s", L.Text("Seed").c_str());
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			if (ImGui::InputInt("##seed", &m_Shell.Seed)) m_Shell.Seed = std::max(0, m_Shell.Seed);
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Random") + "###random").c_str(), ImVec2(-FLT_MIN, 0.0f)))
			{
				static std::mt19937 dice{ std::random_device{}() };
				m_Shell.Seed = static_cast<int>(dice() % 100000u);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The same seed makes the same world").c_str());
			ImGui::TextDisabled("%s", L.Text("Clans").c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::SliderInt("##clans", &m_Shell.Clans, 1, 6);
			ImGui::TextDisabled("%s", L.Text("People per clan").c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::SliderInt("##members", &m_Shell.Members, 2, 40);
			ImGui::TextDisabled("%s", L.Text("How they start").c_str());
			{
				const float each = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
				if (Choice(L.Text("Villages standing") + "###village", !m_Shell.Bare, each)) m_Shell.Bare = false;
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Each clan starts with its village built").c_str());
				ImGui::SameLine();
				if (Choice(L.Text("Nothing built") + "###bare", m_Shell.Bare, each)) m_Shell.Bare = true;
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Each clan gets its land and a well, and raises its village itself").c_str());
			}
			ImGui::TextDisabled("%s", L.Text("Speed at the start").c_str());
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::SliderInt("##speed", &m_Shell.Speed, 1, 20, "x%d");
			// the form kept as a preset of the player's (<saves>/presets.json): its name, then the button (`shell preset save <id> <name>`)
			ImGui::Spacing();
			ImGui::TextDisabled("%s", L.Text("Keep this game as a start of yours").c_str());
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 110.0f);
			ImGui::InputTextWithHint("##preset_name", L.Text("Name").c_str(), m_Shell.PresetName, sizeof m_Shell.PresetName);
			ImGui::SameLine();
			std::string id;   // the file's id from the name: letters and digits kept, the rest '_'
			for (const char c : std::string(m_Shell.PresetName)) id += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
			ImGui::BeginDisabled(id.empty());
			if (ImGui::Button((L.Text("Save") + "###preset_save").c_str(), ImVec2(-FLT_MIN, 0.0f))) m_UiCommands.push_back("shell preset save " + id + " " + m_Shell.PresetName);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text(id.empty() ? "Write a name first" : "It will be under Yours, next time too").c_str());
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
			ImGui::PushStyleColor(ImGuiCol_Text, GameTheme::BoneDim);
			ImGui::TextUnformatted(summary().c_str());
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		if (MenuButton(L.Text("Start") + "###start", true))
		{
			const std::string preset = !custom ? " preset " + m_Shell.Presets[static_cast<size_t>(m_Shell.Preset)].Id : std::string(" custom");
			m_Shell.Pending = "shell new" + preset + " " + kBiomes[std::clamp(m_Shell.Biome, 0, 2)] + " " + std::to_string(m_Shell.Seed) + " " + std::to_string(m_Shell.Clans) + " "
				+ std::to_string(m_Shell.Members) + (m_Shell.Bare ? " bare" : "") + " speed " + std::to_string(m_Shell.Speed);
			m_Shell.PendingFrames = 2;
		}
		if (MenuButton(L.Text("Back") + "###back")) m_UiCommands.push_back("shell page main");
	}

	void EditorApp::DrawShellLoadPage(bool inGame)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImGui::TextUnformatted(L.Text("Load game").c_str());
		ImGui::Spacing();
		// the saves of the saves folder and of the sessions, newest first (read when the page is drawn: a few files, a few times a second at most)
		static double listedAt = -1.0;
		static std::vector<std::pair<Save::SaveInfo, Json>> rows;
		if (ImGui::GetTime() - listedAt > 1.0 || ImGui::GetTime() < listedAt)
		{
			listedAt = ImGui::GetTime();
			rows.clear();
			std::vector<Save::SaveInfo> all = Save::ListSaves(m_SaveDir);
			std::error_code ec;
			const fs::path sessions = fs::path(m_SaveDir) / "sessions";
			if (fs::is_directory(sessions, ec))
				for (const auto& entry : fs::directory_iterator(sessions, ec))
					if (entry.is_directory(ec))
						for (Save::SaveInfo& i : Save::ListSaves(entry.path().generic_string())) { i.Name = entry.path().filename().generic_string() + " / " + i.Name; all.push_back(std::move(i)); }
			std::sort(all.begin(), all.end(), [](const Save::SaveInfo& x, const Save::SaveInfo& y) { return x.Modified > y.Modified; });
			if (all.size() > 40) all.resize(40);
			for (Save::SaveInfo& i : all)
			{
				std::ifstream in(GodPath(i.Path), std::ios::binary);
				Json god = in ? Json::parse(in, nullptr, false) : Json();
				rows.emplace_back(std::move(i), god.is_object() ? god : Json());
			}
		}
		if (rows.empty()) ImGui::TextDisabled("%s", L.Text("No saved games yet").c_str());
		else if (ImGui::BeginTable("##saves", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, std::min(360.0f, 28.0f * static_cast<float>(rows.size() + 1)))))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn(L.Text("Game").c_str(), ImGuiTableColumnFlags_WidthStretch, 3.0f);
			ImGui::TableSetupColumn(L.Text("Date").c_str(), ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn(L.Text("Day").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn(L.Text("Clans").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn(L.Text("Alive").c_str(), ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableHeadersRow();
			int n = 0;
			for (const auto& row : rows)
			{
				const Save::SaveInfo& info = row.first;
				const Json& god = row.second;
				ImGui::TableNextRow();
				ImGui::PushID(n++);
				ImGui::TableNextColumn();
				const std::string where = info.WorldType.empty() ? std::string() : " (" + L.Text(kBiomeLabels[std::max(0, BiomeIndex(info.WorldType))]) + " " + std::to_string(info.Seed) + ")";
				ImGui::TextUnformatted((info.Name + where).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Save::FormatTime(info.Modified).c_str());
				auto number = [&](const char* key) { ImGui::TableNextColumn(); if (god.is_object() && god.contains(key) && god[key].is_number_integer()) ImGui::Text("%d", god[key].get<int>()); else ImGui::TextDisabled("-"); };
				number("day");
				number("clans");
				number("alive");
				ImGui::TableNextColumn();
				if (!info.Ok)
				{
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Bad), "%s", L.Text("Damaged").c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", info.Error.c_str());
				}
				else if (ImGui::Button((L.Text("Load") + "###load").c_str(), ImVec2(-FLT_MIN, 0.0f))) { m_Shell.Pending = "shell load " + Quoted(info.Path); m_Shell.PendingFrames = 2; }
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		if (inGame) ImGui::TextDisabled("%s", L.Text("The game being played is saved in its session folder before another is loaded").c_str());
		if (MenuButton(L.Text("Back") + "###back")) run("shell page main");
	}

	void EditorApp::DrawShellSettingsPage()
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImGui::TextUnformatted(L.Text("Settings").c_str());
		ImGui::Spacing();
		// T49: the same graphics presets as the game's pause menu (`game quality`: applied at once and kept in the player's settings file)
		ImGui::TextDisabled("%s", L.Text("Graphics").c_str());
		{
			struct Pick { const char* Id; const char* Level; const char* Label; const char* Tip; };
			std::vector<Pick> picks;
			for (const GraphicsPreset& p : GraphicsPresets()) picks.push_back({ p.Id, p.Level, p.Label, p.Tip });
			picks.push_back({ "auto", "auto", "Auto", "Starts like Medium without ambient occlusion and drops to Low as soon as frames are slow" });
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float each = (ImGui::GetContentRegionAvail().x - gap * static_cast<float>(picks.size() - 1)) / static_cast<float>(picks.size());
			for (size_t i = 0; i < picks.size(); ++i)
			{
				if (i > 0) ImGui::SameLine();
				if (Choice(L.Text(picks[i].Label) + "###quality_" + picks[i].Id, m_Controls.Game.Quality == picks[i].Level, each)) run(std::string("game quality ") + picks[i].Id);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(picks[i].Tip).c_str());
			}
			const GraphicsPreset* now = PresetOfLevel(m_QualityLevel);
			ImGui::TextDisabled("%s", L.Text("In use: {0}", now ? L.Text(now->Label) : L.Text(m_LaunchQuality.empty() ? "Custom" : "Custom (given at launch)")).c_str());
		}
		ImGui::Spacing();
		ImGui::TextDisabled("%s", L.Text("Screen").c_str());
		{
			const float each = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			if (Choice(L.Text("Full screen") + "###full", m_Shell.Fullscreen, each)) run("shell fullscreen on");
			ImGui::SameLine();
			if (Choice(L.Text("Window") + "###window", !m_Shell.Fullscreen, each)) run("shell fullscreen off");
		}
		ImGui::Spacing();
		if (MenuButton(L.Text("Back") + "###back")) run("shell page main");
	}

	void EditorApp::DrawShellPause()
	{
		if (!m_Shell.Pause || m_Shell.Now != ShellState::Screen::God) return;
		const Lang& L = m_Lang;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImGui::GetForegroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 110));
		const float width = m_Shell.PausePage == "load" ? std::min(760.0f, vp->Size.x - 40.0f) : 300.0f;
		ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.45f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowFocus();
		const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		bool open = true;
		if (ImGui::Begin((L.Text("Paused") + "###shell_pause").c_str(), &open, flags))
		{
			if (m_Shell.PausePage == "load") DrawShellLoadPage(true);
			else if (m_Shell.PausePage == "settings") DrawShellSettingsPage();
			else
			{
				if (MenuButton(L.Text("Resume") + "###resume", true)) run("shell resume");
				if (MenuButton(L.Text("Save") + " [" + L.Key(m_Shell.Rules.Keys["quicksave"]) + "]###save")) run("shell save");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("A save of its own, named by the world and the day; [{0}] is the quick save", L.Key(m_Shell.Rules.Keys["quicksave"])).c_str());
				if (MenuButton(L.Text("Load") + "###load")) run("shell page load");
				if (MenuButton(L.Text("Settings") + "###settings")) run("shell page settings");
				ImGui::Separator();
				if (MenuButton(L.Text("Main menu") + "###menu")) run("shell menu");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The game is saved in its session folder on leaving").c_str());
				if (MenuButton(L.Text("Quit") + "###quit")) run("shell quit");
			}
		}
		ImGui::End();
		if (!open) run("shell resume");
	}

	void EditorApp::DrawShellNote()
	{
		if (m_Shell.NoteOpenIn > 0 && --m_Shell.NoteOpenIn == 0) m_Shell.NoteOpen = true;   // (the capture was taken on the frame before)
		if (!m_Shell.NoteOpen) return;
		const Lang& L = m_Lang;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(std::min(560.0f, vp->Size.x - 40.0f), 0.0f), ImGuiCond_Always);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
		if (ImGui::Begin((L.Text("Director's note") + "###shell_note").c_str(), nullptr, flags))
		{
			const float hour = m_Tribe.Hour();
			char clock[16];
			std::snprintf(clock, sizeof clock, "%02d:%02d", static_cast<int>(hour), static_cast<int>((hour - std::floor(hour)) * 60.0f));
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", L.Text("Day {0}, {1}. The capture of the screen is already taken; the camera and the day go with the note.", GameDay(), std::string(clock)).c_str());
			ImGui::PopTextWrapPos();
			if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
			ImGui::InputTextMultiline("##note", m_Shell.NoteText, sizeof m_Shell.NoteText, ImVec2(-FLT_MIN, 140.0f));
			const float each = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
			{
				const GameTheme::PrimaryScope lit;
				if (ImGui::Button((L.Text("Save the note") + "###note_save").c_str(), ImVec2(each, 30.0f))) run("shell note save");
			}
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Cancel") + " [" + L.Key("Escape") + "]###note_cancel").c_str(), ImVec2(each, 30.0f))) run("shell note cancel");
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) run("shell note cancel");
		}
		ImGui::End();
	}

	void EditorApp::DrawShellOverlay()
	{
		const Lang& L = m_Lang;
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		// a long order under way: a card in the middle says so (the frame then stops while it works)
		if (!m_Shell.Pending.empty())
		{
			const std::string text = L.Text(m_Shell.Pending.rfind("shell load", 0) == 0 ? "Loading the game..." : "Making the world and its clans...");
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
			dl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 120));
			GameTheme::Card(dl, ImVec2(c.x - size.x * 0.5f - 24.0f, c.y - 22.0f), ImVec2(c.x + size.x * 0.5f + 24.0f, c.y + 22.0f), GameTheme::Ochre);
			dl->AddText(ImVec2(c.x - size.x * 0.5f, c.y - size.y * 0.5f), GameTheme::Bone, text.c_str());
		}
		// the toast: what a key just did (saved, captured), for two and a half seconds under the god's bar
		const double age = ImGui::GetTime() - m_Shell.ToastAt;
		if (!m_Shell.Toast.empty() && age >= 0.0 && age < 2.5)
		{
			const std::string& text = m_Shell.Toast;
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const float alpha = static_cast<float>(std::min(1.0, (2.5 - age) / 0.5));
			const ImVec2 at(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, vp->Pos.y + std::max(m_Observer.BarBottom - vp->Pos.y, 0.0f) + 48.0f);
			GameTheme::Card(dl, ImVec2(at.x - 14.0f, at.y - 8.0f), ImVec2(at.x + size.x + 14.0f, at.y + size.y + 8.0f), GameTheme::Ochre, alpha);
			dl->AddText(at, GameTheme::Faded(GameTheme::Bone, alpha), text.c_str());
		}
	}
}
