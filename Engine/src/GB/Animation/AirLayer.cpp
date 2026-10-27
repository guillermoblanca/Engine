#include "GB/Animation/AirLayer.h"
#include <algorithm>

namespace GB
{
	AirLayer::AirLayer(const Model& model, const ClipAnalysisConfig& analysis)
		: m_Model(model), m_Root(model.FindNode(analysis.RootBone)) {}

	bool AirLayer::SetClip(const std::string& name)
	{
		const int clip = m_Model.FindClip(name);
		if (clip < 0) return false;
		ClipStats stats = AnalyzeClip(m_Model, clip);
		if (!stats.Valid || stats.AirStart < 0.0f || stats.AirEnd <= stats.AirStart) return false;
		stats.Duration = m_Model.GetClips()[static_cast<size_t>(clip)].Duration;
		m_Clip = clip;
		m_Stats = stats;
		return true;
	}

	void AirLayer::Update(const MotionState& state, float dt)
	{
		if (m_Clip < 0) return;
		const float airStart = m_Stats.AirStart * m_Stats.Duration, airEnd = m_Stats.AirEnd * m_Stats.Duration;
		if (!state.Grounded)
		{
			if (!m_InAir)
			{
				m_InAir = true;
				m_Time = std::max(airStart - LeadIn, 0.0f);
			}
			m_Time = std::min(m_Time + dt, airEnd);   // holds the falling pose until the ground
			m_Weight = std::min(m_Weight + dt / std::max(BlendIn, 1e-3f), 1.0f);
			return;
		}
		if (m_InAir)
		{
			m_InAir = false;
			m_Time = std::max(m_Time, airEnd);   // a short hop skips straight to the landing
		}
		if (m_Weight > 0.0f)
		{
			m_Time = std::min(m_Time + dt, m_Stats.Duration);
			m_Weight = std::max(m_Weight - dt / std::max(BlendOut, 1e-3f), 0.0f);
		}
	}

	void AirLayer::Apply(Pose& pose)
	{
		if (m_Clip < 0 || m_Weight <= 0.0f) return;
		SampleClip(m_Model, m_Clip, m_Time, m_Scratch);
		if (m_Root >= 0)
		{
			const float p = m_Stats.Duration > 0.0f ? m_Time / m_Stats.Duration : 0.0f;
			BonePose& root = m_Scratch[static_cast<size_t>(m_Root)];
			root.T.x -= m_Stats.RootStart.x + m_Stats.RootDelta.x * p;
			root.T.z -= m_Stats.RootStart.z + m_Stats.RootDelta.z * p;
			root.T.y = std::min(root.T.y, m_Stats.RootStart.y);   // no rise: the controller jumps; keep the crouches
		}
		const float s = m_Weight * m_Weight * (3.0f - 2.0f * m_Weight);
		BlendPoses(pose, m_Scratch, s, pose);
	}
}
