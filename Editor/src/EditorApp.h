#pragma once
#include "Commands.h"
#include "EditorSave.h"
#include "ImGuiLayer.h"
#include "LogBuffer.h"
#include "PlayerMode.h"
#include "Properties.h"
#include "SceneEditor.h"
#include "TestScenarios.h"
#include "WorldInstance.h"
#include "GB/Application.h"
#include "GB/Audio/FootstepSounds.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Effects/Sky.h"
#include "GB/Effects/Weather.h"
#include "GB/Game/EntityRender.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/CameraController.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace GB::Editor
{
	// The editor: a dockable ImGui interface around a viewport that renders a scene with the engine's renderer.
	// Everything it shows is also reachable by text commands (EditorApp::Execute), so people, scripts and agents
	// drive the same thing: `set post.ssao 0.8`, `generate alpine 4`, `time 21.5`, `screenshot out.png`...
	class EditorApp : public Application
	{
		friend class EditorSaveHost;
	public:
		EditorApp();
		~EditorApp() override;

		CommandResult Execute(const std::string& line);
		void RunScript(const std::string& text);                 // queued: runs a line per frame, `wait N` pauses
		void RunScriptFirst(const std::string& text);            // same, but before what is already queued
		size_t PendingScriptLines() const { return m_Script.size(); }

		PropertyRegistry& Properties() { return m_Props; }
		CommandRunner& Commands() { return m_Commands; }
		Scene& GetScene() { return m_Scene; }
		WorldInstance& World() { return m_World; }
		SceneEditor& Scenes() { return m_SceneEditor; }
		Weather& GetWeather() { return *m_Weather; }
		Framebuffer* Viewport() { return m_ViewportFb.get(); }
		LogBuffer& Log() { return *m_Log; }
		OrbitController& Orbit() { return m_Orbit; }
		bool IsPanelOpen(const std::string& name) const;
		float GetFrameMs() const { return m_FrameMsSmooth; }

	protected:
		void OnUpdate(float dt) override;
		void OnRender() override;

	private:
		void RegisterCommands();
		void RegisterProperties();
		void RegisterWaterProperties();
		void BuildWorld(const WorldDesc& desc);
		void RebaseWeather();
		void ApplyTimeOfDay();
		void SetQuality(const std::string& name);
		void FrameWorld();
		void ResetWorld();                                       // File > New Scene: nothing generated, default sky
		CommandResult LaunchSandbox(const std::string& scene);

		void DrawUI();
		void DrawMenu();
		void DrawViewport();
		void DrawProperties();
		void DrawWorldPanel();
		void DrawHierarchy();
		void DrawHierarchyNode(EntityId id);
		void DrawInspector();
		void DrawGizmo(float originX, float originY, float width, float height);
		void HandleSceneShortcuts();
		void DrawStats();
		void DrawConsole();
		void DrawAssets();
		void DrawModelPanel();
		void DrawAnimationPanel();
		void DrawAbilitiesPanel();
		void DrawTribePanel();
		void DrawBag(const std::string& actorName);                // the inventory of a character, as a table
		std::string PickActor(const struct Ray& ray) const;       // the character under a viewport click ("" = none)
		void SelectActor(const std::string& name, bool focus);   // the character the Abilities and Tribe panels show
		// saved games and replays (EditorSave.cpp)
		void RegisterSaveCommands();
		void StepSimulation(float dt);                           // gameplay and the garden in fixed ticks; also the autosave timer
		bool RecordIfSim(const std::string& line);               // false: an order a replay cannot keep (scene files), which is refused
		void DrawSaveMenu();
		CommandResult SaveSlot(const std::string& name);
		CommandResult LoadSlot(const std::string& name);
		CommandResult ReplayCommand(const std::vector<std::string>& a);
		void BindResources();                                    // resource nodes of the scene: what is gathered goes to the actor's bag
		void StepResources();                                    // tools as tags and depleted nodes, once per simulation step
		CommandResult ResourcesCommand(const std::vector<std::string>& a);
		CommandResult TribeCommand(const std::vector<std::string>& a);
		void SyncGameplay();                                     // ability systems from the scene's entities (after a scene is loaded or the garden built)
		CommandResult AbilityCommand(const std::vector<std::string>& a);
		CommandResult GardenCommand(const std::vector<std::string>& a);
		void DrawTimelinePanel();
		void DrawGraphPanel();
		void DrawTestsPanel();
		void BuildDefaultLayout(unsigned int dockspaceId, float width, float height);

		// scene
		Scene m_Scene;
		Camera m_Camera;
		OrbitController m_Orbit;
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<Framebuffer> m_ViewportFb;
		std::unique_ptr<Weather> m_Weather;
		WorldInstance m_World;
		WorldDesc m_WorldDesc;
		std::unique_ptr<EnvironmentMap> m_EnvMap;
		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		GameplayWorld m_Gameplay;                                // tags, attributes, effects and abilities of the entities and the player (see GB/Gameplay)
		GardenSystem m_Garden;
		ResourceSystem m_Resources;
		int m_FocusTribe = 0;                                    // frames left in which the Tribe panel is brought to the front (after tribe build)
		int m_TimeScale = 1;                                     // simulation steps per frame (`timescale`)
		Tribe m_Tribe;                                           // the first tribe (`tribe build`): workers that gather, carry, craft and light a fire alone
		std::vector<std::unique_ptr<ITribeBody>> m_TribeBodies;
		std::string m_AbilityActor = "Gardener";                 // whose abilities the Abilities panel shows
		ActorWorld m_Actors;                                     // every character of the world: the player's and the NPCs' (`actor` command)
		ItemLibrary m_Items;                                      // era-1 items and recipes, filled on first use of `actor bag`
		RecipeBook m_Recipes;
		std::map<std::string, ResourceNodeDef> m_NodeDefs;
		PlayerMode m_Player;                                     // `player` command / View > Play: walk the world in third person
		size_t m_StaticObjects = 0;
		std::unique_ptr<AudioEngine> m_Audio;
		std::unique_ptr<FootstepSounds> m_Steps;
		std::vector<float> m_AudioRecord;
		std::string m_AudioRecordPath;
		bool m_Recording = false;
		size_t m_WeatherLights = 0;
		SceneEditor m_SceneEditor;
		std::unique_ptr<EntityRenderer> m_EntityRenderer;
		// viewport interaction: click to pick, drag a gizmo axis to move
		int m_GizmoAxis = -1;                 // -1 none, 0..2 = x, y, z while dragging
		float m_GizmoStart = 0.0f;
		glm::vec3 m_GizmoOrigin{ 0.0f };
		glm::vec2 m_PressPos{ 0.0f };
		bool m_PressedInViewport = false;
		std::vector<std::string> m_UiCommands;   // commands issued while drawing, run once the frame is built
		char m_NameBuffer[128] = {};
		EntityId m_NameFor = NullEntity;
		glm::vec3 m_ModelPosition{ 0.0f };
		float m_ModelYaw = 0.0f, m_ModelScale = 1.0f, m_ModelSpeed = 1.0f;

		// time of day
		struct TimeOfDay { bool Enabled = false; float Hours = 12.0f, Azimuth = 200.0f, Clouds = 0.3f, Haze = 0.0f; } m_Tod;
		Environment m_TodBaseEnv;
		DirectionalLight m_TodBaseSun;
		int m_WeatherPreset = 0;

		// saved games
		std::unique_ptr<EditorSaveHost> m_SaveHost;
		Save::AutoSaver m_AutoSaver;
		Save::ReplayRecorder m_Recorder;
		uint64_t m_SimTick = 0;                                  // fixed 1/60 s ticks of gameplay since the world began
		double m_SimAccum = 0.0;
		std::string m_SaveDir = "saves";                         // GB_SAVE_DIR overrides it

		// automation
		PropertyRegistry m_Props;
		CommandRunner m_Commands;
		std::deque<std::string> m_Script;
		int m_ScriptWait = 0;
		std::string m_PendingUiShot;

		// ui
		std::unique_ptr<ImGuiLayer> m_Ui;
		std::shared_ptr<LogBuffer> m_Log;
		struct Panels { bool Viewport = true, Hierarchy = true, Inspector = true, Properties = true, World = true, Stats = true, Console = true, Assets = true, Model = true, Animation = true, Abilities = true, Tribe = true, Timeline = false, Graph = false, Tests = true, Demo = false; } m_Panels;
		bool m_ViewportHovered = false;
		uint32_t m_ViewportW = 960, m_ViewportH = 540;
		bool m_LayoutBuilt = false;
		std::string m_PropFilter;
		char m_ConsoleInput[256] = {};
		std::vector<std::string> m_History;
		int m_HistoryPos = -1;
		bool m_ScrollConsole = true, m_ShowInfo = true, m_ShowWarn = true, m_ShowErr = true;
		size_t m_ConsoleVersion = 0;
		std::vector<float> m_FrameTimes;
		float m_FrameMsSmooth = 16.0f;
		float m_FrameDt = 1.0f / 60.0f;
		int m_WorldTypeIndex = 0;
		std::string m_AssetRoot;
		std::string m_TimelineMontage, m_TimelineFile = "montages.json";
		int m_TimelineNotify = -1;
		bool m_TimelineDrag = false;
		std::string m_GraphState, m_GraphDragging, m_GraphFile = "animgraph.json";
		int m_GraphTransition = -1;
		float m_GraphPanX = 10.0f, m_GraphPanY = 10.0f;
		int m_GraphFocus = 0;
		int m_TimelineFocus = 0;   // frames left to ask for focus (a docked tab needs a couple)
		std::vector<std::string> m_TimelineLog;   // the latest notifies and endings
		std::string m_AnimTabRequest;   // the Animation panel selects this tab on the next frame
	};
}
