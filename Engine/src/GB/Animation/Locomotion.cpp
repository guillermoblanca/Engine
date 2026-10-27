#include "GB/Animation/Locomotion.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	LocomotionBlender::LocomotionBlender(const Model& model, const LocomotionOptions& options)
		: m_Model(model), m_Options(options), m_Root(model.FindNode(options.Analysis.RootBone))
	{
		m_Pose = RestPose(model);
		m_Scratch = m_Pose;
	}

	bool LocomotionBlender::AddClip(const std::string& name) { return AddClip(m_Model.FindClip(name)); }

	bool LocomotionBlender::AddClip(int clip)
	{
		const GaitInfo info = MakeGaitInfo(m_Model, clip, m_Options.Analysis);
		if (info.Clip < 0) return false;
		m_Space.Add(info, info.Velocity);

		// does the clip close its loop? when a bone ends more than a few degrees from where it began, keep how far
		std::vector<glm::quat> fix;
		Pose first, last;
		SampleClip(m_Model, info.Clip, 0.0f, first);
		SampleClip(m_Model, info.Clip, m_Model.GetClips()[static_cast<size_t>(info.Clip)].Duration, last);
		float worst = 0.0f;
		for (size_t b = 0; b < first.size(); ++b)
		{
			glm::quat d = last[b].R * glm::conjugate(first[b].R);
			if (d.w < 0.0f) d = -d;
			worst = std::max(worst, glm::degrees(2.0f * std::acos(std::min(d.w, 1.0f))));
			fix.push_back(d);
		}
		if (worst <= 4.0f) fix.clear();
		m_LoopFix.resize(m_Space.Size());
		m_LoopFix[m_Space.Size() - 1] = std::move(fix);
		return true;
	}

	float LocomotionBlender::MaxSpeed() const
	{
		float best = 0.0f;
		for (size_t i = 0; i < m_Space.Size(); ++i)
			best = std::max(best, glm::length(m_Space.Info(i).Velocity));
		return best;
	}

	std::vector<LocomotionBlender::Contribution> LocomotionBlender::Contributions() const
	{
		std::vector<Contribution> out;
		for (const BlendSpace::Weight& w : m_Weights)
			out.push_back({ m_Model.GetClips()[static_cast<size_t>(m_Space.Info(w.Index).Clip)].Name, w.Value });
		return out;
	}

	void LocomotionBlender::Update(float dt)
	{
		m_Events.clear();
		if (m_Space.Size() == 0) return;

		// follow the commanded velocity (exponential smoothing, frame-rate independent)
		const float follow = m_Options.VelocitySmoothing > 1e-4f ? 1.0f - std::exp(-dt / m_Options.VelocitySmoothing) : 1.0f;
		m_Velocity += (m_Target - m_Velocity) * follow;

		m_Weights = m_Space.Evaluate(m_Velocity);

		// what the blend of clips travels at, how much of it is a gait, and how fast it cycles (frequencies blend, not durations,
		// so an idle at 8 s does not stretch a walk and a walk does not rush an idle)
		glm::vec2 blendVelocity(0.0f);
		float frequency = 0.0f, gait = 0.0f, rightPhase = 0.0f, rightWeight = 0.0f;
		for (const BlendSpace::Weight& w : m_Weights)
		{
			const GaitInfo& g = m_Space.Info(w.Index);
			blendVelocity += g.Velocity * w.Value;
			frequency += w.Value / std::max(g.Duration, 1e-3f);
			if (g.HasGait())
			{
				gait += w.Value;
				rightPhase += w.Value * std::fmod(g.RightPlant - g.LeftPlant + 1.0f, 1.0f);
				rightWeight += w.Value;
			}
		}
		m_GaitWeight = gait;
		rightPhase = rightWeight > 1e-4f ? rightPhase / rightWeight : 0.5f;

		// match the feet to the ground: play faster when the commanded speed is above what the blended clips travel at
		float scale = 1.0f;
		const float blendSpeed = glm::length(blendVelocity), speed = glm::length(m_Velocity);
		if (blendSpeed > 0.3f)
			scale = std::clamp(speed / blendSpeed, m_Options.MinSpeedScale, m_Options.MaxSpeedScale);
		m_SpeedScale = 1.0f + (scale - 1.0f) * std::clamp(gait, 0.0f, 1.0f);

		// advance the shared phase and report the landings it crossed
		const float before = m_Phase;
		const float after = before + dt * frequency * m_SpeedScale;
		if (gait >= m_Options.MinGaitWeight)
		{
			auto crossings = [&](float marker, bool left)
			{
				// markers at marker + k for integer k inside (before, after]
				for (int k = static_cast<int>(std::floor(before - marker)) + 1; marker + static_cast<float>(k) <= after + 1e-6f; ++k)
					if (marker + static_cast<float>(k) > before + 1e-6f)
						m_Events.push_back({ left, gait });
			};
			crossings(0.0f, true);
			crossings(rightPhase, false);
		}
		m_Phase = after - std::floor(after);

		// sample every clip at the shared phase (aligned on its own left landing) and blend them
		bool first = true;
		float total = 0.0f;
		for (const BlendSpace::Weight& w : m_Weights)
		{
			const GaitInfo& g = m_Space.Info(w.Index);
			const float offset = g.HasGait() ? g.LeftPlant : 0.0f;
			float p = m_Phase + offset;
			p -= std::floor(p);
			SampleClip(m_Model, g.Clip, p * g.Duration, m_Scratch);
			if (w.Index < m_LoopFix.size() && !m_LoopFix[w.Index].empty())
			{
				const std::vector<glm::quat>& fix = m_LoopFix[w.Index];
				for (size_t b = 0; b < fix.size() && b < m_Scratch.size(); ++b)
					m_Scratch[b].R = glm::normalize(glm::conjugate(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), fix[b], p)) * m_Scratch[b].R);
			}
			if (m_Options.StripRootMotion && m_Root >= 0)
			{
				// the clip's forward travel is the controller's job: keep only the sway around the straight line
				BonePose& root = m_Scratch[static_cast<size_t>(m_Root)];
				root.T.x -= g.RootStart.x + g.RootDelta.x * p;
				root.T.z -= g.RootStart.z + g.RootDelta.z * p;
			}
			if (first) { m_Pose = m_Scratch; first = false; total = w.Value; }
			else
			{
				total += w.Value;
				BlendPoses(m_Pose, m_Scratch, w.Value / total, m_Pose);
			}
		}
	}
}
