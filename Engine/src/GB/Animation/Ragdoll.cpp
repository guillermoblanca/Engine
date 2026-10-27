#include "GB/Animation/Ragdoll.h"
#include "GB/Animation/IK.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		int g_NextGroup = 100;

		glm::quat WorldRotation(const glm::mat4& m)
		{
			return glm::normalize(glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])), glm::normalize(glm::vec3(m[2])))));
		}
		glm::vec3 Safe(const glm::vec3& v, const glm::vec3& fallback)
		{
			return glm::length(v) > 1e-5f ? glm::normalize(v) : fallback;
		}
	}

	Ragdoll::Ragdoll(const Model& model, const HumanoidRig& rig, PhysicsWorld& world, const RagdollConfig& config)
		: Config(config), m_Model(model), m_Rig(rig), m_World(world)
	{
		m_Valid = rig.HasArms() && rig.HasLegs() && rig.Hips >= 0 && rig.Spine >= 0 && rig.Head >= 0;
	}

	bool Ragdoll::Activate(const Pose& pose, const glm::mat4& characterToWorld, const glm::vec3& velocity)
	{
		if (!m_Valid) return false;
		Deactivate();
		const float s = std::max(glm::length(glm::vec3(characterToWorld[1])), 1e-3f);
		std::vector<glm::mat4> model;
		PoseToWorld(m_Model, pose, model);
		auto boneWorld = [&](int bone) { return characterToWorld * model[static_cast<size_t>(bone)]; };
		auto at = [&](int bone) { return glm::vec3(boneWorld(bone)[3]); };

		// the character's own axes in the world: up, its left (+X in model space) and its front (+Z)
		const glm::vec3 side = Safe(glm::vec3(characterToWorld[0]), { 1, 0, 0 });
		const glm::vec3 front = Safe(glm::vec3(characterToWorld[2]), { 0, 0, 1 });
		const glm::vec3 upAxis = Safe(glm::vec3(characterToWorld[1]), { 0, 1, 0 });

		struct Spec
		{
			int Bone;
			glm::vec3 From, To;        // the segment the body covers (the box's long axis runs along it)
			glm::vec3 Half;            // half extents across (x = side, z = front/up) and the minimum length along
			float Mass;                // fraction of the total
			int Parent;                // index of the part it hangs from (-1: the root)
		};
		std::vector<Spec> specs;
		auto add = [&](int bone, const glm::vec3& a, const glm::vec3& b, float radiusX, float radiusZ, float mass, int parent)
		{
			specs.push_back({ bone, a, b, { radiusX * s, 0.0f, radiusZ * s }, mass, parent });
			return static_cast<int>(specs.size()) - 1;
		};
		enum { Pelvis, Chest, Head, UpperArmL, ForeArmL, UpperArmR, ForeArmR, ThighL, CalfL, FootL, ThighR, CalfR, FootR };
		const glm::vec3 hips = at(m_Rig.Hips);
		const glm::vec3 neck = at(m_Rig.Neck >= 0 ? m_Rig.Neck : m_Rig.Head);
		const float hipWidth = glm::distance(at(m_Rig.LeftLeg.UpLeg), at(m_Rig.RightLeg.UpLeg));
		// the pelvis: from a little below the hips to the spine
		const glm::vec3 spine = at(m_Rig.Spine);
		add(m_Rig.Hips, hips - upAxis * (0.06f * s), spine + upAxis * (0.02f * s), hipWidth * 0.5f / s + 0.04f, 0.10f, 0.14f, -1);
		add(m_Rig.Spine, spine, neck, 0.17f, 0.11f, 0.30f, Pelvis);
		const glm::vec3 headPos = at(m_Rig.Head);
		const glm::vec3 headUp = Safe(headPos - neck, upAxis);
		add(m_Rig.Head, headPos, headPos + headUp * (0.22f * s), 0.085f, 0.095f, 0.08f, Chest);
		auto arm = [&](const HumanoidRig::Arm& a)
		{
			const glm::vec3 shoulder = at(a.UpperArm), elbow = at(a.ForeArm), wrist = at(a.Hand);
			const glm::vec3 tip = a.Middle >= 0 ? at(a.Middle) : wrist + Safe(wrist - elbow, -upAxis) * (0.09f * s);
			const int upper = add(a.UpperArm, shoulder, elbow, 0.05f, 0.05f, 0.027f, Chest);
			add(a.ForeArm, elbow, tip, 0.04f, 0.04f, 0.022f, upper);
			return upper;
		};
		arm(m_Rig.Left);
		arm(m_Rig.Right);
		auto leg = [&](const HumanoidRig::Leg& l)
		{
			const glm::vec3 hip = at(l.UpLeg), knee = at(l.Knee), ankle = at(l.Foot);
			const glm::vec3 toe = l.Toe >= 0 ? at(l.Toe) : ankle + front * (0.15f * s);
			const int thigh = add(l.UpLeg, hip, knee, 0.085f, 0.085f, 0.10f, Pelvis);
			const int calf = add(l.Knee, knee, ankle, 0.06f, 0.06f, 0.047f, thigh);
			add(l.Foot, ankle - upAxis * (0.04f * s), toe, 0.05f, 0.04f, 0.014f, calf);
		};
		leg(m_Rig.LeftLeg);
		leg(m_Rig.RightLeg);
		float massSum = 0.0f;
		for (const Spec& sp : specs) massSum += sp.Mass;

		// the bodies
		m_Group = ++g_NextGroup;
		const float totalMass = Config.TotalMass * s * s * s;
		std::vector<glm::quat> rotations;
		for (const Spec& sp : specs)
		{
			const glm::vec3 axis = Safe(sp.To - sp.From, upAxis);
			// the frame: Y along the segment, X as close to the character's side as it can be
			glm::vec3 x = side - axis * glm::dot(side, axis);
			x = Safe(x, Safe(glm::cross(axis, front), { 1, 0, 0 }));
			const glm::vec3 z = glm::cross(x, axis);
			const glm::quat rot = glm::normalize(glm::quat_cast(glm::mat3(x, axis, z)));
			const float length = std::max(glm::distance(sp.From, sp.To), 0.04f * s);
			BodyDesc d;
			d.Shape = ShapeType::Box;
			d.HalfExtents = { sp.Half.x, length * 0.5f, sp.Half.z };
			const float volume = 8.0f * d.HalfExtents.x * d.HalfExtents.y * d.HalfExtents.z;
			d.Density = (totalMass * sp.Mass / massSum) / std::max(volume, 1e-6f);
			d.Friction = Config.Friction;
			d.Restitution = Config.Restitution;
			d.LinearDamping = Config.LinearDamping;
			d.AngularDamping = Config.AngularDamping;
			d.InertiaScale = Config.InertiaScale;
			d.Group = m_Group;
			Part p;
			p.Body = m_World.AddBody(d, (sp.From + sp.To) * 0.5f, rot);
			p.Bone = sp.Bone;
			p.Offset = glm::inverse(glm::translate(glm::mat4(1.0f), (sp.From + sp.To) * 0.5f) * glm::mat4_cast(rot)) * boneWorld(sp.Bone);
			m_World.GetBody(p.Body).Velocity = velocity;
			m_Parts.push_back(p);
			rotations.push_back(rot);
		}
		m_Pelvis = Pelvis; m_Chest = Chest; m_Head = Head;

		// the joints, at the child's start; limits are relative to the pose it was activated in
		auto body = [&](int part) { return m_Parts[static_cast<size_t>(part)].Body; };
		auto dir = [&](int part) { return Safe(specs[static_cast<size_t>(part)].To - specs[static_cast<size_t>(part)].From, upAxis); };
		auto cone = [&](int child, float swing, float twist)
		{
			const Spec& sp = specs[static_cast<size_t>(child)];
			// A is the parent and B the child: the swing and twist are the child's relative to the parent
			JointDesc j = m_World.MakeJoint(body(sp.Parent), body(child), sp.From, dir(child), side);
			j.SwingLimit = swing;
			j.TwistMin = -twist;
			j.TwistMax = twist;
			const int id = m_World.AddJoint(j);
			if (id >= 0) m_Joints.push_back(id);
		};
		auto hinge = [&](int child, const glm::vec3& flexTo, float maxFlex)
		{
			const Spec& sp = specs[static_cast<size_t>(child)];
			const int parent = sp.Parent;
			const glm::vec3 u = dir(parent), v = dir(child);
			glm::vec3 a = Safe(glm::cross(u, flexTo), side);   // positive rotation about it takes the upper segment toward the way the joint bends
			const float flex0 = std::atan2(glm::dot(a, glm::cross(u, v)), glm::dot(u, v));
			JointDesc j = m_World.MakeJoint(body(parent), body(child), sp.From, a, front);
			j.Hinge = true;
			j.TwistMin = -flex0 - 0.08f;
			j.TwistMax = std::max(maxFlex - flex0, 0.1f);
			const int id = m_World.AddJoint(j);
			if (id >= 0) m_Joints.push_back(id);
		};
		cone(Chest, glm::radians(35.0f), glm::radians(30.0f));
		cone(Head, glm::radians(50.0f), glm::radians(60.0f));
		cone(UpperArmL, glm::radians(110.0f), glm::radians(70.0f));
		hinge(ForeArmL, front, Config.MaxElbowFlex);
		cone(UpperArmR, glm::radians(110.0f), glm::radians(70.0f));
		hinge(ForeArmR, front, Config.MaxElbowFlex);
		cone(ThighL, glm::radians(85.0f), glm::radians(40.0f));
		hinge(CalfL, -front, Config.MaxKneeFlex);
		cone(FootL, glm::radians(40.0f), glm::radians(20.0f));
		cone(ThighR, glm::radians(85.0f), glm::radians(40.0f));
		hinge(CalfR, -front, Config.MaxKneeFlex);
		cone(FootR, glm::radians(40.0f), glm::radians(20.0f));
		m_Active = true;
		return true;
	}

	void Ragdoll::Deactivate()
	{
		for (int j : m_Joints) m_World.RemoveJoint(j);
		for (const Part& p : m_Parts) m_World.RemoveBody(p.Body);
		m_Joints.clear();
		m_Parts.clear();
		m_Active = false;
	}

	void Ragdoll::ReadPose(Pose& pose, const glm::mat4& characterToWorld) const
	{
		if (!m_Active) return;
		const glm::mat4 inverse = glm::inverse(characterToWorld);
		const auto& nodes = m_Model.GetNodes();
		std::vector<int> partOf(nodes.size(), -1);
		for (size_t i = 0; i < m_Parts.size(); ++i) partOf[static_cast<size_t>(m_Parts[i].Bone)] = static_cast<int>(i);
		std::vector<glm::mat4> world(nodes.size());
		for (int n : m_Model.GetNodeOrder())
		{
			BonePose& b = pose[static_cast<size_t>(n)];
			const int parent = nodes[static_cast<size_t>(n)].Parent;
			const glm::mat4 parentWorld = parent >= 0 ? world[static_cast<size_t>(parent)] : glm::mat4(1.0f);
			const int part = partOf[static_cast<size_t>(n)];
			if (part >= 0 && m_World.IsAlive(m_Parts[static_cast<size_t>(part)].Body))
			{
				const RigidBody& body = m_World.GetBody(m_Parts[static_cast<size_t>(part)].Body);
				const glm::mat4 W = inverse * body.GetTransform() * m_Parts[static_cast<size_t>(part)].Offset;
				const glm::mat4 local = glm::inverse(parentWorld) * W;
				b.T = glm::vec3(local[3]);
				b.R = WorldRotation(local);
				world[static_cast<size_t>(n)] = W;
			}
			else
				world[static_cast<size_t>(n)] = parentWorld * glm::translate(glm::mat4(1.0f), b.T) * glm::mat4_cast(glm::normalize(b.R)) * glm::scale(glm::mat4(1.0f), b.S);
		}
	}

	void Ragdoll::Hit(const glm::vec3& worldPoint, const glm::vec3& impulse)
	{
		int best = -1;
		float bestDistance = 1e9f;
		for (const Part& p : m_Parts)
		{
			if (!m_World.IsAlive(p.Body)) continue;
			const float d = glm::distance(m_World.GetBody(p.Body).Position, worldPoint);
			if (d < bestDistance) { bestDistance = d; best = p.Body; }
		}
		if (best >= 0) m_World.GetBody(best).AddImpulse(impulse, worldPoint);
	}

	glm::vec3 Ragdoll::PelvisPosition() const
	{
		return m_Active && m_Pelvis >= 0 ? m_World.GetBody(m_Parts[static_cast<size_t>(m_Pelvis)].Body).Position : glm::vec3(0.0f);
	}

	glm::vec3 Ragdoll::HeadDirection() const
	{
		if (!m_Active) return { 0, 1, 0 };
		return Safe(m_World.GetBody(m_Parts[static_cast<size_t>(m_Head)].Body).Position - PelvisPosition(), { 0, 1, 0 });
	}

	bool Ragdoll::LyingDown() const
	{
		return m_Active && HeadDirection().y < 0.6f;
	}

	bool Ragdoll::FacingUp() const
	{
		if (!m_Active) return false;
		const RigidBody& chest = m_World.GetBody(m_Parts[static_cast<size_t>(m_Chest)].Body);
		return (chest.Rotation * glm::vec3(0.0f, 0.0f, 1.0f)).y > 0.0f;   // the body's Z is the character's front (up to the chest's own roll)
	}

	float Ragdoll::GetUpYaw() const
	{
		if (!m_Active) return 0.0f;
		const RigidBody& chest = m_World.GetBody(m_Parts[static_cast<size_t>(m_Chest)].Body);
		glm::vec3 f = chest.Rotation * glm::vec3(0.0f, 0.0f, 1.0f);
		if (std::fabs(f.y) > 0.7f || glm::length(glm::vec2(f.x, f.z)) < 0.3f)   // lying: it gets up facing the way its head points
			f = HeadDirection();
		return std::atan2(f.x, f.z);
	}

	float Ragdoll::Speed() const
	{
		if (!m_Active || m_Parts.empty()) return 0.0f;
		float sum = 0.0f;
		for (const Part& p : m_Parts) sum += glm::length(m_World.GetBody(p.Body).Velocity);
		return sum / static_cast<float>(m_Parts.size());
	}

	bool Ragdoll::AtRest() const
	{
		if (!m_Active) return false;
		for (const Part& p : m_Parts)
		{
			const RigidBody& b = m_World.GetBody(p.Body);
			if (!b.Sleeping && (glm::length(b.Velocity) > 0.25f || glm::length(b.AngularVelocity) > 0.8f)) return false;
		}
		return true;
	}
}
