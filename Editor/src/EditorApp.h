#pragma once
#include "Commands.h"
#include "ImGuiLayer.h"
#include "LogBuffer.h"
#include "Properties.h"
#include "SceneEditor.h"
#include "WorldInstance.h"
#include "GB/Application.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Effects/Sky.h"
#include "GB/Effects/Weather.h"
#include "GB/Game/EntityRender.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/CameraController.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Renderer.h"
#include <deque>
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
	public:
		EditorApp();
		~EditorApp() override;

		CommandResult Execute(const std::string& line);
		void RunScript(const std::string& text);                 // queued: runs a line per frame, `wait N` pauses
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
		size_t m_StaticObjects = 0;
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

		// automation
		PropertyRegistry m_Props;
		CommandRunner m_Commands;
		std::deque<std::string> m_Script;
		int m_ScriptWait = 0;
		std::string m_PendingUiShot;

		// ui
		std::unique_ptr<ImGuiLayer> m_Ui;
		std::shared_ptr<LogBuffer> m_Log;
		struct Panels { bool Viewport = true, Hierarchy = true, Inspector = true, Properties = true, World = true, Stats = true, Console = true, Assets = true, Model = true, Demo = false; } m_Panels;
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
	};
}
