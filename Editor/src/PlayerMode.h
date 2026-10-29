#pragma once
#include "GB/Game/Actor.h"
#include "GB/Game/ActorWorld.h"
#include "GB/Renderer/Camera.h"
#include <memory>
#include <string>
#include <vector>

namespace GB::Editor
{
	class SceneEditor;
	class WorldInstance;

	// "Play" in the viewport: one actor of the ActorWorld is the player's. This class is the camera that follows it and the reading of the
	// keyboard or pad; everything about the character (model, controller, animation, abilities, orders, ragdoll) is GB::Actor, which an NPC is
	// too. The commands that said "player" keep working: they go to the controlled actor, which can be changed with Control().
	class PlayerMode
	{
	public:
		// The actors of the editor. Must be set before Start and outlive this object.
		void Attach(ActorWorld* actors) { m_Actors = actors; }

		// What a new actor gets from the generated world: its ground and water, and as obstacles the trees (trunks), the rocks and the bodies
		// the user gave to entities. `scene` is where interaction points are looked up.
		static ActorHost MakeHost(WorldInstance& world, const SceneEditor& scene, FootstepSounds* steps, const GameplayWorld* gameplay = nullptr);
		// The navigation grid of the actors (GB/Navigation): built from the generated world on the first call, then only the cells around trees that went
		// or appeared are redone. False (and `error`) when there is no world.
		bool SyncNavigation(WorldInstance& world, const SceneEditor& scene, FootstepSounds* steps, std::string* error = nullptr);
		// Kinds: "fox", "cesium" or the path of an FBX character (relative to the assets, or absolute).
		static bool IsFbx(const std::string& kind);

		// Creates the actor `name` for `kind` near `near` and, with `control`, hands it to the player. Null and `error` on failure.
		Actor* Spawn(const std::string& name, const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
			const glm::vec3& near, FootstepSounds* steps, std::string& error, bool createEntity = false);

		// `kind` as Spawn, with the actor called AbilityActor (the player of the garden) taking over the one of that name, if any.
		bool Start(const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
			const glm::vec3& near, std::string& error);
		// Releases the control; the actor Start made is removed, one that was spawned by hand stays in the world.
		void Stop();
		// Hands the view back without removing the character (it stays in the world, nobody steers it); Control(name) takes it again.
		void Release() { m_Controlled.clear(); m_LookCamera = false; }
		void SetTurnWithQE(bool on) { m_Camera.TurnWithQE = on; }   // false: E / Q are free for the game's own keys (arrows turn the camera)
		bool IsActive() const { return Controlled() != nullptr; }

		// Which actor the player handles (the camera snaps to it). False when there is none of that name.
		bool Control(const std::string& name);
		const std::string& ControlledName() const { return m_Controlled; }
		Actor* Controlled() const { return m_Actors && !m_Controlled.empty() ? m_Actors->Find(m_Controlled) : nullptr; }

		// The frame: BeginFrame feeds the controlled actor the keyboard (`acceptInput`: false while the user types in a text box) and turns the
		// camera; ActorWorld::Update steps everyone; EndFrame moves the camera after the actor.
		void BeginFrame(float dt, bool acceptInput);
		void EndFrame(float dt);
		// BeginFrame, ActorWorld::Update, EndFrame.
		void Update(float dt, bool acceptInput);

