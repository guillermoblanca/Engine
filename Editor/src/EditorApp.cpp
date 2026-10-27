#include "EditorApp.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
		m_Weather = std::make_unique<Weather>(5);
		m_Orbit.Pitch = 0.3f; m_Orbit.Yaw = 0.6f; m_Orbit.Distance = 60.0f; m_Orbit.MinDistance = 1.0f; m_Orbit.MaxDistance = 600.0f;
		m_Camera.FarPlane = 1500.0f;

		RegisterCommands();
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
		if (name == "Properties") return m_Panels.Properties;
		if (name == "World") return m_Panels.World;
		if (name == "Stats") return m_Panels.Stats;
		if (name == "Console") return m_Panels.Console;
		if (name == "Assets") return m_Panels.Assets;
		if (name == "Model") return m_Panels.Model;
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
		m_WorldDesc = desc;
		m_Instance.reset();
		m_World.Build(desc, m_Scene);
		m_StaticObjects = m_Scene.Objects.size();
		m_Scene.Env.ShadowCenter = m_Orbit.Target;
		RebaseWeather();
		FrameWorld();
		if (m_Props.All().size() > 0)
			RegisterWaterProperties();
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
		const CommandResult r = m_Commands.Execute(line);
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

	void EditorApp::RegisterCommands()
	{
		CommandRunner& c = m_Commands;
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
		if (m_ViewportHovered && !io.WantTextInput) m_Orbit.Update(dt);
		m_Orbit.Apply(m_Camera);
		ApplyTimeOfDay();
		m_Weather->Update(dt, m_Camera);
		m_Weather->Apply(m_Scene);
		m_Scene.Env.ShadowCenter = m_Orbit.Target;
		m_Scene.Post.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
		if (m_Instance) m_Instance->Update(dt);
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
		if (m_Instance)
			m_Instance->AddToScene(m_Scene, glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_ModelPosition), m_ModelYaw, glm::vec3(0, 1, 0)), glm::vec3(m_ModelScale)));
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
		ImGuiID center = dockspaceId, left, right, bottom;
		left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.25f, nullptr, &center);
		right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, nullptr, &center);
		bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.27f, nullptr, &center);
		ImGui::DockBuilderDockWindow("Properties", left);
		ImGui::DockBuilderDockWindow("World", right);
		ImGui::DockBuilderDockWindow("Stats", right);
		ImGui::DockBuilderDockWindow("Model", right);
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
		if (m_Panels.Properties) DrawProperties();
		if (m_Panels.World) DrawWorldPanel();
		if (m_Panels.Stats) DrawStats();
		if (m_Panels.Model) DrawModelPanel();
		if (m_Panels.Console) DrawConsole();
		if (m_Panels.Assets) DrawAssets();
		if (m_Panels.Demo) ImGui::ShowDemoWindow(&m_Panels.Demo);
	}

	void EditorApp::DrawMenu()
	{
		if (!ImGui::BeginMainMenuBar()) return;
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::BeginMenu("New world"))
			{
				if (ImGui::MenuItem("Temperate")) Execute("generate temperate");
				if (ImGui::MenuItem("Alpine")) Execute("generate alpine");
				if (ImGui::MenuItem("Desert")) Execute("generate desert");
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Save viewport screenshot")) Execute("screenshot editor_screenshot.png");
			ImGui::Separator();
			if (ImGui::MenuItem("Exit")) Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			ImGui::MenuItem("Viewport", nullptr, &m_Panels.Viewport);
			ImGui::MenuItem("Properties", nullptr, &m_Panels.Properties);
			ImGui::MenuItem("World", nullptr, &m_Panels.World);
			ImGui::MenuItem("Stats", nullptr, &m_Panels.Stats);
			ImGui::MenuItem("Model", nullptr, &m_Panels.Model);
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
				if (std::find(groups.begin(), groups.end(), p.Group) == groups.end()) groups.push_back(p.Group);
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
					if (p.Group != group || (!needle.empty() && Lower(p.Path).find(needle) == std::string::npos)) continue;
					ImGui::PushID(p.Path.c_str());
					bool changed = false;
					const char* label = p.Path.c_str();
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
					case PropType::Vec3: changed = ImGui::DragFloat3(label, &static_cast<glm::vec3*>(p.Ptr)->x, (p.Max - p.Min) * 0.002f, p.Min, p.Max, "%.3g"); break;
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
					if (changed) PropertyRegistry::Notify(p);
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
						{ d->DeleteChars(sp + 1, d->BufTextLen - static_cast<int>(sp) - 1); d->InsertChars(d->CursorPos, options[0].c_str()); }
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
			ImGui::TextDisabled("double-click a model (.glb/.gltf) to load it, an .hdr to use it as the environment");
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
					if (ext == ".glb" || ext == ".gltf") Execute("load \"" + f.generic_string() + "\"");
					else if (ext == ".hdr") Execute("hdri \"" + f.generic_string() + "\"");
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
