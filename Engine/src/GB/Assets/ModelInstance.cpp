#include "ModelInstance.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	ModelInstance::ModelInstance(const Model& model) : m_Model(model)
	{
		const auto& nodes = model.GetNodes();
		m_Rest = RestPose(model);
		m_PoseA = m_PoseB = m_Rest;
		m_World.assign(nodes.size(), glm::mat4(1.0f));
		m_Palettes.resize(nodes.size());
		Evaluate();
	}

	bool ModelInstance::Play(int clip, bool loop, float fadeSeconds)
	{
		if (clip < 0 || static_cast<size_t>(clip) >= m_Model.GetClips().size())
			return false;
		if (fadeSeconds > 0.0f && m_Current.Clip >= 0)
		{
			m_Previous = m_Current;
			m_FadeDuration = fadeSeconds;
			m_FadeElapsed = 0.0f;
		}
		else
			m_FadeDuration = 0.0f;
		m_External = false;
		m_Current = { clip, 0.0f, loop };
		Evaluate();
		return true;
	}

	bool ModelInstance::Play(const std::string& clipName, bool loop, float fadeSeconds)
	{
		return Play(m_Model.FindClip(clipName), loop, fadeSeconds);
	}

	void ModelInstance::Stop()
	{
		m_External = false;
		m_Current = {};
		m_FadeDuration = 0.0f;
		Evaluate();
	}

	void ModelInstance::SetTime(float seconds)
	{
		if (m_Current.Clip < 0)
			return;
		m_FadeDuration = 0.0f;
		m_Current.Time = std::max(seconds, 0.0f);
		Advance(m_Current, 0.0f);   // wrap / clamp
		Evaluate();
	}

	bool ModelInstance::IsFinished() const
	{
		if (m_Current.Clip < 0)
			return false;
		return !m_Current.Loop && m_Current.Time >= m_Model.GetClips()[m_Current.Clip].Duration;
	}

	void ModelInstance::Advance(Playhead& head, float dt) const
	{
		if (head.Clip < 0)
			return;
		const float duration = m_Model.GetClips()[head.Clip].Duration;
		head.Time += dt;
		if (duration <= 0.0f)
			head.Time = 0.0f;
		else if (head.Loop)
			head.Time = std::fmod(head.Time, duration);
		else
			head.Time = std::min(head.Time, duration);
	}

	void ModelInstance::Update(float deltaTime)
	{
		if (m_Paused || m_External)
			return;
		const float dt = deltaTime * m_Speed;
		Advance(m_Current, dt);
		if (m_FadeDuration > 0.0f)
		{
			Advance(m_Previous, dt);
			m_FadeElapsed += std::abs(deltaTime);
			if (m_FadeElapsed >= m_FadeDuration)
				m_FadeDuration = 0.0f;
		}
		Evaluate();
	}

	void ModelInstance::Sample(const Playhead& head, Pose& pose) const
	{
		SampleClip(m_Model, head.Clip, head.Time, pose);
	}

	void ModelInstance::SetPose(const Pose& pose)
	{
		if (pose.size() != m_Rest.size())
			return;
		m_External = true;
		m_FadeDuration = 0.0f;
		m_PoseA = pose;
		UpdateMatrices();
	}

	void ModelInstance::Evaluate()
	{
		Sample(m_Current, m_PoseA);
		if (m_FadeDuration > 0.0f)
		{
			Sample(m_Previous, m_PoseB);
			const float w = std::clamp(m_FadeElapsed / m_FadeDuration, 0.0f, 1.0f);
			for (size_t i = 0; i < m_PoseA.size(); ++i)
			{
				m_PoseA[i].T = glm::mix(m_PoseB[i].T, m_PoseA[i].T, w);
				m_PoseA[i].S = glm::mix(m_PoseB[i].S, m_PoseA[i].S, w);
				m_PoseA[i].R = glm::slerp(m_PoseB[i].R, m_PoseA[i].R, w);
			}
		}

		UpdateMatrices();
	}

	void ModelInstance::UpdateMatrices()
	{
		const auto& nodes = m_Model.GetNodes();
		for (int n : m_Model.GetNodeOrder())   // parents come before their children
		{
			const BonePose& p = m_PoseA[n];
			const glm::mat4 local = glm::translate(glm::mat4(1.0f), p.T) * glm::mat4_cast(glm::normalize(p.R))
				* glm::scale(glm::mat4(1.0f), p.S);
			m_World[n] = nodes[n].Parent >= 0 ? m_World[nodes[n].Parent] * local : local;
		}

		for (size_t n = 0; n < nodes.size(); ++n)
		{
			if (nodes[n].SkinIndex < 0)
				continue;
			const ModelSkin& skin = m_Model.GetSkins()[nodes[n].SkinIndex];
			std::vector<glm::mat4>& palette = m_Palettes[n];
			palette.resize(skin.Joints.size());
			for (size_t j = 0; j < skin.Joints.size(); ++j)
				palette[j] = m_World[skin.Joints[j]] * skin.InverseBind[j];
		}
	}

	bool ModelInstance::GetCamera(int index, Camera& out, const glm::mat4& root) const
	{
		const auto& cameras = m_Model.GetCameras();
		if (index < 0 || static_cast<size_t>(index) >= cameras.size())
			return false;
		const ModelCamera& src = cameras[index];
		const glm::mat4 world = root * m_World[src.Node];
		out.Position = glm::vec3(world[3]);
		out.Target = out.Position + glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
		out.Up = glm::normalize(glm::vec3(world * glm::vec4(0, 1, 0, 0)));
		out.Orthographic = src.Orthographic;
		out.FovYDegrees = src.FovYDegrees;
		out.OrthoHalfHeight = src.OrthoHalfHeight;
		out.NearPlane = src.NearPlane;
		out.FarPlane = src.FarPlane;
		return true;
	}

	void ModelInstance::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		const auto& nodes = m_Model.GetNodes();
		for (int n : m_Model.GetNodeOrder())
		{
			if (nodes[n].MeshIndex < 0)
				continue;
			const bool skinned = nodes[n].SkinIndex >= 0;
			for (const Model::Primitive& prim : m_Model.GetPrimitives(nodes[n].MeshIndex))
			{
				RenderObject obj;
				obj.MeshPtr = prim.MeshData.get();
				obj.Mat = m_Model.GetMaterial(prim.MaterialIndex);
				if (skinned && prim.MeshData->HasSkin())
				{
					// glTF: the transform of a skinned mesh's node is ignored; the joints place the vertices.
					obj.Transform = root;
					obj.JointMatrices = &m_Palettes[n];
				}
				else
					obj.Transform = root * m_World[n];
				scene.Objects.push_back(std::move(obj));
			}
		}
	}
}
