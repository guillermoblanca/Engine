#pragma once
#include "Commands.h"
#include "EditorSave.h"
#include "ImGuiLayer.h"
#include "LogBuffer.h"
#include "PlayerMode.h"
#include "GB/Telemetry/TelemetrySink.h"
#include "GameView.h"
#include "GameMode.h"
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
#include "GB/Game/TribeVisuals.h"
#include "GB/Visual/SceneVisuals.h"
#include "GB/Gameplay/Garden.h"
#include "GB/Game/Clans.h"
#include "GB/Game/ClanHostility.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/SimQA.h"
#include "GB/Game/GameContent.h"
#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Interactives.h"
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

struct ImDrawList;

namespace GB::Editor
{
	// What the animation commands answer when there is no animated character to act on.
	inline constexpr const char* kNeedsFbxPlayer = "start the player with an FBX character first (player <character.fbx>)";

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
		enum class CamMode { Free, TopDown, Follow };
		CommandResult SetCamMode(CamMode mode, const std::string& member = {});   // camera free | top | follow [member]
		bool GroundUnderCursor(glm::vec3& out);                           // the terrain point under the mouse (the orbit camera zooms towards it)
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
		void DrawNpcInspector(const TribeMember& m);
		void DrawAiDebug(ImDrawList* dl, const glm::mat4& viewProj, const glm::vec2& size, float originX, float originY);
		void DrawClanOverlay(ImDrawList* dl, const glm::mat4& viewProj, const glm::vec2& size, float originX, float originY);
		void RestoreTribe(const std::string& json);   // after a load: the members of a saved tribe, with their actors
		void DrawWorldStatePanel();
		CommandResult EcologyCommand(const std::vector<std::string>& a);
		void StepWorld(double clock);
		void ApplySeasonLook(double clock);                      // weather, snow and foliage colour of the season                            // seasons, rain on the plots and the animals, once per simulation step
		void DrawBag(const std::string& actorName);                // the inventory of a character, as a table
		std::string PickActor(const struct Ray& ray) const;       // the character under a viewport click ("" = none)
		void SelectActor(const std::string& name, bool focus);   // the character the Abilities and Tribe panels show
		// saved games and replays (EditorSave.cpp)
		void RegisterSaveCommands();
		void RegisterAnimationCommands();
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
		CommandResult TelemetryCommand(const std::vector<std::string>& a);
		// H12 (EditorExplore.cpp): the regions of the generated world, kept in step with it and handed to the tribe; `region list|info`.
		void SyncRegions();
		CommandResult RegionCommand(const std::vector<std::string>& a);
		CommandResult AiCommand(const std::vector<std::string>& a);
		CommandResult KnowledgeCommand(const std::vector<std::string>& a);   // `knowledge status [name|clan]`, `knowledge grant <name|clan> <tech>` (H11)
		// H18 (EditorEconomy.cpp): `economy status|plan|give|stockpile|demand` and `field status|mark`: production chains, spoilage, stores and fields.
		CommandResult EconomyCommand(const std::vector<std::string>& a);
		CommandResult FieldCommand(const std::vector<std::string>& a);
		// H14 (EditorConflict.cpp): the fights of the world in the simulation step (a death in the scene is a ragdoll), `combat status|log|hurt|fight`,
		// `hostile [a b [value]]` and `raid <clan>|status`.
		void StepCombat();
		bool RestoreCombat(const std::string& json);             // the "combat" section of a save: {"fights": ..., "hostility": ...}
		CommandResult CombatCommand(const std::vector<std::string>& a);
		CommandResult HostileCommand(const std::vector<std::string>& a);
		CommandResult RaidCommand(const std::vector<std::string>& a);
		// Social life (EditorSocial.cpp, docs/VIDA_SOCIAL.md): `social status`, `rite list|start`, `belief status|omen`, `hunt status|wildlife`; the
		// game of the rules let loose around a new camp, and the "Social life" section of the Tribe panel.
		void RegisterSocialCommands();
		std::string SpawnWildlife(const Tribe& tribe);
		void DrawSocialSection();
		// H15 (GameMode.cpp): `play` makes a game and puts the player in clan 0; `game <action>` is what its keys do; each frame the keys, the
		// quality, the body and camera of the player, the news; the interface over the viewport and the clan panel.
		void RegisterGameCommands();
		CommandResult PlayCommand(const std::vector<std::string>& a);
		CommandResult GameCommand(const std::vector<std::string>& a);
		void StepGame(float dt);
		void GameKeys();
		void RefreshPrompt();
		std::vector<std::string> GameHud() const;
		void DrawGameHud(float ox, float oy, float w, float h);
		void GameNote(const std::string& text);
		GameModeState m_Game;
		ControlRules m_Controls = DefaultControls();             // the "controls" section: keys, interface, how `play` starts
		CommandResult NavCommand(const std::vector<std::string>& a);
		std::string AiStatsLine() const;   // H16: `stats ai` (EditorGameplay.cpp)
		void DrawNavDebug(float ox, float oy, float w, float h);   // blocked and costly cells and the paths walked, over the viewport
		CommandResult InteractivesCommand(const std::vector<std::string>& a);
		CommandResult SiteCommand(const std::vector<std::string>& a);         // H8: where a clan would settle
		CommandResult SettlementCommand(const std::vector<std::string>& a);   // H8: the buildings and sites of the tribe
		void SyncGameplay();                                     // ability systems from the scene's entities (after a scene is loaded or the garden built)
		CommandResult AbilityCommand(const std::vector<std::string>& a);
		CommandResult GardenCommand(const std::vector<std::string>& a);
		void DrawTimelinePanel();
		void DrawGraphPanel();
		void DrawTestsPanel();
		// simulation QA (EditorQA.cpp): `qa`, the live monitor of the invariants and the QA panel
		CommandResult QaCommand(const std::vector<std::string>& a);
		SimView QaView() const;
		void StepQa();
		void DrawQaPanel();
		void BuildDefaultLayout(unsigned int dockspaceId, float width, float height);

