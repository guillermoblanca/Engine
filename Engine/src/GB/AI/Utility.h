#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	// The decision engine of an agent (H6): utility AI. Whoever knows the world (the tribe) lists what the agent could do now as options with a
	// score; each score is a product of considerations (how much it needs it, how well its role fits, how far it is, how sure it is of what it
	// remembers), recorded as named factors so the inspector can say WHY. ChooseOption then picks one, with hysteresis: the one it is already
	// doing keeps a small bonus, a rival must beat it by a margin, and it is kept for a minimum time unless something becomes critical, so the
	// agent does not dither between two almost equal jobs. Pure and deterministic: ties go to the first option in the list.

	struct ScoreFactor
	{
		std::string Name;
		float Value = 0.0f;
	};

	struct UtilityOption
	{
		std::string Kind;        // "eat", "gather", "sleep", "explore"...
		std::string Target;      // the node, item or place it is about
		glm::vec2 Where{ 0.0f }; // where it happens
		float Score = 0.0f;
		float Commit = 0.0f;     // minimum seconds to stay with it (0 = the configured default)
		std::vector<ScoreFactor> Factors;
		std::string Note;        // a sentence for people ("hunger 22/100, 3 berries in the bag")
	};

	// 0 at or below `lo`, 1 at or above `hi`, a straight line between.
	float Ramp(float x, float lo, float hi);
	// 1 at distance 0, 0.5 at `half` metres, falling slowly after.
	float Falloff(float distance, float half);
	// How pressing a need is: 0 when it is at or above `from`, 1 when empty (shaped by `power`: >1 stays calm until it is nearly empty).
	float Urgency(float level, float from, float power = 1.0f);

	// What it is doing, so a new decision can compare with it.
	struct Commitment
	{
		std::string Kind, Target;
		double Since = 0.0;
		float Hold = 0.0f;       // minimum seconds asked by the option chosen
	};

	struct ChooserConfig
	{
		float MinCommit = 8.0f;        // seconds before a rival of similar score can take over
		float Margin = 0.15f;          // how much better a rival must be
		float StickBonus = 0.05f;      // extra for what it is already doing
		float CriticalScore = 1.6f;    // a rival at or above this takes over at once
	};

	// Index of the option to do, or -1 when the list is empty. Adds the stick bonus in the comparison only (scores in the list are not changed).
	int ChooseOption(const std::vector<UtilityOption>& options, const Commitment& current, double now, const ChooserConfig& cfg = ChooserConfig());
}
