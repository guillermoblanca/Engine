#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	// What an agent can sense (H5): sight (a cone with a range, plus a short all-round awareness, blocked by static obstacles) and hearing (events
	// with a radius: axe blows, steps, shouts). Pure maths on the ground plane (x, z): no scene, no renderer, no randomness, so it runs headless and
	// the same inputs always give the same answer.

	struct VisionParams
	{
		float Range = 28.0f;          // metres the cone reaches
		float HalfFov = 1.0472f;      // radians either side of the facing (60 degrees: 120 in all)
		float NearRange = 3.0f;       // everything this close is noticed whatever the facing
	};

	// A static obstacle that blocks sight: a trunk, a boulder, a wall piece (a circle on the ground).
	struct Occluder
	{
		std::string Id;
		glm::vec2 Center{ 0.0f };
		float Radius = 0.5f;
	};

	// Distance and cone only (no obstacles).
	bool InCone(const VisionParams& vision, const glm::vec2& eye, const glm::vec2& facing, const glm::vec2& target);
	// Does the segment eye -> target pass through the circle?
	bool SegmentHitsCircle(const glm::vec2& a, const glm::vec2& b, const glm::vec2& center, float radius);
	// Can the agent see the target? `ignoreId` is the occluder that IS the target (a tree is not hidden behind itself); an occluder around the eye
	// (the agent stands next to a trunk) does not block either.
	bool CanSee(const VisionParams& vision, const glm::vec2& eye, const glm::vec2& facing, const glm::vec2& target,
		const std::vector<Occluder>& occluders, const std::string& ignoreId = "");

	// Something that made a noise.
	struct SoundEvent
	{
		uint64_t Seq = 0;
		std::string Kind;         // "chop", "steps", "scream"
		std::string Source;       // who made it
		std::string Subject;      // what it was about (the tree that is being felled)
		glm::vec2 Pos{ 0.0f };
		float Radius = 20.0f;     // how far it carries
		double Time = 0.0;
	};

	// The sounds of the last few seconds. Agents ask for what they have not heard yet (by sequence number), so nothing is heard twice.
	class SoundBus
	{
	public:
		uint64_t Emit(const std::string& kind, const std::string& source, const std::string& subject, const glm::vec2& pos, float radius, double now);
		// Events after `since` that reach `listener` (radius scaled by `hearing`), oldest first. The listener's own sounds are left out.
		std::vector<SoundEvent> Heard(const std::string& listenerName, const glm::vec2& listener, float hearing, uint64_t since) const;
		void Prune(double now, double keep = 6.0);
		uint64_t LastSeq() const { return m_Seq; }
		const std::vector<SoundEvent>& Recent() const { return m_Events; }
		void Clear() { m_Events.clear(); }
		void SetSeq(uint64_t seq) { m_Seq = seq; }   // a loaded game goes on numbering where it left off
	private:
		std::vector<SoundEvent> m_Events;
		uint64_t m_Seq = 0;
	};
}