		// scene
		Scene m_Scene;
		Camera m_Camera;
		OrbitController m_Orbit;
		CamMode m_CamMode = CamMode::Free;
		std::string m_FollowMember;                              // whose tribe member the Follow camera orbits
		glm::vec2 m_CursorNorm{ 0.5f };                          // the mouse over the viewport, 0..1 (top-left origin), for the ground pick
		std::unique_ptr<Renderer> m_Renderer;
		std::unique_ptr<Framebuffer> m_ViewportFb;
		std::unique_ptr<Weather> m_Weather;
		WorldInstance m_World;
		WorldDesc m_WorldDesc;
		float m_WildResources = 0.0f;   // density of the last `resources world` on this world (0 = none): saved in WorldParams, the nodes go in the baseline
		std::unique_ptr<EnvironmentMap> m_EnvMap;
		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		GameplayWorld m_Gameplay;                                // tags, attributes, effects and abilities of the entities and the player (see GB/Gameplay)
		GardenSystem m_Garden;
		ResourceSystem m_Resources;
		bool m_SeasonVisuals = false;                            // the season drives weather, snow and foliage (`ecology visuals`)
		std::string m_AutoWeather;
		float m_AutoSnow = -1.0f;
		glm::vec3 m_AutoLeaf{ -1.0f };
		EcologySystem m_Ecology;                                 // rabbits that eat, breed and starve (`ecology spawn`)
		CombatSystem m_Combat;                                   // H14: the fights of the world (members, animals), wounds and morale (`combat`)
		HostilityTable m_Hostility;                              // H14: who is hostile to whom until the clans of H13 say it (`hostile`)
		uint64_t m_CombatSeq = 0;                                // the last combat event the editor looked at (deaths become ragdolls)
		double m_HostilityDay = 0.0;                             // the day of the needs' clock the grievances last faded on

