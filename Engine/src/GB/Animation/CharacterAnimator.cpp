#include "GB/Animation/CharacterAnimator.h"

namespace GB
{
	CharacterAnimator::CharacterAnimator(const Model& model, ModelInstance& instance, const LocomotionOptions& options)
		: m_Model(model), m_Instance(instance), m_Loco(model, options),
		  m_LeftFoot(model.FindNode(options.Analysis.LeftFoot)), m_RightFoot(model.FindNode(options.Analysis.RightFoot))
	{
	}

	int CharacterAnimator::AddDefaultLocomotionClips()
	{
		int added = 0;
		for (const char* name : { "idle", "walking", "standard run", "left strafe walking", "right strafe walking", "left strafe", "right strafe" })
			added += m_Loco.AddClip(name) ? 1 : 0;
		return added;
	}

	void CharacterAnimator::Update(const MotionState& state, float dt, const glm::mat4& characterToWorld)
	{
		m_Loco.SetVelocity(state.LocalVelocity);
		m_Loco.Update(dt);
		m_Loco.Apply(m_Instance);
		if (!m_Steps) return;

		const float speed = state.Speed;
		if (state.Landed)
		{
			// a landing is a heavy step: both feet, louder with the fall
			const glm::vec3 p = glm::vec3(characterToWorld[3]);
			m_Steps->Step(p, 3.0f + state.LandingSpeed, 1.0f);
		}
		if (!state.Grounded) return;   // no footsteps while in the air
		for (const FootEvent& e : m_Loco.Events())
		{
			const int bone = e.Left ? m_LeftFoot : m_RightFoot;
			if (bone < 0) continue;
			glm::vec3 foot = glm::vec3(characterToWorld * m_Instance.GetNodeWorld(bone)[3]);
			foot.y = state.Position.y;
			m_Steps->Step(foot, speed, e.Weight);
		}
	}
}
