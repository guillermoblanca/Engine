#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/World/Water/WaterParamInfo.h"
#include "GB/AI/Needs.h"
#include "GB/Game/GameSeeds.h"
#include "GB/Animation/Character.h"
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Log.h"
#include "GB/Renderer/ModelGpu.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "GB/Tools/DocsExport.h"
#include "TerrainGraphUi.h"
#include "GB/World/Graph/TerrainGraphPresets.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>

namespace GB::Editor
{
	namespace
	{
		namespace fs = std::filesystem;
		const char* const kWeatherNames[] = { "clear", "lightrain", "rain", "storm", "snow", "blizzard", "dust", "sandstorm", "tornado" };
		const WeatherPreset kWeatherPresets[] = { WeatherPreset::Clear, WeatherPreset::LightRain, WeatherPreset::Rain, WeatherPreset::Thunderstorm, WeatherPreset::Snowfall,
			WeatherPreset::Blizzard, WeatherPreset::Dust, WeatherPreset::Sandstorm, WeatherPreset::Tornado };

		ApplicationConfig MakeConfig(const LaunchOptions& launch)
		{
			ApplicationConfig c;
			c.Launch = launch;
			c.Title = "GB Editor";
			// --width=/--height=: a bigger (or smaller) window for a screenshot/record session (--hidden); the panel layout keeps its
			// proportions (MakeDockLayout), so the viewport grows with it. Unset: the usual 1600x900.
			c.Width = static_cast<uint32_t>(std::max(320, std::atoi(launch.GetValue("width", "1600").c_str())));
			c.Height = static_cast<uint32_t>(std::max(240, std::atoi(launch.GetValue("height", "900").c_str())));
			c.CreateWindow = true;
			return c;
		}
		// Draws the editing widget of one property; true when the user changed it.
		bool DrawPropertyWidget(Property& p, const char* label)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(-ImGui::GetContentRegionAvail().x * 0.42f);
			switch (p.Type)
			{
			case PropType::Float:
			{
				float& v = *static_cast<float*>(p.Ptr);
				changed = p.Max - p.Min <= 1000.0f ? ImGui::SliderFloat(label, &v, p.Min, p.Max, "%.3g") : ImGui::DragFloat(label, &v, 0.5f, p.Min, p.Max);
				break;
			}
			case PropType::Int: changed = ImGui::SliderInt(label, static_cast<int*>(p.Ptr), static_cast<int>(p.Min), static_cast<int>(p.Max)); break;
			case PropType::Bool: changed = ImGui::Checkbox(label, static_cast<bool*>(p.Ptr)); break;
			case PropType::Vec3: changed = ImGui::DragFloat3(label, &static_cast<glm::vec3*>(p.Ptr)->x, std::min((p.Max - p.Min) * 0.002f, 0.05f), p.Min, p.Max, "%.3g"); break;
			case PropType::Color: changed = ImGui::ColorEdit3(label, &static_cast<glm::vec3*>(p.Ptr)->x, ImGuiColorEditFlags_Float); break;
			case PropType::Enum:
			{
				int& i = *static_cast<int*>(p.Ptr);
				if (ImGui::BeginCombo(label, i >= 0 && static_cast<size_t>(i) < p.Names.size() ? p.Names[static_cast<size_t>(i)].c_str() : "?"))
				{
					for (size_t k = 0; k < p.Names.size(); ++k)
						if (ImGui::Selectable(p.Names[k].c_str(), static_cast<int>(k) == i)) { i = static_cast<int>(k); changed = true; }
					ImGui::EndCombo();
				}
				break;
			}
			}
			if (!p.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.Tooltip.c_str());
			return changed;
		}

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

	EditorApp::EditorApp(const LaunchOptions& launch) : Application(MakeConfig(launch))
	{
		if (!IsValid())
			return;
		const LaunchOptions& options = Options();

		m_Log = std::make_shared<LogBuffer>();
		m_LogSink = GB::Log::AddSink([log = m_Log.get()](GB::Log::Channel, GB::Log::Level level, std::string_view text) { log->Sink(level, text); });

		const bool headless = options.Hidden || !options.RecordDir.empty();
		m_Ui = std::make_unique<ImGuiLayer>(*GetWindow(), headless ? "" : "editor_layout.ini");
		m_Renderer = std::make_unique<Renderer>(m_ViewportW, m_ViewportH, 4, 2048);
		if (!m_Renderer->IsValid())
		{
			GB_CLIENT_ERROR("Renderer could not be created");
			Close();
			return;
		}
		m_ViewportFb = std::make_unique<Framebuffer>(m_ViewportW, m_ViewportH);
		m_AssetRoot = GB_ASSET_DIR;
		m_SceneEditor.SetAssetRoot(m_AssetRoot);
		m_Lang.Load(m_AssetRoot + "/ui/lang", "es");   // the game's interface in Spanish (`lang en` for the code's English)
		m_SceneEditor.OnNewScene = [this]() { ResetWorld(); };   // File > New Scene also drops the generated world
		m_SceneEditor.SetPropBounds([this](const PropComponent& p, glm::vec3& center, float& radius) { return m_World.PropBounds(p, center, radius); });
		m_Weather = std::make_unique<Weather>(5);
		m_Orbit.Pitch = 0.3f; m_Orbit.Yaw = 0.6f; m_Orbit.Distance = 60.0f; m_Orbit.MinDistance = 0.6f; m_Orbit.MaxDistance = 600.0f;
		m_Orbit.Keys = OrbitController::KeyScheme::Walk;   // WASD walks over the ground, Q/E turn, the wheel zooms towards the cursor
		m_Orbit.Ground = [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; };
		m_Orbit.CursorPoint = [this](glm::vec3& p) { return GroundUnderCursor(p); };
		m_Camera.FarPlane = 1500.0f;

		{
			AudioConfig audio;
			audio.Device = !headless;   // headless: no output at all; the editor mixes in step with the simulation (audio_record)
			m_Audio = std::make_unique<AudioEngine>(audio);
			m_Audio->Library().SetAssetDir(GB_ASSET_DIR);
			m_Steps = std::make_unique<FootstepSounds>(*m_Audio);
			m_Steps->SetSurfaceFunction([this](const glm::vec3& p) -> Surface
			{
				if (!m_World.IsBuilt()) return Surface::Grass;
				switch (m_World.World().BiomeAt(p.x, p.z))
				{
				case Biome::Water: return Surface::Water;
				case Biome::Shore: case Biome::Dunes: return Surface::Sand;
				case Biome::Forest: return Surface::Dirt;
				case Biome::Rock: return Surface::Stone;
				case Biome::Snow: return Surface::Snow;
				default: return Surface::Grass;
				}
			});
		}
		RegisterGardenContent(m_Gameplay.Library);
		// The game's content is the pack it plays (GamePack(): assets/data/era1.pack.json lists every file): every section of it goes through the
		// loaders registered below, all or nothing. There is no catalogue in the code (ShippedContent.h); the check is only reported: a gap must not
		// stop the editor.
		ContentTargets content;   // where each section goes: this editor's systems (the list of sections is GameContent's)
		content.Items = &m_Items; content.Recipes = &m_Recipes; content.Nodes = &m_NodeDefs; content.Techs = &m_Techs; content.Buildings = &m_Buildings.Defs;
		content.Roles = &m_Tribe.Config.Roles; content.Agenda = &m_Tribe.Config.Agenda; content.Economy = &m_Tribe.Config.Economy; content.Work = &m_Tribe.Config.Work;
		content.Explore = &m_Tribe.Config.Explore; content.Econ = &m_Tribe.Config.Econ; content.Combat = &m_Combat.Rules;
		content.Player = &m_Tribe.Config.Player; content.Controls = &m_Controls; content.Interactives = &m_Interactives.Defs; content.Species = &m_Ecology.Species;
		content.Cameras = &m_GameView.Params; content.Social = &m_Tribe.Config.Social; content.Society = &m_Tribe.Config.Society; content.Diplomacy = &m_Clans.Rules;
		content.Nature = &m_Nature;
		content.Trails = &m_Tribe.Config.Trails;   // T5c
		content.Needs = &m_Tribe.Config.DataNeeds;   // V5
		AddContentSections(m_Packs, content);
		AddVisualContentSection(m_Packs, m_Visuals.Library);   // the "visuals" section is the visual layer's (the simulation does not read it)
		{
			ContentReport report;
			if (const std::string error = m_Packs.Load(GamePack(), m_AssetRoot + "/data", &report); !error.empty()) GB_CLIENT_WARN("content not loaded: {}", error);
			for (const std::string& p : report.Problems) GB_CLIENT_WARN("content check: {}", p);
			for (const std::string& p : UnplacedNodes(m_NodeDefs, m_Nature)) GB_CLIENT_WARN("content check: {}", p);
		}
		m_Tribe.SetInteractives(&m_Interactives.Defs);   // the tribe's campfire is the fire pit of the data
		m_Tribe.SetTechs(&m_Techs);   // H11: the members know the technologies of the start and find, watch and teach the rest
		m_Clans.Add("", &m_Tribe);   // H13: the first tribe is clan 0 (the others come with `clan new`)
		m_Clans.FoundClan = [this](ClanId parent, const glm::vec2& camp, const std::string& name) { return FoundClan(parent, camp, name); };   // N7: a split founds a clan
		m_Tribe.SetBuildings(&m_Buildings.Defs);         // and it builds what the data says (H8)
		m_Tribe.SetGround([this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; });
		m_Tribe.SetWellDigger([this](const std::string& name, const glm::vec2& at) { PlaceWell(name, at); });   // a big village digs more wells
		RegisterBuildingContent(m_Gameplay.Library);     // the ability Build
		m_Hostility.FromRules(m_Combat.Rules);
		m_ClanHostility.Bind(&m_Clans, &m_Hostility);   // H14: hostility between clans is the diplomacy's (H13); predators and the table are the rest
		m_Tribe.SetCombat(&m_Combat, &m_ClanHostility);   // the clan fights, flees, hunts, tends its wounded and raids (every clan: StepCombat)

		RegisterResourceContent(m_Gameplay.Library, m_NodeDefs);
		RegisterInteractiveContent(m_Gameplay.Library, m_Interactives.Defs);
		RegisterNeedContent(m_Gameplay.Library);   // a saved agent may carry Need.* effects: the library must know them before any load
		m_Player.SetAbilities(&m_Gameplay, m_AbilityActor);
		m_Player.Attach(&m_Actors);
		m_Actors.Bind(&m_SceneEditor.World(), &m_Gameplay);
		RegisterCommands();
		m_SceneEditor.Bind(m_Props);
		m_SceneEditor.Register(m_Commands);
		RegisterSaveCommands();
		RegisterAnimationCommands();
		RegisterVisualCommands();
		RegisterTrailCommands();   // T5c: `trails` (TrailsView.cpp)
		RegisterGameCommands();   // H15: `play` and `game` (GameMode.cpp)
		RegisterSocialCommands();   // social life: `social`, `rite`, `belief`, `hunt` (EditorSocial.cpp)
		m_EntityRenderer = std::make_unique<EntityRenderer>(m_AssetRoot);
		m_EntityRenderer->SetPropBuilder([this](const PropComponent& p, const glm::mat4& m, Scene& s) { return m_World.AddProp(p, m, s); });
		BindVisuals();
		BuildWorld(m_WorldDesc);
		RegisterProperties();

		m_StrictScript = !options.GetValue("strict").empty();
		const std::string quality = options.GetValue("quality");
		if (!quality.empty()) SetQuality(quality);
		const std::string telemetry = options.GetValue("telemetry");
		if (!telemetry.empty()) { const CommandResult r = TelemetryCommand({ "start", telemetry }); if (!r.Ok) GB_CLIENT_ERROR("{}", r.Message); }
		const std::string exec = options.GetValue("exec");
		if (!exec.empty()) RunScript(exec);
		const std::string script = options.GetValue("script");
		if (!script.empty())
		{
			std::ifstream in(script);
			if (!in) GB_CLIENT_ERROR("Could not read the script '{}'", script);
			else { std::stringstream ss; ss << in.rdbuf(); RunScript(ss.str()); }
		}
		// H15: `Editor --play` (or --play="alpine 7 5 3") starts a game before anything else
		if (options.HasFlag("play") || !options.GetValue("play").empty()) RunScriptFirst("play " + options.GetValue("play"));
		GB_CLIENT_INFO("GB Editor ready. Type 'help' in the console for the commands.");
	}

	EditorApp::~EditorApp()
	{
		// the scene's GPU objects must go before the context does
		m_Instance.reset();
		m_World.Clear(m_Scene);
		if (m_LogSink)
			GB::Log::RemoveSink(m_LogSink);
	}

	bool EditorApp::IsPanelOpen(const std::string& name) const
	{
		if (name == "Viewport") return m_Panels.Viewport;
		if (name == "Hierarchy") return m_Panels.Hierarchy;
		if (name == "Inspector") return m_Panels.Inspector;
		if (name == "Properties") return m_Panels.Properties;
		if (name == "World") return m_Panels.World;
		if (name == "Stats") return m_Panels.Stats;
		if (name == "Console") return m_Panels.Console;
		if (name == "Assets") return m_Panels.Assets;
		if (name == "Model") return m_Panels.Model;
		if (name == "Animation") return m_Panels.Animation;
		if (name == "Abilities") return m_Panels.Abilities;
		if (name == "Tribe") return m_Panels.Tribe;
		if (name == "World state") return m_Panels.WorldState;
		if (name == "Timeline") return m_Panels.Timeline;
		if (name == "AnimBindings") return m_Panels.AnimBindings;
		if (name == "Graph") return m_Panels.Graph;
		if (name == "Tests") return m_Panels.Tests;
		if (name == "QA") return m_Panels.QA;
		if (name == "Priority Graph") return m_ShowPriorityGraph;
		return false;
	}

	// ------------------------------------------------------------------ world and environment
	void EditorApp::RebaseWeather()
	{
		// The weather re-derives the scene's environment from a base every frame: give it the current base back
		// and forget what its own effects added (particles, lights), then capture the new scene.
		m_Scene.Env = m_Weather->HasBase() ? m_Weather->BaseEnvironment() : m_Scene.Env;
		m_Scene.Sun = m_Weather->HasBase() ? m_Weather->BaseSun() : m_Scene.Sun;
		m_Scene.Particles.clear();
		m_Scene.ParticleBatches.clear();
		m_Scene.PointLights.clear();
		m_Weather->SetGroundFunction(m_World.IsBuilt() ? m_World.GroundFunction() : nullptr);
		m_Weather->SetBase(m_Scene);
	}

	void EditorApp::ApplyGameSeeds()
	{
		const uint64_t master = MasterSeedOf(m_WorldDesc.Seed);
		GB::ApplyGameSeeds(master, { &m_Techs.Rules, &m_Tribe.Config, &m_Clans, &m_Combat.Rules, &m_Ecology });
		m_Weather->Reseed(StreamSeed(master, SeedStream::Weather));
	}

	void EditorApp::BuildWorld(const WorldDesc& desc)
	{
		BuildWorld(WorldGenerator::Generate(desc));
	}

	void EditorApp::BuildWorld(GeneratedWorld&& world)
	{
		m_Actors.Clear();   // they point into the old terrain
		m_Player.Stop();
		m_WorldDesc = world.Desc;
		m_Values.Seed = world.Desc.Seed;   // V2: the cultures of this world (GB/Game/Values.h)
		m_Clans.SetCultureSeed(world.Desc.Seed);   // V4: the same cultures shape what each clan builds
		ApplyGameSeeds();   // T0: the seeds of every system come from the world's (m_WorldDesc, set above)
		m_WildResources = 0.0f;
		m_Instance.reset();
		m_SceneEditor.RemoveEnvironment();   // the old trees, rocks and wild resource nodes go with the old world; what the user added stays
		m_World.Build(std::move(world), m_Scene);
		m_StaticObjects = m_Scene.Objects.size();   // the terrain; entities are appended every frame
		m_SceneEditor.PopulateEnvironment(m_World.World().Trees, m_World.World().Rocks);
		// the actors' host (ground, water, obstacles) is made again for this world: the old one held the freed terrain, and the QA monitor and the
		// next spawn read it (a use after free: the crashes right after "World generation completed")
		m_Actors.SetHost(PlayerMode::MakeHost(m_World, m_SceneEditor, m_Steps.get(), &m_Gameplay));
		m_Scene.Env.ShadowCenter = m_Orbit.Target;
		m_SceneEditor.SetSpawnPoint(m_Orbit.Target);
		RebaseWeather();
		FrameWorld();
		if (m_Props.All().size() > 0)
			RegisterWaterProperties();
	}

	bool EditorApp::RequestWorldBuild(const WorldDesc& desc)
	{
		PollWorldBuild();
		if (m_WorldBuildPending)
		{
			GB_CLIENT_WARN("World generation already in progress; request ignored");
			return false;
		}
		m_WorldBuildPending = true;
		m_WorldBuild = std::async(std::launch::async, [desc]()
		{
			GeneratedWorld world = WorldGenerator::GenerateData(desc);
			if (world.TerrainPtr) world.TerrainPtr->PrepareRenderData();   // (the CPU half of the mesh and textures here too: the main thread only uploads them)
			TreeFactory::PrepareTextures();
			return world;
		});
		GB_CLIENT_INFO("World generation started in background (seed {})", desc.Seed);
		return true;
	}

	std::string EditorApp::SimStatsLine() const
	{
		static const char* const kNames[kSimParts] = { "actors", "gameplay", "garden+resources", "world (ecology, seasons)", "interactives+buildings", "combat", "clans", "qa" };
		double total = 0.0;
		for (const double ms : m_SimPartMean) total += ms;
		char text[512];
		int n = std::snprintf(text, sizeof text, "sim tick %.2f ms (mean of 300 ticks):", total);
		for (int k = 0; k < kSimParts && n > 0 && n < static_cast<int>(sizeof text); ++k) n += std::snprintf(text + n, sizeof text - static_cast<size_t>(n), "%s %s %.2f", k ? "," : "", kNames[k], m_SimPartMean[k]);
		return text;
	}

	void EditorApp::FinishWorldBuild()
	{
		if (!m_WorldBuildPending || !m_WorldBuild.valid()) return;
		m_WorldBuild.wait();
		PollWorldBuild();
	}

