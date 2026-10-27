#pragma once
#include "GB/Animation/Rig.h"
#include "GB/Animation/Pose.h"
#include <string>
#include <vector>

namespace GB
{
	class ModelInstance;

	// A named place on a skeleton that things can be attached to (a hand, the back, the carrying point in front of the chest).
	// Defined on the skeleton, not on a model, so it works for every character that shares it. `Offset` is the socket's frame in the
	// space of its bone.
	struct Socket
	{
		std::string Name;
		int Bone = -1;
		glm::mat4 Offset{ 1.0f };
	};

	class SocketSet
	{
	public:
		// Adds a socket from a frame given in the rest pose's model space (the character faces +Z, +X is its left): `point` and
		// `rotation` are converted into the bone's space, so the socket keeps its place when the bone moves. False for a missing bone.
		bool AddFromRest(const Model& model, const std::string& name, int bone, const glm::vec3& point, const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
		bool Add(const Socket& socket);
		const Socket* Find(const std::string& name) const;
		std::vector<std::string> Names() const;
		size_t Size() const { return m_Sockets.size(); }

		// The socket's frame in model space, for the world matrices of a pose (PoseToWorld or ModelInstance::GetNodeWorld).
		glm::mat4 Matrix(const std::vector<glm::mat4>& world, const std::string& name) const;
		glm::mat4 Matrix(const ModelInstance& instance, const std::string& name) const;

	private:
		std::vector<Socket> m_Sockets;
		std::vector<glm::mat4> m_RestWorld;
	};

	// The usual sockets of a humanoid, placed from the rest pose (the ones whose bones exist): hand_r / hand_l (the palm), head,
	// back, carry (chest height, in front: a box held with both hands), hip_r / hip_l, foot_r / foot_l.
	SocketSet StandardSockets(const Model& model, const HumanoidRig& rig);
}
