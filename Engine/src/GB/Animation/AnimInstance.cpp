#include "GB/Animation/AnimInstance.h"
#include <algorithm>

namespace GB
{
	AnimInstance::AnimInstance(const Model& model, ModelInstance& instance, const LocomotionOptions& options)
		: m_Model(model), m_Instance(instance), m_Options(options),
		  m_LeftFoot(model.FindNode(options.Analysis.LeftFoot)), m_RightFoot(model.FindNode(options.Analysis.RightFoot)),
		  m_Air(model, options.Analysis), m_Turn(model, options.Analysis), m_Montages(model, options.Analysis)
	{
		const std::string& root = options.Analysis.RootBone;   // "mixamorig:Hips": the rig's bones share its prefix
		m_Rig = FindHumanoidRig(model, root.size() > 4 && root.compare(root.size() - 4, 4, "Hips") == 0 ? root.substr(0, root.size() - 4) : std::string());
		if (!m_Rig.HasLegs())   // not Mixamo-prefixed: try the other naming conventions
		{
			const HumanoidRig other = FindHumanoidRigAuto(model);
			if (other.HasLegs()) m_Rig = other;
		}
		m_Sockets = StandardSockets(model, m_Rig);
		m_Feet = std::make_unique<FootIK>(model, m_Rig);
		m_Look = std::make_unique<LookAt>(model, m_Rig);
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
		m_GraphRunner.Update(state, dt, *this);   // the state graph picks the set or montage before the animation reads it
		LocomotionBlender& active = Active();
		active.SetVelocity(state.LocalVelocity);
		active.Update(dt);
		m_Events.clear();
		m_Plants.clear();

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
		m_Air.Update(state, dt);
		if (m_Air.Active())
		{
			m_Air.Apply(m_Pose);
			if (m_Air.Weight() > 0.5f) m_Events.clear();
		}
		m_Turn.Update(state, dt, Overlay());
		if (m_Turn.Active())
		{
			m_Turn.Apply(m_Pose);
			if (m_Turn.Weight() > 0.5f) m_Events.clear();   // the feet shuffle on the spot: no footsteps
		}
		m_Montages.Update(dt);
		if (m_Montages.Busy())
		{
			m_Montages.Apply(m_Pose);
			if (m_Montages.FullBodyWeight() > 0.5f) m_Events.clear();   // the legs are not walking: no footsteps
		}
		{
			// the procedural layers fade out while an action or a jump has the whole body
			const float free = (1.0f - std::min(m_Montages.FullBodyWeight(), 1.0f)) * (1.0f - std::min(m_Air.Weight(), 1.0f));
			m_Feet->Update(m_Pose, characterToWorld, free, dt);
			m_Look->Update(m_Pose, characterToWorld, 1.0f - std::min(m_Montages.FullBodyWeight(), 1.0f), dt);
		}
		for (Attached& a : m_Props)   // the hands reach for the prop (or let go of it)
			a.Weight = a.Leaving ? std::max(a.Weight - dt / std::max(a.P.FadeSeconds, 1e-3f), 0.0f) : std::min(a.Weight + dt / std::max(a.P.FadeSeconds, 1e-3f), 1.0f);
		if (!m_Props.empty())
			ApplyGrips(characterToWorld);
		if (m_Ragdoll && m_Ragdoll->Active())
		{
			m_Ragdoll->ReadPose(m_Pose, characterToWorld);   // the skeleton follows the bodies
			m_Events.clear();
			m_Plants.clear();
		}
		else if (m_RagBlend < 1.0f)
		{
			m_RagBlend = std::min(m_RagBlend + dt / std::max(m_RagBlendSeconds, 1e-3f), 1.0f);
			const float t = m_RagBlend * m_RagBlend * (3.0f - 2.0f * m_RagBlend);
			BlendPoses(m_RagSnapshot, m_Pose, t, m_Pose);   // from the pose it lay in to the animation
		}
		m_Instance.SetPose(m_Pose);
		for (Attached& a : m_Props)
			a.World = characterToWorld * m_Sockets.Matrix(m_Instance, a.P.Socket) * a.P.Offset;
		m_Props.erase(std::remove_if(m_Props.begin(), m_Props.end(), [](const Attached& a) { return a.Leaving && a.Weight <= 0.0f; }), m_Props.end());

		if (state.Grounded)
		{
			const glm::vec3 heading = glm::normalize(glm::vec3(characterToWorld * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
			for (const FootEvent& e : m_Events)
			{
				const int bone = e.Left ? m_LeftFoot : m_RightFoot;
				if (bone < 0) continue;
				FootPlant plant;
				plant.Foot = e.Left ? 0 : 1;
				plant.Position = glm::vec3(characterToWorld * m_Instance.GetNodeWorld(bone)[3]);
				plant.Position.y = state.Position.y;
				plant.Heading = heading;
				plant.Speed = state.Speed;
				m_Plants.push_back(plant);
			}
		}

		if (!m_Steps) return;
		if (state.Landed)
			m_Steps->Step(glm::vec3(characterToWorld[3]), 3.0f + state.LandingSpeed, 1.0f);   // a landing is a heavy step
		if (!state.Grounded) return;
		for (size_t i = 0, p = 0; i < m_Events.size(); ++i)
		{
			if ((m_Events[i].Left ? m_LeftFoot : m_RightFoot) < 0) continue;
			m_Steps->Step(m_Plants[p++].Position, state.Speed, m_Events[i].Weight);   // the sound is played where the plant is
		}
	}

	bool AnimInstance::StartRagdoll(PhysicsWorld& world, const glm::mat4& characterToWorld, const glm::vec3& velocity)
	{
		m_Ragdoll.reset();
		m_Ragdoll = std::make_unique<Ragdoll>(m_Model, m_Rig, world);
		m_RagBlend = 1.0f;
		if (!m_Ragdoll->Valid() || !m_Ragdoll->Activate(m_Pose, characterToWorld, velocity))
		{
			m_Ragdoll.reset();
			return false;
		}
		return true;
	}

	void AnimInstance::EndRagdoll(const glm::mat4& placement, float blendSeconds)
	{
		if (!m_Ragdoll || !m_Ragdoll->Active()) return;
		m_RagSnapshot = m_Pose;
		m_Ragdoll->ReadPose(m_RagSnapshot, placement);
		m_Ragdoll->Deactivate();
		m_RagBlend = 0.0f;
		m_RagBlendSeconds = blendSeconds;
	}

	bool AnimInstance::Attach(const Prop& prop)
	{
		if (prop.Name.empty() || !m_Sockets.Find(prop.Socket)) return false;
		for (const GripPoint& g : prop.Grips)
			if (!m_Sockets.Find(g.Socket)) return false;
		for (Attached& a : m_Props)
			if (a.P.Name == prop.Name) { a.P = prop; a.Leaving = false; return true; }
		Attached a;
		a.P = prop;
		m_Props.push_back(a);
		return true;
	}

	void AnimInstance::SnapGrips(Prop& prop) const
	{
		prop.Grips.clear();
		if (!m_Sockets.Find(prop.Socket)) return;
		const glm::mat4 inverseProp = glm::inverse(m_Sockets.Matrix(m_Instance, prop.Socket) * prop.Offset);
		for (const char* hand : { "hand_l", "hand_r" })
			if (m_Sockets.Find(hand))
				prop.Grips.push_back({ hand, inverseProp * m_Sockets.Matrix(m_Instance, hand) });
	}

	void AnimInstance::Detach(const std::string& name)
	{
		for (Attached& a : m_Props)
			if (a.P.Name == name) a.Leaving = true;
	}

	const AnimInstance::Attached* AnimInstance::FindProp(const std::string& name) const
	{
		for (const Attached& a : m_Props)
			if (a.P.Name == name) return &a;
		return nullptr;
	}

	bool AnimInstance::HasProp(const std::string& name) const { return FindProp(name) != nullptr; }

	std::vector<std::string> AnimInstance::PropNames() const
	{
		std::vector<std::string> names;
		for (const Attached& a : m_Props) names.push_back(a.P.Name);
		return names;
	}

	bool AnimInstance::PropMatrix(const std::string& name, glm::mat4& world) const
	{
		const Attached* a = FindProp(name);
		if (!a) return false;
		world = a->World;
		return true;
	}

	float AnimInstance::PropWeight(const std::string& name) const
	{
		const Attached* a = FindProp(name);
		return a ? a->Weight : 0.0f;
	}

	void AnimInstance::ApplyGrips(const glm::mat4& characterToWorld)
	{
		(void)characterToWorld;
		PoseToWorld(m_Model, m_Pose, m_WorldScratch);
		for (const Attached& a : m_Props)
		{
			if (a.Weight <= 0.0f) continue;
			const glm::mat4 prop = m_Sockets.Matrix(m_WorldScratch, a.P.Socket) * a.P.Offset;
			for (const GripPoint& g : a.P.Grips)
			{
				const bool left = g.Socket == "hand_l";
				const HumanoidRig::Arm& arm = left ? m_Rig.Left : m_Rig.Right;
				const Socket* hand = m_Sockets.Find(g.Socket);
				if (!hand || arm.UpperArm < 0 || arm.ForeArm < 0 || arm.Hand < 0 || hand->Bone != arm.Hand) continue;
				const glm::mat4 boneTarget = prop * g.Local * glm::inverse(hand->Offset);   // where the hand bone must be for the palm to land on the grip
				const glm::quat rotation = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(boneTarget[0])), glm::normalize(glm::vec3(boneTarget[1])), glm::normalize(glm::vec3(boneTarget[2]))));
				const glm::vec3 elbow(m_WorldScratch[static_cast<size_t>(arm.ForeArm)][3]);
				SolveTwoBoneIK(m_Model, m_Pose, { arm.UpperArm, arm.ForeArm, arm.Hand }, glm::vec3(boneTarget[3]), elbow, a.Weight * g.Weight, &rotation);
				PoseToWorld(m_Model, m_Pose, m_WorldScratch);   // the other hand starts from this one's result
			}
		}
	}

	std::vector<LocomotionSetDesc> MixamoSets()
	{
		// (the free-movement set is the default one: see CharacterAnimator::AddDefaultLocomotionClips)
		return {
			{ "box", { "box idle", "box walk arc" } },
			{ "holding", { "holding idle", "holding walk" } },
			{ "wheelbarrow", { "wheelbarrow idle", "wheelbarrow walk" } },
			{ "injured", { "injured idle", "injured walk", "injured run", "injured walk backwards", "injured run backwards" } },
		};
	}
}