		InteractiveSystem m_Interactives;                        // campfire, well and door with a state of their own (`interactives build`)
		BuildingSystem m_Buildings;                              // H8: the construction sites and buildings (their defs are the "buildings" of the content)
		std::string m_NoInteractiveTimers = "{\"timers\":[]}";   // what InteractiveSystem::ToJson says when nothing is burning or filling
		bool m_StrictScript = false;                             // --strict=1: scripted lines that fail make the editor exit with 1
		int m_FocusWorldState = 0, m_FocusAbilities = 0;         // the same for the World state and Abilities panels (panel <name> on)
		int m_FocusTribe = 0;                                    // frames left in which the Tribe panel is brought to the front (after tribe build)
		bool m_AiDebug = false;                                  // `ai debug on`: cones, obstacles and memory drawn in the viewport
		bool m_NavDebug = false;                                 // `nav debug on`
		bool m_ClanOverlay = true;                               // `overlay on|off`: fields, stockpiles and a label (name, trade, task) over each member
		float m_NavSyncTimer = 0.0f;                             // real seconds to the next look at whether trees went or appeared
		int m_TimeScale = 1;                                     // simulation steps per frame (`timescale`)
		RegionMap m_Regions;                                     // H12: the regions of the world (derived from the seed and the wild density, never saved)
		std::string m_RegionsKey;                                // what they were built from ("" = not built)
		int m_RegionTicks = 0;                                   // simulation steps to the next count of the predators
		Tribe m_Tribe;                                           // the first tribe (`tribe build`): workers that gather, carry, craft and light a fire alone
		std::vector<std::unique_ptr<ITribeBody>> m_TribeBodies;
		std::string m_TribeModel = "fox";                        // what the members were made from: the children of the tribe are spawned from it (H9)
		void SetTribeSpawner();                                  // gives the tribe the way to give a newborn an actor
		SimMonitor m_SimQA;                                      // the invariants of the simulation, checked live (`qa`, QA panel)
		bool m_QaWarned = false;
		int m_FocusQA = 0;
		std::string m_QaLastSim, m_QaLastFindings;               // what the last `qa sim` said
		// H13 (EditorClans.cpp): every clan of the world; clan 0 is m_Tribe (the first tribe, saved in the "ai" section as before), the others are
		// kept by the clan world (`clan new`) and saved in the "clans" section. `clan select` chooses the one the Tribe panel and `tribe` reports show.
		ClanWorld m_Clans;
		TelemetrySink m_Telemetry;                               // the telemetry of the run (`telemetry start <file>`, --telemetry=<file>): see docs/DATOS_INDIVIDUO.md
		ClanHostility m_ClanHostility;                           // H14: the hostility of the fights from the clans' diplomacy (the table for the rest)
		// The visual layer (EditorVisuals.cpp, docs/VISUALES.md): node, building and creature looks from assets/data/visuals.json (the "visuals" section of
		// the packs) drawn by the EntityRenderer's hook, and the tribe director (props, activity montages, reach, fields and piles) for every clan.
		SceneVisuals m_Visuals;
		TribeVisuals m_TribeVisuals;
		void BindVisuals();
		void UpdateVisuals(float dt);
		void AddVisualsToScene();
		void RegisterVisualCommands();
		ClanId m_ActiveClan = 0;

		Tribe& ActiveTribe();
		CommandResult ClanCommand(const std::vector<std::string>& a);
		void PrepareClanTribe(Tribe& tribe);                     // binds a kept clan's tribe to the scene, the content, the regions and a spawner
		void ClearKeptClans();                                   // the clans `clan new` made go (their actors stay)
		void RestoreClans(const std::string& json);              // after a load: the kept clans with their members' actors, then the diplomacy
		void DrawClansSection();
		std::unique_ptr<ITribeBody> MakeTribeBody(const std::string& actor);   // a real actor as a member of a tribe (ActorBody)
		bool IsClanZeroBody(const ITribeBody* body) const;       // a body of a member of the first tribe
		std::string m_AbilityActor = "Gardener";                 // whose abilities the Abilities panel shows
		ActorWorld m_Actors;                                     // every character of the world: the player's and the NPCs' (`actor` command)
		ItemLibrary m_Items;                                      // era-1 items and recipes, filled on first use of `actor bag`
		RecipeBook m_Recipes;
		std::map<std::string, ResourceNodeDef> m_NodeDefs;
		TechTree m_Techs = DefaultTechs();                        // the technologies (H11, "techs" of the packs): what the tribe knows, finds and teaches (`knowledge`)
		ContentPacks m_Packs;                                    // the eras as data packs (`pack load`): sections over m_Items, m_Recipes, m_NodeDefs and m_Tribe.Config.Roles
		PlayerMode m_Player;                                     // `player` command / View > Play: walk the world in third person
		GameView m_GameView;                                     // `camera game ...`: follow, overview and cinematic cameras (H19)
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
		struct Panels { bool Viewport = true, Hierarchy = true, Inspector = true, Properties = true, World = true, Stats = true, Console = true, Assets = true, Model = true, Animation = true, Abilities = true, Tribe = true, WorldState = true, Timeline = false, Graph = false, Tests = true, QA = true, Demo = false; } m_Panels;
		bool m_ViewportHovered = false;
		uint32_t m_ViewportW = 960, m_ViewportH = 540;
		float m_RenderScale = 1.0f;          // the 3D view is rendered at this fraction of the viewport (`quality minimum`)
		bool m_MinimumQuality = false;       // the renderer was made without MSAA and with a small shadow map
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
