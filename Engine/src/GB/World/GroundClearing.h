#pragma once
// T332: where the ground is bare of wild grass because something stands or is worked on it: the fields (their plots) and the footprints of the
// buildings and the sites. A set of rectangles (turned by a yaw) grown by a margin, with a soft fade outside; At answers 0 (untouched) .. 1
// (bare) for any place, fast enough to be asked for every grass blade (a bucket grid of the rectangles). Changed gives the areas whose answer
// differs between two sets, for the grass to grow again only there (the editor's GrassField::RegrowAreas). Pure CPU, deterministic.
#include <glm/glm.hpp>
#include <map>
#include <utility>
#include <vector>

namespace GB
{
	class GroundClearing
	{
	public:
		struct Patch
		{
			glm::vec2 Centre{ 0.0f };
			glm::vec2 Half{ 0.0f };   // half its size along its own axes (the margin included)
			float Yaw = 0.0f;         // degrees about the vertical (as Transform.Rotation.y and GroundRect)
			// optional: the patch only clears inside this polygon (x, z points; a painted zone's perimeter) grown by ClipMargin m, so the cells
			// whose square sticks out of the perimeter do not clear the grass beyond it
			std::vector<glm::vec2> Clip;
			float ClipMargin = 0.0f;
			bool operator==(const Patch&) const = default;
			bool operator<(const Patch& o) const;
		};

		// Replaces the patches (sorted, so the same set in any order is the same clearing); `feather` m of fade outside each.
		void Set(std::vector<Patch> patches, float feather);
		float At(float x, float z) const;
		const std::vector<Patch>& Patches() const { return m_Patches; }
		float Feather() const { return m_Feather; }
		// The world box (min x, min z, max x, max z) a patch changes, with its fade.
		static glm::vec4 Bounds(const Patch& p, float feather);
		// The boxes of the patches in one set and not the other (both sorted, as Patches() is): what to regrow when `before` becomes `after`.
		static std::vector<glm::vec4> Changed(const std::vector<Patch>& before, float featherBefore, const std::vector<Patch>& after, float featherAfter);

	private:
		static constexpr float kBucket = 8.0f;   // m a side of a bucket
		std::vector<Patch> m_Patches;
		std::vector<glm::vec2> m_Axis;            // per patch: cos, sin of its yaw
		float m_Feather = 0.5f;
		std::map<std::pair<int, int>, std::vector<int>> m_Buckets;
	};
}
