#pragma once
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/MotionState.h"
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
		// `kind`: "fox" (idle / walk / run clips), "cesium", or the path of an FBX character (relative to the assets, or absolute): the
		// other FBX files in its folder are loaded as its animation clips, and it walks with the locomotion blend (walk/run/strafe mixed
		// by speed, feet in step, footsteps on every landing) instead of the plain idle/walk/run switch. `near` is where to start (moved off water and trees).
		bool Start(const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
			const glm::vec3& near, std::string& error);
		void Stop();
		bool IsActive() const { return m_Instance != nullptr; }

		// `acceptInput`: false while the user types in a text box. `script` (if any) replaces the keyboard.
		void Update(float dt, bool acceptInput);
		void SetScript(const std::string& text);   // see GB::ParseCharacterScript; time starts now
		// An AI leads the character to a point of the world (xz) instead of the keyboard: same controller, animation and footsteps.
		void MoveTo(const glm::vec2& xz, bool run);
		bool AIArrived() const { return m_UseAI && m_AI.Arrived(); }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; if (m_Animator) m_Animator->SetFootsteps(steps); }
		bool UsesLocomotionBlend() const { return m_Animator != nullptr; }
		const MotionState& GetMotion() const { return m_Motion; }
		void ApplyCamera(Camera& camera) const { m_Camera.Apply(camera); }
		void AddToScene(Scene& scene) const;

		glm::vec3 GetPosition() const { return m_Controller.GetPosition(); }
		float GetSpeed() const { return m_Controller.GetSpeed(); }
		std::string Describe() const;

	private:
		glm::mat4 RootMatrix() const;

		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		CharacterAnimation m_Animation;                          // the plain idle/walk/run switch, for models without a locomotion set
		std::unique_ptr<CharacterAnimator> m_Animator;           // the locomotion blend (set when the model has the clips)
		MotionTracker m_Tracker;
		MotionState m_Motion;
		AIDriver m_AI;
		bool m_UseAI = false;
		FootstepSounds* m_Steps = nullptr;
		CharacterController m_Controller;
		CharacterWorld m_World;
		mutable ThirdPersonCamera m_Camera;
		std::vector<CharacterScriptStep> m_Script;
		float m_ScriptTime = 0.0f;
		float m_Scale = 1.0f;
		size_t m_Obstacles = 0;
	};
}
