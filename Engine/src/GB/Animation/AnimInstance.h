#pragma once
#include "GB/Animation/AirLayer.h"
#include "GB/Animation/AnimLayer.h"
#include "GB/Animation/AnimGraph.h"
#include "GB/Animation/Locomotion.h"
#include "GB/Animation/FootIK.h"
#include "GB/Animation/IK.h"
#include "GB/Animation/LookAt.h"
#include "GB/Animation/Montage.h"
#include "GB/Animation/Ragdoll.h"
#include "GB/Animation/Socket.h"
#include "GB/Animation/TurnLayer.h"
#include "GB/Audio/FootstepSounds.h"
#include "GB/Game/MotionState.h"
#include "GB/World/Footsteps.h"
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// A family of clips that move a character in one posture or while carrying one kind of thing: its idle, its walk,
	// its run, its turns. The clips are placed by what they do (their measured root velocity), so any mix works:
	// {idle, walk} for a character carrying a box, {idle, walk, run, strafes} for free movement.
	struct LocomotionSetDesc
	{
		std::string Name;
		std::vector<std::string> Clips;   // clip names in the model
	};

	// Where a hand should hold a prop: `Socket` is "hand_l" or "hand_r" and `Local` the palm's frame in the prop's own space. The arm
	// reaches it with IK, so a box is held by both sides whatever the clip did and whatever the character's height is.
	struct GripPoint
	{
		std::string Socket;
		glm::mat4 Local{ 1.0f };
		float Weight = 1.0f;
	};

	// Something the character carries: it follows a socket of the body (`Offset` places the prop in the socket's frame), and
	// each grip pulls a hand onto it.
	struct Prop
	{
		std::string Name;
		std::string Socket = "hand_r";
		glm::mat4 Offset{ 1.0f };
		std::vector<GripPoint> Grips;
		float FadeSeconds = 0.25f;   // the hands reach for it (or let go) over this long
	};

	// The per-character animation state: several locomotion sets (free, carrying a box, pushing a wheelbarrow...) of
	// which one is active. Changing the set (the "overlay") cross-fades the poses; the new set takes over at the same
	// gait phase and velocity, so feet stay in step through the change. It reads only the MotionState.
	class AnimInstance : public IGraphTarget
	{
	public:
		AnimInstance(const Model& model, ModelInstance& instance, const LocomotionOptions& options = {});
		~AnimInstance() override;
		// The layers keep references to this object's own members: it stays where it is.
		AnimInstance(const AnimInstance&) = delete;
		AnimInstance& operator=(const AnimInstance&) = delete;

		static constexpr const char* kDefaultSet = "default";

		// Adds a set made of the clips the model has (missing ones are skipped). False when none exists.
		bool AddSet(const LocomotionSetDesc& desc);
		// The set called "default" always exists (it starts empty and is the active one): clips can be added to it directly.
		LocomotionBlender& Default() { return *m_Sets[0].Blender; }
		std::vector<std::string> SetNames() const override;
		bool HasSet(const std::string& name) const override { return Find(name) >= 0; }

		// Makes `name` the active set, cross-fading over `fadeSeconds`. False for an unknown set (nothing changes).
		bool SetOverlay(const std::string& name, float fadeSeconds = 0.3f) override;
		const std::string& Overlay() const { return m_Sets[static_cast<size_t>(m_Active)].Name; }
		bool Transitioning() const { return m_From != -1; }
		float TransitionProgress() const { return m_From != -1 ? m_Progress : 1.0f; }

		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; }
		LocomotionBlender& Active() { return *m_Sets[static_cast<size_t>(m_Active)].Blender; }
		const std::vector<FootEvent>& Events() const { return m_Events; }
		// The same landings as world-space FootPlants (the type FootstepTracker gives for any animated model), so footprints, dust and
		// splashes are fed the same way whichever animation system moves the character. Empty while airborne.
		const std::vector<FootPlant>& Plants() const { return m_Plants; }

		// Procedural layers, after the clips, montages and the air pose: the feet on the terrain (give it a ground height with
		// Feet().SetGround) and the head turned toward a point (Look().SetTarget). Both fade out by themselves while a full-body action or
		// a jump is on screen.
		FootIK& Feet() { return *m_Feet; }
		LookAt& Look() { return *m_Look; }

		// Physics: the character as a ragdoll in `world` (see Ragdoll): its bodies start from the pose on screen, moving with `velocity`, and while
		// it lasts the skeleton follows them (the world's terrain, statics, water, wind and explosions act on it). `characterToWorld` is where the
		// character is drawn; the host keeps it on the pelvis (PelvisPosition) so the pose stays near its root. The world must outlive the ragdoll
		// (call EndRagdoll or destroy this AnimInstance first).
		bool StartRagdoll(PhysicsWorld& world, const glm::mat4& characterToWorld, const glm::vec3& velocity);
		bool Ragdolled() const { return m_Ragdoll && m_Ragdoll->Active(); }
		Ragdoll* GetRagdoll() { return m_Ragdoll.get(); }
		// Lets the ragdoll go: the character stands up at `placement` (position and facing) and the pose blends from the one it lay in to the animation.
		void EndRagdoll(const glm::mat4& placement, float blendSeconds = 0.8f);
		bool Recovering() const { return m_RagBlend < 1.0f; }

		// The state graph: states (a locomotion set or a montage) and transitions by conditions on parameters (the motion's and the game's), run
		// before the animation each frame. Empty by default: the character is then driven by SetOverlay and the montages directly.
		GraphRunner& Graph() { return m_GraphRunner; }
		const GraphRunner& Graph() const { return m_GraphRunner; }

		// Sockets and props. A prop follows its socket and, through its grips, pulls the hands onto itself with IK (after the clips and
		// montages, before the pose reaches the instance). `Detach` lets go over the prop's fade time.
		SocketSet& Sockets() { return m_Sockets; }
		const SocketSet& Sockets() const { return m_Sockets; }
		const HumanoidRig& Rig() const { return m_Rig; }
		bool Attach(const Prop& prop);   // false for an unknown socket; the same name replaces the prop
		void Detach(const std::string& name);
		bool HasProp(const std::string& name) const;
		std::vector<std::string> PropNames() const;
		// Authoring shortcut: sets the hand grips of `prop` to where the palms are on screen now, relative to the prop (socket * Offset), so
		// the hands keep exactly the pose the current clips give them. Adds a grip for each hand the rig has.
		void SnapGrips(Prop& prop) const;
		// The prop's world matrix after the last Update (false when it is not attached).
		bool PropMatrix(const std::string& name, glm::mat4& world) const;
		float PropWeight(const std::string& name) const;   // 0..1: how much the hands are on it

		// One-shot actions over the locomotion (see MontagePlayer): `Montages().Play("pick_fruit")`. While a full-body montage is
		// on screen the locomotion's footsteps are muted.
		MontagePlayer& Montages() { return m_Montages; }
		const MontagePlayer& Montages() const { return m_Montages; }

		// The jump / fall pose, driven by MotionState::Grounded (see AirLayer). Set its clip ("jump") to use it.
		AirLayer& Air() { return m_Air; }

		// Turning on the spot (MotionState::YawRate while standing still): a turn clip per locomotion set, see TurnLayer.
		TurnLayer& Turn() { return m_Turn; }

		// IGraphTarget: what the state graph switches and plays.
		std::string DefaultSetName() const override { return kDefaultSet; }
		bool HasMontage(const std::string& name) const override { return m_Montages.Has(name); }
		bool PlayMontage(const std::string& name) override { return m_Montages.Play(name); }
		bool MontagePlaying(const std::string& name) const override { return m_Montages.IsPlaying(name); }

		// Adds a layer of the game's own after the jump, turn and montage layers (and before the procedural ones): it is updated every frame
		// and, while it is active, changes the pose. The instance owns it.
		void AddLayer(std::unique_ptr<IAnimLayer> layer);

		// Poses the instance for this frame's motion; `characterToWorld` puts the footsteps where the feet are.
		void Update(const MotionState& state, float dt, const glm::mat4& characterToWorld);

	private:
		struct Set
		{
			std::string Name;
			std::unique_ptr<LocomotionBlender> Blender;
		};
		int Find(const std::string& name) const;

		const Model& m_Model;
		ModelInstance& m_Instance;
		LocomotionOptions m_Options;
		std::vector<Set> m_Sets;
		int m_Active = 0, m_From = -1;   // m_From: the set being left, -1 when not changing, -2 when leaving a frozen pose
		Pose m_FromSnapshot;
		float m_Progress = 1.0f, m_FadeSeconds = 0.3f;
		FootstepSounds* m_Steps = nullptr;
		int m_LeftFoot = -1, m_RightFoot = -1;
		struct Attached
		{
			Prop P;
			float Weight = 0.0f;
			bool Leaving = false;
			glm::mat4 World{ 1.0f };
		};
		const Attached* FindProp(const std::string& name) const;
		void ApplyGrips(const glm::mat4& characterToWorld);

		HumanoidRig m_Rig;
		GraphRunner m_GraphRunner;
		std::unique_ptr<Ragdoll> m_Ragdoll;
		Pose m_RagSnapshot;
		float m_RagBlend = 1.0f, m_RagBlendSeconds = 0.8f;
		std::unique_ptr<FootIK> m_Feet;
		std::unique_ptr<LookAt> m_Look;
		SocketSet m_Sockets;
		std::vector<Attached> m_Props;
		std::vector<glm::mat4> m_WorldScratch;
		AirLayer m_Air;
		TurnLayer m_Turn;
		MontagePlayer m_Montages;
		std::vector<std::unique_ptr<IAnimLayer>> m_Layers;   // the jump, turn and montage layers first, then the game's
		Pose m_Pose;
		std::vector<FootEvent> m_Events;
		std::vector<FootPlant> m_Plants;
	};

	// The sets the Mixamo locomotion and farming packs make (when the model has the clips): "free", "box", "holding",
	// "wheelbarrow". Add them with AnimInstance::AddSet.
	std::vector<LocomotionSetDesc> MixamoSets();
}
