#include "GB/Game/Components/Physics/RagdollComponent.h"
#include "GB/Game/Components/Presentation/AnimationComponent.h"
#include <iomanip>
#include <sstream>

namespace GB
{
	bool RagdollComponent::Ragdolled() const
	{
		const AnimInstance* anim = m_Animation ? m_Animation->Anim() : nullptr;
		return anim && anim->Ragdolled();
	}

	void RagdollComponent::BuildPhysics()
	{
		const CharacterWorld& world = m_Movement.World();
		m_Physics = std::make_unique<PhysicsWorld>();
		const auto ground = world.GroundHeight ? world.GroundHeight : std::function<float(float, float)>([](float, float) { return 0.0f; });
		m_Physics->SetGround(ground, [ground](float x, float z)
		{
			const float e = 0.3f;
			return glm::normalize(glm::vec3(ground(x - e, z) - ground(x + e, z), 2.0f * e, ground(x, z - e) - ground(x, z + e)));
		});
		if (world.WaterLevel) m_Physics->SetWater(world.WaterLevel);
		for (size_t i = 0; i < m_Movement.StaticObstacles() && i < world.Obstacles.size(); ++i)   // trees, rocks and the bodies the user added: vertical columns (the other actors are not in this world)
		{
			const CircleObstacle& o = world.Obstacles[i];
			StaticCollider c;
			c.Shape = ShapeType::Capsule;
			c.Center = { o.Center.x, ground(o.Center.x, o.Center.y) + 1.5f, o.Center.y };
			c.Radius = o.Radius;
			c.HalfHeight = 1.5f;
			m_Physics->AddStatic(c);
		}
	}

	bool RagdollComponent::Start(const glm::vec3& velocity)
	{
		AnimInstance* anim = m_Animation ? m_Animation->Anim() : nullptr;
		if (!anim || Ragdolled()) return false;
		BuildPhysics();
		m_RestTime = 0.0f;
		if (!anim->StartRagdoll(*m_Physics, m_Geometry.RootMatrix(), velocity))
		{
			m_Physics.reset();
			return false;
		}
		return true;
	}

	void RagdollComponent::GetUp()
	{
		if (!Ragdolled()) return;
		AnimInstance& anim = *m_Animation->Anim();
		Ragdoll* rag = anim.GetRagdoll();
		const glm::vec3 pelvis = rag->PelvisPosition();
		m_Movement.Controller().SetPosition(m_Movement.World(), pelvis.x, pelvis.z, rag->GetUpYaw());
		anim.EndRagdoll(m_Geometry.RootMatrix(), 0.9f);
		m_RestTime = 0.0f;
	}

	void RagdollComponent::Step(float dt)
	{
		m_Physics->Step(dt);
		Ragdoll* rag = m_Animation->Anim()->GetRagdoll();
		const glm::vec3 pelvis = rag->PelvisPosition();
		m_Movement.Controller().SetPosition(m_Movement.World(), pelvis.x, pelvis.z, m_Movement.Controller().GetYaw());
	}

	float RagdollComponent::AdvanceRest(float dt)
	{
		Ragdoll* rag = m_Animation->Anim()->GetRagdoll();
		m_RestTime = rag->AtRest() ? m_RestTime + dt : 0.0f;
		return m_RestTime;
	}

	bool RagdollComponent::Explode(const glm::vec2& xz, float radius, float speed)
	{
		if (!m_Physics) return false;
		const CharacterWorld& world = m_Movement.World();
		const float y = world.GroundHeight ? world.GroundHeight(xz.x, xz.y) : 0.0f;
		m_Physics->Explode({ xz.x, y, xz.y }, radius, speed);
		return true;
	}

	void RagdollComponent::Hit(const glm::vec3& impulse)
	{
		if (!Ragdolled()) return;
		Ragdoll* rag = m_Animation->Anim()->GetRagdoll();
		rag->Hit(rag->PelvisPosition() + glm::vec3(0.0f, 0.3f, 0.0f), impulse);
	}

	std::string RagdollComponent::Status() const
	{
		const AnimInstance* anim = m_Animation ? m_Animation->Anim() : nullptr;
		if (!anim) return "";
		if (Ragdolled())
		{
			const Ragdoll* rag = const_cast<AnimInstance*>(anim)->GetRagdoll();
			std::ostringstream s;
			s << std::fixed << std::setprecision(2) << "ragdoll (" << static_cast<double>(rag->Speed()) << " m/s, " << (rag->LyingDown() ? "lying" : "on its feet") << (rag->AtRest() ? ", at rest" : "") << ")";
			return s.str();
		}
		return anim->Recovering() ? "getting up" : "";
	}
}
