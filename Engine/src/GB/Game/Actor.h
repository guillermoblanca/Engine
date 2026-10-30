#pragma once
#include "GB/Animation/Character.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Animation/Retarget.h"
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/AIDriver.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/MotionState.h"
#include "GB/Game/TaskList.h"
#include "GB/Gameplay/GameplayWorld.h"
#include "GB/Gameplay/Items.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
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

	// Who gives an actor its orders. The same actor can change source at any time (a player that an AI walks somewhere, an NPC that a
	// person takes over): the controller, the animation and the abilities do not know the difference.
	enum class OrderSource
	{
		Direct,   // the input the host pushes with SetInput (keyboard, pad); none this frame = standing still
		AI,       // AIDriver walks it to a point (MoveTo)
		Tasks,    // a task list (RunTasks): go to, interact, wait, ability...
		Script,   // a timeline of keys (SetScript)
		Replay,   // a recording of the inputs of an earlier run (StartReplay)
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
		glm::vec2 Start{ 0.0f };                    // xz; moved to free dry ground
		float StartYaw = 0.0f;
		std::vector<CircleObstacle> Others;         // other actors to keep clear of when choosing the spot
	};

	// One character of the world: model and pose, controller, animation (locomotion sets, montages, props, IK, look, ragdoll), the ability
	// system it is in the scene, and a source of orders. The player and every NPC are this class; nothing here knows the camera, the
	// keyboard, ImGui or the editor. Not copyable or movable (the abilities hold its address): ActorWorld keeps them by pointer.
	class Actor
	{
	public:
		Actor() = default;
		Actor(const Actor&) = delete;
		Actor& operator=(const Actor&) = delete;
		~Actor();

		// Loads an FBX character (relative to the assets, or absolute) together with the animation FBX files of its folder. Null (and `error`) on failure.
		static std::shared_ptr<Model> LoadCharacter(const std::string& path, const std::string& assetRoot, std::string& error);

		// Builds the actor around a model (which may be shared with others: each actor has its own ModelInstance). False and `error` when a rigged
		// character has no locomotion clips.
		bool Init(std::shared_ptr<Model> model, const ActorHost& host, const ActorSpec& spec, std::string& error);
		bool Valid() const { return m_Instance != nullptr; }
		const std::string& Name() const { return m_Name; }
		const std::shared_ptr<Model>& GetModel() const { return m_Model; }

		// ---- the frame
		// Once per frame. Pushes of SetInput are used (and forgotten) here.
		void Update(float dt);
		void SetInput(const CharacterInput& input) { m_Manual = input; m_HasManual = true; }
		bool KeysTakeOver = false;   // true: a key pressed drops an AI walk or a task list and the host's input drives (a person playing); false: the orders win
		// The world circles of the other actors, to push against this frame (the static ones come with the host).
		void SetOthers(const std::vector<CircleObstacle>& others);
		void AddToScene(Scene& scene) const;

		// ---- orders
		OrderSource Source() const { return m_Source; }
		void ReleaseOrders();                                   // back to Direct: the host's input again
		void SetScript(const std::string& text);                // see ParseCharacterScript; time starts now
		bool ScriptRunning() const { return m_Source == OrderSource::Script && !m_Script.empty() && m_ScriptTime < ScriptLength(); }
		float ViewYaw() const { return m_ViewYaw; }             // what "forward" is relative to for Direct and Script input; scripts turn it
		void SetViewYaw(float yaw) { m_ViewYaw = yaw; }
		void MoveTo(const glm::vec2& xz, bool run);
		// Replaces the static obstacles (trees felled or planted, a door that closed): until now they were the ones the actor was built with.
		void SetStaticObstacles(const std::vector<CircleObstacle>& obstacles) { m_World.Obstacles = obstacles; m_StaticObstacles = obstacles.size(); m_World.DynamicFrom = m_StaticObstacles; }
		void SetNavigation(const NavGrid* nav) { m_World.Nav = nav; }   // null: straight lines
		NavProfile Navigation;   // how this actor values the terrain when it plans a path (role, load); see GB/Navigation/NavGrid.h
		bool AIArrived() const { return m_Source == OrderSource::AI && m_AI.Arrived(); }
		bool AIStuck() const { return m_Source == OrderSource::AI && m_AI.Stuck(); }
		// Tasks (see GB/Game/TaskList.h): the actor does them like any NPC. False (and `error`) when the text is not a task list.
		bool RunTasks(const std::string& text, std::string& error);
		bool Interact(const std::string& entity, const std::string& point, std::string& error);
		void StopTasks();
		bool TasksRunning() const { return m_Source == OrderSource::Tasks && m_Tasks.Running(); }
		bool TasksFailed() const { return m_Tasks.Failed(); }
		std::string TaskStatus() const { return m_Tasks.Describe(); }
		// Recording and replay: the input given to the controller each frame (after abilities) is kept, and a recording plays back exactly.
		void Record(bool on) { m_Recording = on; if (on) m_Recorded.clear(); }
		const std::vector<CharacterInput>& Recorded() const { return m_Recorded; }
		void StartReplay(std::vector<CharacterInput> frames);
		bool ReplayFinished() const { return m_Source == OrderSource::Replay && m_ReplayIndex >= m_Replay.size(); }

		// ---- animation
		bool UsesLocomotionBlend() const { return m_Animator != nullptr; }
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f) { return m_Animator && m_Animator->Anim().SetOverlay(name, fadeSeconds); }
		std::vector<std::string> OverlayNames() const { return m_Animator ? m_Animator->Anim().SetNames() : std::vector<std::string>(); }
		const AnimManifest& Manifest() const { return m_Manifest; }
		bool LoadManifestFile(const std::string& path, std::string& report);
		// The same as loading a manifest file, with the manifest already in memory (the animation bindings apply to one this way).
		bool SetManifest(AnimManifest manifest, std::string& report, const std::string& source = "");
		bool SaveManifestFile(const std::string& path, std::string& report) const;
		std::string ManifestInfo() const;
		bool PlayMontage(const std::string& name, const std::string& section = "") { return m_Animator && m_Animator->Anim().Montages().Play(name, section); }
		void StopMontage();
		std::vector<std::string> MontageNames() const { return m_Animator ? m_Animator->Anim().Montages().Names() : std::vector<std::string>(); }
		std::string MontageStatus() const;
		// Props ("box", "bag", "can", "shovel", "seeds", "tool"): the hands reach the grips with IK and the prop follows its socket.
		bool AttachProp(const std::string& name);
		void DetachProp(const std::string& name = "");   // empty: all of them
		std::vector<std::string> PropNames() const { return m_Animator ? m_Animator->Anim().PropNames() : std::vector<std::string>(); }
		bool EquipProp(const std::string& name, bool on);   // also changes the walk for the box and the wheelbarrow
		// A prop drawn by a look of the visuals (GB/Visual): `look` is handed to the prop drawer, `socket` is where it hangs ("hand_r", "hip_r",
		// "back"...; "carry" holds it in both arms between the palms, measured from the clips on screen: switch to the carrying set first, see
		// TribeVisuals) and `offset` places it in the socket's frame. The same name replaces the prop. False when the rig has no such socket.
		bool AttachLook(const std::string& name, const std::string& socket, const std::string& look, const glm::mat4& offset = glm::mat4(1.0f));
		std::string PropLookOf(const std::string& name) const { const auto it = m_PropLooks.find(name); return it == m_PropLooks.end() ? std::string() : it->second.Look; }
		std::string PropSocketOf(const std::string& name) const { const auto it = m_PropLooks.find(name); return it == m_PropLooks.end() ? std::string() : it->second.Socket; }
		// Draws the looks (without one, a prop is a coloured box): the look, the prop's world matrix and the scene. False = draw the box.
		using PropDrawer = std::function<bool(const std::string& look, const glm::mat4& world, Scene& scene)>;
		void SetPropDrawer(PropDrawer drawer) { m_PropDrawer = std::move(drawer); }
		// Where the model is drawn: its space to the world (position, facing and the scale to its height). Targets of IK layers go through it.
		glm::mat4 ModelToWorld() const { return RootMatrix(); }
		void SetFootIK(bool on) { m_FeetOn = on; ApplyFeet(); }
		bool FootIKOn() const { return m_FeetOn; }
		// What the actor carries (H2): a backpack of 12 slots. Saved with the actor.
		Inventory& Bag() { return m_Bag; }
		const Inventory& Bag() const { return m_Bag; }
		void LookAtPoint(const glm::vec3& p) { if (m_Animator) m_Animator->Anim().Look().SetTarget(p); }
		void ClearLook() { if (m_Animator) m_Animator->Anim().Look().ClearTarget(); }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; if (m_Animator) m_Animator->SetFootsteps(steps); }
		const ModelInstance* Instance() const { return m_Instance.get(); }
		AnimInstance* Anim() { return m_Animator ? &m_Animator->Anim() : nullptr; }
		const AnimInstance* Anim() const { return m_Animator ? &m_Animator->Anim() : nullptr; }
		const CharacterProfile* Profile() const { return m_Animator ? &m_Profile : nullptr; }
		// Animations made for another skeleton: retargeted onto the model (which every actor that shares it sees; those must call RebuildAnimator).
		bool Retarget(const std::string& path, const std::string& assetRoot, std::string& report);
		const std::string& RetargetReport() const { return m_RetargetReport; }
		bool RebuildAnimator(std::string& error);
		const MotionState& GetMotion() const { return m_Motion; }
		const std::vector<FootPlant>& Plants() const { static const std::vector<FootPlant> none; return m_Animator ? m_Animator->Anim().Plants() : none; }

		// ---- abilities (see GB/Gameplay): this actor is the ability system called `name` of `gameplay`
		void SetAbilities(GameplayWorld* gameplay, const std::string& name);
		void PrepareAbilities();
		AbilitySystemComponent* Abilities() { return m_Gameplay ? m_Gameplay->Find(m_AbilityName) : nullptr; }
		const std::string& AbilityName() const { return m_AbilityName; }
		bool UseAbility(const std::string& ability, const std::string& entity, std::string& error);

		// ---- ragdoll
		bool StartRagdoll(const glm::vec3& extraVelocity = glm::vec3(0.0f));
		void GetUp();
		bool Ragdolled() const { return m_Animator && m_Animator->Anim().Ragdolled(); }
		// Level of detail of the animation for the next Update (H16, see AnimInstance::SetDetail): presentation only, the movement, the abilities
		// and their montages go exactly as at full detail. ActorWorld sets it from the distance to the viewer.
		// `props` false: its props are not drawn (far away they are a few pixels; they stay attached and come back when it is nearer).
		void SetAnimationDetail(bool pose, bool ik, bool props = true) { if (m_Animator) m_Animator->Anim().SetDetail(pose, ik); m_DrawProps = props; }
		bool Blast(const glm::vec2& xz, float radius, float speed);
		void HitRagdoll(const glm::vec3& impulse);
		std::string RagdollStatus() const;
		float RagdollLandingSpeed = 9.0f;   // m/s of a landing that knocks the actor down (0 = never)
		bool AutoGetUp = true;
		bool MeasureCost = false;
		const ActorCost& Cost() const { return m_Cost; }
		void ResetCost() { m_Cost = ActorCost(); }

		// ---- body
		CharacterController& Controller() { return m_Controller; }
		const CharacterController& Controller() const { return m_Controller; }
		glm::vec3 GetPosition() const { return m_Controller.GetPosition(); }
		float GetYaw() const { return m_Controller.GetYaw(); }
		float GetSpeed() const { return m_Controller.GetSpeed(); }
		float Radius() const { return m_Controller.Config.Radius; }
		float Height() const { return m_Height; }
		// Grows or shrinks it to `height` metres (a child of the tribe, H9): the model's scale, the capsule's radius and how deep it wades follow.
		void SetHeight(float height);
		void Teleport(float x, float z, float yaw);
		const CharacterWorld& World() const { return m_World; }
		std::string Describe() const;

	private:
		glm::mat4 RootMatrix() const;
		void ApplyFeet();
		void BuildPhysics();
		bool BuildAnimator(std::string& error);
		float ScriptLength() const;
		CharacterInput ChooseInput(float dt);
		TaskRunner::AbilityHandler AbilityHandler();

		std::string m_Name;
		float m_Height = 1.75f;
		GameplayWorld* m_Gameplay = nullptr;
		std::string m_AbilityName;
		AbilityRuntime m_AbilityRt;
		TaskRunner::Resolver m_Interactions;
		bool m_FeetOn = true;
		Inventory m_Bag{ 12, 0.0f };
		struct PropLook { glm::vec3 Size{ 0.3f }; glm::vec3 Color{ 0.6f, 0.42f, 0.25f }; std::string Look, Socket; };
		PropDrawer m_PropDrawer;
		std::map<std::string, PropLook> m_PropLooks;
		mutable std::unique_ptr<Mesh> m_PropMesh;
		bool m_DrawProps = true;   // H16: SetAnimationDetail
		std::string m_RetargetReport;
		std::unique_ptr<PhysicsWorld> m_Physics;   // declared before the animator: the ragdoll lives in it and must go first
		float m_RestTime = 0.0f;
		CharacterProfile m_Profile;
		TaskRunner m_Tasks;
		std::string m_PendingProp;   // waits for the box overlay to finish before measuring the grips
		float m_BaseWalk = 0.0f, m_BaseRun = 0.0f;   // the controller's own speeds, before a set with no run clip caps them

		AnimManifest m_Manifest = MixamoManifest();
		ManifestReport m_ManifestReport;
		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		CharacterAnimation m_Animation;                          // the plain idle/walk/run switch, for models without a locomotion set
		std::unique_ptr<CharacterAnimator> m_Animator;           // the locomotion blend (set when the model has the clips)
		MotionTracker m_Tracker;
		MotionState m_Motion;
		AIDriver m_AI;
		bool m_FailLogged = false;   // the failure of the current orders has been logged
		OrderSource m_Source = OrderSource::Direct;
		FootstepSounds* m_Steps = nullptr;
		CharacterController m_Controller;
		CharacterWorld m_World;
		size_t m_StaticObstacles = 0;
		CharacterInput m_Manual;
		bool m_HasManual = false;
		std::vector<CharacterScriptStep> m_Script;
		float m_ScriptTime = 0.0f;
		float m_ViewYaw = 3.14159265f;
		bool m_Recording = false;
		std::vector<CharacterInput> m_Recorded, m_Replay;
		size_t m_ReplayIndex = 0;
		float m_Scale = 1.0f;
		ActorCost m_Cost;
	};
}
