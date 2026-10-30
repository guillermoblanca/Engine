#pragma once
#include "GB/Game/Components/ActorStateSaver.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include "GB/Game/Components/Gameplay/MovementComponent.h"
#include "GB/World/Physics.h"
#include <memory>
#include <string>

namespace GB
{
	class AnimationComponent;

	// An actor that falls: the physics world the ragdoll lives in (built when it falls: the ground, the water and the static obstacles of the
	// body's world) and the steps that follow the pelvis with the controller. The skeleton is the animation's; this must be added before it so the
	// animator, which holds the ragdoll, is destroyed before the world it lives in.
	class RagdollComponent : public GBObjectComponent, public IStateSaver
	{
	public:
		RagdollComponent(GeometryComponent& geometry, MovementComponent& movement) : m_Geometry(geometry), m_Movement(movement) {}
		void SetAnimation(AnimationComponent* animation) { m_Animation = animation; }

		bool Ragdolled() const;
		bool Simulating() const { return Ragdolled() && m_Physics != nullptr; }
		bool Start(const glm::vec3& velocity);
		void GetUp();
		// One step of a fallen actor: the physics moves it, the controller follows the pelvis.
		void Step(float dt);
		float AdvanceRest(float dt);   // seconds the ragdoll has been at rest
		bool Explode(const glm::vec2& xz, float radius, float speed);   // false when there is no physics world (nobody fell)
		void Hit(const glm::vec3& impulse);
		std::string Status() const;
		void Drop() { m_Physics.reset(); }   // the animator was rebuilt: the world goes with the old ragdoll

		void CaptureState(Save::ActorState& state) const override { state.Ragdoll = Ragdolled(); }
		void OnDestroy(GBObject&) override { m_Physics.reset(); }

	private:
		void BuildPhysics();

		GeometryComponent& m_Geometry;
		MovementComponent& m_Movement;
		AnimationComponent* m_Animation = nullptr;
		std::unique_ptr<PhysicsWorld> m_Physics;
		float m_RestTime = 0.0f;
	};
}
