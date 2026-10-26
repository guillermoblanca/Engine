#include "ModelInstance.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		// Keyframe lookup: returns i with Times[i] <= t < Times[i+1] (clamped to [0, n-2]).
		size_t FindKey(const std::vector<float>& times, float t)
		{
			const auto it = std::upper_bound(times.begin(), times.end(), t);
			const size_t upper = static_cast<size_t>(it - times.begin());
			return upper == 0 ? 0 : std::min(upper - 1, times.size() - 2);
		}

		glm::vec4 ReadVec(const std::vector<float>& values, size_t index, int components)
		{
			glm::vec4 v(0.0f);
			for (int c = 0; c < components; ++c)
				v[c] = values[index * components + c];
			return v;
		}

		// Samples one channel at `time`; result has 3 (translation/scale) or 4 (rotation) meaningful components.
		glm::vec4 SampleChannel(const AnimationChannel& ch, float time)
		{
			const int comps = ch.Target == AnimationTarget::Rotation ? 4 : 3;
			const bool cubic = ch.Interpolation == AnimationInterpolation::CubicSpline;
			const size_t stride = cubic ? 3 : 1;
			const size_t n = ch.Times.size();
			auto valueAt = [&](size_t key) { return ReadVec(ch.Values, key * stride + (cubic ? 1 : 0), comps); };

			glm::vec4 result;
			if (n == 1 || time <= ch.Times.front())
				result = valueAt(0);
			else if (time >= ch.Times.back())
				result = valueAt(n - 1);
			else
			{
				const size_t i = FindKey(ch.Times, time);
				const float dt = ch.Times[i + 1] - ch.Times[i];
				const float t = dt > 0.0f ? (time - ch.Times[i]) / dt : 0.0f;
				if (ch.Interpolation == AnimationInterpolation::Step)
					result = valueAt(i);
				else if (cubic)
				{
					const glm::vec4 p0 = valueAt(i), p1 = valueAt(i + 1);
					const glm::vec4 m0 = ReadVec(ch.Values, i * 3 + 2, comps) * dt;       // out-tangent of key i
					const glm::vec4 m1 = ReadVec(ch.Values, (i + 1) * 3 + 0, comps) * dt;  // in-tangent of key i+1
					const float t2 = t * t, t3 = t2 * t;
					result = (2 * t3 - 3 * t2 + 1) * p0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * p1 + (t3 - t2) * m1;
				}
				else if (ch.Target == AnimationTarget::Rotation)
				{
					const glm::vec4 a = valueAt(i), b = valueAt(i + 1);
					const glm::quat q = glm::slerp(glm::quat(a.w, a.x, a.y, a.z), glm::quat(b.w, b.x, b.y, b.z), t);
					result = glm::vec4(q.x, q.y, q.z, q.w);
				}
				else
					result = glm::mix(valueAt(i), valueAt(i + 1), t);
			}
			if (ch.Target == AnimationTarget::Rotation)
			{
				const float len = glm::length(result);
				if (len > 0.0f)
					result /= len;
			}
			return result;
		}
	}

	ModelInstance::ModelInstance(const Model& model) : m_Model(model)
	{
		const auto& nodes = model.GetNodes();
		m_Rest.resize(nodes.size());
		for (size_t i = 0; i < nodes.size(); ++i)
			m_Rest[i] = { nodes[i].Translation, nodes[i].Rotation, nodes[i].Scale };
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
		if (m_Paused)
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

	void ModelInstance::Sample(const Playhead& head, std::vector<Pose>& pose) const
	{
		pose = m_Rest;
		if (head.Clip < 0)
			return;
		for (const AnimationChannel& ch : m_Model.GetClips()[head.Clip].Channels)
		{
			const glm::vec4 v = SampleChannel(ch, head.Time);
			Pose& p = pose[ch.Node];
			switch (ch.Target)
			{
			case AnimationTarget::Translation: p.T = glm::vec3(v); break;
			case AnimationTarget::Scale: p.S = glm::vec3(v); break;
			case AnimationTarget::Rotation: p.R = glm::quat(v.w, v.x, v.y, v.z); break;
			}
		}
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

		const auto& nodes = m_Model.GetNodes();
		for (int n : m_Model.GetNodeOrder())   // parents come before their children
		{
			const Pose& p = m_PoseA[n];
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
