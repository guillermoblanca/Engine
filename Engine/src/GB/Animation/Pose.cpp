#include "GB/Animation/Pose.h"
#include <algorithm>
#include <cmath>
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
				v[c] = values[index * static_cast<size_t>(components) + static_cast<size_t>(c)];
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

		void BlendBone(const BonePose& a, const BonePose& b, float t, BonePose& out)
		{
			out.T = glm::mix(a.T, b.T, t);
			out.S = glm::mix(a.S, b.S, t);
			// the short way round: flip b when the two point away from each other
			const glm::quat bq = glm::dot(a.R, b.R) < 0.0f ? -b.R : b.R;
			out.R = glm::normalize(glm::slerp(a.R, bq, t));
		}
	}

	Pose RestPose(const Model& model)
	{
		const auto& nodes = model.GetNodes();
		Pose pose(nodes.size());
		for (size_t i = 0; i < nodes.size(); ++i)
			pose[i] = { nodes[i].Translation, nodes[i].Rotation, nodes[i].Scale };
		return pose;
	}

	void SampleClip(const Model& model, int clip, float time, Pose& out)
	{
		out = RestPose(model);
		if (clip < 0 || static_cast<size_t>(clip) >= model.GetClips().size())
			return;
		for (const AnimationChannel& ch : model.GetClips()[static_cast<size_t>(clip)].Channels)
		{
			if (ch.Node < 0 || static_cast<size_t>(ch.Node) >= out.size() || ch.Times.empty())
				continue;
			const glm::vec4 v = SampleChannel(ch, time);
			BonePose& p = out[static_cast<size_t>(ch.Node)];
			switch (ch.Target)
			{
			case AnimationTarget::Translation: p.T = glm::vec3(v); break;
			case AnimationTarget::Scale: p.S = glm::vec3(v); break;
			case AnimationTarget::Rotation: p.R = glm::quat(v.w, v.x, v.y, v.z); break;
			}
		}
	}

	void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out)
	{
		const size_t n = std::min(a.size(), b.size());
		if (&out != &a) out.resize(n);
		for (size_t i = 0; i < n; ++i)
			BlendBone(a[i], b[i], t, out[i]);
	}

	void BlendPosesMasked(const Pose& a, const Pose& b, float t, const std::vector<float>& mask, Pose& out)
	{
		const size_t n = std::min(a.size(), b.size());
		if (&out != &a) out.resize(n);
		for (size_t i = 0; i < n; ++i)
			BlendBone(a[i], b[i], i < mask.size() ? t * std::clamp(mask[i], 0.0f, 1.0f) : 0.0f, out[i]);
	}

	std::vector<float> MakeBoneMask(const Model& model, const std::string& rootBone)
	{
		const int root = model.FindNode(rootBone);
		if (root < 0) return {};
		std::vector<float> mask(model.GetNodes().size(), 0.0f);
		for (int n : model.GetNodeOrder())   // parents first
		{
			if (n == root) mask[static_cast<size_t>(n)] = 1.0f;
			else if (model.GetNodes()[static_cast<size_t>(n)].Parent >= 0) mask[static_cast<size_t>(n)] = mask[static_cast<size_t>(model.GetNodes()[static_cast<size_t>(n)].Parent)];
		}
		return mask;
	}

	void PoseToWorld(const Model& model, const Pose& pose, std::vector<glm::mat4>& world)
	{
		const auto& nodes = model.GetNodes();
		world.resize(nodes.size());
		for (int n : model.GetNodeOrder())   // parents come before their children
		{
			const BonePose& p = pose[static_cast<size_t>(n)];
			const glm::mat4 local = glm::translate(glm::mat4(1.0f), p.T) * glm::mat4_cast(glm::normalize(p.R)) * glm::scale(glm::mat4(1.0f), p.S);
			const int parent = nodes[static_cast<size_t>(n)].Parent;
			world[static_cast<size_t>(n)] = parent >= 0 ? world[static_cast<size_t>(parent)] * local : local;
		}
	}
}
