#include "EditorApp.h"
#include "GB/Animation/Character.h"
#include "GB/Animation/ClipAnalysis.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

		ApplicationConfig MakeConfig()
		{
			ApplicationConfig c;
			c.Title = "GB Editor";
			c.Width = 1600;
			c.Height = 900;
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

	EditorApp::EditorApp() : Application(MakeConfig())
	{
		if (!IsValid())
			return;
		const LaunchOptions& options = GetLaunchOptions();

		m_Log = std::make_shared<LogBuffer>();
		m_Log->set_pattern("[%H:%M:%S] %v");
		Log::GetCoreLogger()->sinks().push_back(m_Log);
		Log::GetClientLogger()->sinks().push_back(m_Log);

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
		m_SceneEditor.OnNewScene = [this]() { ResetWorld(); };   // File > New Scene also drops the generated world
		m_SceneEditor.SetPropBounds([this](const PropComponent& p, glm::vec3& center, float& radius) { return m_World.PropBounds(p, center, radius); });
		m_Weather = std::make_unique<Weather>(5);
		m_Orbit.Pitch = 0.3f; m_Orbit.Yaw = 0.6f; m_Orbit.Distance = 60.0f; m_Orbit.MinDistance = 1.0f; m_Orbit.MaxDistance = 600.0f;
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
		RegisterCommands();
		m_SceneEditor.Bind(m_Props);
		m_SceneEditor.Register(m_Commands);
		m_EntityRenderer = std::make_unique<EntityRenderer>(m_AssetRoot);
		m_EntityRenderer->SetPropBuilder([this](const PropComponent& p, const glm::mat4& m, Scene& s) { return m_World.AddProp(p, m, s); });
		BuildWorld(m_WorldDesc);
		RegisterProperties();

		const std::string quality = options.GetValue("quality");
		if (!quality.empty()) SetQuality(quality);
		const std::string exec = options.GetValue("exec");
		if (!exec.empty()) RunScript(exec);
		const std::string script = options.GetValue("script");
		if (!script.empty())
		{
			std::ifstream in(script);
			if (!in) GB_CLIENT_ERROR("Could not read the script '{}'", script);
			else { std::stringstream ss; ss << in.rdbuf(); RunScript(ss.str()); }
		}
		GB_CLIENT_INFO("GB Editor ready. Type 'help' in the console for the commands.");
	}

	EditorApp::~EditorApp()
	{
		// the scene's GPU objects must go before the context does
		m_Instance.reset();
		m_World.Clear(m_Scene);
		if (m_Log)
		{
			auto drop = [&](std::shared_ptr<spdlog::logger>& logger)
			{
				auto& sinks = logger->sinks();
				sinks.erase(std::remove(sinks.begin(), sinks.end(), std::static_pointer_cast<spdlog::sinks::sink>(m_Log)), sinks.end());
			};
			drop(Log::GetCoreLogger());
			drop(Log::GetClientLogger());
		}
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
		if (name == "Tests") return m_Panels.Tests;
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

	void EditorApp::BuildWorld(const WorldDesc& desc)
	{
		m_Player.Stop();   // it points into the old terrain
		m_WorldDesc = desc;
		m_Instance.reset();
		m_SceneEditor.RemoveEnvironment();   // the old trees and rocks go with the old world; what the user added stays
		m_World.Build(desc, m_Scene);
		m_StaticObjects = m_Scene.Objects.size();   // the terrain; entities are appended every frame
		m_SceneEditor.PopulateEnvironment(m_World.World().Trees, m_World.World().Rocks);
		m_Scene.Env.ShadowCenter = m_Orbit.Target;
		m_SceneEditor.SetSpawnPoint(m_Orbit.Target);
		RebaseWeather();
		FrameWorld();
		if (m_Props.All().size() > 0)
			RegisterWaterProperties();
	}

	void EditorApp::ResetWorld()
	{
		m_Player.Stop();
		m_Instance.reset();
		m_Model.reset();
		m_Props.RemovePrefix("model.");
		m_Props.Set("tod.enabled", "false");
		m_Props.Set("weather.preset", "clear");
		m_Weather->Snap();
		m_World.Clear(m_Scene);
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
		p = fresh;
		if (name == "low") {}
		else if (name == "medium") { p.SSAO = 0.4f; }
		else if (name == "high") { p.SSAO = 0.55f; p.Grain = 0.15f; }
		else if (name == "ultra") { p.SSAO = 0.75f; p.AutoExposure = true; p.Grain = 0.25f; p.ChromaticAberration = 0.2f; p.Saturation = 1.08f; p.Contrast = 1.06f; }
		else { GB_CLIENT_WARN("Unknown quality '{}' (low, medium, high, ultra)", name); return; }
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
			m_Props.Float(p + "waveHeight", q.WaveHeight, 0.0f, 1.5f, "Water");
			m_Props.Float(p + "waveLength", q.WaveLength, 1.0f, 40.0f, "Water");
			m_Props.Float(p + "steepness", q.Steepness, 0.0f, 1.0f, "Water");
			m_Props.Float(p + "choppiness", q.Choppiness, 0.0f, 3.0f, "Water");
			m_Props.Float(p + "clarity", q.Clarity, 0.1f, 12.0f, "Water");
			m_Props.Float(p + "foam", q.Foam, 0.0f, 2.0f, "Water");
			m_Props.Float(p + "roughness", q.Roughness, 0.01f, 0.6f, "Water");
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
		const CommandResult r = m_Commands.Execute(line);
		m_SceneEditor.Tick(m_GizmoAxis >= 0);   // an edit made by `set sel.*` is its own undo step
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
		c.Register("generate", "generate <temperate|alpine|desert> [seed] [trees] [rocks]: builds a new world", [this](const std::vector<std::string>& a) -> CommandResult
		{
			WorldDesc d = m_WorldDesc;
			if (!a.empty())
			{
				const std::string t = Lower(a[0]);
				if (t == "temperate") d.Type = WorldType::Temperate;
				else if (t == "alpine") d.Type = WorldType::Alpine;
				else if (t == "desert") d.Type = WorldType::Desert;
				else return { false, "unknown world type '" + a[0] + "' (temperate, alpine, desert)" };
			}
			if (a.size() > 1) d.Seed = static_cast<uint32_t>(std::atoi(a[1].c_str()));
			if (a.size() > 2) d.TreeBudget = std::max(0, std::atoi(a[2].c_str()));
			if (a.size() > 3) d.RockBudget = std::max(0, std::atoi(a[3].c_str()));
			BuildWorld(d);
			return { true, "world generated (seed " + std::to_string(d.Seed) + ")" };
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
		c.Register("quality", "quality <low|medium|high|ultra>: post-processing preset", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: quality <low|medium|high|ultra>" };
			SetQuality(a[0]);
			return { true, "" };
		});
		c.Register("camera", "camera <yaw> <pitch> <distance> [x y z]: places the orbit camera (angles in degrees)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 3) return { false, "usage: camera <yaw> <pitch> <distance> [x y z]" };
			m_Orbit.Yaw = glm::radians(static_cast<float>(std::atof(a[0].c_str())));
			m_Orbit.Pitch = glm::radians(static_cast<float>(std::atof(a[1].c_str())));
			m_Orbit.Distance = static_cast<float>(std::atof(a[2].c_str()));
			if (a.size() >= 6) m_Orbit.Target = { static_cast<float>(std::atof(a[3].c_str())), static_cast<float>(std::atof(a[4].c_str())), static_cast<float>(std::atof(a[5].c_str())) };
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
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : m_Player.OverlayNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, "sets: " + names + " (default = free movement)" };
			}
			const float fade = a.size() > 1 ? static_cast<float>(std::atof(a[1].c_str())) : 0.3f;
			return m_Player.SetOverlay(a[0], fade) ? CommandResult{ true, "set " + a[0] } : CommandResult{ false, "no such set '" + a[0] + "'" };
		});
		c.Register("player_montage", "player_montage [name|stop]: plays a one-shot action (pick_fruit, water, milk_cow...) over the FBX player; no name lists them", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : m_Player.MontageNames()) names += (names.empty() ? "" : ", ") + n;
				return { true, names.empty() ? "this character has no montages (put the farming animation FBX files next to it)" : "montages: " + names };
			}
			if (a[0] == "stop") { m_Player.StopMontage(); return { true, "stopped" }; }
			return m_Player.PlayMontage(a[0]) ? CommandResult{ true, "playing " + a[0] } : CommandResult{ false, "no such montage '" + a[0] + "'" };
		});
		c.Register("player_attach", "player_attach [box|bag|tool|off]: the FBX player carries a prop (box in both hands with IK, a bag on the back, a tool in the hand); off lets go; no argument lists what is carried", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
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
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
			if (a.size() == 1 && a[0] == "off") { m_Player.ClearLook(); return { true, "looking forward" }; }
			if (a.size() == 1 && a[0] == "camera") { m_Player.LookAtCamera(true); return { true, "looking at the camera" }; }
			if (a.size() < 3) return { false, "usage: player_look <x> <y> <z> | camera | off" };
			m_Player.LookAtPoint({ static_cast<float>(std::atof(a[0].c_str())), static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) });
			return { true, "looking at (" + a[0] + ", " + a[1] + ", " + a[2] + ")" };
		});
		c.Register("player_feet", "player_feet on|off: the FBX player's feet follow the terrain with IK (on by default)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
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
		c.Register("player_tasks", "player_tasks <task list>|stop: the player does a list of tasks like an NPC (goto x z [run] | interact <entity> [point] | wait s | attach prop | detach [prop], separated by '|' or new lines)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive()) return { false, "start the player first (player <character.fbx>)" };
			if (a.empty()) return { true, m_Player.TaskStatus() };
			if (a.size() == 1 && a[0] == "stop") { m_Player.StopTasks(); return { true, "stopped" }; }
			std::string text;
			for (size_t i = 0; i < a.size(); ++i) text += (i ? " " : "") + a[i];
			std::string error;
			return m_Player.RunTasks(text, error) ? CommandResult{ true, "running: " + m_Player.TaskStatus() } : CommandResult{ false, error };
		});
		c.Register("character_check", "character_check [character.fbx]: checks a character (the player's when no file is given): skeleton convention, bones, size and facing, clip roles, sets, montages and what is wrong", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty())
			{
				if (!m_Player.Profile()) return { false, "start the player with an FBX character first, or give a file: character_check <character.fbx>" };
				return { true, FormatCharacterProfile(*m_Player.Profile()) };
			}
			std::string error;
			const auto model = PlayerMode::LoadCharacter(a[0], m_AssetRoot, error);
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
				const auto model = PlayerMode::LoadCharacter(a[1], m_AssetRoot, error);
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
			else if (n == "Animation") flag = &m_Panels.Animation; else if (n == "Tests") flag = &m_Panels.Tests;
			if (!flag) return { false, "no such panel '" + n + "'" };
			if (a.size() > 2 && a[1] == "tab") { m_AnimTabRequest = a[2]; *flag = true; return { true, n + ": tab " + a[2] }; }
			*flag = a.size() > 1 ? (a[1] != "off" && a[1] != "0") : !*flag;
			return { true, n + (*flag ? " shown" : " hidden") };
		});
		c.Register("player_ragdoll", "player_ragdoll [vx vy vz]: the FBX player collapses into a ragdoll (bodies and joints in a physics world made of the same ground, water, trees and rocks), keeping its speed plus the given one; it gets up by itself after a pause", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
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
			if (!m_Player.IsActive() || !m_Player.UsesLocomotionBlend()) return { false, "start the player with an FBX character first (player <character.fbx>)" };
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
		c.Register("load", "load <model.glb|gltf>: puts a glTF model in the scene (animated clips are listed in the Model panel)", [this](const std::vector<std::string>& a) -> CommandResult
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
		c.Register("stats", "stats: frame time, objects drawn/culled and world counts", [this](const std::vector<std::string>&) -> CommandResult
		{
			const RenderStats& s = m_Renderer->GetStats();
			char buf[256];
			std::snprintf(buf, sizeof buf, "frame %.1f ms (%.0f fps), viewport %ux%u, objects drawn %u culled %u, trees %zu rocks %zu grass blades %zu",
				static_cast<double>(m_FrameMsSmooth), 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)), m_ViewportW, m_ViewportH, s.ObjectsDrawn, s.ObjectsCulled,
				m_World.TreeCount(), m_World.RockCount(), m_World.GrassBlades());
			return { true, buf };
		});
		c.Register("clear", "clear: empties the console", [this](const std::vector<std::string>&) -> CommandResult { m_Log->Clear(); return { true, "" }; });
		c.Register("quit", "quit: closes the editor", [this](const std::vector<std::string>&) -> CommandResult { Close(); return { true, "" }; });
	}

	// ------------------------------------------------------------------ frame
	void EditorApp::OnUpdate(float dt)
	{
		m_FrameDt = dt;
		const float ms = dt * 1000.0f;
		m_FrameMsSmooth = m_FrameMsSmooth * 0.9f + ms * 0.1f;
		m_FrameTimes.push_back(ms);
		if (m_FrameTimes.size() > 120) m_FrameTimes.erase(m_FrameTimes.begin());

		// scripted commands: one line per frame (so a `screenshot` sees the effect of the previous line)
		if (m_ScriptWait > 0) --m_ScriptWait;
		else if (!m_Script.empty())
		{
			while (!m_Script.empty() && m_ScriptWait == 0)
			{
				const std::string line = m_Script.front();
				m_Script.pop_front();
				GB_CLIENT_INFO("> {}", line);
				const CommandResult r = Execute(line);
				if (!r.Ok) GB_CLIENT_WARN("script line failed, continuing");
				if (Lower(line).rfind("shot_ui", 0) == 0 || Lower(line).rfind("screenshot", 0) == 0) break;   // render before the next line
			}
		}

		ImGuiIO& io = ImGui::GetIO();
		if (m_Player.IsActive())
		{
			m_Player.Update(dt, m_ViewportHovered && !io.WantTextInput);
			m_Player.ApplyCamera(m_Camera);
			m_Orbit.Target = m_Player.GetPosition();   // the shadows and the weather follow the player
		}
		else
		{
			if (m_ViewportHovered && !io.WantTextInput && m_GizmoAxis < 0) m_Orbit.Update(dt);
			m_Orbit.Apply(m_Camera);
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
		if (m_ViewportFb->GetWidth() != m_ViewportW || m_ViewportFb->GetHeight() != m_ViewportH)
		{
			m_ViewportFb = std::make_unique<Framebuffer>(m_ViewportW, m_ViewportH);
			m_Renderer->Resize(m_ViewportW, m_ViewportH);
		}
		m_Scene.Objects.resize(m_StaticObjects);
		m_Scene.PointLights.resize(m_WeatherLights);
		m_EntityRenderer->Update(m_FrameDt);
		m_EntityRenderer->Build(m_SceneEditor.World(), m_Scene, m_SceneEditor.Selected());
		if (m_Instance)
			m_Instance->AddToScene(m_Scene, glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_ModelPosition), m_ModelYaw, glm::vec3(0, 1, 0)), glm::vec3(m_ModelScale)));
		m_Player.AddToScene(m_Scene);
		m_Renderer->Render(m_Scene, m_Camera, m_ViewportFb.get());

		// the interface
		Framebuffer::BindDefault(ww, wh);
		glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		m_Ui->BeginFrame(m_FrameDt, ww, wh);
		DrawUI();
		m_Ui->EndFrame();

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
		right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.27f, nullptr, &center);
		bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Properties", left);
		ImGui::DockBuilderDockWindow("Hierarchy", leftTop);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("World", right);
		ImGui::DockBuilderDockWindow("Stats", right);
		ImGui::DockBuilderDockWindow("Model", right);
		ImGui::DockBuilderDockWindow("Animation", right);
		ImGui::DockBuilderDockWindow("Tests", bottom);
		ImGui::DockBuilderDockWindow("Console", bottom);
		ImGui::DockBuilderDockWindow("Assets", bottom);
		ImGui::DockBuilderDockWindow("Viewport", center);
		ImGui::DockBuilderFinish(dockspaceId);
	}

	void EditorApp::DrawUI()
	{
		ImGuiViewport* vp = ImGui::GetMainViewport();
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
		if (m_Panels.Tests) DrawTestsPanel();
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
			ImGui::Separator();
			ImGui::MenuItem("Hierarchy", nullptr, &m_Panels.Hierarchy);
			ImGui::MenuItem("Inspector", nullptr, &m_Panels.Inspector);
			ImGui::MenuItem("Tests", nullptr, &m_Panels.Tests);
			ImGui::MenuItem("Viewport", nullptr, &m_Panels.Viewport);
			ImGui::MenuItem("Properties", nullptr, &m_Panels.Properties);
			ImGui::MenuItem("World", nullptr, &m_Panels.World);
			ImGui::MenuItem("Stats", nullptr, &m_Panels.Stats);
			ImGui::MenuItem("Model", nullptr, &m_Panels.Model);
			ImGui::MenuItem("Animation", nullptr, &m_Panels.Animation);
			ImGui::MenuItem("Console", nullptr, &m_Panels.Console);
			ImGui::MenuItem("Assets", nullptr, &m_Panels.Assets);
			ImGui::Separator();
			ImGui::MenuItem("ImGui demo", nullptr, &m_Panels.Demo);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Quality"))
		{
			for (const char* q : { "low", "medium", "high", "ultra" })
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

	void EditorApp::DrawViewport()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		if (ImGui::Begin("Viewport", &m_Panels.Viewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const uint32_t w = static_cast<uint32_t>(std::max(avail.x, 64.0f)), h = static_cast<uint32_t>(std::max(avail.y, 64.0f));
			if (std::abs(static_cast<int>(w) - static_cast<int>(m_ViewportW)) > 1 || std::abs(static_cast<int>(h) - static_cast<int>(m_ViewportH)) > 1)
			{
				m_ViewportW = w;
				m_ViewportH = h;
			}
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(m_ViewportFb->GetColorTexture())), ImVec2(static_cast<float>(m_ViewportFb->GetWidth()), static_cast<float>(m_ViewportFb->GetHeight())),
				ImVec2(0, 1), ImVec2(1, 0));
			m_ViewportHovered = ImGui::IsItemHovered();
			DrawGizmo(origin.x, origin.y, static_cast<float>(m_ViewportFb->GetWidth()), static_cast<float>(m_ViewportFb->GetHeight()));
			char info[96];
			std::snprintf(info, sizeof info, "%.0f fps   %ux%u", 1000.0 / static_cast<double>(std::max(m_FrameMsSmooth, 0.01f)), m_ViewportW, m_ViewportH);
			ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), IM_COL32(255, 255, 255, 200), info);
		}
		ImGui::End();
		ImGui::PopStyleVar();
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
			ImGui::SliderInt("tree budget", &m_WorldDesc.TreeBudget, 0, 1500);
			ImGui::SliderInt("rock budget", &m_WorldDesc.RockBudget, 0, 300);
			ImGui::SliderFloat("forest", &m_WorldDesc.ForestAmount, 0.0f, 1.0f);
			ImGui::Checkbox("river", &m_WorldDesc.River); ImGui::SameLine(); ImGui::Checkbox("lake", &m_WorldDesc.Lake);
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
		if (io.WantTextInput) return;
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Execute(io.KeyShift ? "redo" : "undo");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Execute("redo");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && m_SceneEditor.Selected() != NullEntity) Execute("entity duplicate");
		else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Execute("scene save scene.json");
		else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_SceneEditor.Selected() != NullEntity) Execute("entity delete");
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
				m_SceneEditor.Select(m_SceneEditor.Pick(ScreenRay(m_Camera, mouse / size, aspect)));
		}
	}

	void EditorApp::DrawHierarchyNode(EntityId id)
	{
		const GB::Entity* e = m_SceneEditor.World().Find(id);
		if (!e) return;
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
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
			for (EntityId c : children) DrawHierarchyNode(c);
			ImGui::TreePop();
		}
		ImGui::PopID();
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
			for (EntityId root : m_SceneEditor.World().Roots()) DrawHierarchyNode(root);
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
					ImGui::Checkbox("Get up by itself", &m_Player.AutoGetUp);
					ImGui::SliderFloat("Falls this hard knock it down (m/s)", &m_Player.RagdollLandingSpeed, 0.0f, 20.0f);
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
		if (ImGui::Begin("Console", &m_Panels.Console))
		{
			ImGui::Checkbox("info", &m_ShowInfo); ImGui::SameLine(); ImGui::Checkbox("warn", &m_ShowWarn); ImGui::SameLine(); ImGui::Checkbox("error", &m_ShowErr);
			ImGui::SameLine(); if (ImGui::SmallButton("clear")) m_Log->Clear();
			ImGui::Separator();
			const float footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
			ImGui::BeginChild("##lines", ImVec2(0, -footer), false, ImGuiWindowFlags_HorizontalScrollbar);
			for (const LogLine& l : m_Log->Snapshot())
			{
				const bool err = l.Level >= spdlog::level::err, warn = l.Level == spdlog::level::warn;
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
		ImGui::End();
	}

	void EditorApp::DrawAssets()
	{
		if (ImGui::Begin("Assets", &m_Panels.Assets))
		{
			std::error_code ec;
			std::vector<fs::path> files;
			for (auto it = fs::recursive_directory_iterator(m_AssetRoot, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
				if (it->is_regular_file(ec)) files.push_back(it->path());
			std::sort(files.begin(), files.end());
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
