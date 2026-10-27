#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/CharacterController.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/Renderer.h"
#include <memory>
#include <string>
#include <vector>

namespace GB::Editor
{
	class SceneEditor;
	class WorldInstance;

	// "Play" in the viewport: a character you walk around the generated world in third person (WASD / left stick,
	// Shift = run, drag the mouse or use the arrows / right stick to look). Trees, rocks and the bodies the user
	// added are obstacles; deep water is a wall. The logic is GB::CharacterController; this class only gathers the
	// obstacles from the entities, owns the model and builds the camera.
	class PlayerMode
	{
	public:
		// `kind`: "fox" (idle / walk / run clips) or "cesium". `near` is where to start (moved off water and trees).
		bool Start(const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
			const glm::vec3& near, std::string& error);
		void Stop();
		bool IsActive() const { return m_Instance != nullptr; }

		// `acceptInput`: false while the user types in a text box. `script` (if any) replaces the keyboard.
		void Update(float dt, bool acceptInput);
		void SetScript(const std::string& text);   // see GB::ParseCharacterScript; time starts now
		void ApplyCamera(Camera& camera) const { m_Camera.Apply(camera); }
		void AddToScene(Scene& scene) const;

		glm::vec3 GetPosition() const { return m_Controller.GetPosition(); }
		float GetSpeed() const { return m_Controller.GetSpeed(); }
		std::string Describe() const;

	private:
		glm::mat4 RootMatrix() const;

		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		CharacterAnimation m_Animation;
		CharacterController m_Controller;
		CharacterWorld m_World;
		mutable ThirdPersonCamera m_Camera;
		std::vector<CharacterScriptStep> m_Script;
		float m_ScriptTime = 0.0f;
		float m_Scale = 1.0f;
		size_t m_Obstacles = 0;
	};
}