		void SetScript(const std::string& text);   // see GB::ParseCharacterScript; time starts now
		void MoveTo(const glm::vec2& xz, bool run) { if (Actor* a = Controlled()) a->MoveTo(xz, run); }
		bool AIArrived() const { const Actor* a = Controlled(); return a && a->AIArrived(); }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; if (Actor* a = Controlled()) a->SetFootsteps(steps); }
		FootstepSounds* Footsteps() const { return m_Steps; }
		bool UsesLocomotionBlend() const { const Actor* a = Controlled(); return a && a->UsesLocomotionBlend(); }
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f) { Actor* a = Controlled(); return a && a->SetOverlay(name, fadeSeconds); }
		std::vector<std::string> OverlayNames() const { const Actor* a = Controlled(); return a ? a->OverlayNames() : std::vector<std::string>(); }
		const AnimManifest& Manifest() const { return Controlled() ? Controlled()->Manifest() : m_NoManifest; }
		bool LoadManifestFile(const std::string& path, std::string& report);
		bool SaveManifestFile(const std::string& path, std::string& report) const;
		std::string ManifestInfo() const;
		bool PlayMontage(const std::string& name, const std::string& section = "") { Actor* a = Controlled(); return a && a->PlayMontage(name, section); }
		void StopMontage() { if (Actor* a = Controlled()) a->StopMontage(); }
		std::vector<std::string> MontageNames() const { const Actor* a = Controlled(); return a ? a->MontageNames() : std::vector<std::string>(); }
		std::string MontageStatus() const { const Actor* a = Controlled(); return a ? a->MontageStatus() : ""; }
		bool AttachProp(const std::string& name) { Actor* a = Controlled(); return a && a->AttachProp(name); }
		void DetachProp(const std::string& name = "") { if (Actor* a = Controlled()) a->DetachProp(name); }
		std::vector<std::string> PropNames() const { const Actor* a = Controlled(); return a ? a->PropNames() : std::vector<std::string>(); }
		void SetFootIK(bool on) { if (Actor* a = Controlled()) a->SetFootIK(on); }
		bool FootIKOn() const { const Actor* a = Controlled(); return a && a->FootIKOn(); }
		void LookAtPoint(const glm::vec3& p) { m_LookCamera = false; if (Actor* a = Controlled()) a->LookAtPoint(p); }
		void LookAtCamera(bool on) { m_LookCamera = on; if (!on) ClearLookOnly(); }
		void ClearLook() { LookAtCamera(false); }
		bool RunTasks(const std::string& text, std::string& error);
		bool Interact(const std::string& entity, const std::string& point, std::string& error);
		void StopTasks() { if (Actor* a = Controlled()) a->StopTasks(); }
		bool TasksRunning() const { const Actor* a = Controlled(); return a && a->TasksRunning(); }
		std::string TaskStatus() const { const Actor* a = Controlled(); return a ? a->TaskStatus() : ""; }
		const ModelInstance* Instance() const { const Actor* a = Controlled(); return a ? a->Instance() : nullptr; }
		AnimInstance* Anim() { Actor* a = Controlled(); return a ? a->Anim() : nullptr; }
		const CharacterProfile* Profile() const { const Actor* a = Controlled(); return a ? a->Profile() : nullptr; }

		// Abilities: the controlled actor is the ability system called AbilityActor() of the gameplay world (when there is one).
		void SetAbilities(GameplayWorld* gameplay, const std::string& actor = "Gardener") { m_Gameplay = gameplay; m_AbilityActor = actor; }
		void PrepareAbilities() { if (m_Actors) m_Actors->PrepareAbilities(); }
		const std::string& AbilityActor() const { return m_AbilityActor; }
		bool UseAbility(const std::string& ability, const std::string& entity, std::string& error);
		bool EquipProp(const std::string& name, bool on) { Actor* a = Controlled(); return a && a->EquipProp(name, on); }

		bool StartRagdoll(const glm::vec3& extraVelocity = glm::vec3(0.0f)) { Actor* a = Controlled(); return a && a->StartRagdoll(extraVelocity); }
		void GetUp() { if (Actor* a = Controlled()) a->GetUp(); }
		bool Ragdolled() const { const Actor* a = Controlled(); return a && a->Ragdolled(); }
		bool Blast(const glm::vec2& xz, float radius, float speed) { Actor* a = Controlled(); return a && a->Blast(xz, radius, speed); }
		void HitRagdoll(const glm::vec3& impulse) { if (Actor* a = Controlled()) a->HitRagdoll(impulse); }
		std::string RagdollStatus() const { const Actor* a = Controlled(); return a ? a->RagdollStatus() : ""; }
		bool Retarget(const std::string& path, const std::string& assetRoot, std::string& report);
		const std::string& RetargetReport() const { const Actor* a = Controlled(); return a ? a->RetargetReport() : m_NoText; }
		const MotionState& GetMotion() const { const Actor* a = Controlled(); return a ? a->GetMotion() : m_NoMotion; }

		void ApplyCamera(Camera& camera) const { m_Camera.Apply(camera); }
		// The angles of the player's camera (OrbitController convention, radians): a game camera following the player takes them (GameView).
		float CameraYaw() const { return m_Camera.Yaw; }
		float CameraPitch() const { return m_Camera.Pitch; }
		void AddToScene(Scene& scene) const { if (m_Actors) m_Actors->AddToScene(scene); }

		glm::vec3 GetPosition() const { const Actor* a = Controlled(); return a ? a->GetPosition() : glm::vec3(0.0f); }
		float GetSpeed() const { const Actor* a = Controlled(); return a ? a->GetSpeed() : 0.0f; }
		std::string Describe() const;

	private:
		void ClearLookOnly() { if (Actor* a = Controlled()) a->ClearLook(); }
		void SnapCamera(const Actor& actor);

		ActorWorld* m_Actors = nullptr;
		std::string m_Controlled;
		std::string m_Owned;              // the actor Start made (removed by Stop)
		GameplayWorld* m_Gameplay = nullptr;
		std::string m_AbilityActor = "Gardener";
		FootstepSounds* m_Steps = nullptr;
		bool m_LookCamera = false;
		AnimManifest m_Shipped;           // the manifest the assets ship, read once per asset folder
		std::string m_ShippedRoot;
		bool m_HasShipped = false;
		ThirdPersonCamera m_Camera;
		AnimManifest m_NoManifest;
		MotionState m_NoMotion;
		std::string m_NoText;
	};
}
