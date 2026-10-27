#include "GB/Animation/ClipAnalysis.h"
#include "GB/Assets/ModelInstance.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		glm::vec3 Position(const ModelInstance& inst, int node) { return glm::vec3(inst.GetNodeWorld(node)[3]); }

		// Facing on the ground plane: perpendicular to the line between the hips (left to right), or the root bone's +Z.
		float Yaw(const ModelInstance& inst, int root, int leftHip, int rightHip)
		{
			if (leftHip >= 0 && rightHip >= 0)
			{
				glm::vec3 side = Position(inst, leftHip) - Position(inst, rightHip);
				side.y = 0.0f;
				if (glm::dot(side, side) > 1e-8f)
				{
					const glm::vec3 forward = glm::cross(glm::normalize(side), glm::vec3(0.0f, 1.0f, 0.0f));
					return std::atan2(forward.x, forward.z);
				}
			}
			const glm::mat4& m = inst.GetNodeWorld(root);
			return std::atan2(m[2].x, m[2].z);
		}

		// Times (0..1) at which a foot lands: low, and nearly still over the ground.
		std::vector<float> Plants(const std::vector<glm::vec3>& foot, float ground, const ClipAnalysisConfig& cfg, float duration)
		{
			const int samples = static_cast<int>(foot.size());
			auto planted = [&](int i)
			{
				const int a = std::max(i - 1, 0), b = std::min(i + 1, samples - 1);
				const float dt = duration * static_cast<float>(b - a) / static_cast<float>(samples - 1);
				const glm::vec3 d = foot[static_cast<size_t>(b)] - foot[static_cast<size_t>(a)];
				return foot[static_cast<size_t>(i)].y < ground + cfg.PlantHeight && std::hypot(d.x, d.z) / std::max(dt, 1e-4f) < cfg.PlantSpeed;
			};
			std::vector<float> out;
			bool prev = planted(0);
			for (int i = 1; i < samples; ++i)
			{
				const bool on = planted(i);
				if (on && !prev) out.push_back(static_cast<float>(i) / static_cast<float>(samples - 1));
				prev = on;
			}
			return out;
		}
	}

	ClipStats AnalyzeClip(const Model& model, int clip, const ClipAnalysisConfig& config)
	{
		ClipStats s;
		if (clip < 0 || static_cast<size_t>(clip) >= model.GetClips().size()) return s;
		const AnimationClip& c = model.GetClips()[static_cast<size_t>(clip)];
		s.Name = c.Name;
		s.Duration = c.Duration;
		const int root = model.FindNode(config.RootBone), left = model.FindNode(config.LeftFoot), right = model.FindNode(config.RightFoot);
		const int leftHip = model.FindNode(config.LeftHip), rightHip = model.FindNode(config.RightHip);
		if (root < 0 || c.Duration <= 1e-4f) return s;

		ModelInstance inst(model);
		inst.Play(clip, false, 0.0f);
		const int n = std::max(static_cast<int>(std::ceil(c.Duration * config.SampleRate)), 2) + 1;
		std::vector<glm::vec3> rootPos(static_cast<size_t>(n));
		std::vector<float> yaw(static_cast<size_t>(n));
		std::vector<glm::vec3> leftFoot(static_cast<size_t>(n), glm::vec3(0.0f)), rightFoot(static_cast<size_t>(n), glm::vec3(0.0f));
		for (int i = 0; i < n; ++i)
		{
			inst.SetTime(c.Duration * static_cast<float>(i) / static_cast<float>(n - 1));
			rootPos[static_cast<size_t>(i)] = Position(inst, root);
			yaw[static_cast<size_t>(i)] = Yaw(inst, root, leftHip, rightHip);
			if (left >= 0) leftFoot[static_cast<size_t>(i)] = Position(inst, left);
			if (right >= 0) rightFoot[static_cast<size_t>(i)] = Position(inst, right);
		}

		s.RootDelta = rootPos.back() - rootPos.front();
		s.RootSpeed = std::hypot(s.RootDelta.x, s.RootDelta.z) / c.Duration;
		float turn = 0.0f;
		for (size_t i = 1; i < yaw.size(); ++i)
		{
			float d = yaw[i] - yaw[i - 1];
			while (d > 3.14159265f) d -= 6.2831853f;
			while (d < -3.14159265f) d += 6.2831853f;
			turn += d;
		}
		s.RootYawDegrees = glm::degrees(turn);
		s.RootHeightMin = s.RootHeightMax = rootPos[0].y;
		for (const glm::vec3& p : rootPos) { s.RootHeightMin = std::min(s.RootHeightMin, p.y); s.RootHeightMax = std::max(s.RootHeightMax, p.y); }

		// loop quality: how far the last pose is from the first, averaged over the bones with channels
		inst.SetTime(0.0f);
		std::vector<glm::mat4> first;
		for (size_t node = 0; node < model.GetNodes().size(); ++node) first.push_back(inst.GetNodeWorld(static_cast<int>(node)));
		inst.SetTime(c.Duration);
		double err = 0.0;
		size_t counted = 0;
		for (size_t node = 0; node < model.GetNodes().size(); ++node)
		{
			// compare orientations relative to the root so root motion does not count
			const glm::mat3 a(first[node]), b(inst.GetNodeWorld(static_cast<int>(node)));
			const glm::mat3 rel = glm::transpose(a) * b;
			const float cosine = std::clamp((rel[0][0] + rel[1][1] + rel[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
			err += static_cast<double>(std::acos(cosine));
			++counted;
		}
		s.LoopError = counted ? static_cast<float>(glm::degrees(err / static_cast<double>(counted))) : 0.0f;

		if (left >= 0 && right >= 0)
		{
			float ground = 1e9f;
			for (int i = 0; i < n; ++i) ground = std::min({ ground, leftFoot[static_cast<size_t>(i)].y, rightFoot[static_cast<size_t>(i)].y });
			s.LeftPlants = Plants(leftFoot, ground, config, c.Duration);
			s.RightPlants = Plants(rightFoot, ground, config, c.Duration);
			if (!s.LeftPlants.empty())
			{
				s.CycleLength = std::hypot(s.RootDelta.x, s.RootDelta.z) / static_cast<float>(s.LeftPlants.size());
				s.StepsPerSecond = static_cast<float>(s.LeftPlants.size() + s.RightPlants.size()) / c.Duration;
			}
			int first = -1, last = -1;
			for (int i = 0; i < n; ++i)
				if (leftFoot[static_cast<size_t>(i)].y > ground + config.AirHeight && rightFoot[static_cast<size_t>(i)].y > ground + config.AirHeight)
				{
					if (first < 0) first = i;
					last = i;
				}
			if (first >= 0)
			{
				s.AirStart = static_cast<float>(first) / static_cast<float>(n - 1);
				s.AirEnd = static_cast<float>(last) / static_cast<float>(n - 1);
			}
		}
		s.Valid = true;
		return s;
	}

	std::string FormatClipStats(const ClipStats& s)
	{
		if (!s.Valid) return s.Name + ": no root bone (check the bone names)";
		char buf[512];
		std::snprintf(buf, sizeof buf, "%s: %.2fs | root %.2f m (%.2f m/s) up/down %.2f..%.2f m, turn %.0f deg | loop error %.1f deg | plants L%zu R%zu, cycle %.2f m, %.1f steps/s%s",
			s.Name.c_str(), static_cast<double>(s.Duration), static_cast<double>(std::hypot(s.RootDelta.x, s.RootDelta.z)), static_cast<double>(s.RootSpeed),
			static_cast<double>(s.RootHeightMin), static_cast<double>(s.RootHeightMax), static_cast<double>(s.RootYawDegrees), static_cast<double>(s.LoopError),
			s.LeftPlants.size(), s.RightPlants.size(), static_cast<double>(s.CycleLength), static_cast<double>(s.StepsPerSecond),
			s.AirStart >= 0.0f ? (" | air " + std::to_string(static_cast<int>(s.AirStart * 100.0f)) + ".." + std::to_string(static_cast<int>(s.AirEnd * 100.0f)) + "%").c_str() : "");
		return buf;
	}
}
