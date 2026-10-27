#include "GB/Animation/AnimInstance.h"
#include <algorithm>

namespace GB
{
	AnimInstance::AnimInstance(const Model& model, ModelInstance& instance, const LocomotionOptions& options)
		: m_Model(model), m_Instance(instance), m_Options(options),
		  m_LeftFoot(model.FindNode(options.Analysis.LeftFoot)), m_RightFoot(model.FindNode(options.Analysis.RightFoot))
	{
		m_Sets.push_back({ kDefaultSet, std::make_unique<LocomotionBlender>(model, options) });
		m_Pose = RestPose(model);
	}

	int AnimInstance::Find(const std::string& name) const
	{
		for (size_t i = 0; i < m_Sets.size(); ++i)
			if (m_Sets[i].Name == name) return static_cast<int>(i);
		return name == "free" ? 0 : -1;   // free movement is the default set
	}

	std::vector<std::string> AnimInstance::SetNames() const
	{
		std::vector<std::string> names;
		for (const Set& s : m_Sets)
			if (s.Blender->ClipCount() > 0) names.push_back(s.Name);
		return names;
	}

	bool AnimInstance::AddSet(const LocomotionSetDesc& desc)
	{
		const int existing = Find(desc.Name);
		std::unique_ptr<LocomotionBlender> blender = existing == 0 ? nullptr : std::make_unique<LocomotionBlender>(m_Model, m_Options);
		LocomotionBlender& target = blender ? *blender : *m_Sets[0].Blender;
		size_t added = 0;
		for (const std::string& clip : desc.Clips)
			added += target.AddClip(clip) ? 1 : 0;
		if (added == 0) return false;
		if (!blender) return true;   // clips went into the default set
		if (existing > 0) m_Sets[static_cast<size_t>(existing)].Blender = std::move(blender);   // the same name replaces the set
		else m_Sets.push_back({ desc.Name, std::move(blender) });
		return true;
	}

	bool AnimInstance::SetOverlay(const std::string& name, float fadeSeconds)
	{
		const int next = Find(name);
		if (next < 0 || m_Sets[static_cast<size_t>(next)].Blender->ClipCount() == 0) return false;
		if (next == m_Active && !Transitioning()) return true;
		if (next == m_Active) return true;

		LocomotionBlender& from = Active();
		LocomotionBlender& to = *m_Sets[static_cast<size_t>(next)].Blender;
		to.SetPhase(from.Phase());                 // the new set carries on from the same step
		to.SnapVelocity(from.Velocity());
		if (fadeSeconds <= 1e-4f)
		{
			m_From = -1;
			m_Active = next;
			return true;
		}
		// interrupted in the middle of a change: fade from the pose on screen, not from either set
		const bool wasTransitioning = Transitioning();
		m_FromSnapshot = wasTransitioning ? m_Pose : Pose();
		m_From = wasTransitioning ? -2 : m_Active;
		m_Active = next;
		m_Progress = 0.0f;
		m_FadeSeconds = fadeSeconds;
		return true;
	}

	void AnimInstance::Update(const MotionState& state, float dt, const glm::mat4& characterToWorld)
	{
		LocomotionBlender& active = Active();
		active.SetVelocity(state.LocalVelocity);
		active.Update(dt);
		m_Events.clear();

		const bool fading = m_From != -1;
		if (fading)
		{
			const Pose* fromPose = &m_FromSnapshot;
			if (m_From >= 0)
			{
				LocomotionBlender& from = *m_Sets[static_cast<size_t>(m_From)].Blender;
				from.SetVelocity(state.LocalVelocity);
				from.Update(dt);
				fromPose = &from.GetPose();
			}
			m_Progress = std::min(m_Progress + dt / m_FadeSeconds, 1.0f);
			const float s = m_Progress * m_Progress * (3.0f - 2.0f * m_Progress);   // ease in and out
			BlendPoses(*fromPose, active.GetPose(), s, m_Pose);
			// footsteps come from whichever set shows more of itself
			if (m_Progress < 0.5f && m_From >= 0) m_Events = m_Sets[static_cast<size_t>(m_From)].Blender->Events();
			else m_Events = active.Events();
			if (m_Progress >= 1.0f) m_From = -1;
		}
		else
		{
			m_Pose = active.GetPose();
			m_Events = active.Events();
		}
		m_Instance.SetPose(m_Pose);

		if (!m_Steps) return;
		if (state.Landed)
			m_Steps->Step(glm::vec3(characterToWorld[3]), 3.0f + state.LandingSpeed, 1.0f);   // a landing is a heavy step
		if (!state.Grounded) return;
		for (const FootEvent& e : m_Events)
		{
			const int bone = e.Left ? m_LeftFoot : m_RightFoot;
			if (bone < 0) continue;
			glm::vec3 foot = glm::vec3(characterToWorld * m_Instance.GetNodeWorld(bone)[3]);
			foot.y = state.Position.y;
			m_Steps->Step(foot, state.Speed, e.Weight);
		}
	}

	std::vector<LocomotionSetDesc> MixamoSets()
	{
		// (the free-movement set is the default one: see CharacterAnimator::AddDefaultLocomotionClips)
		return {
			{ "box", { "box idle", "box walk arc" } },
			{ "holding", { "holding idle", "holding walk" } },
			{ "wheelbarrow", { "wheelbarrow idle", "wheelbarrow walk" } },
		};
	}
}
