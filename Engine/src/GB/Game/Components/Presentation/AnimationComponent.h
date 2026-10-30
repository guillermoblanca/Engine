#pragma once
#include "GB/Animation/AnimBindings.h"
#include "GB/Animation/Character.h"
#include "GB/Animation/CharacterAnimator.h"
#include "GB/Animation/Retarget.h"
#include "GB/Game/Components/ActorStateSaver.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include "GB/Game/MotionState.h"
#include "GB/Game/Components/Gameplay/MovementComponent.h"
#include "GB/Gameplay/GameplayWorld.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	class RagdollComponent;

	// How an actor moves its model: the locomotion blend (manifest, bindings, sets, montages, IK, look, footsteps) or, for a model with no
	// locomotion set, the plain idle/walk/run switch. It reads the body's motion and writes the pose into the geometry's instance; it decides
	// nothing about the body.
	class AnimationComponent : public GBObjectComponent, public IStateSaver
	{
	public:
		AnimationComponent(GeometryComponent& geometry, MovementComponent& movement) : m_Geometry(geometry), m_Movement(movement) {}

		struct Setup
		{
			AnimManifest Base;
			std::shared_ptr<const BindingsSetup> Bindings;
			std::string Skeleton;
			bool Rigged = true;      // the locomotion blend; false: the plain idle/walk/run switch
			FootstepSounds* Steps = nullptr;
		};
		// False and `error` when a rigged character has no locomotion clips. The geometry must be built.
		bool Build(Setup setup, std::string& error);
		// The ragdoll lives in a physics world that goes when the animator is rebuilt or destroyed.
		void SetRagdoll(RagdollComponent* ragdoll) { m_Ragdoll = ragdoll; }
		void SetGameplay(GameplayWorld* gameplay) { m_Gameplay = gameplay; }

		bool UsesLocomotionBlend() const { return m_Animator != nullptr; }
		AnimInstance* Anim() { return m_Animator ? &m_Animator->Anim() : nullptr; }
		const AnimInstance* Anim() const { return m_Animator ? &m_Animator->Anim() : nullptr; }
		const CharacterProfile* Profile() const { return m_Animator ? &m_Profile : nullptr; }

		// ---- the frame
		// The pose of this frame from the body's motion. `speed` drives the plain switch of a model with no locomotion set.
		void Animate(const MotionState& motion, float dt, float speed);
		void UpdateAnimator(const MotionState& motion, float dt) { if (m_Animator) m_Animator->Update(motion, dt, m_Geometry.RootMatrix()); }
		// The top speed the active set shows (carrying a box has a walk and no run); 1e9 when nothing limits it.
		float SpeedLimit() const;
		void SetDetail(bool pose, bool ik) { if (m_Animator) m_Animator->Anim().SetDetail(pose, ik); }
		std::string Describe(const FootstepSounds* steps, bool airborne, const std::string& tasks) const;

		// ---- manifest and bindings
		const AnimManifest& Manifest() const { return m_Manifest; }
		const AnimManifest& BaseManifest() const { return m_BaseManifest; }
		bool SetBindings(std::shared_ptr<const BindingsSetup> setup, const std::string& skeleton);
		const std::string& Skeleton() const { return m_Skeleton; }
		const BindingsSetup* Bindings() const { return m_Bindings.get(); }
		void SyncAbilities();
		std::string PickMontage(const AbilityDef& ability, int use, const std::map<std::string, std::string>& context) const;
		bool LoadManifestFile(const std::string& path, std::string& report);
		bool SetManifest(AnimManifest manifest, std::string& report, const std::string& source);
		bool SaveManifestFile(const std::string& path, std::string& report) const;
		std::string ManifestInfo() const;
		bool Retarget(const std::string& path, const std::string& assetRoot, std::string& report);
		const std::string& RetargetReport() const { return m_RetargetReport; }
		bool RebuildAnimator(std::string& error);

		// ---- sets, montages, look, feet
		bool SetOverlay(const std::string& name, float fade) { return m_Animator && m_Animator->Anim().SetOverlay(name, fade); }
		std::vector<std::string> OverlayNames() const { return m_Animator ? m_Animator->Anim().SetNames() : std::vector<std::string>(); }
		bool PlayMontage(const std::string& name, const std::string& section) { return m_Animator && m_Animator->Anim().Montages().Play(name, section); }
		void StopMontage();
		std::vector<std::string> MontageNames() const { return m_Animator ? m_Animator->Anim().Montages().Names() : std::vector<std::string>(); }
		std::string MontageStatus() const;
		void LookAtPoint(const glm::vec3& p) { if (m_Animator) m_Animator->Anim().Look().SetTarget(p); }
		void ClearLook() { if (m_Animator) m_Animator->Anim().Look().ClearTarget(); }
		void SetFootsteps(FootstepSounds* steps) { m_Steps = steps; if (m_Animator) m_Animator->SetFootsteps(steps); }
		void SetFootIK(bool on) { m_FeetOn = on; ApplyFeet(); }
		bool FootIKOn() const { return m_FeetOn; }
		const std::vector<FootPlant>& Plants() const { static const std::vector<FootPlant> none; return m_Animator ? m_Animator->Anim().Plants() : none; }

		// ---- save
		void CaptureState(Save::ActorState& state) const override { if (const AnimInstance* anim = Anim()) state.Set = anim->Overlay(); }
		void RestoreState(const Save::ActorState& state) override { if (!state.Set.empty() && m_Animator && m_Animator->Anim().HasSet(state.Set)) SetOverlay(state.Set, 0.0f); }

		void OnDestroy(GBObject&) override { m_Animator.reset(); }   // (the ragdoll's physics world is next to go)

	private:
		void ApplyFeet();
		bool BuildAnimator(std::string& error);
		AnimManifest EffectiveManifest() const;

		GeometryComponent& m_Geometry;
		MovementComponent& m_Movement;
		RagdollComponent* m_Ragdoll = nullptr;
		GameplayWorld* m_Gameplay = nullptr;
		FootstepSounds* m_Steps = nullptr;
		bool m_FeetOn = true;
		std::string m_RetargetReport;

		AnimManifest m_Manifest = MixamoManifest();
		AnimManifest m_BaseManifest = MixamoManifest();
		std::shared_ptr<const BindingsSetup> m_Bindings;
		std::string m_Skeleton;
		ManifestReport m_ManifestReport;
		CharacterProfile m_Profile;

		CharacterAnimation m_Animation;                    // the plain idle/walk/run switch, for models without a locomotion set
		std::unique_ptr<CharacterAnimator> m_Animator;     // the locomotion blend (set when the model has the clips)
	};
}
