#include "GB/Animation/Socket.h"
#include "GB/Assets/ModelInstance.h"
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	bool SocketSet::Add(const Socket& socket)
	{
		if (socket.Name.empty() || socket.Bone < 0) return false;
		for (Socket& s : m_Sockets)
			if (s.Name == socket.Name) { s = socket; return true; }
		m_Sockets.push_back(socket);
		return true;
	}

	bool SocketSet::AddFromRest(const Model& model, const std::string& name, int bone, const glm::vec3& point, const glm::quat& rotation)
	{
		if (bone < 0 || static_cast<size_t>(bone) >= model.GetNodes().size()) return false;
		if (m_RestWorld.size() != model.GetNodes().size()) PoseToWorld(model, RestPose(model), m_RestWorld);
		const glm::mat4 frame = glm::translate(glm::mat4(1.0f), point) * glm::mat4_cast(rotation);
		Socket s;
		s.Name = name;
		s.Bone = bone;
		s.Offset = glm::inverse(m_RestWorld[static_cast<size_t>(bone)]) * frame;
		return Add(s);
	}

	const Socket* SocketSet::Find(const std::string& name) const
	{
		for (const Socket& s : m_Sockets)
			if (s.Name == name) return &s;
		return nullptr;
	}

	std::vector<std::string> SocketSet::Names() const
	{
		std::vector<std::string> names;
		for (const Socket& s : m_Sockets) names.push_back(s.Name);
		return names;
	}

	glm::mat4 SocketSet::Matrix(const std::vector<glm::mat4>& world, const std::string& name) const
	{
		const Socket* s = Find(name);
		return s ? world[static_cast<size_t>(s->Bone)] * s->Offset : glm::mat4(1.0f);
	}

	glm::mat4 SocketSet::Matrix(const ModelInstance& instance, const std::string& name) const
	{
		const Socket* s = Find(name);
		return s ? instance.GetNodeWorld(s->Bone) * s->Offset : glm::mat4(1.0f);
	}

	SocketSet StandardSockets(const Model& model, const HumanoidRig& rig)
	{
		SocketSet sockets;
		std::vector<glm::mat4> rest;
		PoseToWorld(model, RestPose(model), rest);
		auto pos = [&](int bone) { return glm::vec3(rest[static_cast<size_t>(bone)][3]); };
		auto palm = [&](const HumanoidRig::Arm& arm)
		{
			const glm::vec3 wrist = pos(arm.Hand);
			return arm.Middle >= 0 ? wrist + (pos(arm.Middle) - wrist) * 0.6f : wrist;   // the middle of the palm
		};
		if (rig.Right.Hand >= 0) sockets.AddFromRest(model, "hand_r", rig.Right.Hand, palm(rig.Right));
		if (rig.Left.Hand >= 0) sockets.AddFromRest(model, "hand_l", rig.Left.Hand, palm(rig.Left));
		if (rig.Head >= 0) sockets.AddFromRest(model, "head", rig.Head, pos(rig.Head) + glm::vec3(0.0f, 0.1f, 0.0f));
		if (rig.Spine2 >= 0)
		{
			sockets.AddFromRest(model, "back", rig.Spine2, pos(rig.Spine2) + glm::vec3(0.0f, 0.0f, -0.15f));
			sockets.AddFromRest(model, "carry", rig.Spine2, pos(rig.Spine2) + glm::vec3(0.0f, -0.1f, 0.28f));
		}
		if (rig.Hips >= 0)
		{
			sockets.AddFromRest(model, "hip_r", rig.Hips, pos(rig.Hips) + glm::vec3(-0.2f, -0.05f, 0.0f));
			sockets.AddFromRest(model, "hip_l", rig.Hips, pos(rig.Hips) + glm::vec3(0.2f, -0.05f, 0.0f));
		}
		if (rig.RightLeg.Foot >= 0) sockets.AddFromRest(model, "foot_r", rig.RightLeg.Foot, pos(rig.RightLeg.Foot));
		if (rig.LeftLeg.Foot >= 0) sockets.AddFromRest(model, "foot_l", rig.LeftLeg.Foot, pos(rig.LeftLeg.Foot));
		return sockets;
	}
}