	void EditorApp::PollWorldBuild()
	{
		if (!m_WorldBuildPending || !m_WorldBuild.valid()) return;
		if (m_WorldBuild.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

		try
		{
			GeneratedWorld world = m_WorldBuild.get();
			m_WorldBuildPending = false;
			const auto start = std::chrono::steady_clock::now();
			BuildWorld(std::move(world));   // (on this thread: the GPU side and the scene; the window waits for it)
			GB_CLIENT_INFO("World generation completed (seed {}); built into the scene in {:.1f} s", m_WorldDesc.Seed, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
		}
		catch (const std::exception& error)
		{
			m_WorldBuildPending = false;
			GB_CLIENT_ERROR("World generation failed: {}", error.what());
		}
	}

	void EditorApp::ResetWorld()
	{
		m_WildResources = 0.0f;
		m_Actors.Clear();
		m_Player.Stop();
		m_Instance.reset();
		m_Model.reset();
		m_Props.RemovePrefix("model.");
		m_Props.Set("tod.enabled", "false");
		m_Props.Set("weather.preset", "clear");
		m_Weather->Snap();
		m_World.Clear(m_Scene);
		m_Actors.SetHost(PlayerMode::MakeHost(m_World, m_SceneEditor, m_Steps.get(), &m_Gameplay));   // (not the freed terrain: see BuildWorld)
		m_StaticObjects = 0;
		m_EnvMap.reset();
		// The weather owns the sky: write the defaults into its base (the property registry points there), then rebase.
		m_Weather->BaseEnvironment() = Environment{};
		m_Weather->BaseEnvironment().ShadowRadius = 30.0f;
		m_Weather->BaseSun() = DirectionalLight{};
		RebaseWeather();
		RegisterWaterProperties();
		m_Orbit.Target = { 0.0f, 1.0f, 0.0f };
		m_Orbit.Distance = 12.0f;
		m_Orbit.Pitch = 0.3f;
		m_Orbit.Yaw = 0.6f;
	}

	CommandResult EditorApp::LaunchSandbox(const std::string& scene)
	{
#ifdef GB_SANDBOX_EXE
		const fs::path exe = GB_SANDBOX_EXE;
		if (!fs::exists(exe)) return { false, "Sandbox not built: " + exe.string() };
#ifdef _WIN32
		const std::string command = "cd /d \"" + exe.parent_path().string() + "\" && start \"\" \"" + exe.string() + "\" --scene=" + scene;
#else
		const std::string command = "(cd \"" + exe.parent_path().string() + "\" && \"" + exe.string() + "\" --scene=" + scene + " &)";
#endif
		if (std::system(command.c_str()) != 0) return { false, "could not start the Sandbox" };
		return { true, "opened the Sandbox scene '" + scene + "' in its own window" };
#else
		(void)scene;
		return { false, "this editor was built without the Sandbox" };
#endif
	}

	void EditorApp::FrameWorld()
	{
		if (!m_World.IsBuilt()) return;
		const glm::vec2 s = m_World.World().Spawn;
		m_Orbit.Target = { s.x, m_World.GetTerrain().GetHeight(s.x, s.y) + 2.0f, s.y };
		m_Orbit.Distance = 40.0f;
		m_Orbit.Pitch = 0.3f;
	}

	// The terrain under the mouse: a march along the view ray (steps grow with the distance) refined by bisection. False when the mouse is off the
	// viewport or the ray misses the ground (looking at the sky).
	bool EditorApp::GroundUnderCursor(glm::vec3& out)
	{
		if (!m_World.IsBuilt() || !m_ViewportHovered || m_ViewportH == 0) return false;
		const Terrain& terrain = m_World.GetTerrain();
		const Ray ray = ScreenRay(m_Camera, m_CursorNorm, static_cast<float>(m_ViewportW) / static_cast<float>(m_ViewportH));
		auto above = [&](float t) { const glm::vec3 p = ray.Origin + ray.Dir * t; return p.y - terrain.GetHeight(p.x, p.z); };
		float previous = 0.0f;
		if (above(previous) < 0.0f) return false;   // the camera is under the ground
		for (float t = 0.5f; t < 1500.0f; t += std::max(0.5f, t * 0.02f))
		{
			if (above(t) < 0.0f)
			{
				float lo = previous, hi = t;
				for (int i = 0; i < 12; ++i) { const float mid = 0.5f * (lo + hi); (above(mid) < 0.0f ? hi : lo) = mid; }
				out = ray.Origin + ray.Dir * (0.5f * (lo + hi));
				return true;
			}
			previous = t;
		}
		return false;
	}

	// camera free (walk over the ground), top (straight down, WASD still walks) and follow (orbit a tribe member, the wheel zooms, drag turns).
	CommandResult EditorApp::SetCamMode(CamMode mode, const std::string& member)
	{
		if (m_GameView.Active()) m_GameView.Command({ "off" }, m_Actors, m_Player, m_World, m_Camera);   // an editor camera takes the view back
		if (mode == CamMode::Follow)
		{
			std::string name = member;
			if (name.empty()) name = m_AbilityActor;
			bool found = false;
			for (const TribeMember& m : m_Tribe.Members()) if (m.Name == name && !m.Dead) found = true;
			if (!found)   // the selected actor is not a tribe member: the first one alive
				for (const TribeMember& m : m_Tribe.Members()) if (!m.Dead) { name = m.Name; found = true; break; }
			if (!found) return { false, "no tribe member to follow (tribe build [model.fbx])" };
			m_FollowMember = name;
			m_Orbit.SetTopDown(false);
			m_Orbit.Following = true;
			m_Orbit.Distance = std::min(m_Orbit.Distance, 14.0f);
			m_Orbit.Pitch = 0.35f;
			m_CamMode = mode;
			return { true, "camera follows " + name + " (drag to orbit, wheel to zoom, camera free to leave)" };
		}
		m_Orbit.Following = false;
		m_Orbit.SetTopDown(mode == CamMode::TopDown);
		if (mode == CamMode::TopDown) m_Orbit.Distance = std::max(m_Orbit.Distance, 40.0f);
		m_CamMode = mode;
		return { true, mode == CamMode::TopDown ? "top-down camera (WASD walk, Q/E turn, wheel zoom)" : "free camera (WASD walk, Q/E turn, wheel zoom to the cursor)" };
	}

	void EditorApp::ApplyTimeOfDay()
	{
		if (!m_Tod.Enabled) return;
		SkyDesc d;
		d.Hours = std::fmod(m_Tod.Hours, 24.0f);
		d.SunAzimuthDegrees = m_Tod.Azimuth;
		d.CloudCover = m_Tod.Clouds;
		d.Haze = m_Tod.Haze;
		const SkyState st = Sky::Compute(d, m_TodBaseEnv, m_TodBaseSun);
		Environment& e = m_Weather->BaseEnvironment();
		e.SkyTop = st.Env.SkyTop; e.SkyHorizon = st.Env.SkyHorizon; e.SkyGround = st.Env.SkyGround; e.Intensity = st.Env.Intensity;
		e.FogColor = st.Env.FogColor; e.Stars = st.Env.Stars; e.CloudCover = st.Env.CloudCover; e.Exposure = st.Env.Exposure;
		DirectionalLight& s = m_Weather->BaseSun();
		s.Direction = st.Sun.Direction; s.Color = st.Sun.Color; s.Intensity = st.Sun.Intensity; s.CastShadows = st.Sun.CastShadows;
	}

	void EditorApp::SetQuality(const std::string& name)
	{
		PostSettings& p = m_Scene.Post;
		const PostSettings fresh;
		const bool minimum = name == "minimum";
		if (!minimum && name != "low" && name != "medium" && name != "high" && name != "ultra") { GB_CLIENT_WARN("Unknown quality '{}' (minimum, low, medium, high, ultra)", name); return; }
		p = fresh;
		// `minimum` (H19) is for slow GPUs: `low` (no post) plus a smaller render target scaled up to the viewport, no MSAA, a smaller shadow
		// map, no water reflections, and trees, rocks and grass drawn less far. Leaving it puts those back; the other presets only touch the post.
		if (minimum) { m_Scene.ScreenSpaceReflections = false; m_Scene.DrawDistance = 110.0f; m_Scene.GrassDistanceScale = 0.5f; m_RenderScale = 0.6f; }
		else if (m_MinimumQuality) { m_Scene.ScreenSpaceReflections = true; m_Scene.DrawDistance = 0.0f; m_Scene.GrassDistanceScale = 1.0f; m_RenderScale = 1.0f; }
		if (minimum != m_MinimumQuality)
		{
			// MSAA and the shadow map size are fixed when the renderer is made: make it again (the targets are resized on the next frame).
			m_Renderer = std::make_unique<Renderer>(m_ViewportW, m_ViewportH, minimum ? 1u : 4u, minimum ? 1024u : 2048u);
			m_ViewportFb = std::make_unique<Framebuffer>(1u, 1u);   // forces OnRender to size both to the scaled viewport
			m_MinimumQuality = minimum;
		}
		m_Visuals.Cache().SetLowRes(minimum);   // the procedural textures at a quarter of the size
		if (name == "low" || minimum) {}
		else if (name == "medium") { p.SSAO = 0.4f; }
		else if (name == "high") { p.SSAO = 0.55f; p.Grain = 0.15f; }
		else if (name == "ultra") { p.SSAO = 0.75f; p.AutoExposure = true; p.Grain = 0.25f; p.ChromaticAberration = 0.2f; p.Saturation = 1.08f; p.Contrast = 1.06f; }
		m_Actors.SetAnimLod(AnimLodForQuality(name));   // H16: how far the characters keep their full animation
		m_Visuals.NodeDrawDistance = minimum ? 90.0f : name == "low" ? 120.0f : name == "medium" ? 170.0f : name == "high" ? 240.0f : 0.0f;   // H16: resource nodes culled by distance
		GB_CLIENT_INFO("Quality '{}'", name);
	}

	// ------------------------------------------------------------------ properties
	void EditorApp::RegisterProperties()
	{
		Environment& env = m_Weather->BaseEnvironment();
		DirectionalLight& sun = m_Weather->BaseSun();
		PropertyRegistry& r = m_Props;

		r.Vec3("sun.direction", sun.Direction, -1.0f, 1.0f, "Sun", "direction the light travels");
		r.Color("sun.color", sun.Color, "Sun");
		r.Float("sun.intensity", sun.Intensity, 0.0f, 30.0f, "Sun");
		r.Bool("sun.shadows", sun.CastShadows, "Sun");
		r.Float("shadow.radius", env.ShadowRadius, 5.0f, 400.0f, "Sun", "the shadow map covers a sphere of this radius around the target");

		r.Bool("tod.enabled", m_Tod.Enabled, "Time of day", "sun/moon, sky, stars and clouds follow the hour").OnChange = [this]()
		{
			if (m_Tod.Enabled) { m_TodBaseEnv = m_Weather->BaseEnvironment(); m_TodBaseSun = m_Weather->BaseSun(); }
			else
			{
				Environment& e = m_Weather->BaseEnvironment();
				e.SkyTop = m_TodBaseEnv.SkyTop; e.SkyHorizon = m_TodBaseEnv.SkyHorizon; e.SkyGround = m_TodBaseEnv.SkyGround; e.Intensity = m_TodBaseEnv.Intensity;
				e.FogColor = m_TodBaseEnv.FogColor; e.Stars = m_TodBaseEnv.Stars; e.CloudCover = m_TodBaseEnv.CloudCover; e.Exposure = m_TodBaseEnv.Exposure;
				m_Weather->BaseSun() = m_TodBaseSun;
			}
		};
		r.Float("tod.hours", m_Tod.Hours, 0.0f, 24.0f, "Time of day", "6 = sunrise, 18 = sunset");
		r.Float("tod.azimuth", m_Tod.Azimuth, 0.0f, 360.0f, "Time of day");
		r.Float("tod.clouds", m_Tod.Clouds, 0.0f, 1.0f, "Time of day");
		r.Float("tod.haze", m_Tod.Haze, 0.0f, 1.0f, "Time of day");

		r.Enum("weather.preset", m_WeatherPreset, std::vector<std::string>(std::begin(kWeatherNames), std::end(kWeatherNames)), "Weather").OnChange = [this]()
		{
			m_Weather->SetPreset(kWeatherPresets[m_WeatherPreset]);
			GB_CLIENT_INFO("Weather: {}", kWeatherNames[m_WeatherPreset]);
		};
		r.Color("sky.top", env.SkyTop, "Sky and fog");
		r.Color("sky.horizon", env.SkyHorizon, "Sky and fog");
		r.Color("sky.ground", env.SkyGround, "Sky and fog");
		r.Float("sky.intensity", env.Intensity, 0.0f, 4.0f, "Sky and fog", "ambient light from the sky");
		r.Float("sky.stars", env.Stars, 0.0f, 1.0f, "Sky and fog");
		r.Float("sky.clouds", env.CloudCover, 0.0f, 1.0f, "Sky and fog");
		r.Color("fog.color", env.FogColor, "Sky and fog");
		r.Float("fog.density", env.FogDensity, 0.0f, 0.08f, "Sky and fog");
		r.Float("fog.heightFalloff", env.FogHeightFalloff, 0.0f, 0.2f, "Sky and fog");
		r.Float("ground.wetness", env.Wetness, 0.0f, 1.0f, "Sky and fog", "puddles appear above ~0.3");
		r.Float("ground.snow", env.SnowCover, 0.0f, 1.0f, "Sky and fog");
		r.Vec3("wind", env.Wind, -30.0f, 30.0f, "Sky and fog", "m/s, grass and trees sway with it");

		r.Float("exposure", env.Exposure, 0.05f, 10.0f, "Exposure and bloom");
		r.Float("bloom.threshold", env.BloomThreshold, 0.0f, 5.0f, "Exposure and bloom");
		r.Float("bloom.intensity", env.BloomIntensity, 0.0f, 2.0f, "Exposure and bloom");
		r.Float("bloom.radius", env.BloomRadius, 0.2f, 3.0f, "Exposure and bloom");
		r.Float("vignette", env.Vignette, 0.0f, 1.0f, "Exposure and bloom");

		PostSettings& p = m_Scene.Post;
		r.Enum("post.tonemap", p.Mapper, { "aces", "neutral", "filmic" }, "Post-processing");
		r.Float("post.ssao", p.SSAO, 0.0f, 1.0f, "Post-processing", "ambient occlusion strength, 0 = off");
		r.Float("post.ssao.radius", p.SSAORadius, 0.1f, 3.0f, "Post-processing");
		r.Float("post.dof", p.DepthOfField, 0.0f, 8.0f, "Post-processing", "max blur in % of the screen height, 0 = off");
		r.Float("post.dof.focus", p.FocusDistance, 0.5f, 500.0f, "Post-processing");
		r.Float("post.dof.range", p.FocusRange, 0.1f, 200.0f, "Post-processing");
		r.Bool("post.autoexposure", p.AutoExposure, "Post-processing");
		r.Float("post.autoexposure.speed", p.AdaptSpeed, 0.1f, 8.0f, "Post-processing");
		r.Float("post.saturation", p.Saturation, 0.0f, 2.0f, "Post-processing");
		r.Float("post.contrast", p.Contrast, 0.5f, 2.0f, "Post-processing");
		r.Color("post.tint", p.Tint, "Post-processing");
		r.Float("post.ca", p.ChromaticAberration, 0.0f, 1.0f, "Post-processing");
		r.Float("post.grain", p.Grain, 0.0f, 1.0f, "Post-processing");
		r.Bool("render.ssr", m_Scene.ScreenSpaceReflections, "Post-processing", "water reflects nearby objects");
		r.Float("render.grass", m_Scene.GrassDistanceScale, 0.0f, 2.0f, "Post-processing", "how far the grass is drawn (x its draw distance; 0 = none)");
		r.Bool("render.actors", m_DrawActors, "Post-processing", "the characters are drawn");
		r.Bool("ui.stats", m_ShowRenderStats, "Post-processing", "draw calls, triangles and the GPU and CPU time of each part, over the viewport");
		r.Float("sim.budget", m_SimBudgetMs, 0.0f, 100.0f, "Simulation", "milliseconds a frame the simulation may take before the world runs slower than the clock (0 = no limit)");

		r.Float("camera.fov", m_Camera.FovYDegrees, 10.0f, 120.0f, "Camera");
		r.Float("camera.near", m_Camera.NearPlane, 0.02f, 5.0f, "Camera");
		r.Float("camera.far", m_Camera.FarPlane, 50.0f, 5000.0f, "Camera");
		r.Float("orbit.distance", m_Orbit.Distance, 0.5f, 600.0f, "Camera");
		r.Float("orbit.yaw", m_Orbit.Yaw, -6.4f, 6.4f, "Camera");
		r.Float("orbit.pitch", m_Orbit.Pitch, -1.5f, 1.5f, "Camera");
		r.Vec3("orbit.target", m_Orbit.Target, -2000.0f, 2000.0f, "Camera");
		RegisterWaterProperties();
	}

	void EditorApp::RegisterWaterProperties()
	{
		m_Props.RemovePrefix("water.");
		auto add = [&](const char* name, WaterSurface* w)
		{
			if (!w) return;
			const std::string p = std::string("water.") + name + ".";
			WaterParams& q = w->Params;
			for (const WaterParamField& field : kWaterParamFields)   // the names and ranges live with WaterParams (GB/World/Water/WaterParamInfo.h)
				m_Props.Float(p + field.Name, q.*field.Member, field.Min, field.Max, "Water");
			m_Props.Color(p + "shallow", q.ShallowColor, "Water");
			m_Props.Color(p + "deep", q.DeepColor, "Water");
		};
		add("lake", m_World.Lake());
		add("river", m_World.River());
	}

	// ------------------------------------------------------------------ commands
	CommandResult EditorApp::Execute(const std::string& line)
	{
		m_SceneEditor.SetSpawnPoint(m_Orbit.Target);
		if (m_Recorder.Active() && !RecordIfSim(line)) return { false, "not while recording a replay (it would not play back the same): replay stop <file> first" };
		if (const auto words = CommandRunner::Tokenize(line); !words.empty() && (words[0] == "scene_save" || (words[0] == "scene" && words.size() > 1 && words[1] == "save")))
			m_Gameplay.Store(m_SceneEditor.World());   // the file keeps the live state of the garden and the characters
		const CommandResult r = m_Commands.Execute(line);
		m_SceneEditor.Tick(m_GizmoAxis >= 0);   // an edit made by `set sel.*` is its own undo step
		if (const auto words = CommandRunner::Tokenize(line); !words.empty() && r.Ok)
		{
			const std::string& w = words[0];
			if (w == "scene_load" || w == "new_scene" || (w == "scene" && words.size() > 1 && (words[1] == "load" || words[1] == "new")))
				SyncGameplay();   // the entities are not the ones the ability systems came from
		}
		if (!line.empty() && !r.Message.empty())
		{
			if (r.Ok) GB_CLIENT_INFO("{}", r.Message);
			else GB_CLIENT_ERROR("{}", r.Message);
		}
		return r;
	}

	void EditorApp::RunScript(const std::string& text)
	{
		for (const std::string& line : CommandRunner::SplitScript(text))
			m_Script.push_back(line);
	}

	void EditorApp::RunScriptFirst(const std::string& text)
	{
		const std::vector<std::string> lines = CommandRunner::SplitScript(text);
		m_Script.insert(m_Script.begin(), lines.begin(), lines.end());
	}

	void EditorApp::RegisterCommands()
	{
		CommandRunner& c = m_Commands;
		RegisterTestCommands(m_Commands, [this](const std::string& text) { RunScriptFirst(text); }, [this](const std::string& scene) { return LaunchSandbox(scene); });
		c.Register("help", "help [command]: lists the commands, or explains one", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty())
			{
				const std::string h = m_Commands.Help(a[0]);
				return h.empty() ? CommandResult{ false, "no command '" + a[0] + "'" } : CommandResult{ true, h };
			}
			std::string out = "commands:";
			for (const std::string& n : m_Commands.Names()) out += "\n  " + m_Commands.Help(n);
			return { true, out };
		});
		c.Register("list", "list [prefix]: the properties you can get/set", [this](const std::vector<std::string>& a) -> CommandResult
		{
			std::string out;
			for (const std::string& p : m_Props.List(a.empty() ? "" : a[0])) out += (out.empty() ? "" : "\n") + p + " = " + m_Props.Get(p);
			return { true, out.empty() ? "no properties" : out };
		});
		c.Register("get", "get <property>: prints a value", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: get <property>" };
			if (!m_Props.Find(a[0])) return { false, "unknown property '" + a[0] + "'" };
			return { true, a[0] + " = " + m_Props.Get(a[0]) };
		});
		c.Register("set", "set <property> <value...>: changes a value (numbers, true/false, enum names, 'r g b' or #rrggbb)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2) return { false, "usage: set <property> <value...>" };
			std::string value;
			for (size_t i = 1; i < a.size(); ++i) value += (i > 1 ? " " : "") + a[i];
			const SetResult r = m_Props.Set(a[0], value);
			return { r.Ok, r.Message };
		});
		c.Register("terrain_graph", "terrain_graph list | preset <name> [temperate|alpine|desert] [seed] [mesh_detail] [size] [resolution] [trees] [rocks] | apply: builds a world from a terrain graph (docs/GRAFO_TERRENO.md): a preset that ships with the engine (loaded into the Terrain Graph panel too), or the graph in the panel (apply). The world type picks the ground's palette and the tree and rock recipes; the graph decides everything else", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty() || a[0] == "list")
			{
				std::string names;
				for (const std::string& n : TerrainGraphPresetNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, "terrain graph presets: " + names };
			}
			if (!m_TgUi) m_TgUi = std::make_unique<TerrainGraphUi>();
			WorldDesc d = m_WorldDesc;
			d.GeneratorVersion = kWorldGeneratorLatest;
			if (a[0] == "preset")
			{
				TerrainGraph preset;
				if (a.size() < 2 || !MakeTerrainGraphPreset(a[1], preset)) return { false, "unknown preset (terrain_graph list)" };
				m_TgUi->LoadGraph(preset);
				if (a.size() > 2 && a[2] != "-")
				{
					const std::string t = Lower(a[2]);
					if (t == "temperate") d.Type = WorldType::Temperate;
					else if (t == "alpine") d.Type = WorldType::Alpine;
					else if (t == "desert") d.Type = WorldType::Desert;
					else return { false, "unknown world type '" + a[2] + "'" };
				}
				if (a.size() > 3 && a[3] != "-") d.Seed = static_cast<uint32_t>(std::atoi(a[3].c_str()));
				if (a.size() > 4 && a[4] != "-") d.MeshDetail = static_cast<float>(std::atof(a[4].c_str()));
				if (a.size() > 5 && a[5] != "-") d.Size = std::max(32.0f, static_cast<float>(std::atof(a[5].c_str())));
				if (a.size() > 6 && a[6] != "-") d.Resolution = static_cast<uint32_t>(std::clamp(std::atoi(a[6].c_str()), 65, 2049));
				if (a.size() > 7 && a[7] != "-") d.TreeBudget = std::max(0, std::atoi(a[7].c_str()));
				if (a.size() > 8 && a[8] != "-") d.RockBudget = std::max(0, std::atoi(a[8].c_str()));
			}
			else if (a[0] != "apply") return { false, "terrain_graph list | preset <name> [type] [seed] [mesh_detail] [size] [resolution] [trees] [rocks] | apply" };
			const std::vector<TgIssue> issues = ValidateTerrainGraph(*m_TgUi->Graph(), MakeDefaultTerrainNodeRegistry());
			if (HasTgErrors(issues)) return { false, "the terrain graph has errors: " + TgIssuesText(issues) };
			d.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(*m_TgUi->Graph());
			if (!RequestWorldBuild(d)) return { false, "world generation already in progress" };
			return { true, "world generation from the terrain graph '" + m_TgUi->Graph()->Id + "' started (seed " + std::to_string(d.Seed) + ")" };
		});
		c.Register("generate", "generate <temperate|alpine|desert> [seed] [trees] [rocks] [mesh_detail] [erosion] [generator] (a dash keeps an argument as it is; generator: 1 to the newest, to see the water of an older one): builds a new world (mesh_detail: metres, WorldDesc::MeshDetail, <= 0 off; erosion: 0/1, WorldDesc::Erosion, default on)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			WorldDesc d = m_WorldDesc;
			d.GeneratorVersion = kWorldGeneratorLatest;   // a new world uses the newest generator (a loaded game keeps the version it was made with)
			if (!a.empty())
			{
				const std::string t = Lower(a[0]);
				if (t == "temperate") d.Type = WorldType::Temperate;
				else if (t == "alpine") d.Type = WorldType::Alpine;
				else if (t == "desert") d.Type = WorldType::Desert;
				else return { false, "unknown world type '" + a[0] + "' (temperate, alpine, desert)" };
			}
			auto given = [&](size_t i) { return a.size() > i && a[i] != "-"; };   // "-" keeps the current value of that argument
			if (given(1)) d.Seed = static_cast<uint32_t>(std::atoi(a[1].c_str()));
			if (given(2)) d.TreeBudget = std::max(0, std::atoi(a[2].c_str()));
			if (given(3)) d.RockBudget = std::max(0, std::atoi(a[3].c_str()));
			if (given(4)) d.MeshDetail = static_cast<float>(std::atof(a[4].c_str()));
			if (given(5)) d.Erosion = std::atoi(a[5].c_str()) != 0;
			if (given(6))
			{
				const int version = std::atoi(a[6].c_str());
				if (version < static_cast<int>(kWorldGeneratorV1) || version > static_cast<int>(kWorldGeneratorLatest))
					return { false, "generator " + a[6] + " does not exist (1 to " + std::to_string(kWorldGeneratorLatest) + ")" };
				d.GeneratorVersion = static_cast<uint32_t>(version);   // an old generator makes the world it always made
			}
			if (!RequestWorldBuild(d))
				return { false, "world generation already in progress" };
			return { true, "world generation started in background (seed " + std::to_string(d.Seed) + ")" };
		});
		c.Register("time", "time <hours>: sets the hour of the day (0-24) and turns time of day on", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: time <hours>" };
			m_Props.Set("tod.enabled", "true");
			return { true, m_Props.Set("tod.hours", a[0]).Message };
		});
		c.Register("weather", "weather <clear|lightrain|rain|storm|snow|blizzard|dust|sandstorm|tornado> [snap]", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: weather <preset> [snap]" };
			const SetResult r = m_Props.Set("weather.preset", a[0]);
			if (r.Ok && a.size() > 1 && a[1] == "snap") m_Weather->Snap();
			return { r.Ok, r.Message };
		});
		c.Register("quality", "quality <minimum|low|medium|high|ultra>: post-processing preset (minimum also renders at 60% resolution without MSAA and draws less far, for slow GPUs)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: quality <minimum|low|medium|high|ultra>" };
			SetQuality(a[0]);
			return { true, "" };
		});
		c.Register("camera", "camera <yaw> <pitch> <distance> [x y z]: places the orbit camera (angles in degrees; y may be ground or ground+<m>: the terrain there). camera lake|river [yaw pitch distance]: frames the water of the world. camera free | top | follow [member]: walk over the ground with WASD (Q/E turn, Shift runs, the wheel zooms to the cursor), look straight down, or orbit a tribe member (keys F and T in the viewport). camera game follow [actor] | overview [x z [distance]] | observe [actor...|all] | off: the game cameras (H19)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty() && a[0] == "game")
				return m_GameView.Command(std::vector<std::string>(a.begin() + 1, a.end()), m_Actors, m_Player, m_World, m_Camera);
			if (!a.empty() && (a[0] == "lake" || a[0] == "river"))
			{
				// camera lake|river [yaw pitch distance]: frames the water body (reproducible shots for water QA)
				const Terrain& t = m_World.GetTerrain();
				glm::vec3 target;
				float distance;
				if (a[0] == "lake")
				{
					if (!t.HasLake()) return { false, "this world has no lake" };
					target = { t.GetLakeCenter().x, t.GetWaterLevel(), t.GetLakeCenter().y };
					distance = t.GetLakeRadius() * 2.4f;
				}
				else
				{
					const RiverData* r = t.GetRiver();
					if (!r) return { false, "this world has no river" };
					target = r->Points[r->Points.size() * 7 / 10];   // 70 % of the way down: where the river has widened, not its spring
					distance = 38.0f;
				}
				if (m_GameView.Active()) m_GameView.Command({ "off" }, m_Actors, m_Player, m_World, m_Camera);
				m_Orbit.Yaw = glm::radians(a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 35.0f);
				m_Orbit.Pitch = glm::radians(a.size() > 2 ? static_cast<float>(std::atof(a[2].c_str())) : 28.0f);
				m_Orbit.Distance = a.size() > 3 ? static_cast<float>(std::atof(a[3].c_str())) : distance;
				m_Orbit.Target = target;
				char where[96];
				std::snprintf(where, sizeof(where), " (target %.2f %.2f %.2f, distance %.1f)", static_cast<double>(target.x), static_cast<double>(target.y), static_cast<double>(target.z), static_cast<double>(m_Orbit.Distance));
				return { true, "camera on the " + a[0] + where };
			}
			if (!a.empty() && a[0] == "free") return SetCamMode(CamMode::Free);
			if (!a.empty() && (a[0] == "top" || a[0] == "topdown")) return SetCamMode(CamMode::TopDown);
			if (!a.empty() && a[0] == "follow") return SetCamMode(CamMode::Follow, a.size() > 1 ? a[1] : std::string());
			if (a.size() < 3) return { false, "usage: camera <yaw> <pitch> <distance> [x y z] | camera game ..." };
			if (m_GameView.Active()) m_GameView.Command({ "off" }, m_Actors, m_Player, m_World, m_Camera);   // placing the editor camera takes the view back
			m_Orbit.Yaw = glm::radians(static_cast<float>(std::atof(a[0].c_str())));
			m_Orbit.Pitch = glm::radians(static_cast<float>(std::atof(a[1].c_str())));
			m_Orbit.Distance = static_cast<float>(std::atof(a[2].c_str()));
			if (a.size() >= 6)
			{
				// y "ground" (or "ground+<m>"): the height of the terrain there, plus that much (an eye-level shot without knowing the ground's height)
				const float x = static_cast<float>(std::atof(a[3].c_str())), z = static_cast<float>(std::atof(a[5].c_str()));
				float y = static_cast<float>(std::atof(a[4].c_str()));
				if (a[4].rfind("ground", 0) == 0 && m_World.IsBuilt()) y = m_World.GetTerrain().GetHeight(x, z) + (a[4].size() > 7 ? static_cast<float>(std::atof(a[4].c_str() + 7)) : 0.0f);
				m_Orbit.Target = { x, y, z };
			}
			return { true, "camera placed" };
		});
		c.Register("player", "player [fox|cesium|<character.fbx>|off] [keys]: walk the world in third person (WASD, Shift runs, drag the mouse to look). An FBX character loads its folder's animations and walks with the locomotion blend. keys = a script like \"W:2,RW:3,:1\" for headless runs", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty() && (a[0] == "off" || a[0] == "stop"))
			{
				m_Player.Stop();
				return { true, "player mode off" };
			}
			if (a.empty() && m_Player.IsActive()) return { true, m_Player.Describe() };
			std::string error;
			m_Player.SetFootsteps(m_Steps.get());
			if (!m_Player.Start(a.empty() ? "fox" : a[0], m_World, m_SceneEditor, m_AssetRoot, m_Orbit.Target, error)) return { false, error };
			if (a.size() > 1) m_Player.SetScript(a[1]);
			return { true, m_Player.Describe() };
		});
		c.Register("player_goto", "player_goto <x> <z> [run]: an AI leads the player character to that point (same controller, animation and footsteps as the keyboard)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2) return { false, "usage: player_goto <x> <z> [run]" };
			if (!m_Player.IsActive()) return { false, "start the player first (player <fox|cesium|character.fbx>)" };
			m_Player.MoveTo({ static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())) }, a.size() > 2 && a[2] == "run");
			return { true, "going to (" + a[0] + ", " + a[1] + ")" };
		});
		c.Register("player_overlay", "player_overlay [name] [fade seconds]: switches the locomotion set of the FBX player (box, holding, wheelbarrow, free); no name lists them", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : m_Player.OverlayNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, "sets: " + names + " (default = free movement)" };
			}
			const float fade = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 0.3f;
			return m_Player.SetOverlay(a[0], fade) ? CommandResult{ true, "set " + a[0] } : CommandResult{ false, "no such set '" + a[0] + "'" };
		});
		c.Register("manifest_info", "manifest_info: the animation manifest (sets, montages, packs) and what the FBX player has clips for", [this](const std::vector<std::string>&) -> CommandResult { return { true, m_Player.ManifestInfo() }; });
		c.Register("manifest_load", "manifest_load <file.json>: replaces the animation manifest (rebuilds the FBX player's sets, turns, jump and montages)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: manifest_load <file.json>" };
			std::string report;
			const bool ok = m_Player.LoadManifestFile(a[0], report);
			return { ok, report };
		});
		c.Register("manifest_save", "manifest_save <file.json>: writes the animation manifest in use", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: manifest_save <file.json>" };
			std::string report;
			const bool ok = m_Player.SaveManifestFile(a[0], report);
			return { ok, report };
		});
		c.Register("player_montage", "player_montage [name [section]|stop]: plays a one-shot action (pick_fruit, water, milk_cow...) over the FBX player; no name lists them", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : m_Player.MontageNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, names.empty() ? "this character has no montages (put the farming animation FBX files next to it)" : "montages: " + names };
			}
			if (a[0] == "stop") { m_Player.StopMontage(); return { true, "stopped" }; }
			return m_Player.PlayMontage(a[0], a.size() > 1 ? a[1] : "") ? CommandResult{ true, "playing " + a[0] + (a.size() > 1 ? " from " + a[1] : "") } : CommandResult{ false, "no such montage '" + a[0] + "'" };
		});
		c.Register("player_attach", "player_attach [box|bag|tool|off]: the FBX player carries a prop (box in both hands with IK, a bag on the back, a tool in the hand); off lets go; no argument lists what is carried", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : m_Player.PropNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, names.empty() ? "carrying nothing (props: box, bag, tool)" : "carrying: " + names };
			}
			if (a[0] == "off") { m_Player.DetachProp(a.size() > 1 ? a[1] : ""); return { true, "let go" }; }
			return m_Player.AttachProp(a[0]) ? CommandResult{ true, "carrying " + a[0] } : CommandResult{ false, "cannot attach '" + a[0] + "' (box, bag, tool; the character needs the humanoid rig)" };
		});
		c.Register("player_look", "player_look <x> <y> <z> | camera | off: the FBX player turns its head toward a world point, the camera, or forward again", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.size() == 1 && a[0] == "off") { m_Player.ClearLook(); return { true, "looking forward" }; }
			if (a.size() == 1 && a[0] == "camera") { m_Player.LookAtCamera(true); return { true, "looking at the camera" }; }
			if (a.size() < 3) return { false, "usage: player_look <x> <y> <z> | camera | off" };
			m_Player.LookAtPoint({ static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) });
			return { true, "looking at (" + a[0] + ", " + a[1] + ", " + a[2] + ")" };
		});
		c.Register("player_feet", "player_feet on|off: the FBX player's feet follow the terrain with IK (on by default)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { true, m_Player.FootIKOn() ? "foot IK is on" : "foot IK is off" };
			m_Player.SetFootIK(a[0] != "off" && a[0] != "0");
			return { true, m_Player.FootIKOn() ? "foot IK on" : "foot IK off" };
		});
		c.Register("interact", "interact <entity> [point]: the player goes to an interaction point of an entity and uses it (underscores in names stand for spaces); no argument lists the points of the scene", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : ListInteractions(m_SceneEditor.World())) names += (names.empty() ? "" : ", ") + n;
				return { true, names.empty() ? "no entity has interaction points (entity interact <entity> <point> <montage> <x> <y> <z> <yaw>)" : "interactions: " + names };
			}
			if (!m_Player.IsActive()) return { false, "start the player first (player <character.fbx>)" };
			std::string error;
			return m_Player.Interact(a[0], a.size() > 1 ? a[1] : "", error) ? CommandResult{ true, "going to " + a[0] } : CommandResult{ false, error };
		});
		c.Register("actor", "actor spawn <name> [model.fbx|fox|cesium] [x z] [player|ai] | list | control <name> | tasks <name> <task list>|stop | remove <name> | sync: the characters of the world (the player is one of them; NPCs use the same controller, animation and abilities). player_* commands act on the controlled one", [this](const std::vector<std::string>& a) -> CommandResult
		{
			const std::string sub = a.empty() ? "list" : a[0];
			if (sub == "list")
			{
				if (m_Actors.Count() == 0) return { true, "no actors (actor spawn <name> [model.fbx] [x z] [player|ai])" };
				std::string s;
				for (const std::string& n : m_Actors.Names()) s += (n == m_Player.ControlledName() ? "* " : "  ") + m_Actors.Find(n)->Describe() + "\n";
				return { true, s };
			}
			if (sub == "sync") return { true, std::to_string(m_Actors.SyncEntities()) + " actor(s) written into their entities" };
			if (sub == "spawn")
			{
				FinishWorldBuild();
				if (a.size() < 2) return { false, "usage: actor spawn <name> [model.fbx|fox|cesium] [x z] [player|ai]" };
				std::string kind = "fox", mode;
				std::vector<float> numbers;
				for (size_t i = 2; i < a.size(); ++i)
				{
					const std::string l = Lower(a[i]);
					if (l == "player" || l == "ai") mode = l;
					else if (l == "fox" || l == "cesium" || PlayerMode::IsFbx(a[i])) kind = a[i];
					else numbers.push_back(static_cast<float>(std::atof(a[i].c_str())));
				}
				glm::vec3 where = m_Orbit.Target;
				if (numbers.size() >= 2) where = { numbers[0], 0.0f, numbers[1] };
				else if (m_Actors.Count() > 0) where.x += 1.5f * static_cast<float>(m_Actors.Count());   // a step aside of the others
				std::string error;
				if (m_Actors.Find(a[1])) return { false, "there is already an actor called '" + a[1] + "'" };
				const bool batch = m_SpawnBatch > 0;   // one of a crew: the actor and its own ability system only (SpawnBatch)
				if (!batch) m_Gameplay.Store(m_SceneEditor.World());   // (the systems are rebuilt from their components just below: keep what is running)
				Actor* actor = m_Player.Spawn(a[1], kind, m_World, m_SceneEditor, m_AssetRoot, where, m_Steps.get(), error, true);
				if (!actor) return { false, error };
				if (batch) m_Actors.PrepareAbilities();
				else
				{
					SyncGameplay();
					m_SceneEditor.Commit();
				}
				if (mode == "player" || (mode.empty() && !m_Player.IsActive())) m_Player.Control(a[1]);
				return { true, actor->Describe() + (m_Player.ControlledName() == a[1] ? " [player]" : " [ai]") };
			}
			if (a.size() < 2) return { false, "usage: actor " + sub + " <name>" };
			Actor* actor = m_Actors.Find(a[1]);
			if (!actor) return { false, "no actor called '" + a[1] + "' (actor list)" };
			if (sub == "control") return m_Player.Control(a[1]) ? CommandResult{ true, "you control " + a[1] } : CommandResult{ false, "cannot control " + a[1] };
			if (sub == "remove")
			{
				if (m_Player.ControlledName() == a[1]) m_Player.Stop();
				m_Actors.Remove(a[1]);
				return { true, "removed " + a[1] };
			}
			if (sub == "tasks")
			{
				if (a.size() == 2) return { true, actor->TaskStatus() };
				if (a.size() == 3 && a[2] == "stop") { actor->StopTasks(); return { true, "stopped" }; }
				std::string text;
				for (size_t i = 2; i < a.size(); ++i) text += (i > 2 ? " " : "") + a[i];
				std::string error;
				return actor->RunTasks(text, error) ? CommandResult{ true, a[1] + " is running: " + actor->TaskStatus() } : CommandResult{ false, error };
			}
			if (sub == "bag")
			{
				Inventory& bag = actor->Bag();
				const std::string verb = a.size() > 2 ? a[2] : "list";
				const int n = a.size() > 4 ? std::max(1, std::atoi(a[4].c_str())) : 1;
				if (verb == "add" && a.size() > 3)
				{
					const int got = bag.Add(m_Items, a[3], n);
					StepResources();   // tools are tags of the ability system: the next ability sees them at once
					return { got > 0, got > 0 ? "added " + std::to_string(got) + " " + a[3] : "cannot add " + a[3] + " (unknown item or no room)" };
				}
				if (verb == "remove" && a.size() > 3)
				{
					const int took = bag.Remove(a[3], n);
					StepResources();
					return { true, "removed " + std::to_string(took) + " " + a[3] };
				}
				if (verb == "craft" && a.size() > 3)
				{
					const std::string station = a.size() > 4 ? a[4] : "";
					const std::string why = m_Recipes.WhyNot(m_Items, bag, a[3], station);
					if (!why.empty()) return { false, why };
					m_Recipes.Craft(m_Items, bag, a[3], station);
					StepResources();
					return { true, "made " + a[3] };
				}
				std::string text;
				for (const ItemSlot& s : bag.Stacks()) text += (text.empty() ? "" : ", ") + s.Id + " x" + std::to_string(s.Count);
				std::string report = text.empty() ? a[1] + " carries nothing" : a[1] + " carries " + text;
				report += "\n  bag: " + CapacityText(m_Items, bag);
				for (const std::string& tool : ActiveTools(m_Items, bag)) report += "\n  tool " + tool;
				return { true, report };
			}
			return { false, "usage: actor spawn|list|control|tasks|remove|sync|bag <name> [add|remove|craft <item|recipe> [n|station]]" };
		});
		c.Register("player_tasks", "player_tasks <task list>|stop: the player does a list of tasks like an NPC (goto x z [run] | interact <entity> [point] | wait s | attach prop | detach [prop] | ability <name> [entity], separated by '|' or new lines)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive()) return { false, "start the player first (player <character.fbx>)" };
			if (a.empty()) return { true, m_Player.TaskStatus() };
			if (a.size() == 1 && a[0] == "stop") { m_Player.StopTasks(); return { true, "stopped" }; }
			std::string text;
			for (size_t i = 0; i < a.size(); ++i) text += (i ? " " : "") + a[i];
			std::string error;
			return m_Player.RunTasks(text, error) ? CommandResult{ true, "running: " + m_Player.TaskStatus() } : CommandResult{ false, error };
		});
		c.Register("retarget", "retarget <animations.fbx|folder>: applies the animations of another skeleton (any naming, proportions or rest pose) to the FBX player; the clips are added to it and its animator rebuilt", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { true, m_Player.RetargetReport().empty() ? "usage: retarget <animations.fbx|folder>" : m_Player.RetargetReport() };
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			std::string text;
			for (size_t i = 0; i < a.size(); ++i) text += (i ? " " : "") + a[i];
			std::string report;
			const bool ok = m_Player.Retarget(text, m_AssetRoot, report);
			return { ok, report };
		});
		c.Register("character_check", "character_check [character.fbx]: checks a character (the player's when no file is given): skeleton convention, bones, size and facing, clip roles, sets, montages and what is wrong", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty())
			{
				if (!m_Player.Profile()) return { false, "start the player with an FBX character first, or give a file: character_check <character.fbx>" };
				return { true, FormatCharacterProfile(*m_Player.Profile()) };
			}
			std::string error;
			const auto model = Actor::LoadCharacter(a[0], m_AssetRoot, error);
			if (!model) return { false, error };
			return { true, FormatCharacterProfile(AnalyzeCharacter(*model)) };
		});
		c.Register("character_export", "character_export <file.json> [character.fbx]: writes the character check as JSON (the player's when no character is given)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: character_export <file.json> [character.fbx]" };
			CharacterProfile profile;
			if (a.size() > 1)
			{
				std::string error;
				const auto model = Actor::LoadCharacter(a[1], m_AssetRoot, error);
				if (!model) return { false, error };
				profile = AnalyzeCharacter(*model);
			}
			else if (m_Player.Profile()) profile = *m_Player.Profile();
			else return { false, "start the player with an FBX character first, or give a file" };
			std::ofstream out(a[0]);
			if (!out) return { false, "could not write " + a[0] };
			out << CharacterProfileToJson(profile);
			return { true, "saved " + a[0] };
		});
		c.Register("panel", "panel <name> [on|off]: shows or hides a panel (Animation, Model, Stats, ...); panel Animation tab <Character|Animator|Montages|Sockets|IK> picks its tab", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: panel <name> [on|off]" };
			bool* flag = nullptr;
			const std::string n = a[0];
			if (n == "Viewport") flag = &m_Panels.Viewport; else if (n == "Hierarchy") flag = &m_Panels.Hierarchy; else if (n == "Inspector") flag = &m_Panels.Inspector;
			else if (n == "Properties") flag = &m_Panels.Properties; else if (n == "World") flag = &m_Panels.World; else if (n == "Stats") flag = &m_Panels.Stats;
			else if (n == "Console") flag = &m_Panels.Console; else if (n == "Assets") flag = &m_Panels.Assets; else if (n == "Model") flag = &m_Panels.Model;
			else if (n == "Animation") flag = &m_Panels.Animation; else if (n == "Timeline") flag = &m_Panels.Timeline; else if (n == "AnimBindings") flag = &m_Panels.AnimBindings; else if (n == "Graph") flag = &m_Panels.Graph; else if (n == "Abilities") flag = &m_Panels.Abilities; else if (n == "Tribe") flag = &m_Panels.Tribe; else if (n == "WorldState") flag = &m_Panels.WorldState; else if (n == "Tests") flag = &m_Panels.Tests; else if (n == "QA") flag = &m_Panels.QA;
			if (!flag) return { false, "no such panel '" + n + "' (Viewport, Hierarchy, Inspector, Properties, World, Stats, Console, Assets, Model, Animation, Timeline, AnimBindings, Graph, Abilities, Tribe, WorldState, Tests, QA)" };
			if (a.size() > 2 && a[1] == "tab") { if (n == "Timeline") m_TimelineFocus = 3; else if (n == "Graph") m_GraphFocus = 3; else m_AnimTabRequest = a[2]; *flag = true; return { true, n + ": tab " + a[2] }; }
			*flag = a.size() > 1 ? (a[1] != "off" && a[1] != "0") : !*flag;
			if (*flag) { if (n == "Tribe") m_FocusTribe = 20; else if (n == "WorldState") m_FocusWorldState = 20; else if (n == "Abilities") m_FocusAbilities = 20; else if (n == "QA") m_FocusQA = 20; }   // to the front of its dock
			return { true, n + (*flag ? " shown" : " hidden") };
		});
		c.Register("player_ragdoll", "player_ragdoll [vx vy vz]: the FBX player collapses into a ragdoll (bodies and joints in a physics world made of the same ground, water, trees and rocks), keeping its speed plus the given one; it gets up by itself after a pause", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			glm::vec3 v(0.0f);
			if (a.size() >= 3) v = { static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) };
			return m_Player.StartRagdoll(v) ? CommandResult{ true, "ragdoll" } : CommandResult{ false, "this character cannot be a ragdoll (it needs the humanoid rig), or it already is" };
		});
		c.Register("player_getup", "player_getup: the ragdoll stands up where it lies (facing the way its head points) and blends back to the animation", [this](const std::vector<std::string>&) -> CommandResult
		{
			if (!m_Player.Ragdolled()) return { false, "the player is not a ragdoll" };
			m_Player.GetUp();
			return { true, "getting up" };
		});
		c.Register("player_blast", "player_blast <x> <z> [radius] [speed]: an explosion on the ground there; within its radius the player is thrown as a ragdoll", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, kNeedsFbxPlayer };
			if (a.size() < 2) return { false, "usage: player_blast <x> <z> [radius] [speed]" };
			const float radius = a.size() > 2 ? static_cast<float>(std::atof(a[2].c_str())) : 7.0f, speed = a.size() > 3 ? static_cast<float>(std::atof(a[3].c_str())) : 14.0f;
			return m_Player.Blast({ static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())) }, radius, speed) ? CommandResult{ true, "boom" } : CommandResult{ false, "nothing to blast" };
		});
		c.Register("player_hit", "player_hit <x> <y> <z>: an impulse (N s) on the ragdoll's pelvis (a blow)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.Ragdolled()) return { false, "the player is not a ragdoll (player_ragdoll first)" };
			if (a.size() < 3) return { false, "usage: player_hit <x> <y> <z>" };
			m_Player.HitRagdoll({ static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) });
			return { true, "hit" };
		});
		c.Register("resources", "resources build [x z [workers]] | world [density] | status: puts trees, rocks, berry bushes and fibre tufts in the scene to gather from (build: a patch around a point; world: spread over the generated map by biome) (abilities Chop, Mine, Gather; the yield goes to the actor's bag, see actor bag)", [this](const std::vector<std::string>& a) { return ResourcesCommand(a); });
		c.Register("timescale", "timescale <1-20>: runs the simulation (gameplay, actors, tribe) that many times per frame; 1 is real time", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { true, "timescale " + std::to_string(m_TimeScale) };
			m_TimeScale = std::max(1, std::min(20, std::atoi(a[0].c_str())));
			return { true, "timescale " + std::to_string(m_TimeScale) };
		});
		c.Register("docs", "docs export <file.json>: writes what the rules say about themselves (abilities, effects, attributes and tags by kind of entity, needs, senses, roles, seasons, ecology settings) as one JSON; tools/make_wiki.py builds the wiki from it", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2 || a[0] != "export") return { false, "usage: docs export <file.json>" };
			DocsInputs in;
			in.Library = &m_Gameplay.Library;
			in.World = &m_Gameplay;
			in.Needs = m_Tribe.Config.Needs;
			in.Vision = m_Tribe.Config.Vision;
			in.Memory = m_Tribe.Config.Memory;
			in.Chooser = m_Tribe.Config.Chooser;
			for (const auto& [name, role] : m_Tribe.Config.Roles)
			{
				in.Roles[name] = role.Weights;
				in.Roles[name]["default"] = role.Default;
			}
			in.Ecology = m_Ecology.Species;
			in.Interactives = m_Interactives.Defs;
			in.Garden = m_Garden.Config;
			for (const std::string& name : m_Commands.Names()) in.Commands.push_back({ name, m_Commands.Help(name) });
			const std::string text = ExportDocsJson(in);
			const std::filesystem::path path(a[1]);
			if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
			std::ofstream out(a[1], std::ios::binary);
			if (!out) return { false, "cannot write " + a[1] };
			out << text;
			return { true, "rules written to " + a[1] + " (" + std::to_string(m_Gameplay.Library.Abilities().size()) + " abilities, " + std::to_string(m_Gameplay.Library.Effects().size()) + " effects, " + std::to_string(m_Gameplay.Count()) + " entities read)" };
		});
		c.Register("content", "content status | export <file.json> | load <file.json>: the items, recipes, resource nodes and tribe roles, the interactives and the animal species as data (the pack era1, assets/data/era1.json and world.json, is read at start: see `pack`); load adds to or replaces by id, all sections or none", [this](const std::vector<std::string>& a) -> CommandResult
		{
			const std::string sub = a.empty() ? "status" : a[0];
			if (sub == "export" && a.size() > 1)
			{
				std::filesystem::create_directories(std::filesystem::path(a[1]).parent_path());
				std::ofstream out(a[1], std::ios::binary);
				if (!out) return { false, "cannot write " + a[1] };
				out << ContentToJson(m_Items, m_Recipes, m_NodeDefs);
				return { true, "content written to " + a[1] };
			}
			if (sub == "load" && a.size() > 1)
			{
				// every registered section (items, roles, interactives, species...) reads the file, all or nothing: a file whose roles are wrong changes nothing at all
				ContentReport report;
				if (const std::string error = m_Packs.LoadFile(a[1], &report); !error.empty()) return { false, error };
				m_Visuals.Cache().Clear();   // (the looks may have changed)
				RegisterResourceContent(m_Gameplay.Library, m_NodeDefs);
				RegisterInteractiveContent(m_Gameplay.Library, m_Interactives.Defs);
				if (m_SaveHost) m_SaveHost->ForgetBaseline();   // the wild nodes of the baseline are planned with this data
				ApplyGameSeeds();   // T0: (a section may have read a seed from the file)
				return { true, "content loaded from " + a[1] + ": " + std::to_string(m_Items.Ids().size()) + " items, " + std::to_string(m_Recipes.Ids().size()) + " recipes, " + std::to_string(m_NodeDefs.size()) + " resource nodes"
					+ (report.Problems.empty() ? std::string() : "\n" + report.Text()) };
			}
			return { true, std::to_string(m_Items.Ids().size()) + " items, " + std::to_string(m_Recipes.Ids().size()) + " recipes, " + std::to_string(m_NodeDefs.size()) + " resource nodes, "
				+ std::to_string(m_Interactives.Defs.size()) + " interactives, " + std::to_string(m_Ecology.Species.size()) + " species" };
		});
		c.Register("pack", "pack list | load <id|manifest.json> [strict] | check: the eras as data packs (assets/data/<id>.pack.json: its content files, the packs it depends on, what is given, the stations, the known gaps). load loads the missing dependencies first, all or nothing, and checks that every item and recipe can be had (strict: a problem refuses the load); era1 is loaded at start", [this](const std::vector<std::string>& a) -> CommandResult
		{
			const std::string sub = a.empty() ? "list" : a[0];
			const std::string dir = m_AssetRoot + "/data";
			if (sub == "list")
			{
				std::string out;
				for (const PackManifest& m : ContentPacks::Available(dir))
					out += (m_Packs.IsLoaded(m.Id) ? "* " : "  ") + m.Id + " (era " + std::to_string(m.Era) + ", v" + m.Version + "): " + m.Name
						+ (m.Depends.empty() ? std::string() : ", needs " + [&] { std::string d; for (const std::string& x : m.Depends) d += (d.empty() ? "" : " ") + x; return d; }()) + "\n";
				for (const PackManifest& m : m_Packs.Loaded())
					if (std::filesystem::path(m.Path).parent_path() != std::filesystem::path(dir)) out += "* " + m.Id + " (" + m.Path + ")\n";
				return { true, out.empty() ? "no packs in " + dir : out + "(* = loaded)" };
			}
			if (sub == "load" && a.size() > 1)
			{
				ContentPacks::Options options;
				options.Strict = a.size() > 2 && a[2] == "strict";
				ContentReport report;
				if (const std::string error = m_Packs.Load(a[1], dir, &report, options); !error.empty()) return { false, error + (report.Problems.empty() ? std::string() : "\n" + report.Text()) };
				m_Visuals.Cache().Clear();   // (the looks may have changed)
				RegisterResourceContent(m_Gameplay.Library, m_NodeDefs);
				RegisterInteractiveContent(m_Gameplay.Library, m_Interactives.Defs);
				if (m_SaveHost) m_SaveHost->ForgetBaseline();   // (see content load)
				ApplyGameSeeds();   // T0: (see content load)
				return { true, "pack " + a[1] + " loaded: " + std::to_string(m_Items.Ids().size()) + " items, " + std::to_string(m_Recipes.Ids().size()) + " recipes, " + std::to_string(m_NodeDefs.size())
					+ " resource nodes, " + std::to_string(report.Problems.size()) + " problem(s)\n" + report.Text() };
			}
			if (sub == "check")
			{
				const ContentReport report = m_Packs.Check();
				return { true, std::to_string(report.Problems.size()) + " problem(s), " + std::to_string(report.Notes.size()) + " note(s)\n" + report.Text() };
			}
			return { false, "pack list | load <id|manifest.json> [strict] | check" };
		});
		c.Register("site", "site best | score [x z] | list [n]: where a clan would settle (H8): every place of the generated world scored by the water, the resource nodes the clan wants near, flat ground, the biome and shelter; `tribe build` without a place uses the best", [this](const std::vector<std::string>& a) { return SiteCommand(a); });
		c.Register("settlement", "settlement status | start <building> [x z] | buildings: the buildings and construction sites of the tribe (H8): the clan plans them from its needs (beds, a full store, a station), carries the materials and works each phase with Build", [this](const std::vector<std::string>& a) { return SettlementCommand(a); });
		c.Register("interactives", "interactives build [x z] | status: puts a campfire (Stoke with wood), a well (DrawWater) and a gate (OpenDoor, CloseDoor) in the scene; their state is saved with the game", [this](const std::vector<std::string>& a) { return InteractivesCommand(a); });
		c.Register("ecology", "ecology spawn [n] [x z] [species] | status | season [spring|summer|autumn|winter]: animals of the species of assets/data/world.json (rabbits eat berry bushes and fibre tufts, foxes hunt rabbits) wander, eat, breed when fed (not in winter) and starve; season shows or sets the season (it follows the simulation clock)", [this](const std::vector<std::string>& a) { return EcologyCommand(a); });
		c.Register("ai", "ai debug [on|off] | status [name] | memory <name> | scream <name>: the brain of the tribe members: needs, what they decide and why, what they know; `debug` draws their view cones, the obstacles and their memory in the viewport", [this](const std::vector<std::string>& a) { return AiCommand(a); });
		c.Register("knowledge", "knowledge status [name|clan] | grant <name|clan> <tech> | techs: what each tribe member and the clan know of the technologies (H11), what they are learning and trying; grant teaches one at once", [this](const std::vector<std::string>& a) { return KnowledgeCommand(a); });
		c.Register("nav", "nav status | debug on|off | rebuild | sync | path <x1 z1 x2 z2> [load 0-1]: navigation over the terrain: the grid the characters plan their paths on (trunks, rocks, deep water and cliffs are walls; wading and slopes cost), the paths they follow and what the planning has cost", [this](const std::vector<std::string>& a) { return NavCommand(a); });
		c.Register("economy", "economy status | plan | give <item> [n] | stockpile <name> [x z] [kind] | demand <source> <item> <n>: the clan's economy (H18): what it makes from what (production chains), what spoils, its stores and stockpiles, what it hauls and how much food it brings in; give puts things in the main store", [this](const std::vector<std::string>& a) { return EconomyCommand(a); });
		c.Register("field", "field status | mark [x z]: the clan's fields (H18, E7): plots grass, tilled, sown, sprouting or ripe, what needs water and what the season allows; mark lays out a field (the clan does it by itself once it knows tillage and has seeds)", [this](const std::vector<std::string>& a) { return FieldCommand(a); });
		c.Register("overlay", "overlay [on|off]: draws the clan's fields (one colour per plot state), stockpiles and, over each member, its name, trade and what it is doing", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty()) m_ClanOverlay = a[0] == "on" || a[0] == "1" || a[0] == "true";
			else m_ClanOverlay = !m_ClanOverlay;
			return { true, std::string("overlay ") + (m_ClanOverlay ? "on" : "off") };
		});
		c.Register("clan", "clan new <name> [x z] [members] [model] | list | select <id|name> | relations | trade <a> <b> [force] | visit <a> <b> | wed <a> <b> | move <member> <clan> | territory | meetings | log [n]: several clans in one world (H13): each a tribe of its own, placed by the site chooser away from the others; they meet, claim land, trade, teach each other, wed and make pacts", [this](const std::vector<std::string>& a) { return ClanCommand(a); });
		c.Register("village", "village [clans] [members] [model.fbx|fox|cesium] [x z] [bare] | report [every <seconds>|off]: (bare: founded with their resources and well, nothing built, to watch them raise it; report: how each clan develops, its buildings, store, food and cost) a world that looks lived in: clans (3, of 8, X Bot of packs/full or CesiumMan) founded where the site chooser says, each with its village already standing in a clearing around its fire (a longhouse and huts for everyone, a store and a granary, the work stations, a hearth, a totem); the wild resource nodes are laid out first if the scene has none", [this](const std::vector<std::string>& a) { return VillageCommand(a); });
		c.Register("region", "region list | info <id|name> | explore: the regions of the generated world (H12): area, biome, water, wild resources and danger; what the clan knows of each, its expedition and its moves", [this](const std::vector<std::string>& a) { return RegionCommand(a); });
		c.Register("combat", "combat status [name] | log [n] | hurt <name> <health> [severity] | fight <a> <b> | flee <a> <b> | stop <name>: the fights of the world (H14): who fights or flees whom, health, stamina, morale, wounds; hurt wounds a character, fight sets one on another (a member's clan then decides)", [this](const std::vector<std::string>& a) { return CombatCommand(a); });
		c.Register("hostile", "hostile [a b [0-1]]: who is hostile to whom (clans, animal tags such as Animal.Fox), the grievances and the camps; with a pair, sets how hostile they are to each other (0 makes peace)", [this](const std::vector<std::string>& a) { return HostileCommand(a); });
		c.Register("raid", "raid <clan> | status: sends the tribe to raid a hostile clan's camp (it steals from the store, food first, and comes back), or says how its raid goes; the clan also weighs raids by itself (boldness, hunger, grievance)", [this](const std::vector<std::string>& a) { return RaidCommand(a); });
		c.Register("telemetry", "telemetry start <file.db> [every seconds] | stop | status: writes what happens to every individual of every clan (needs, place, job, skills, events) to a SQLite file by a thread of its own, for analysis; --telemetry=<file.db> starts it with the editor", [this](const std::vector<std::string>& a) { return TelemetryCommand(a); });
		c.Register("tribe", "tribe build [model.fbx|fox] [x z] [workers] | status | jobs | log [n] | remove <name> | clear: the first tribe: workers (named from the data, their trades shared out by the clan's needs) gather, carry to a store, make tools and light a campfire by themselves; they get better at what they practise and take jobs from the clan's board", [this](const std::vector<std::string>& a) { return TribeCommand(a); });
		c.Register("qa", "qa sim [days] [members] | check | status | reset | expect \"<command>\" <regex>: simulation QA (H20): `sim` runs the current scene (its tribe, or a new one) headless for days and checks every invariant at every step (counts, claims, stuck members, deaths, store, fire, positions, ground, job board, skills, knowledge, expeditions, migration, society), `check` checks the live scene now, `status` what the live monitor has seen (QA panel), `expect` fails unless a command's answer matches (for scripted demos)", [this](const std::vector<std::string>& a) { return QaCommand(a); });
		c.Register("garden", "garden build [x z] | status: builds the vegetable garden (plots, tree spot, fruit tree, cow, boxes, wheelbarrow, the Gardener and the Farmhand with their abilities) at a point (default: the middle of the view)", [this](const std::vector<std::string>& a) { return GardenCommand(a); });
		c.Register("ability", "ability list [entity] | status [entity] | grant <ability> [entity] | activate <ability> [target] [actor] | cancel [ability] [actor] | apply <effect> [entity] | sync | store: gameplay abilities (Gardener = the player, Farmhand = an NPC; names with spaces in quotes)", [this](const std::vector<std::string>& a) { return AbilityCommand(a); });
		c.Register("frame", "frame: looks at the middle of the world again", [this](const std::vector<std::string>&) -> CommandResult { FrameWorld(); return { true, "framed" }; });
		c.Register("screenshot", "screenshot <file.png>: saves the viewport (the 3D view without the interface)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: screenshot <file.png>" };
			if (!m_ViewportFb) return { false, "no viewport yet" };
			return m_ViewportFb->SavePNG(a[0].c_str()) ? CommandResult{ true, "saved " + a[0] } : CommandResult{ false, "could not write " + a[0] };
		});
		c.Register("shot_ui", "shot_ui <file.png>: saves the whole window, panels included, at the end of this frame", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: shot_ui <file.png>" };
			m_PendingUiShot = a[0];
			return { true, "" };
		});
		c.Register("load_model", "load_model <model.glb|gltf>, or load <model.glb|gltf>: puts a glTF model in the scene (animated clips are listed in the Model panel)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: load <path>" };
			std::string path = a[0];
			if (!fs::exists(path) && fs::exists(m_AssetRoot + "/" + path)) path = m_AssetRoot + "/" + path;
			std::unique_ptr<Model> model = Model::Load(path);
			if (!model) return { false, "could not load " + a[0] };
			m_Instance.reset();
			m_Model = std::move(model);
			m_Instance = std::make_unique<ModelInstance>(*m_Model);
			if (!m_Model->GetClips().empty()) m_Instance->Play(0, true, 0.0f);
			m_ModelPosition = m_Orbit.Target; m_ModelYaw = 0.0f; m_ModelScale = 1.0f;
			m_Props.RemovePrefix("model.");
			m_Props.Vec3("model.position", m_ModelPosition, -2000.0f, 2000.0f, "Model");
			m_Props.Float("model.yaw", m_ModelYaw, -6.4f, 6.4f, "Model");
			m_Props.Float("model.scale", m_ModelScale, 0.01f, 50.0f, "Model");
			m_Props.Float("model.speed", m_ModelSpeed, 0.0f, 4.0f, "Model").OnChange = [this]() { if (m_Instance) m_Instance->SetSpeed(m_ModelSpeed); };
			return { true, "loaded " + path + " (" + std::to_string(m_Model->GetClips().size()) + " animations)" };
		});
		c.Register("anims", "anims <file.fbx|folder>: adds the animation clips of an FBX file (or every FBX in a folder) to the loaded model, matching bones by name", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: anims <file.fbx|folder>" };
			if (!m_Model) return { false, "load a model first" };
			std::vector<std::string> files;
			std::error_code ec;
			if (fs::is_directory(a[0], ec))
			{
				for (const auto& e : fs::directory_iterator(a[0], ec))
					if (e.is_regular_file() && Lower(e.path().extension().string()) == ".fbx") files.push_back(e.path().generic_string());
				std::sort(files.begin(), files.end());
			}
			else files.push_back(a[0]);
			int added = 0;
			for (const std::string& f : files) { std::string report; added += m_Model->AppendAnimations(f, &report); }
			if (m_Instance && m_Instance->GetCurrentClip() < 0 && !m_Model->GetClips().empty()) m_Instance->Play(0, true, 0.0f);
			return { true, std::to_string(added) + " clip(s) added; the model now has " + std::to_string(m_Model->GetClips().size()) };
		});
		c.Register("clips", "clips: lists the animation clips of the loaded model", [this](const std::vector<std::string>&) -> CommandResult
		{
			if (!m_Model) return { false, "no model loaded" };
			std::string out;
			const auto& clips = m_Model->GetClips();
			for (size_t i = 0; i < clips.size(); ++i)
			{
				char buf[160];
				std::snprintf(buf, sizeof buf, "%s%zu  %s  %.2f s  %zu channels", i ? "\n" : "", i, clips[i].Name.c_str(), static_cast<double>(clips[i].Duration), clips[i].Channels.size());
				out += buf;
			}
			return { true, out.empty() ? "(no clips)" : out };
		});
		c.Register("anim_stats", "anim_stats [clip|all]: root motion, loop quality and foot plants measured from the clips of the loaded model", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Model) return { false, "no model loaded" };
			std::string out;
			const auto& clips = m_Model->GetClips();
			for (size_t i = 0; i < clips.size(); ++i)
			{
				if (!a.empty() && a[0] != "all" && a[0] != clips[i].Name && a[0] != std::to_string(i)) continue;
				out += (out.empty() ? "" : "\n") + FormatClipStats(AnalyzeClip(*m_Model, static_cast<int>(i)));
			}
			return { !out.empty(), out.empty() ? "no such clip" : out };
		});
		c.Register("audio_record", "audio_record <file.wav>: records what the engine mixes (headless runs mix in step with the simulation) until audio_stop", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: audio_record <file.wav>" };
			if (m_Audio->HasDevice()) return { false, "recording works in headless runs (--hidden), where the editor drives the mixer" };
			m_AudioRecord.clear();
			m_AudioRecordPath = a[0];
			m_Recording = true;
			return { true, "recording audio to " + a[0] };
		});
		c.Register("audio_stop", "audio_stop: writes the recorded audio", [this](const std::vector<std::string>&) -> CommandResult
		{
			if (!m_Recording) return { false, "not recording" };
			m_Recording = false;
			const float seconds = static_cast<float>(m_AudioRecord.size() / 2) / static_cast<float>(m_Audio->Mixer().SampleRate());
			return SaveWav(m_AudioRecordPath, m_AudioRecord, m_Audio->Mixer().SampleRate(), 2) ? CommandResult{ true, "saved " + m_AudioRecordPath + " (" + std::to_string(seconds) + " s)" } : CommandResult{ false, "could not write " + m_AudioRecordPath };
		});
		c.Register("play", "play <clip name|index> [fade seconds]: plays a clip of the loaded model", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty() || !m_Instance) return { false, "usage: play <clip> [fade] (with a model loaded)" };
			const float fade = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 0.25f;
			const bool ok = std::isdigit(static_cast<unsigned char>(a[0][0])) ? m_Instance->Play(std::atoi(a[0].c_str()), true, fade) : m_Instance->Play(a[0], true, fade);
			return ok ? CommandResult{ true, "playing " + a[0] } : CommandResult{ false, "no clip '" + a[0] + "'" };
		});
		c.Register("hdri", "hdri <file.hdr|off>: uses an HDR environment for the sky and reflections", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: hdri <file.hdr|off>" };
			if (a[0] == "off") { m_EnvMap.reset(); m_Weather->BaseEnvironment().EnvMap = nullptr; return { true, "environment map off" }; }
			std::string path = a[0];
			if (path == "sunset") path = m_AssetRoot + "/hdri/sunset_sky.hdr";
			auto map = EnvironmentMap::Load(path);
			if (!map) return { false, "could not load " + path };
			m_EnvMap = std::move(map);
			m_Weather->BaseEnvironment().EnvMap = m_EnvMap.get();
			return { true, "environment map " + path };
		});
		c.Register("wait", "wait <frames>: in a script, pauses for that many frames", [this](const std::vector<std::string>& a) -> CommandResult
		{
			m_ScriptWait = a.empty() ? 1 : std::max(1, std::atoi(a[0].c_str()));
			return { true, "" };
		});
		c.Register("run", "run <script.txt>: runs the commands in a file", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: run <script>" };
			std::ifstream in(a[0]);
			if (!in) return { false, "could not read " + a[0] };
			std::stringstream ss; ss << in.rdbuf();
			RunScript(ss.str());
			return { true, "queued " + a[0] };
		});
		c.Register("echo", "echo <text>: prints text (for scripts)", [](const std::vector<std::string>& a) -> CommandResult
		{
			std::string s;
			for (size_t i = 0; i < a.size(); ++i) s += (i ? " " : "") + a[i];
			return { true, s };
		});
		c.Register("areas", "areas [on|off|territory|home|zones|productivity [item]|reach <m>|zonelist]: the map of areas (U1): each clan's territory in its colour, its home ground, the resource zones of the world and the productivity of an item round each camp, painted on the ground near what the camera looks at; zonelist lists the zones", [this](const std::vector<std::string>& a) -> CommandResult
		{
			return MapAreasCommand(a);
		});
		c.Register("pick", "pick <name>|off: picks a character, a resource node or a building as a click does (the card and the bubble of SelectionView.cpp); in a game, the game's selection", [this](const std::vector<std::string>& a) -> CommandResult
		{
			std::string& selected = m_Game.Active ? m_Game.Selected : m_FreeSelected;
			if (a.empty() || a[0] == "off") { selected.clear(); return { true, "nothing selected" }; }
			std::string name;
			for (const std::string& w : a) name += (name.empty() ? "" : " ") + w;
			if (!m_Gameplay.Find(name)) return { false, "nothing called '" + name + "' has abilities (a character, a node, a building)" };
			selected = name;
			return { true, "selected " + name };
		});
		c.Register("stats", "stats [ai|sim]: frame time, objects drawn/culled and world counts; `stats sim`: the milliseconds of each part of the gameplay tick; `stats ai`: the cost of the AI (H16): thinks, plans and the queue, ms per system, animation LOD", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty() && a[0] == "ai") return { true, AiStatsLine() };
			if (!a.empty() && a[0] == "sim") return { true, SimStatsLine() };
			const RenderStats& s = m_Renderer->GetStats();
			char buf[320];
			std::snprintf(buf, sizeof buf, "frame %.1f ms (%.0f fps), viewport %ux%u (rendered %ux%u), objects drawn %u culled %u, trees %zu rocks %zu grass blades %zu",
				static_cast<double>(m_FrameMsSmooth), 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)), m_ViewportW, m_ViewportH, m_ViewportFb->GetWidth(), m_ViewportFb->GetHeight(), s.ObjectsDrawn, s.ObjectsCulled,
				m_World.TreeCount(), m_World.RockCount(), m_World.GrassBlades());
			return { true, buf };
		});
		c.Register("drawcalls", "drawcalls [detail]: the draw calls of the last frame by pass (shadows, objects, grass, water), the triangles, the instanced batches and the GPU and CPU milliseconds of each part (also shown over the viewport: set ui.stats 0|1); detail: the scene's objects grouped by mesh, the heaviest first", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty() && a[0] == "detail")
			{
				struct Group { size_t Objects = 0; uint64_t Triangles = 0; bool Instanced = false, Bounded = false, Skinned = false; float MaxDistance = 0.0f; };
				std::map<const Mesh*, Group> groups;
				for (const RenderObject& o : m_Scene.Objects)
				{
					if (!o.MeshPtr) continue;
					Group& g = groups[o.MeshPtr];
					++g.Objects;
					g.Triangles += o.MeshPtr->GetIndexCount() / 3;
					g.Instanced = o.Instanced; g.Bounded = o.BoundingRadius > 0.0f; g.Skinned = o.JointMatrices != nullptr; g.MaxDistance = o.MaxDistance;
				}
				std::vector<std::pair<const Mesh*, Group>> list(groups.begin(), groups.end());
				std::sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return x.second.Triangles > y.second.Triangles; });
				size_t loose = 0, unbounded = 0;
				for (const auto& [m, g] : list) { if (!g.Instanced) loose += g.Objects; if (!g.Bounded) unbounded += g.Objects; }
				std::string out = std::to_string(m_Scene.Objects.size()) + " objects in " + std::to_string(list.size()) + " meshes; " + std::to_string(loose) + " not instanced, " + std::to_string(unbounded) + " without bounds (never culled) | heaviest:";
				for (size_t i = 0; i < std::min<size_t>(list.size(), 14); ++i)
				{
					const Group& g = list[i].second;
					char b[160];
					std::snprintf(b, sizeof b, " | %zu x %llu tris%s%s%s%s", g.Objects, static_cast<unsigned long long>(g.Triangles / std::max<size_t>(g.Objects, 1)), g.Instanced ? " instanced" : "", g.Bounded ? "" : " UNBOUNDED",
						g.Skinned ? " skinned" : "", g.MaxDistance > 0.0f ? (" max " + std::to_string(static_cast<int>(g.MaxDistance)) + " m").c_str() : "");
					out += b;
				}
				return { true, out };
			}
			std::string s = RenderStatsLine();
			for (char& ch : s) if (ch == '\n') ch = '|';
			return { true, s };
		});
		c.Register("clear", "clear: empties the console", [this](const std::vector<std::string>&) -> CommandResult { m_Log->Clear(); return { true, "" }; });
		c.Register("quit", "quit: closes the editor", [this](const std::vector<std::string>&) -> CommandResult { Close(); return { true, "" }; });
	}

	// ------------------------------------------------------------------ frame
	void EditorApp::OnUpdate(float dt)
	{
		struct UpdateClock { float& Into; std::chrono::steady_clock::time_point T = std::chrono::steady_clock::now(); ~UpdateClock() { Into = Into * 0.9f + 0.1f * static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T).count()); } } updateClock{ m_CpuUpdateMs };
		PollWorldBuild();
		m_FrameDt = dt;
		const float ms = dt * 1000.0f;
		m_FrameMsSmooth = m_FrameMsSmooth * 0.9f + ms * 0.1f;
		m_FrameTimes.push_back(ms);
		if (m_FrameTimes.size() > 120) m_FrameTimes.erase(m_FrameTimes.begin());

		// scripted commands: one line per frame (so a `screenshot` sees the effect of the previous line). A world being generated in the background
		// holds the script until it is built: the lines after `generate` or `terrain_graph preset` (villages, resources, actors) would otherwise
		// land on the old world and keep its heights when the new one replaces it.
		if (m_WorldBuildPending) {}
		else if (m_ScriptWait > 0) --m_ScriptWait;
		else if (!m_Script.empty())
		{
			while (!m_Script.empty() && m_ScriptWait == 0 && !m_WorldBuildPending)
			{
				const std::string line = m_Script.front();
				m_Script.pop_front();
				GB_CLIENT_INFO("> {}", line);
				const CommandResult r = Execute(line);
				if (!r.Ok) { GB_CLIENT_WARN("script line failed, continuing"); if (m_StrictScript) SetExitCode(1); }   // --strict=1: a failed line makes the exit code 1 (a test)
				if (Lower(line).rfind("shot_ui", 0) == 0 || Lower(line).rfind("screenshot", 0) == 0) break;   // render before the next line
			}
		}

		ImGuiIO& io = ImGui::GetIO();
		m_FrameStart = std::chrono::steady_clock::now();   // (the simulation's budget for this frame: StepSimulation)
		const auto simStart = std::chrono::steady_clock::now();
		if (!m_SimBudgetSet)   // a script, a recording, a test: every tick, as before (`set sim.budget` changes it)
		{
			m_SimBudgetSet = true;
			if (Options().Hidden || !Options().RecordDir.empty() || Options().StepFps > 0.0f) m_SimBudgetMs = 0.0;
		}
		for (int step = 0; step < (m_Game.Paused || m_Game.Menu ? 0 : m_TimeScale); ++step)   // `timescale n` runs the simulation n times per frame (the tribe's day in a minute); a paused game none
		{
			m_Actors.PrepareAbilities();   // abilities first: what they ask of the characters is this frame's input
			StepSimulation(dt);            // gameplay and the garden in fixed ticks (and the autosave timer)
			if (m_Player.IsActive()) m_Player.BeginFrame(GetInput(), dt, (m_ViewportHovered || m_Game.Active) && !io.WantTextInput && m_GameView.CharacterKeys());
			if (m_Player.IsActive()) m_Actors.SetViewer(m_Camera.Position);
			if (m_Player.IsActive()) m_Actors.Update(dt);   // (with no player the actors move in the fixed tick: EditorSaveHost::Step)
		}
		m_CpuSimMs = m_CpuSimMs * 0.9f + 0.1f * static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - simStart).count());
		StepGame(dt);   // H15: the player's body, camera, quality and news (nothing when not playing)
		m_NavSyncTimer -= dt;
		if (m_NavSyncTimer <= 0.0f)   // trees felled or planted, a building: the grid redoes only the cells around them
		{
			m_NavSyncTimer = 1.0f;
			if (m_Actors.Navigation().Ready() && m_World.IsBuilt()) m_Player.SyncNavigation(m_World, m_SceneEditor, m_Steps.get());
		}
		if (m_Player.IsActive())
		{
			m_Player.EndFrame(dt);
			m_Player.ApplyCamera(m_Camera);
			m_Orbit.Target = m_Player.GetPosition();   // the shadows and the weather follow the player
		}
		else if (!m_GameView.Active())
		{
			if (m_ViewportHovered && !io.WantTextInput && m_GizmoAxis < 0) m_Orbit.Update(dt, GetInput());
			if (m_CamMode == CamMode::Follow)   // the target eases to the member; dragging still orbits it
			{
				const Actor* actor = m_Actors.Find(m_FollowMember);
				bool alive = false;
				for (const TribeMember& m : m_Tribe.Members()) if (m.Name == m_FollowMember && !m.Dead) alive = true;
				if (actor && alive) m_Orbit.FollowTo(actor->GetPosition() + glm::vec3(0.0f, 1.2f, 0.0f), dt);
				else SetCamMode(CamMode::Free);   // gone or dead: back to the free camera where it stands
			}
			m_Orbit.Apply(m_Camera);
		}
		if (m_GameView.Active())   // a game camera (camera game ...) owns the view; shadows and weather centre on what it looks at
		{
			m_GameView.Update(GetInput(), dt, m_ViewportHovered && !io.WantTextInput, m_Actors, m_Player, m_World, m_Camera);
			if (!m_Player.IsActive()) m_Orbit.Target = m_GameView.Focus();
		}
		// while playing, the sky follows the clan's clock (a clock at 00:01 under a noon sun reads wrong); the editor's own setting comes back after
		if (m_Game.Active)
		{
			if (!m_Tod.Enabled) { m_TodBaseEnv = m_Weather->BaseEnvironment(); m_TodBaseSun = m_Weather->BaseSun(); m_Tod.Enabled = true; m_TodByGame = true; }
			m_Tod.Hours = m_Tribe.Hour();
		}
		else if (m_TodByGame)
		{
			Environment& e = m_Weather->BaseEnvironment();
			e.SkyTop = m_TodBaseEnv.SkyTop; e.SkyHorizon = m_TodBaseEnv.SkyHorizon; e.SkyGround = m_TodBaseEnv.SkyGround; e.Intensity = m_TodBaseEnv.Intensity;
			e.FogColor = m_TodBaseEnv.FogColor; e.Stars = m_TodBaseEnv.Stars; e.CloudCover = m_TodBaseEnv.CloudCover; e.Exposure = m_TodBaseEnv.Exposure;
			m_Weather->BaseSun() = m_TodBaseSun;
			m_Tod.Enabled = m_TodByGame = false;
		}
		ApplyTimeOfDay();
		m_Weather->Update(dt, m_Camera);
		m_Weather->Apply(m_Scene);
		m_WeatherLights = m_Scene.PointLights.size();
		m_SceneEditor.Tick(m_GizmoAxis >= 0 || ImGui::IsAnyItemActive());
		m_Scene.Env.ShadowCenter = m_Orbit.Target;
		m_Scene.Post.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
		if (m_Instance && !m_Player.IsActive()) m_Instance->Update(dt);
		{
			Listener listener;   // the listener hears from the camera
			listener.Position = m_Camera.Position;
			listener.Forward = m_Camera.GetForward();
			m_Audio->Mixer().SetListener(listener);
		}
		if (!m_Audio->HasDevice())
		{
			// no output device (headless): mix the time that passed, so sounds finish and can be recorded
			uint32_t frames = static_cast<uint32_t>(std::clamp(dt, 0.0f, 1.0f) * static_cast<float>(m_Audio->Mixer().SampleRate()));
			std::vector<float> block(static_cast<size_t>(frames) * 2);
			m_Audio->Mixer().Render(block.data(), frames);
			if (m_Recording) m_AudioRecord.insert(m_AudioRecord.end(), block.begin(), block.end());
		}
	}

	void EditorApp::OnRender()
	{
		Window& window = *GetWindow();
		const uint32_t ww = window.GetWidth(), wh = window.GetHeight();

		// the 3D view
		const uint32_t rw = std::max(1u, static_cast<uint32_t>(static_cast<float>(m_ViewportW) * m_RenderScale + 0.5f));   // `quality minimum` renders smaller
		const uint32_t rh = std::max(1u, static_cast<uint32_t>(static_cast<float>(m_ViewportH) * m_RenderScale + 0.5f));
		if (m_ViewportFb->GetWidth() != rw || m_ViewportFb->GetHeight() != rh)
		{
			m_ViewportFb = std::make_unique<Framebuffer>(rw, rh);
			m_Renderer->Resize(rw, rh);
		}
		const auto buildStart = std::chrono::steady_clock::now();
		m_Scene.Objects.resize(m_StaticObjects);
		m_Scene.PointLights.resize(m_WeatherLights);
		m_EntityRenderer->Update(m_FrameDt);
		UpdateVisuals(m_FrameDt);
		UpdateTrailsView();   // T5c: the trails on the ground, the grass and the navigation (TrailsView.cpp)
		m_EntityRenderer->SetPropCull(m_Camera.Position, 420.0f);   // (a map of kilometres: the far props are not built at all; the trees are drawn to 340 m)
		m_EntityRenderer->Build(m_SceneEditor.World(), m_Scene, m_SceneEditor.Selected());
		AddVisualsToScene();
		if (m_Instance)
			AddToScene(*m_Instance, m_Scene, glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_ModelPosition), m_ModelYaw, glm::vec3(0, 1, 0)), glm::vec3(m_ModelScale)));
		if (m_DrawActors) m_ActorRenderer.AddToScene(m_Actors, m_Scene);
		if (m_World.IsBuilt()) m_World.FollowCamera(m_Camera.Position);
		const auto renderStart = std::chrono::steady_clock::now();
		m_CpuBuildMs = m_CpuBuildMs * 0.9f + 0.1f * static_cast<float>(std::chrono::duration<double, std::milli>(renderStart - buildStart).count());
		m_Renderer->Render(m_Scene, m_Camera, m_ViewportFb.get());
		m_CpuRenderMs = m_CpuRenderMs * 0.9f + 0.1f * static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count());

		// the interface
		Framebuffer::BindDefault(ww, wh);
		glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		const auto uiStart = std::chrono::steady_clock::now();
		m_Ui->BeginFrame(m_FrameDt, ww, wh);
		DrawUI();
		m_Ui->EndFrame();
		m_CpuUiMs = m_CpuUiMs * 0.9f + 0.1f * static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - uiStart).count());

		if (!m_PendingUiShot.empty())
		{
			SaveBackbufferPNG(m_PendingUiShot.c_str(), ww, wh);
			GB_CLIENT_INFO("Saved {}", m_PendingUiShot);
			m_PendingUiShot.clear();
		}
	}

	// ------------------------------------------------------------------ ui
	void EditorApp::BuildDefaultLayout(unsigned int dockspaceId, float width, float height)
	{
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, ImVec2(width, height));
		ImGuiID center = dockspaceId, left, leftTop, right, bottom;
		left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.25f, nullptr, &center);
		leftTop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.45f, nullptr, &left);
		right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.34f, nullptr, &center);
		bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Properties", left);
		ImGui::DockBuilderDockWindow("Hierarchy", leftTop);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("World", right);
		ImGui::DockBuilderDockWindow("Stats", right);
		ImGui::DockBuilderDockWindow("Model", right);
		ImGui::DockBuilderDockWindow("Animation", right);
		ImGui::DockBuilderDockWindow("Abilities", right);
		ImGui::DockBuilderDockWindow("Tribe", right);
		ImGui::DockBuilderDockWindow("World state", right);
		ImGui::DockBuilderDockWindow("QA", right);
		ImGui::DockBuilderDockWindow("Tests", bottom);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderDockWindow("Assets", bottom);
		ImGui::DockBuilderDockWindow("Timeline", bottom);
		ImGui::DockBuilderDockWindow("Graph", bottom);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderFinish(dockspaceId);
	}

	void EditorApp::DrawUI()
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();
		if (m_Game.Active)
		{   // the game: only its view, over the whole window. The editor's dock layout is kept alive (not shown) so `play off` finds it as it was; no
			// docked window is drawn meanwhile (it would land in the hidden node). The console is the game's own window (`game console`).
			ImGui::DockSpaceOverViewport(0, vp, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_KeepAliveOnly);
			DrawGameScreen();
			for (const std::string& line : m_UiCommands) Execute(line);
			m_UiCommands.clear();
			return;
		}
		DrawMenu();
		const ImGuiID dock = ImGui::DockSpaceOverViewport(0, vp, ImGuiDockNodeFlags_PassthruCentralNode);
		if (!m_LayoutBuilt)
		{
			m_LayoutBuilt = true;
			if (ImGui::GetIO().IniFilename == nullptr || ImGui::DockBuilderGetNode(dock) == nullptr || ImGui::DockBuilderGetNode(dock)->IsLeafNode())
				BuildDefaultLayout(dock, vp->Size.x, vp->Size.y);
		}
		if (m_Panels.Viewport) DrawViewport();
		if (m_Panels.Hierarchy) DrawHierarchy();
		if (m_Panels.Inspector) DrawInspector();
		if (m_Panels.Properties) DrawProperties();
		if (m_Panels.World) DrawWorldPanel();
		if (m_Panels.Stats) DrawStats();
		if (m_Panels.Model) DrawModelPanel();
		if (m_Panels.Animation) DrawAnimationPanel();
		if (m_Panels.Abilities) DrawAbilitiesPanel();
		if (m_Panels.Tribe) DrawTribePanel();
		if (m_Panels.WorldState) DrawWorldStatePanel();
		if (m_Panels.Timeline) DrawTimelinePanel();
		if (m_Panels.AnimBindings) DrawAnimBindingsPanel();
		if (m_Panels.Graph) DrawGraphPanel();
		if (m_Panels.Tests) DrawTestsPanel();
		if (m_Panels.QA) DrawQaPanel();
		if (m_ShowPriorityGraph) DrawPriorityGraphPanel();
		if (m_ShowTerrainGraph)
		{
			if (!m_TgUi) m_TgUi = std::make_unique<TerrainGraphUi>();
			m_TgUi->Draw(&m_ShowTerrainGraph);
			if (m_TgUi->ConsumeApplyRequest())
			{
				WorldDesc graphDesc = m_WorldDesc;
				// a copy: the world is built on another thread while the panel keeps editing its own graph
				graphDesc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(*m_TgUi->Graph());
				RequestWorldBuild(graphDesc);
			}
		}
		if (m_Panels.Console) DrawConsole();
		if (m_Panels.Assets) DrawAssets();
		if (m_Panels.Demo) ImGui::ShowDemoWindow(&m_Panels.Demo);
		HandleSceneShortcuts();
		for (const std::string& line : m_UiCommands) Execute(line);
		m_UiCommands.clear();
	}

	void EditorApp::DrawMenu()
	{
		if (!ImGui::BeginMainMenuBar()) return;
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "empty: camera and sun only")) Execute("new_scene");
			if (ImGui::MenuItem("Open scene (scene.json)")) Execute("scene_load scene.json");
			if (ImGui::MenuItem("Save scene (scene.json)")) Execute("scene_save scene.json");
			ImGui::Separator();
			if (ImGui::BeginMenu("New world"))
			{
				if (ImGui::MenuItem("Temperate")) Execute("generate temperate");
				if (ImGui::MenuItem("Alpine")) Execute("generate alpine");
				if (ImGui::MenuItem("Desert")) Execute("generate desert");
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("New scene")) Execute("scene new");
			if (ImGui::MenuItem("Save scene", "Ctrl+S")) Execute("scene save scene.json");
			if (ImGui::MenuItem("Load scene")) Execute("scene load scene.json");
			ImGui::Separator();
			DrawSaveMenu();
			ImGui::Separator();
			if (ImGui::MenuItem("Save viewport screenshot")) Execute("screenshot editor_screenshot.png");
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_SceneEditor.UndoDepth() > 0)) Execute("undo");
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_SceneEditor.RedoDepth() > 0)) Execute("redo");
			ImGui::Separator();
			const bool has = m_SceneEditor.Selected() != NullEntity;
			if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has)) Execute("entity duplicate");
			if (ImGui::MenuItem("Delete", "Del", false, has)) Execute("entity delete");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Entity"))
		{
			for (const char* kind : { "empty", "cube", "sphere", "plane", "light" })
				if (ImGui::MenuItem(kind)) Execute(std::string("entity create ") + kind);
			if (ImGui::BeginMenu("glTF model"))
			{
				std::error_code ec;
				for (auto it = fs::recursive_directory_iterator(m_AssetRoot, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				{
					const std::string ext = Lower(it->path().extension().string());
					if ((ext == ".glb" || ext == ".gltf") && ImGui::MenuItem(fs::relative(it->path(), m_AssetRoot, ec).generic_string().c_str()))
						Execute("add \"" + it->path().generic_string() + "\"");
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Tests"))
		{
			for (const TestScenario& s : TestScenarios())
				if (ImGui::MenuItem(s.Name.c_str(), s.Approximate ? "approx." : nullptr)) Execute("test " + s.Name);
			ImGui::Separator();
			ImGui::MenuItem("Tests panel", nullptr, &m_Panels.Tests);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			if (ImGui::MenuItem("Play as fox (WASD)", nullptr, m_Player.IsActive())) Execute(m_Player.IsActive() ? "player off" : "player fox");
			if (ImGui::MenuItem("Play the game (H15)", nullptr, m_Game.Active)) m_UiCommands.push_back(m_Game.Active ? "play off" : "play");
			if (ImGui::MenuItem("Observe the clans", nullptr, m_Observer.On)) m_UiCommands.push_back("observer toggle");
			ImGui::Separator();
			ImGui::MenuItem("Hierarchy", nullptr, &m_Panels.Hierarchy);
			ImGui::MenuItem("Inspector", nullptr, &m_Panels.Inspector);
			ImGui::MenuItem("Tests", nullptr, &m_Panels.Tests);
			ImGui::MenuItem("Priority Graph", nullptr, &m_ShowPriorityGraph);
			ImGui::MenuItem("Terrain Graph", nullptr, &m_ShowTerrainGraph);
			ImGui::MenuItem("Viewport", nullptr, &m_Panels.Viewport);
			ImGui::MenuItem("Properties", nullptr, &m_Panels.Properties);
			ImGui::MenuItem("World", nullptr, &m_Panels.World);
			ImGui::MenuItem("Stats", nullptr, &m_Panels.Stats);
			ImGui::MenuItem("Model", nullptr, &m_Panels.Model);
			ImGui::MenuItem("Animation", nullptr, &m_Panels.Animation);
			ImGui::MenuItem("Abilities", nullptr, &m_Panels.Abilities);
			ImGui::MenuItem("Tribe", nullptr, &m_Panels.Tribe);
			ImGui::MenuItem("World state", nullptr, &m_Panels.WorldState);
			ImGui::MenuItem("QA", nullptr, &m_Panels.QA);
			ImGui::MenuItem("Timeline", nullptr, &m_Panels.Timeline);
			ImGui::MenuItem("Animation Bindings", nullptr, &m_Panels.AnimBindings);
			ImGui::MenuItem("Animation graph", nullptr, &m_Panels.Graph);
			ImGui::MenuItem("Console", nullptr, &m_Panels.Console);
			ImGui::MenuItem("Assets", nullptr, &m_Panels.Assets);
			ImGui::Separator();
			ImGui::MenuItem("ImGui demo", nullptr, &m_Panels.Demo);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Quality"))
		{
			for (const char* q : { "minimum", "low", "medium", "high", "ultra" })
				if (ImGui::MenuItem(q)) SetQuality(q);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("List the commands")) Execute("help");
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	std::string EditorApp::RenderStatsLine() const
	{
		const RenderStats& s = m_Renderer->GetStats();
		char text[512];
		std::snprintf(text, sizeof text,
			"draw calls %u: shadows %u, objects %u, grass %u, water %u | %u instanced batches | %.2f M triangles | objects %u drawn, %u culled\n"
			"GPU %.1f ms: shadows %.1f, objects %.1f, grass %.1f, water %.1f, particles+resolve %.1f, post %.1f\n"
			"CPU: update %.1f ms (simulation %.1f), building the scene %.1f ms, sending it %.1f ms, interface %.1f ms, frame %.1f ms",
			s.DrawCalls, s.ShadowCalls, s.SceneCalls, s.GrassCalls, s.WaterCalls, s.InstancedDraws, static_cast<double>(s.Triangles) * 1e-6, s.ObjectsDrawn, s.ObjectsCulled,
			static_cast<double>(s.GpuTotalMs), static_cast<double>(s.GpuShadowMs), static_cast<double>(s.GpuSceneMs), static_cast<double>(s.GpuGrassMs), static_cast<double>(s.GpuWaterMs),
			static_cast<double>(s.GpuEffectsMs), static_cast<double>(s.GpuPostMs),
			static_cast<double>(m_CpuUpdateMs), static_cast<double>(m_CpuSimMs), static_cast<double>(m_CpuBuildMs), static_cast<double>(m_CpuRenderMs), static_cast<double>(m_CpuUiMs), static_cast<double>(m_FrameMsSmooth));
		return text;
	}

	void EditorApp::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (ImGui::Begin("Viewport", &m_Panels.Viewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) DrawViewportBody();
		ImGui::End();
		ImGui::PopStyleVar();
	}

	// The game's screen: the 3D view and its interface over the whole window, with no frame, title or docking (H15).
	void EditorApp::DrawGameScreen()
	{
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->Pos);
		ImGui::SetNextWindowSize(vp->Size);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		const bool open = ImGui::Begin("Game##screen", nullptr, flags);
		ImGui::PopStyleVar(3);   // (the windows the game opens inside keep their own padding)
		if (open) DrawViewportBody();
		ImGui::End();
	}

	void EditorApp::DrawViewportBody()
	{
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const uint32_t w = static_cast<uint32_t>(std::max(avail.x, 64.0f)), h = static_cast<uint32_t>(std::max(avail.y, 64.0f));
			if (std::abs(static_cast<int>(w) - static_cast<int>(m_ViewportW)) > 1 || std::abs(static_cast<int>(h) - static_cast<int>(m_ViewportH)) > 1)
			{
				m_ViewportW = w;
				m_ViewportH = h;
			}
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_ViewportFb->GetColorTexture())), ImVec2(static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH)),   // scaled up when rendered smaller
				ImVec2(0, 1), ImVec2(1, 0));
			m_ViewportHovered = ImGui::IsItemHovered();
			{
				const ImVec2 mp = ImGui::GetIO().MousePos;
				m_CursorNorm = { (mp.x - origin.x) / std::max(static_cast<float>(m_ViewportW), 1.0f), (mp.y - origin.y) / std::max(static_cast<float>(m_ViewportH), 1.0f) };
			}
			if (!m_Game.Active)
			{   // camera mode buttons over the viewport corner (they take the mouse: no picking or orbiting through them; a game has its own camera)
				const char* names[3] = { "Free", "Top", "Follow" };
				const CamMode modes[3] = { CamMode::Free, CamMode::TopDown, CamMode::Follow };
				const char* commands[3] = { "camera free", "camera top", "camera follow" };
				bool overButton = false;
				for (int i = 0; i < 3; ++i)
				{
					ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f + 58.0f * static_cast<float>(i), origin.y + 26.0f));
					const bool on = m_CamMode == modes[i];
					if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.80f, 0.85f));
					if (ImGui::Button(names[i], ImVec2(54.0f, 0.0f))) Execute(commands[i]);
					if (on) ImGui::PopStyleColor();
					overButton = overButton || ImGui::IsItemHovered();
				}
				{   // the observer of the clans (ObserverView.cpp)
					ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f + 58.0f * 3.0f + 6.0f, origin.y + 26.0f));
					if (m_Observer.On) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.80f, 0.85f));
					if (ImGui::Button("Clans", ImVec2(54.0f, 0.0f))) Execute("observer toggle");
					if (m_Observer.On) ImGui::PopStyleColor();
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Observe the clans one by one: their members, board, store, buildings, knowledge and relations");
					overButton = overButton || ImGui::IsItemHovered();
				}
				if (overButton) m_ViewportHovered = false;
			}
			if (!m_Game.Active) DrawGizmo(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH));   // (no editing handles in a game: H15)
			if (m_NavDebug) DrawNavDebug(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH));
			if (m_Game.Active && m_ClanOverlay)   // (the editor's DrawGizmo paints the overlay; a game has no gizmo, so it is painted here)
			{
				const float aspect = static_cast<float>(m_ViewportW) / std::max(static_cast<float>(m_ViewportH), 1.0f);
				DrawClanOverlay(ImGui::GetWindowDrawList(), m_Camera.GetProjection(aspect) * m_Camera.GetView(),
					glm::vec2(static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH)), origin.x, origin.y);
			}
			DrawMapOverlay(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH));   // U1: the areas, under the cards
			DrawBubble(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH), m_Game.Active ? m_Game.Selected : m_FreeSelected);   // over the selected character
			if (!m_Game.Active) DrawMapPanel();   // (in a game the map is a sheet of the clan book)
			if (!m_Game.Active)
			{
				DrawObserver(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH));
				if (m_Observer.On) { const GameTheme::Scope theme; DrawSelection(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH), m_FreeSelected); }
				else DrawSelection(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH), m_FreeSelected);
			}
			if (m_Game.Active) DrawGameHud(origin.x, origin.y, static_cast<float>(m_ViewportW), static_cast<float>(m_ViewportH));   // H15
			else if (!m_Game.Player.empty() && m_Tribe.FindMember(m_Game.Player))   // a game left with Esc: one click goes back to it (`play resume`)
			{
				ImGui::SetCursorScreenPos(ImVec2(origin.x + 12.0f, origin.y + 12.0f));
				if (ImGui::Button("Back to the game")) m_UiCommands.push_back("play resume");
			}
			if (m_Game.Active) return;   // (in a game the frame rate and the draw calls are in its console: `game console`)
			char info[96];
			std::snprintf(info, sizeof info, "%.0f fps   %ux%u", 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)), m_ViewportW, m_ViewportH);
			ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(255, 255, 255, 200), info);
			if (m_ShowRenderStats && !m_Observer.On)   // the draw calls and where the frame's time goes (not under the observer's book: `drawcalls` says the same), in the top right corner (`set ui.stats 0` hides it)
			{
				const std::string lines = RenderStatsLine();
				ImDrawList* dl = ImGui::GetWindowDrawList();
				float y = origin.y + 6.0f;
				size_t from = 0;
				while (from < lines.size())
				{
					const size_t to = lines.find('\n', from);
					const std::string line = lines.substr(from, to == std::string::npos ? std::string::npos : to - from);
					const ImVec2 size = ImGui::CalcTextSize(line.c_str());
					const float x = origin.x + static_cast<float>(m_ViewportW) - size.x - 10.0f;
					dl->AddRectFilled(ImVec2(x - 4.0f, y - 1.0f), ImVec2(x + size.x + 4.0f, y + size.y + 1.0f), IM_COL32(0, 0, 0, 120));
					dl->AddText(ImVec2(x, y), IM_COL32(255, 255, 255, 230), line.c_str());
					y += size.y + 2.0f;
					if (to == std::string::npos) break;
					from = to + 1;
				}
			}
		}
	}

	void EditorApp::DrawProperties()
	{
		if (ImGui::Begin("Properties", &m_Panels.Properties))
		{
			char filter[64];
			std::snprintf(filter, sizeof filter, "%s", m_PropFilter.c_str());
			if (ImGui::InputTextWithHint("##filter", "search...", filter, sizeof filter)) m_PropFilter = filter;
			ImGui::Separator();
			std::vector<std::string> groups;
			for (const Property& p : m_Props.All())
				if (p.Path.rfind("sel.", 0) != 0 && std::find(groups.begin(), groups.end(), p.Group) == groups.end()) groups.push_back(p.Group);
			const std::string needle = Lower(m_PropFilter);
			for (const std::string& group : groups)
			{
				bool any = needle.empty();
				for (const Property& p : m_Props.All())
					if (p.Group == group && (needle.empty() || Lower(p.Path).find(needle) != std::string::npos)) any = true;
				if (!any) continue;
				if (!ImGui::CollapsingHeader(group.c_str(), needle.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) continue;
				for (Property& p : m_Props.All())
				{
					if (p.Path.rfind("sel.", 0) == 0 || p.Group != group || (!needle.empty() && Lower(p.Path).find(needle) == std::string::npos)) continue;
					ImGui::PushID(p.Path.c_str());
					if (DrawPropertyWidget(p, p.Path.c_str())) PropertyRegistry::Notify(p);
					ImGui::PopID();
				}
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawWorldPanel()
	{
		if (ImGui::Begin("World", &m_Panels.World))
		{
			static const char* types[] = { "Temperate", "Alpine", "Desert" };
			m_WorldTypeIndex = m_WorldDesc.Type == WorldType::Temperate ? 0 : m_WorldDesc.Type == WorldType::Alpine ? 1 : 2;
			if (ImGui::Combo("type", &m_WorldTypeIndex, types, 3))
				m_WorldDesc.Type = m_WorldTypeIndex == 0 ? WorldType::Temperate : m_WorldTypeIndex == 1 ? WorldType::Alpine : WorldType::Desert;
			int seed = static_cast<int>(m_WorldDesc.Seed);
			if (ImGui::InputInt("seed", &seed)) m_WorldDesc.Seed = static_cast<uint32_t>(std::max(seed, 0));
			ImGui::SliderInt("tree budget", &m_WorldDesc.TreeBudget, 0, 60000);   // (a forest over an open world of a few km wants tens of thousands)
			ImGui::SliderInt("rock budget", &m_WorldDesc.RockBudget, 0, 3000);
			ImGui::SliderFloat("forest", &m_WorldDesc.ForestAmount, 0.0f, 1.0f);
			ImGui::Checkbox("river", &m_WorldDesc.River); ImGui::SameLine(); ImGui::Checkbox("lake", &m_WorldDesc.Lake);
			if (m_WorldBuildPending)
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.25f, 1.0f), "Generating world in background...");
			if (ImGui::Button("Generate", ImVec2(-1, 0))) Execute("generate " + std::string(m_WorldDesc.Type == WorldType::Temperate ? "temperate" : m_WorldDesc.Type == WorldType::Alpine ? "alpine" : "desert") +
				" " + std::to_string(m_WorldDesc.Seed) + " " + std::to_string(m_WorldDesc.TreeBudget) + " " + std::to_string(m_WorldDesc.RockBudget));
			if (ImGui::Button("Frame world", ImVec2(-1, 0))) FrameWorld();
			ImGui::Separator();
			if (m_World.IsBuilt())
			{
				const auto shares = m_World.World().BiomeShares(24);
				const char* names[] = { "water", "shore", "meadow", "forest", "rock", "snow", "dunes" };
				ImGui::Text("trees %zu   rocks %zu   grass %zu", m_World.TreeCount(), m_World.RockCount(), m_World.GrassBlades());
				for (size_t i = 0; i < shares.size(); ++i)
					if (shares[i] > 0.001f) { char t[32]; std::snprintf(t, sizeof t, "%.0f%%", static_cast<double>(shares[i]) * 100.0); ImGui::ProgressBar(shares[i], ImVec2(-1, 0), t); ImGui::SameLine(0.0f, 6.0f); ImGui::TextUnformatted(names[i]); }
			}
		}
		ImGui::End();
	}

	// ------------------------------------------------------------------ scene editing
	void EditorApp::HandleSceneShortcuts()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantTextInput || m_Game.Active) return;   // (in a game the keys are the game's: GameKeys)
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Execute(io.KeyShift ? "redo" : "undo");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Execute("redo");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && m_SceneEditor.Selected() != NullEntity) Execute("entity duplicate");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Execute("scene save scene.json");
		else if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) Execute("quicksave");
		else if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) Execute("quickload");
		else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_SceneEditor.Selected() != NullEntity) Execute("entity delete");
		else if (m_ViewportHovered && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) Execute(m_CamMode == CamMode::Follow ? "camera free" : "camera follow");
		else if (m_ViewportHovered && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false)) Execute(m_CamMode == CamMode::TopDown ? "camera free" : "camera top");
	}

	void EditorApp::DrawGizmo(float ox, float oy, float w, float h)
	{
		ImGuiIO& io = ImGui::GetIO();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		EntityWorld& world = m_SceneEditor.World();
		const glm::vec2 size(w, h), mouse(io.MousePos.x - ox, io.MousePos.y - oy);
		const float aspect = w / h;
		const glm::mat4 viewProj = m_Camera.GetProjection(aspect) * m_Camera.GetView();
		const glm::vec3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		const ImU32 colors[3] = { IM_COL32(235, 70, 70, 255), IM_COL32(90, 220, 90, 255), IM_COL32(80, 130, 245, 255) };

		const EntityId sel = m_SceneEditor.Selected();
		glm::vec3 origin(0.0f);
		float len = 0.0f;
		int hover = -1;
		if (sel != NullEntity)
		{
			origin = glm::vec3(world.WorldMatrix(sel)[3]);
			len = glm::distance(m_Camera.Position, origin) * 0.16f;
			float best = 9.0f;   // pixels
			glm::vec2 a;
			if (ProjectToScreen(viewProj, origin, size, a))
				for (int i = 0; i < 3; ++i)
				{
					glm::vec2 b;
					if (!ProjectToScreen(viewProj, origin + axes[i] * len, size, b)) continue;
					const glm::vec2 ab = b - a;
					const float t = glm::clamp(glm::dot(mouse - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
					const float d = glm::distance(mouse, a + ab * t);
					if (d < best && m_ViewportHovered) { best = d; hover = i; }
				}
			for (int i = 0; i < 3; ++i)
			{
				glm::vec2 b;
				if (!ProjectToScreen(viewProj, origin + axes[i] * len, size, b) || !ProjectToScreen(viewProj, origin, size, a)) continue;
				const bool active = i == hover || i == m_GizmoAxis;
				dl->AddLine(ImVec2(ox + a.x, oy + a.y), ImVec2(ox + b.x, oy + b.y), active ? IM_COL32(255, 235, 90, 255) : colors[i], active ? 4.0f : 2.5f);
				dl->AddCircleFilled(ImVec2(ox + b.x, oy + b.y), active ? 6.0f : 4.5f, active ? IM_COL32(255, 235, 90, 255) : colors[i]);
			}
		}

		if (m_ClanOverlay) DrawClanOverlay(dl, viewProj, size, ox, oy);
		if (m_AiDebug) DrawAiDebug(dl, viewProj, size, ox, oy);

		if (m_ViewportHovered && ImGui::IsMouseClicked(0))
		{
			m_PressPos = mouse;
			m_PressedInViewport = true;
			if (hover >= 0)
			{
				m_GizmoAxis = hover;
				m_GizmoOrigin = origin;
				m_GizmoStart = RayAxisParam(ScreenRay(m_Camera, mouse / size, aspect), origin, axes[hover]);
				m_PressedInViewport = false;
			}
		}
		if (m_GizmoAxis >= 0)
		{
			GB::Entity* e = world.Find(sel);
			if (e && ImGui::IsMouseDown(0))
			{
				const float t = RayAxisParam(ScreenRay(m_Camera, mouse / size, aspect), m_GizmoOrigin, axes[m_GizmoAxis]);
				const glm::vec3 target = m_GizmoOrigin + axes[m_GizmoAxis] * (t - m_GizmoStart);
				e->Transform.Position = glm::vec3(glm::inverse(world.WorldMatrix(e->Parent)) * glm::vec4(target, 1.0f));
				m_SceneEditor.MarkDirty();
			}
			else m_GizmoAxis = -1;   // released: Tick commits the move as one undo step
		}
		else if (m_PressedInViewport && ImGui::IsMouseReleased(0))
		{
			m_PressedInViewport = false;
			if (glm::distance(mouse, m_PressPos) < 4.0f && m_ViewportHovered)
			{
				const Ray ray = ScreenRay(m_Camera, mouse / size, aspect);
				m_FreeSelected = PickSelectable(ray);   // the card and the bubble of the free mode (SelectionView.cpp)
				const std::string actor = PickActor(ray);   // a character under the cursor wins over the scene behind it
				if (!actor.empty()) SelectActor(actor, false);
				else
				{
					m_SceneEditor.Select(m_SceneEditor.Pick(ray));
					if (const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.Selected()); e && m_Gameplay.Find(e->Name)) m_AbilityActor = e->Name;   // a plot, a tree or a bush shows its abilities too
				}
			}
		}
	}

	void EditorApp::DrawHierarchyNode(EntityId id)
	{
		const GB::Entity* e = m_SceneEditor.World().Find(id);
		if (!e) return;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | (e->Children.size() <= kHierarchyListed ? ImGuiTreeNodeFlags_DefaultOpen : 0);
		if (e->Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
		if (id == m_SceneEditor.Selected()) flags |= ImGuiTreeNodeFlags_Selected;
		ImGui::PushID(static_cast<int>(id));
		const char* tag = e->Light ? "[light] " : e->Mesh ? (e->Mesh->Model.empty() ? "[mesh] " : "[model] ") : e->Body ? "[body] " : "";
		const bool open = ImGui::TreeNodeEx("##node", flags, "%s%s", tag, e->Name.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_SceneEditor.Select(id);
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("GB_ENTITY", &id, sizeof id);
			ImGui::TextUnformatted(e->Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GB_ENTITY"))
				m_UiCommands.push_back("entity parent " + std::to_string(*static_cast<const EntityId*>(pl->Data)) + " " + std::to_string(id));
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem())
		{
			m_SceneEditor.Select(id);
			if (ImGui::MenuItem("Duplicate")) m_UiCommands.push_back("entity duplicate " + std::to_string(id));
			if (ImGui::MenuItem("Delete")) m_UiCommands.push_back("entity delete " + std::to_string(id));
			if (ImGui::MenuItem("Add child (empty)")) m_UiCommands.push_back("entity create empty Entity " + std::to_string(id));
			if (ImGui::MenuItem("Add child (cube)")) m_UiCommands.push_back("entity create cube Cube " + std::to_string(id));
			if (e->Parent != NullEntity && ImGui::MenuItem("Unparent")) m_UiCommands.push_back("entity parent " + std::to_string(id) + " none");
			ImGui::EndPopup();
		}
		if (open)
		{
			const std::vector<EntityId> children = e->Children;   // the list can change through m_UiCommands only after drawing
			DrawHierarchyList(children);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	// A few entities as a tree; many (the trees, rocks and wild nodes of a big world, tens of thousands) as a clipped list of one line each:
	// only the rows on screen are built, and one is opened by selecting it (the Inspector shows it).
	void EditorApp::DrawHierarchyList(const std::vector<EntityId>& ids)
	{
		if (ids.size() <= kHierarchyListed)
		{
			for (EntityId id : ids) DrawHierarchyNode(id);
			return;
		}
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(ids.size()));
		while (clipper.Step())
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
			{
				const EntityId id = ids[static_cast<size_t>(i)];
				const GB::Entity* e = m_SceneEditor.World().Find(id);
				if (!e) continue;
				ImGui::PushID(static_cast<int>(id));
				char label[160];
				if (e->Children.empty()) std::snprintf(label, sizeof label, "%s", e->Name.c_str());
				else std::snprintf(label, sizeof label, "%s  (%zu)", e->Name.c_str(), e->Children.size());
				if (ImGui::Selectable(label, id == m_SceneEditor.Selected())) m_SceneEditor.Select(id);
				ImGui::PopID();
			}
	}

	void EditorApp::DrawHierarchy()
	{
		if (ImGui::Begin("Hierarchy", &m_Panels.Hierarchy))
		{
			if (ImGui::Button("+ Add")) ImGui::OpenPopup("##add");
			if (ImGui::BeginPopup("##add"))
			{
				for (const char* kind : { "empty", "cube", "sphere", "plane", "light" })
					if (ImGui::MenuItem(kind)) m_UiCommands.push_back(std::string("entity create ") + kind);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%zu entities", m_SceneEditor.World().Count());
			ImGui::Separator();
			if (m_HierarchyCount != m_SceneEditor.World().Count() || ImGui::GetTime() - m_HierarchyAt > 1.0)   // (the roots, listed again when the scene changes)
			{
				m_HierarchyRoots = m_SceneEditor.World().Roots();
				m_HierarchyCount = m_SceneEditor.World().Count();
				m_HierarchyAt = ImGui::GetTime();
			}
			DrawHierarchyList(m_HierarchyRoots);
			// the empty area: click to deselect, drop an entity to unparent it
			const ImVec2 rest = ImGui::GetContentRegionAvail();
			ImGui::InvisibleButton("##empty", ImVec2(-1, std::max(rest.y, 24.0f)));
			if (ImGui::IsItemClicked()) m_SceneEditor.Select(NullEntity);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("GB_ENTITY"))
					m_UiCommands.push_back("entity parent " + std::to_string(*static_cast<const EntityId*>(pl->Data)) + " none");
				ImGui::EndDragDropTarget();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawInspector()
	{
		if (ImGui::Begin("Inspector", &m_Panels.Inspector))
		{
			const EntityId id = m_SceneEditor.Selected();
			GB::Entity* e = m_SceneEditor.World().Find(id);
			if (!e)
			{
				ImGui::TextDisabled("Nothing selected. Click an object in the viewport or the hierarchy.");
				ImGui::End();
				return;
			}
			if (m_NameFor != id)
			{
				m_NameFor = id;
				std::snprintf(m_NameBuffer, sizeof m_NameBuffer, "%s", e->Name.c_str());
			}
			ImGui::Text("#%u", id);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##name", m_NameBuffer, sizeof m_NameBuffer, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				m_UiCommands.push_back("entity rename " + std::to_string(id) + " \"" + m_NameBuffer + "\"");
				m_NameFor = NullEntity;
			}
			ImGui::Separator();

			const char* groups[] = { "Transform", "Mesh", "Light", "Body" };
			for (const char* group : groups)
			{
				const std::string g = group;
				if ((g == "Mesh" && !e->Mesh) || (g == "Light" && !e->Light) || (g == "Body" && !e->Body)) continue;
				const bool removable = g != "Transform";
				bool visible = true;
				if (!(removable ? ImGui::CollapsingHeader(group, &visible, ImGuiTreeNodeFlags_DefaultOpen) : ImGui::CollapsingHeader(group, ImGuiTreeNodeFlags_DefaultOpen)))
				{
					if (!visible) m_UiCommands.push_back(std::string("entity remove ") + Lower(group));
					continue;
				}
				if (!visible) { m_UiCommands.push_back(std::string("entity remove ") + Lower(group)); continue; }
				for (Property& p : m_Props.All())
				{
					if (p.Group != group || p.Path.rfind("sel.", 0) != 0) continue;
					ImGui::PushID(p.Path.c_str());
					if (DrawPropertyWidget(p, p.Path.c_str() + 4)) PropertyRegistry::Notify(p);
					ImGui::PopID();
				}
				if (std::string(group) == "Mesh" && !e->Mesh->Model.empty())
					ImGui::TextDisabled("model: %s", e->Mesh->Model.c_str());
			}
			ImGui::Separator();
			if (ImGui::Button("Add component", ImVec2(-1, 0))) ImGui::OpenPopup("##component");
			if (ImGui::BeginPopup("##component"))
			{
				if (!e->Mesh && ImGui::MenuItem("Mesh")) m_UiCommands.push_back("entity add mesh");
				if (!e->Light && ImGui::MenuItem("Light")) m_UiCommands.push_back("entity add light");
				if (!e->Body && ImGui::MenuItem("Body")) m_UiCommands.push_back("entity add body");
				ImGui::EndPopup();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawStats()
	{
		if (ImGui::Begin("Stats", &m_Panels.Stats))
		{
			ImGui::Text("%.1f ms  (%.0f fps)", static_cast<double>(m_FrameMsSmooth), 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)));
			ImGui::PlotLines("##frames", m_FrameTimes.data(), static_cast<int>(m_FrameTimes.size()), 0, "frame ms", 0.0f, 60.0f, ImVec2(-1, 60));
			const RenderStats& s = m_Renderer->GetStats();
			ImGui::Text("objects drawn %u   culled %u", s.ObjectsDrawn, s.ObjectsCulled);
			ImGui::Text("viewport %ux%u", m_ViewportW, m_ViewportH);
			ImGui::Text("scene objects %zu  water %zu  grass fields %zu  lights %zu", m_Scene.Objects.size(), m_Scene.Water.size(), m_Scene.Grass.size(), m_Scene.PointLights.size());
			ImGui::Text("particle systems %zu", m_Scene.Particles.size());
			ImGui::Separator();
			ImGui::TextWrapped("%s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
			ImGui::TextWrapped("OpenGL %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
		}
		ImGui::End();
	}

	void EditorApp::DrawModelPanel()
	{
		if (ImGui::Begin("Model", &m_Panels.Model))
		{
			if (!m_Instance) ImGui::TextWrapped("No model loaded. Double-click a .glb in Assets or type: load <file>");
			else
			{
				const auto& clips = m_Model->GetClips();
				ImGui::Text("%zu animation(s)", clips.size());
				for (size_t i = 0; i < clips.size(); ++i)
					if (ImGui::Selectable(clips[i].Name.empty() ? ("clip " + std::to_string(i)).c_str() : clips[i].Name.c_str(), m_Instance->GetCurrentClip() == static_cast<int>(i)))
						m_Instance->Play(static_cast<int>(i), true, 0.25f);
				bool paused = m_Instance->IsPaused();
				if (ImGui::Checkbox("paused", &paused)) m_Instance->SetPaused(paused);
				if (ImGui::Button("Remove model")) { m_Instance.reset(); m_Model.reset(); m_Props.RemovePrefix("model."); }
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawAnimationPanel()
	{
		if (!m_AnimTabRequest.empty()) ImGui::SetNextWindowFocus();   // `panel Animation tab ...` brings it to the front of its dock
		if (ImGui::Begin("Animation", &m_Panels.Animation))
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim)
			{
				ImGui::TextWrapped("No animated character. Start one with: player <character.fbx> (the animation FBX files go in the same folder). Check any character with: character_check <file.fbx>");
				ImGui::End();
				return;
			}
			if (ImGui::BeginTabBar("##animtabs"))
			{
				if (ImGui::BeginTabItem("Character", nullptr, m_AnimTabRequest == "Character" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					const CharacterProfile& p = *m_Player.Profile();
					ImGui::Text("Skeleton: %s", p.Convention.empty() ? "not recognised" : p.Convention.c_str());
					ImGui::Text("Size %.2f m, faces %s", static_cast<double>(p.Height), p.FacesForward ? "+Z" : "-Z");
					ImGui::Text("%d clips, %zu sockets", p.ClipCount, p.Sockets.size());
					ImGui::SeparatorText("Clip roles");
					for (const CharacterProfile::Role& r : p.Roles) ImGui::BulletText("%s: %s", r.Name.c_str(), r.Clip.c_str());
					ImGui::SeparatorText("Sets and montages");
					std::string sets;
					for (const std::string& n : p.Sets) sets += (sets.empty() ? "" : ", ") + n;
					ImGui::TextWrapped("Sets: %s", sets.empty() ? "none" : sets.c_str());
					ImGui::Text("Montages: %zu", p.Montages.size());
					ImGui::SeparatorText("Retargeting");
					static char retargetPath[256] = "";
					ImGui::SetNextItemWidth(-90.0f);
					ImGui::InputTextWithHint("##retarget", "animations.fbx or a folder", retargetPath, sizeof retargetPath);
					ImGui::SameLine();
					if (ImGui::Button("Retarget") && retargetPath[0]) Execute(std::string("retarget ") + retargetPath);
					if (!m_Player.RetargetReport().empty()) ImGui::TextWrapped("%s", m_Player.RetargetReport().c_str());
					ImGui::SeparatorText("Check");
					if (p.Warnings.empty()) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Ready");
					for (const std::string& w : p.Warnings) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", w.c_str());
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Animator", nullptr, m_AnimTabRequest == "Animator" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					const MotionState& m = m_Player.GetMotion();
					ImGui::Text("Speed %.2f m/s  local (%.2f, %.2f)", static_cast<double>(m.Speed), static_cast<double>(m.LocalVelocity.x), static_cast<double>(m.LocalVelocity.y));
					ImGui::Text("%s%s", m.Grounded ? "grounded" : "in the air", m.Blocked ? ", blocked" : "");
					ImGui::SeparatorText("Locomotion set");
					for (const std::string& name : anim->SetNames())
					{
						const bool active = anim->Overlay() == name;
						if (ImGui::RadioButton(name == AnimInstance::kDefaultSet ? "free" : name.c_str(), active)) m_Player.SetOverlay(name);
						ImGui::SameLine();
					}
					ImGui::NewLine();
					if (anim->Transitioning()) ImGui::ProgressBar(anim->TransitionProgress(), ImVec2(-1, 0), "changing set");
					ImGui::SeparatorText("Blend");
					const LocomotionBlender& loco = anim->Active();
					ImGui::Text("phase %.2f   speed x%.2f", static_cast<double>(loco.Phase()), static_cast<double>(loco.SpeedScale()));
					for (const auto& c : loco.Contributions()) ImGui::ProgressBar(c.Weight, ImVec2(-1, 0), c.Clip.c_str());
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Montages", nullptr, m_AnimTabRequest == "Montages" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					MontagePlayer& montages = anim->Montages();
					if (ImGui::Button("Stop")) m_Player.StopMontage();
					ImGui::SameLine();
					ImGui::TextDisabled("%s", m_Player.MontageStatus().c_str());
					ImGui::Text("Slots: FullBody %.0f%%, UpperBody %.0f%%", static_cast<double>(montages.FullBodyWeight() * 100.0f), static_cast<double>(montages.SlotWeight("UpperBody") * 100.0f));
					for (const std::string& name : montages.Names())
					{
						if (ImGui::Button(("Play##" + name).c_str())) m_Player.PlayMontage(name);
						ImGui::SameLine();
						ImGui::Text("%s  (%.1f s)", name.c_str(), static_cast<double>(montages.Duration(name)));
					}
					if (montages.Names().empty()) ImGui::TextWrapped("No montages: put the farming animation FBX files next to the character.");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Sockets", nullptr, m_AnimTabRequest == "Sockets" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					ImGui::SeparatorText("Props");
					for (const char* prop : { "box", "bag", "tool" })
					{
						if (ImGui::Button(prop)) m_Player.AttachProp(prop);
						ImGui::SameLine();
					}
					if (ImGui::Button("let go")) m_Player.DetachProp();
					for (const std::string& n : anim->PropNames()) ImGui::BulletText("%s  (hands %.0f%%)", n.c_str(), static_cast<double>(anim->PropWeight(n) * 100.0f));
					ImGui::SeparatorText("Sockets");
					for (const std::string& n : anim->Sockets().Names())
					{
						const glm::vec3 pos = glm::vec3(anim->Sockets().Matrix(*m_Player.Instance(), n)[3]);
						ImGui::BulletText("%s  (%.2f, %.2f, %.2f)", n.c_str(), static_cast<double>(pos.x), static_cast<double>(pos.y), static_cast<double>(pos.z));
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Ragdoll", nullptr, m_AnimTabRequest == "Ragdoll" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					const bool rag = m_Player.Ragdolled();
					ImGui::TextWrapped("The character as bodies and joints in a physics world made of the ground, water, trees and rocks it walks on.");
					if (!rag && ImGui::Button("Ragdoll")) m_Player.StartRagdoll();
					if (rag && ImGui::Button("Get up")) m_Player.GetUp();
					ImGui::SameLine();
					if (Actor* who = m_Player.Controlled()) ImGui::Checkbox("Get up by itself", &who->AutoGetUp);
					if (Actor* who = m_Player.Controlled()) ImGui::SliderFloat("Falls this hard knock it down (m/s)", &who->RagdollLandingSpeed, 0.0f, 20.0f);
					ImGui::TextDisabled("%s", m_Player.RagdollStatus().empty() ? "standing" : m_Player.RagdollStatus().c_str());
					if (rag)
					{
						if (ImGui::Button("Hit forward")) m_Player.HitRagdoll({ 0.0f, 60.0f, 220.0f });
						ImGui::SameLine();
						if (ImGui::Button("Blast beside it"))
						{
							const glm::vec3 p = m_Player.GetPosition();
							m_Player.Blast({ p.x - 1.5f, p.z }, 7.0f, 14.0f);
						}
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("IK", nullptr, m_AnimTabRequest == "IK" ? ImGuiTabItemFlags_SetSelected : 0))
				{
					bool feet = m_Player.FootIKOn();
					if (ImGui::Checkbox("Feet on the terrain", &feet)) m_Player.SetFootIK(feet);
					FootIK& f = anim->Feet();
					ImGui::Text("pelvis %.2f m   left foot %.2f   right foot %.2f", static_cast<double>(f.PelvisOffset()), static_cast<double>(f.FootOffset(true)), static_cast<double>(f.FootOffset(false)));
					ImGui::SliderFloat("follow sharpness", &f.Config.Sharpness, 2.0f, 30.0f);
					ImGui::SliderFloat("max offset", &f.Config.MaxOffset, 0.05f, 0.8f);
					ImGui::SliderFloat("sole tilt", &f.Config.TiltWeight, 0.0f, 1.0f);
					ImGui::SeparatorText("Look at");
					LookAt& look = anim->Look();
					bool camera = look.HasTarget();
					if (ImGui::Checkbox("Look at the camera", &camera)) { if (camera) m_Player.LookAtCamera(true); else m_Player.ClearLook(); }
					ImGui::Text("yaw %.0f deg  pitch %.0f deg", static_cast<double>(glm::degrees(look.Yaw())), static_cast<double>(glm::degrees(look.Pitch())));
					ImGui::SliderFloat("max yaw", &look.Config.MaxYaw, 0.2f, 1.6f);
					ImGui::SliderFloat("max pitch", &look.Config.MaxPitch, 0.1f, 1.2f);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
				m_AnimTabRequest.clear();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawTestsPanel()
	{
		if (ImGui::Begin("Tests", &m_Panels.Tests))
		{
			ImGui::TextWrapped("The Sandbox test scenes. Run builds them in the viewport (you can then select, move and delete their objects); Sandbox opens the original in its own window.");
			ImGui::Separator();
			for (const TestScenario& s : TestScenarios())
			{
				ImGui::PushID(s.Name.c_str());
				if (ImGui::Button("Run")) Execute("test " + s.Name);
				ImGui::SameLine();
				if (ImGui::Button("Sandbox")) Execute("test " + s.Name + " sandbox");
				ImGui::SameLine();
				ImGui::Text("%s%s", s.Title.c_str(), s.Approximate ? "  (approximation)" : "");
				ImGui::TextDisabled("--scene=%s: %s", s.Name.c_str(), s.Description.c_str());
				ImGui::PopID();
			}
		}
		ImGui::End();
	}

	void EditorApp::DrawConsole()
	{
		if (ImGui::Begin("Console", &m_Panels.Console)) DrawConsoleBody();
		ImGui::End();
	}

	// The log and the command line: the editor's Console panel and the game's console (`game console`) show the same.
	void EditorApp::DrawConsoleBody()
	{
		{
			ImGui::Checkbox("info", &m_ShowInfo); ImGui::SameLine(); ImGui::Checkbox("warn", &m_ShowWarn); ImGui::SameLine(); ImGui::Checkbox("error", &m_ShowErr);
			ImGui::SameLine(); if (ImGui::SmallButton("clear")) m_Log->Clear();
			ImGui::Separator();
			const float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
			ImGui::BeginChild("##lines", ImVec2(0, -footer), false, ImGuiWindowFlags_HorizontalScrollbar);
			for (const LogLine& l : m_Log->Snapshot())
			{
				const bool err = l.Level >= GB::Log::Level::Error, warn = l.Level == GB::Log::Level::Warn;
				if ((err && !m_ShowErr) || (warn && !m_ShowWarn) || (!err && !warn && !m_ShowInfo)) continue;
				ImGui::PushStyleColor(ImGuiCol_Text, err ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : warn ? ImVec4(1.0f, 0.82f, 0.35f, 1.0f) : ImVec4(0.82f, 0.85f, 0.9f, 1.0f));
				ImGui::TextUnformatted(l.Text.c_str());
				ImGui::PopStyleColor();
			}
			if (m_Log->Version() != m_ConsoleVersion) { m_ConsoleVersion = m_Log->Version(); ImGui::SetScrollHereY(1.0f); }
			ImGui::EndChild();
			ImGui::SetNextItemWidth(-1);
			const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
			auto callback = [](ImGuiInputTextCallbackData* d) -> int
			{
				EditorApp* self = static_cast<EditorApp*>(d->UserData);
				if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory)
				{
					const int prev = self->m_HistoryPos;
					if (d->EventKey == ImGuiKey_UpArrow) self->m_HistoryPos = self->m_HistoryPos < 0 ? static_cast<int>(self->m_History.size()) - 1 : std::max(0, self->m_HistoryPos - 1);
					else if (d->EventKey == ImGuiKey_DownArrow && self->m_HistoryPos >= 0) self->m_HistoryPos = self->m_HistoryPos + 1 >= static_cast<int>(self->m_History.size()) ? -1 : self->m_HistoryPos + 1;
					if (prev != self->m_HistoryPos) { d->DeleteChars(0, d->BufTextLen); if (self->m_HistoryPos >= 0) d->InsertChars(0, self->m_History[static_cast<size_t>(self->m_HistoryPos)].c_str()); }
				}
				else if (d->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
				{
					const std::string text(d->Buf, static_cast<size_t>(d->BufTextLen));
					if (text.find(' ') == std::string::npos)
					{
						const auto options = self->m_Commands.Complete(text);
						if (options.size() == 1) { d->DeleteChars(0, d->BufTextLen); d->InsertChars(0, (options[0] + " ").c_str()); }
					}
					else
					{
						const size_t sp = text.find(' ');
						const auto options = self->m_Props.List(text.substr(sp + 1));
						if (!options.empty() && (text.rfind("set", 0) == 0 || text.rfind("get", 0) == 0 || text.rfind("list", 0) == 0))
						{ d->DeleteChars(static_cast<int>(sp) + 1, d->BufTextLen - static_cast<int>(sp) - 1); d->InsertChars(d->CursorPos, options[0].c_str()); }
					}
				}
				return 0;
			};
			if (ImGui::InputTextWithHint("##input", "command (help, set post.ssao 0.8, generate alpine 3, time 21 ...)", m_ConsoleInput, sizeof m_ConsoleInput, flags, callback, this))
			{
				const std::string line = m_ConsoleInput;
				m_ConsoleInput[0] = '\0';
				if (!line.empty())
				{
					m_History.push_back(line);
					m_HistoryPos = -1;
					GB_CLIENT_INFO("> {}", line);
					Execute(line);
				}
				ImGui::SetKeyboardFocusHere(-1);
			}
		}
	}

	void EditorApp::DrawAssets()
	{
		if (ImGui::Begin("Assets", &m_Panels.Assets))
		{
			std::error_code ec;
			if (m_AssetFiles.empty() || ImGui::GetTime() - m_AssetScanAt > 2.0)   // (the folder is read every two seconds, not every frame)
			{
				m_AssetFiles.clear();
				for (auto it = fs::recursive_directory_iterator(m_AssetRoot, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
					if (it->is_regular_file(ec)) m_AssetFiles.push_back(it->path());
				std::sort(m_AssetFiles.begin(), m_AssetFiles.end());
				m_AssetScanAt = ImGui::GetTime();
			}
			const std::vector<fs::path>& files = m_AssetFiles;
			ImGui::TextDisabled("%s", m_AssetRoot.c_str());
			ImGui::TextDisabled("double-click a model (.glb/.gltf) to add it to the scene (right-click: animation viewer), an .hdr to use it as the environment");
			ImGui::BeginChild("##assets");
			for (const fs::path& f : files)
			{
				const std::string rel = fs::relative(f, m_AssetRoot, ec).generic_string();
				const std::string ext = Lower(f.extension().string());
				if (ext == ".md") continue;
				ImGui::PushID(rel.c_str());
				const char* kind = ext == ".glb" || ext == ".gltf" ? "[model]" : ext == ".hdr" ? "[hdri]" : ext == ".jpg" || ext == ".png" ? "[image]" : "";
				if (ImGui::Selectable((std::string(kind) + " " + rel).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0))
				{
					if (ext == ".glb" || ext == ".gltf") Execute("add \"" + f.generic_string() + "\"");
					else if (ext == ".hdr") Execute("hdri \"" + f.generic_string() + "\"");
				}
				if ((ext == ".glb" || ext == ".gltf") && ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Add to scene as an entity"))
					{
						const std::string name = f.stem().string();
						m_UiCommands.push_back("entity create empty \"" + name + "\"");
						m_UiCommands.push_back("entity model \"" + rel + "\"");
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
