#include "GB/AI/Utility.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	float Ramp(float x, float lo, float hi)
	{
		if (hi <= lo) return x >= hi ? 1.0f : 0.0f;
		return std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
	}

	float Falloff(float distance, float half)
	{
		return 1.0f / (1.0f + std::max(0.0f, distance) / std::max(half, 1e-3f));
	}

	float Urgency(float level, float from, float power)
	{
		if (from <= 0.0f) return 0.0f;
		return std::pow(std::clamp((from - level) / from, 0.0f, 1.0f), power);
	}

	int ChooseOption(const std::vector<UtilityOption>& options, const Commitment& current, double now, const ChooserConfig& cfg)
	{
		if (options.empty()) return -1;
		int best = 0, held = -1;
		for (size_t i = 0; i < options.size(); ++i)
		{
			if (options[i].Score > options[static_cast<size_t>(best)].Score) best = static_cast<int>(i);
			if (held < 0 && !current.Kind.empty() && options[i].Kind == current.Kind && options[i].Target == current.Target) held = static_cast<int>(i);
		}
		if (held < 0 || held == best) return best;
		const UtilityOption& rival = options[static_cast<size_t>(best)];
		const UtilityOption& mine = options[static_cast<size_t>(held)];
		if (rival.Score >= cfg.CriticalScore && rival.Score > mine.Score) return best;   // an emergency
		const float hold = std::max(cfg.MinCommit, current.Hold);
		if (now - current.Since < static_cast<double>(hold)) return held;               // still committed
		return rival.Score > mine.Score + cfg.StickBonus + cfg.Margin ? best : held;
	}
}
