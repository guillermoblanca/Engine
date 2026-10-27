#pragma once
#include "GB/Animation/Character.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/MotionState.h"
#include "GB/Game/TaskList.h"
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/Renderer.h"
#include "GB/World/Physics.h"
#include "GB/Renderer/Mesh.h"
#include <map>
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
		// Loads an FBX character (relative to the assets, or absolute) together with the animation FBX files of its folder. Null (and `error`) on failure.
		static std::shared_ptr<Model> LoadCharacter(const std::string& path, const std::string& assetRoot, std::string& error);

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
		// Switches the locomotion set (free, box, holding, wheelbarrow...) with a cross-fade; false when the character has no such set.
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f) { return m_Animator && m_Animator->Anim().SetOverlay(name, fadeSeconds); }
		std::vector<std::string> OverlayNames() const { return m_Animator ? m_Animator->Anim().SetNames() : std::vector<std::string>(); }
		// One-shot actions over the locomotion (pick_fruit, water, milk_cow...): the FBX player's MontagePlayer. False for an unknown one.
		bool PlayMontage(const std::string& name) { return m_Animator && m_Animator->Anim().Montages().Play(name); }
		void StopMontage() { if (m_Animator) m_Animator->Anim().Montages().StopSlot("FullBody"), m_Animator->Anim().Montages().StopSlot("UpperBody"); }
		std::vector<std::string> MontageNames() const { return m_Animator ? m_Animator->Anim().Montages().Names() : std::vector<std::string>(); }
		std::string MontageStatus() const;
		// Props the FBX player carries: "box" (both hands, with the box walk set), "bag" (on the back), "tool" (one hand). The hands reach the
		// grips with IK and the prop follows its socket. Returns false for an unknown prop or a character without a rig.
		bool AttachProp(const std::string& name);
		void DetachProp(const std::string& name = "");   // empty: all of them
		std::vector<std::string> PropNames() const { return m_Animator ? m_Animator->Anim().PropNames() : std::vector<std::string>(); }
		// Procedural layers of the FBX player: feet on the terrain (on by default) and the head turned toward a world point.
		void SetFootIK(bool on) { m_FeetOn = on; ApplyFeet(); }
		bool FootIKOn() const { return m_FeetOn; }
		void LookAtPoint(const glm::vec3& p) { m_LookCamera = false; if (m_Animator) m_Animator->Anim().Look().SetTarget(p); }
		void LookAtCamera(bool on) { m_LookCamera = on; if (!on && m_Animator) m_Animator->Anim().Look().ClearTarget(); }
		void ClearLook() { LookAtCamera(false); }
		// Tasks (see GB/Game/TaskList.h): the character does them like an NPC would, with the same controller and animation. `interact` uses the
		// interaction points of the scene's entities. False (and `error`) when the text is not a task list.
		bool RunTasks(const std::string& text, std::string& error);
		bool Interact(const std::string& entity, const std::string& point, std::string& error);
		void StopTasks() { m_Tasks.Clear(); }
		bool TasksRunning() const { return m_Tasks.Running(); }
		std::string TaskStatus() const { return m_Tasks.Describe(); }
		// For the animation panel: the locomotion / montage / socket state of the FBX player and the character check made when it was loaded.
		const ModelInstance* Instance() const { return m_Instance.get(); }
		AnimInstance* Anim() { return m_Animator ? &m_Animator->Anim() : nullptr; }
		const CharacterProfile* Profile() const { return m_Animator ? &m_Profile : nullptr; }
		// Ragdoll: the character as bodies and joints in a physics world made of the same ground, water, trees and rocks it walks on. It falls from where it
		// stands (carrying its speed), lies where it lands and gets up after a pause (or on `player_getup`). A hard landing (RagdollLandingSpeed) starts one too.
		bool StartRagdoll(const glm::vec3& extraVelocity = glm::vec3(0.0f));
		void GetUp();
		bool Ragdolled() const { return m_Animator && m_Animator->Anim().Ragdolled(); }
		bool Blast(const glm::vec2& xz, float radius, float speed);   // an explosion on the ground there: affects the ragdoll like anything in the world
		void HitRagdoll(const glm::vec3& impulse);
		std::string RagdollStatus() const;
		float RagdollLandingSpeed = 9.0f;   // m/s of a landing that knocks the character down (0 = never)
		bool AutoGetUp = true;
		const MotionState& GetMotion() const { return m_Motion; }
		// This frame's foot landings (world space): the input for footprints, dust and splashes. Same FootPlant type as FootstepTracker.
		const std::vector<FootPlant>& Plants() const { static const std::vector<FootPlant> none; return m_Animator ? m_Animator->Anim().Plants() : none; }
		void ApplyCamera(Camera& camera) const { m_Camera.Apply(camera); }
		void AddToScene(Scene& scene) const;

		glm::vec3 GetPosition() const { return m_Controller.GetPosition(); }
		float GetSpeed() const { return m_Controller.GetSpeed(); }
		std::string Describe() const;

	private:
		glm::mat4 RootMatrix() const;
		void ApplyFeet();
		bool m_FeetOn = true, m_LookCamera = false;
		struct PropLook { glm::vec3 Size{ 0.3f }; glm::vec3 Color{ 0.6f, 0.42f, 0.25f }; };
		std::map<std::string, PropLook> m_PropLooks;
		mutable std::unique_ptr<Mesh> m_PropMesh;
		void BuildPhysics();
		std::unique_ptr<PhysicsWorld> m_Physics;   // declared before the animator: the ragdoll lives in it and must go first
		float m_RestTime = 0.0f;
		CharacterProfile m_Profile;
		TaskRunner m_Tasks;
		const SceneEditor* m_Edit = nullptr;
		std::string m_PendingProp;   // waits for the box overlay to finish before measuring the grips

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
