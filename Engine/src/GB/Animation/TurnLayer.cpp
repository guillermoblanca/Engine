#include "GB/Animation/TurnLayer.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		// The part of `q` that turns around `axis` (swing-twist decomposition), taking the short way round.
		glm::quat Twist(glm::quat q, const glm::vec3& axis)
		{
			if (q.w < 0.0f) q = -q;
			const glm::vec3 v(q.x, q.y, q.z);
			const glm::vec3 p = axis * glm::dot(v, axis);
			const float len = std::sqrt(q.w * q.w + glm::dot(p, p));
			if (len < 1e-6f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			return glm::quat(q.w / len, p.x / len, p.y / len, p.z / len);
		}
	}

	TurnLayer::TurnLayer(const Model& model, const ClipAnalysisConfig& analysis)
		: m_Model(model), m_Analysis(analysis), m_Root(model.FindNode(analysis.RootBone)) {}

	int TurnLayer::Find(const std::string& set) const
	{
		for (size_t i = 0; i < m_Sets.size(); ++i)
			if (m_Sets[i].Set == set) return static_cast<int>(i);
		return -1;
	}

	bool TurnLayer::AddClip(const std::string& set, const std::string& clip)
	{
		const int index = m_Model.FindClip(clip);
		if (index < 0 || m_Root < 0) return false;
		ClipStats stats = AnalyzeClip(m_Model, index, m_Analysis);
		if (!stats.Valid || std::fabs(stats.RootYawDegrees) < 20.0f) return false;   // not a turn
		stats.Duration = m_Model.GetClips()[static_cast<size_t>(index)].Duration;
		const int slot = stats.RootYawDegrees > 0.0f ? 0 : 1;
		int at = Find(set);
		if (at < 0)
		{
			m_Sets.push_back({});
			at = static_cast<int>(m_Sets.size()) - 1;
			m_Sets.back().Set = set;
		}
		SetClips& entry = m_Sets[static_cast<size_t>(at)];
		entry.Clip[slot] = index;
		entry.Stats[slot] = stats;
		Pose first, last;
		SampleClip(m_Model, index, 0.0f, first);
		SampleClip(m_Model, index, stats.Duration, last);
		glm::quat delta = last[static_cast<size_t>(m_Root)].R * glm::conjugate(first[static_cast<size_t>(m_Root)].R);
		if (delta.w < 0.0f) delta = -delta;
		const glm::vec3 v(delta.x, delta.y, delta.z);
		entry.Axis[slot] = glm::length(v) > 1e-4f ? glm::normalize(v) : glm::vec3(0.0f, 1.0f, 0.0f);
		entry.Start[slot] = first[static_cast<size_t>(m_Root)].R;
		return true;
	}

	void TurnLayer::Update(const MotionState& state, float dt, const std::string& set)
	{
		int idx = Find(set);
		if (idx < 0) idx = Find("");
		const int side = state.YawRate > 0.0f ? 1 : -1;
		const int slot = side > 0 ? 0 : 1;
		const float threshold = m_Weight > 0.0f ? StartRate * 0.4f : StartRate;   // hysteresis: a turn that slows keeps playing
		const bool want = idx >= 0 && m_Sets[static_cast<size_t>(idx)].Clip[slot] >= 0 && state.Grounded && state.Speed < StopSpeed
			&& std::fabs(state.YawRate) >= threshold;
		if (want)
		{
			if (m_Current != idx || m_Side != side || m_Weight <= 0.0f)
			{
				m_Current = idx;
				m_Side = side;
				m_Time = 0.0f;
			}
			const ClipStats& stats = m_Sets[static_cast<size_t>(idx)].Stats[slot];
			const float clipRate = std::fabs(stats.RootYawDegrees) / std::max(stats.Duration, 1e-3f);
			const float scale = glm::clamp(std::fabs(state.YawRate) / std::max(clipRate, 1.0f), 0.5f, 2.0f);   // turn as fast as the body does
			m_Time = std::min(m_Time + dt * scale, stats.Duration);
			m_Weight = std::min(m_Weight + dt / std::max(BlendIn, 1e-3f), 1.0f);
			return;
		}
		if (m_Weight > 0.0f)
		{
			m_Weight = std::max(m_Weight - dt / std::max(BlendOut, 1e-3f), 0.0f);
			if (m_Weight <= 0.0f)
			{
				m_Current = -1;
				m_Side = 0;
				m_Time = 0.0f;
			}
		}
	}

	void TurnLayer::Apply(Pose& pose)
	{
		if (m_Current < 0 || m_Weight <= 0.0f) return;
		const int slot = m_Side > 0 ? 0 : 1;
		const SetClips& entry = m_Sets[static_cast<size_t>(m_Current)];
		const ClipStats& stats = entry.Stats[slot];
		SampleClip(m_Model, entry.Clip[slot], m_Time, m_Scratch);
		BonePose& root = m_Scratch[static_cast<size_t>(m_Root)];
		const float p = stats.Duration > 0.0f ? m_Time / stats.Duration : 0.0f;
		root.T.x -= stats.RootStart.x + stats.RootDelta.x * p;   // no drift: the controller owns the position
		root.T.z -= stats.RootStart.z + stats.RootDelta.z * p;
		// and no spin of the hips either: take out the rotation the clip has accumulated around its turning axis
		const glm::quat twist = Twist(root.R * glm::conjugate(entry.Start[slot]), entry.Axis[slot]);
		root.R = glm::normalize(glm::conjugate(twist) * root.R);
		const float s = m_Weight * m_Weight * (3.0f - 2.0f * m_Weight);
		BlendPoses(pose, m_Scratch, s, pose);
	}
}
