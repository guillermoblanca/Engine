#pragma once
#include "GB/Animation/AnimBindings.h"
#include "GB/Assets/Model.h"
#include "GB/Game/Components/Gameplay/AbilityComponent.h"
#include "GB/Game/Components/Presentation/AnimationComponent.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include "GB/Game/Components/Gameplay/InventoryComponent.h"
#include "GB/Game/Components/Gameplay/MovementComponent.h"
#include "GB/Game/Components/Gameplay/OrdersComponent.h"
#include "GB/Game/Components/Presentation/PropsComponent.h"
#include "GB/Game/Components/Physics/RagdollComponent.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/World/Physics.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// What the world gives an actor: the ground, the water, the map edges and the static obstacles it walks among, where to find the
	// interaction points of the scene, and the sounds of its steps. The host (the editor, a game, a test) builds it; the actor copies it.
	struct ActorHost
	{
		CharacterWorld World;                   // GroundHeight, WaterLevel, Bounds and the static Obstacles
		TaskRunner::Resolver Interactions;      // empty: no interaction points
		FootstepSounds* Steps = nullptr;
	};

	// Where the time of an actor's frame goes, in seconds, added up while MeasureCost is on (the numbers that fix the budget per actor).
	struct ActorCost
	{
		double Control = 0.0;     // orders, abilities, controller, motion tracker
		double Animation = 0.0;   // locomotion blend, montages, IK, look, props, skinning of the pose
		double Ragdoll = 0.0;     // the physics step of a fallen actor and the pose read back from it
		long Frames = 0;
	};

	struct ActorSpec
	{
		std::string Name = "Actor";
		float Height = 1.75f;                       // the model is scaled to this
		bool Rigged = true;                         // true: the locomotion blend, sets, IK; false: the plain idle/walk/run switch (Fox, CesiumMan)
		const AnimManifest* Manifest = nullptr;     // null: the built-in Mixamo lists
		std::shared_ptr<const BindingsSetup> Bindings;   // the animation bindings every character shares (null: none)
		std::string Skeleton;                       // the key of the model the bindings are scoped by (SkeletonKey)
		glm::vec2 Start{ 0.0f };                    // xz; moved to free dry ground
		float StartYaw = 0.0f;
		std::vector<CircleObstacle> Others;         // other actors to keep clear of when choosing the spot
	};

	// One character of the world, a GBObject made of components. Always: MovementComponent (the body), OrdersComponent (who gives the orders),
	// AbilityComponent (the link to the ability system) and InventoryComponent. With a model: GeometryComponent, RagdollComponent,
	// AnimationComponent and PropsComponent. An actor without a model walks, works and fights the same and is simply not drawn. The player and every
	// NPC are this class; nothing here knows the camera, the keyboard, ImGui or the editor. This class orders the frame and forwards: what each
	// part does is the component's. Not copyable or movable (the abilities and the components hold its address): ActorWorld keeps them by pointer.
	class Actor : public GBObject
	{
	public:
		Actor();
		~Actor() override;
		void EndPlay() override;     // cancels the abilities and takes their runtime back, then the components end (in reverse)
		void OnDestroy() override;   // the components go, the model's last

		// Loads an FBX character (relative to the assets, or absolute) together with the animation FBX files of its folder. Null (and `error`) on failure.
		static std::shared_ptr<Model> LoadCharacter(const std::string& path, const std::string& assetRoot, std::string& error);

		// Builds the actor, with the model's components when there is a model (it may be shared with others: each actor has its own ModelInstance).
		// A null model makes a logic-only actor. False and `error` when a rigged character has no locomotion clips.
		bool Init(std::shared_ptr<Model> model, const ActorHost& host, const ActorSpec& spec, std::string& error);
		bool Valid() const { return m_Built; }
		bool HasPresentation() const { return m_Geometry != nullptr; }
		bool Paused() const { return State() == ObjectState::Paused; }   // the world's level of detail "suspended": not animated, not drawn, not an obstacle
		// The level the world updated it at in the last frame (0 full, 1 reduced, 2 far); the world sets it, TribeVisuals reads it through ActorWorld::AnimLevel.
		int AnimLevel() const { return m_AnimLevel; }
		void SetAnimLevel(int level) { m_AnimLevel = level; }
		const std::shared_ptr<Model>& GetModel() const { return m_Geometry ? m_Geometry->GetModel() : kNoModel(); }

		// ---- the frame
		// Once per frame. Pushes of SetInput are used (and forgotten) here.
		void Update(float dt) override;
		void SetInput(const CharacterInput& input) { m_Orders->SetInput(input); }
		bool KeysTakeOver = false;   // true: a key pressed drops an AI walk or a task list and the host's input drives (a person playing); false: the orders win
		// The world circles of the other actors, to push against this frame (the static ones come with the host).
		void SetOthers(const std::vector<CircleObstacle>& others) { m_Movement->SetOthers(others); }
		void AddToScene(Scene& scene) const { if (m_Geometry) m_Geometry->AddToScene(scene); if (m_Props) m_Props->AddToScene(scene); }

		// ---- save: name, kind and place are the object's; each component adds its own (InventoryComponent, AnimationComponent, PropsComponent...)
		void CaptureState(Save::ActorState& state) const;
		// Back to a controlled start: orders stopped, resumed, put where it was, and each component restores what it saved.
		void RestoreState(const Save::ActorState& state);

		// ---- orders
		OrderSource Source() const { return m_Orders->Source(); }
		void ReleaseOrders() { m_Orders->Release(); }           // back to Direct: the host's input again
		void SetScript(const std::string& text) { m_Orders->SetScript(text); }
		bool ScriptRunning() const { return m_Orders->ScriptRunning(); }
		float ViewYaw() const { return m_Orders->ViewYaw(); }   // what "forward" is relative to for Direct and Script input; scripts turn it
		void SetViewYaw(float yaw) { m_Orders->SetViewYaw(yaw); }
		void MoveTo(const glm::vec2& xz, bool run) { m_Orders->MoveTo(xz, run, m_Movement->Controller().Config.WalkSpeed, m_Movement->Controller().Config.RunSpeed); }
		// Replaces the static obstacles (trees felled or planted, a door that closed): until now they were the ones the actor was built with.
		void SetStaticObstacles(const std::vector<CircleObstacle>& obstacles) { m_Movement->SetStaticObstacles(obstacles); }
		void SetNavigation(const NavGrid* nav) { m_Movement->SetNavigation(nav); }   // null: straight lines
		NavProfile Navigation;   // how this actor values the terrain when it plans a path (role, load); see GB/Navigation/NavGrid.h
		bool AIArrived() const { return m_Orders->AIArrived(); }
		bool AIStuck() const { return m_Orders->AIStuck(); }
		// Tasks (see GB/Game/TaskList.h): the actor does them like any NPC. False (and `error`) when the text is not a task list.
		bool RunTasks(const std::string& text, std::string& error);
		bool Interact(const std::string& entity, const std::string& point, std::string& error);
		void StopTasks() { m_Orders->StopTasks(); }
		bool TasksRunning() const { return m_Orders->TasksRunning(); }
		bool TasksFailed() const { return m_Orders->TasksFailed(); }
		std::string TaskStatus() const { return m_Orders->TaskStatus(); }
		// Recording and replay: the input given to the controller each frame (after abilities) is kept, and a recording plays back exactly.
		void Record(bool on) { m_Orders->Record(on); }
		const std::vector<CharacterInput>& Recorded() const { return m_Orders->Recorded(); }
		void StartReplay(std::vector<CharacterInput> frames) { m_Orders->StartReplay(std::move(frames)); }
		bool ReplayFinished() const { return m_Orders->ReplayFinished(); }

		// ---- animation (everything from here to "abilities" is the model's components'; without a model the answers are empty and the changes do nothing)
		bool UsesLocomotionBlend() const { return m_Animation && m_Animation->UsesLocomotionBlend(); }
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f) { return m_Animation && m_Animation->SetOverlay(name, fadeSeconds); }
		std::vector<std::string> OverlayNames() const { return m_Animation ? m_Animation->OverlayNames() : std::vector<std::string>(); }
		const AnimManifest& Manifest() const { return m_Animation ? m_Animation->Manifest() : kNoManifest(); }          // what the character plays: the base manifest with the bindings applied
		const AnimManifest& BaseManifest() const { return m_Animation ? m_Animation->BaseManifest() : kNoManifest(); }  // before the bindings
		// The animation bindings of the character (see BindingsSetup): its manifest is the base one with them applied, and the montage of each use of an
		// ability is picked from them by context. `skeleton` is the key of its model. False when nothing changed for this character (the animator is
		// only rebuilt when its manifest is not the one it has).
		bool SetBindings(std::shared_ptr<const BindingsSetup> setup, const std::string& skeleton) { return m_Animation && m_Animation->SetBindings(std::move(setup), skeleton); }
		const std::string& Skeleton() const { return m_Animation ? m_Animation->Skeleton() : kNoString(); }
		const BindingsSetup* Bindings() const { return m_Animation ? m_Animation->Bindings() : nullptr; }
		// Puts the manifest's ability montages (and the notifies the abilities need) into the shared library again: the rules registered it anew.
		void SyncAbilities() { if (m_Animation) m_Animation->SyncAbilities(); }
		// The montage the bindings pick for a use of `ability` (Actor's PickMontage hook of the ability runtime); "" = the ability's own list.
		std::string PickMontage(const AbilityDef& ability, int use, const std::map<std::string, std::string>& context) const { return m_Animation ? m_Animation->PickMontage(ability, use, context) : std::string(); }
		bool LoadManifestFile(const std::string& path, std::string& report) { return Animated(report) && m_Animation->LoadManifestFile(path, report); }
		// The same as loading a manifest file, with the manifest already in memory (the animation bindings apply to one this way).
		bool SetManifest(AnimManifest manifest, std::string& report, const std::string& source = "") { return Animated(report) && m_Animation->SetManifest(std::move(manifest), report, source); }
		bool SaveManifestFile(const std::string& path, std::string& report) const { return Animated(report) && m_Animation->SaveManifestFile(path, report); }
		std::string ManifestInfo() const { return m_Animation ? m_Animation->ManifestInfo() : std::string("no presentation"); }
		bool PlayMontage(const std::string& name, const std::string& section = "") { return m_Animation && m_Animation->PlayMontage(name, section); }
		void StopMontage() { if (m_Animation) m_Animation->StopMontage(); }
		std::vector<std::string> MontageNames() const { return m_Animation ? m_Animation->MontageNames() : std::vector<std::string>(); }
		std::string MontageStatus() const { return m_Animation ? m_Animation->MontageStatus() : std::string(); }
		// Props ("box", "bag", "can", "shovel", "seeds", "tool"): the hands reach the grips with IK and the prop follows its socket.
		bool AttachProp(const std::string& name) { return m_Props && m_Props->Attach(name); }
		void DetachProp(const std::string& name = "") { if (m_Props) m_Props->Detach(name); }   // empty: all of them
		std::vector<std::string> PropNames() const { return m_Props ? m_Props->Names() : std::vector<std::string>(); }
		bool EquipProp(const std::string& name, bool on) { return m_Props && m_Props->Equip(name, on); }   // also changes the walk for the box and the wheelbarrow
		// A prop drawn by a look of the visuals (GB/Visual): `look` is handed to the prop drawer, `socket` is where it hangs ("hand_r", "hip_r",
		// "back"...; "carry" holds it in both arms between the palms, measured from the clips on screen: switch to the carrying set first, see
		// TribeVisuals) and `offset` places it in the socket's frame. The same name replaces the prop. False when the rig has no such socket.
		bool AttachLook(const std::string& name, const std::string& socket, const std::string& look, const glm::mat4& offset = glm::mat4(1.0f)) { return m_Props && m_Props->AttachLook(name, socket, look, offset); }
		std::string PropLookOf(const std::string& name) const { return m_Props ? m_Props->LookOf(name) : std::string(); }
		std::string PropSocketOf(const std::string& name) const { return m_Props ? m_Props->SocketOf(name) : std::string(); }
		// Draws the looks (without one, a prop is a coloured box): the look, the prop's world matrix and the scene. False = draw the box.
		using PropDrawer = PropsComponent::Drawer;
		void SetPropDrawer(PropDrawer drawer) { if (m_Props) m_Props->SetDrawer(std::move(drawer)); }
		// Where the model is drawn: its space to the world (position, facing and the scale to its height). Targets of IK layers go through it.
		glm::mat4 ModelToWorld() const { return m_Geometry ? m_Geometry->RootMatrix() : glm::mat4(1.0f); }
		void SetFootIK(bool on) { if (m_Animation) m_Animation->SetFootIK(on); }
		bool FootIKOn() const { return m_Animation && m_Animation->FootIKOn(); }
		// What the actor carries (H2): a backpack of 12 slots. Saved with the actor.
		Inventory& Bag() { return m_Inventory->Bag(); }
		const Inventory& Bag() const { return m_Inventory->Bag(); }
		void LookAtPoint(const glm::vec3& p) { if (m_Animation) m_Animation->LookAtPoint(p); }
		void ClearLook() { if (m_Animation) m_Animation->ClearLook(); }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; if (m_Animation) m_Animation->SetFootsteps(steps); }
		const ModelInstance* Instance() const { return m_Geometry ? m_Geometry->Instance() : nullptr; }
		AnimInstance* Anim() { return m_Animation ? m_Animation->Anim() : nullptr; }
		const AnimInstance* Anim() const { return m_Animation ? m_Animation->Anim() : nullptr; }
		const CharacterProfile* Profile() const { return m_Animation ? m_Animation->Profile() : nullptr; }
		// Animations made for another skeleton: retargeted onto the model (which every actor that shares it sees; those must call RebuildAnimator).
		bool Retarget(const std::string& path, const std::string& assetRoot, std::string& report) { return Animated(report) && m_Animation->Retarget(path, assetRoot, report); }
		const std::string& RetargetReport() const { return m_Animation ? m_Animation->RetargetReport() : kNoString(); }
		bool RebuildAnimator(std::string& error) { return !m_Animation || m_Animation->RebuildAnimator(error); }
		const MotionState& GetMotion() const { return m_Movement->Motion(); }
		const std::vector<FootPlant>& Plants() const { static const std::vector<FootPlant> none; return m_Animation ? m_Animation->Plants() : none; }

		// ---- abilities (see GB/Gameplay): this actor is the ability system called `name` of `gameplay`
		void SetAbilities(GameplayWorld* gameplay, const std::string& name);
		void PrepareAbilities();
		AbilitySystemComponent* Abilities() { return m_Ability->Abilities(); }
		const std::string& AbilityName() const { return m_Ability->AbilityName(); }
		bool UseAbility(const std::string& ability, const std::string& entity, std::string& error);

		// ---- ragdoll
		bool StartRagdoll(const glm::vec3& extraVelocity = glm::vec3(0.0f));
		void GetUp() { if (m_Ragdoll) m_Ragdoll->GetUp(); }
		bool Ragdolled() const { return m_Ragdoll && m_Ragdoll->Ragdolled(); }
		// Level of detail of the animation for the next Update (H16, see AnimInstance::SetDetail): presentation only, the movement, the abilities
		// and their montages go exactly as at full detail. ActorWorld sets it from the distance to the viewer.
		// `props` false: its props are not drawn (far away they are a few pixels; they stay attached and come back when it is nearer).
		void SetAnimationDetail(bool pose, bool ik, bool props = true) { if (m_Animation) m_Animation->SetDetail(pose, ik); if (m_Props) m_Props->SetVisible(props); }
		bool Blast(const glm::vec2& xz, float radius, float speed);
		void HitRagdoll(const glm::vec3& impulse) { if (m_Ragdoll) m_Ragdoll->Hit(impulse); }
		std::string RagdollStatus() const { return m_Ragdoll ? m_Ragdoll->Status() : std::string(); }
		float RagdollLandingSpeed = 9.0f;   // m/s of a landing that knocks the actor down (0 = never)
		bool AutoGetUp = true;
		bool MeasureCost = false;
		const ActorCost& Cost() const { return m_Cost; }
		void ResetCost() { m_Cost = ActorCost(); }

		// ---- body
		CharacterController& Controller() { return m_Movement->Controller(); }
		const CharacterController& Controller() const { return m_Movement->Controller(); }
		glm::vec3 GetPosition() const { return m_Movement->Controller().GetPosition(); }
		float GetYaw() const { return m_Movement->Controller().GetYaw(); }
		float GetSpeed() const { return m_Movement->Controller().GetSpeed(); }
		float Radius() const { return m_Movement->Controller().Config.Radius; }
		float Height() const { return m_Movement->Height(); }
		// Grows or shrinks it to `height` metres (a child of the tribe, H9): the model's scale, the capsule's radius and how deep it wades follow.
		void SetHeight(float height);
		void Teleport(float x, float z, float yaw);
		const CharacterWorld& World() const { return m_Movement->World(); }
		std::string Describe() const;

	private:
		static const std::shared_ptr<Model>& kNoModel() { static const std::shared_ptr<Model> none; return none; }
		static const AnimManifest& kNoManifest() { static const AnimManifest none = MixamoManifest(); return none; }
		static const std::string& kNoString() { static const std::string none; return none; }
		bool Animated(std::string& report) const { if (!m_Animation) report = "the actor has no presentation (no model)"; return m_Animation != nullptr; }
		void UpdateBody(float dt);
		void SyncTransform();   // the controller is the truth of where it stands; the object's Transform follows
		TaskRunner::AbilityHandler AbilityHandler();
		bool StartAbility(const std::string& ability, const std::string& entity, std::string& error);

		bool m_Built = false;
		int m_AnimLevel = 0;
		TaskRunner::Resolver m_Interactions;
		FootstepSounds* m_Steps = nullptr;
		ActorCost m_Cost;

		// the components, which the object owns (non-owning pointers here)
		MovementComponent* m_Movement = nullptr;
		OrdersComponent* m_Orders = nullptr;
		AbilityComponent* m_Ability = nullptr;
		InventoryComponent* m_Inventory = nullptr;
		GeometryComponent* m_Geometry = nullptr;     // the next four: only with a model
		RagdollComponent* m_Ragdoll = nullptr;
		AnimationComponent* m_Animation = nullptr;
		PropsComponent* m_Props = nullptr;
	};
}
