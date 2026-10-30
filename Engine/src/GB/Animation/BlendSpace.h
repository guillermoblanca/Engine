#pragma once
#include "GB/Animation/ClipAnalysis.h"
#include <array>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// What the blend and sync code needs to know about a locomotion clip, measured from the animation itself.
	struct GaitInfo
	{
		int Clip = -1;
		float Duration = 0.0f;
		glm::vec2 Velocity{ 0.0f };      // root velocity in the clip: x toward the character's left, y forward (m/s)
		glm::vec3 RootStart{ 0.0f };     // root position at the first frame
		glm::vec3 RootDelta{ 0.0f };     // root displacement over the clip
		float LeftPlant = -1.0f;         // normalized time of the left foot landing (-1: no steps)
		float RightPlant = -1.0f;
		bool HasGait() const { return LeftPlant >= 0.0f && RightPlant >= 0.0f && Velocity != glm::vec2(0.0f); }
	};

	GaitInfo MakeGaitInfo(const Model& model, int clip, const ClipAnalysisConfig& config = {});

	// Clips placed on a plane (1D or 2D blend tree): a query point gets blend weights that are exact at the clips
	// and linear in between (barycentric inside the smallest triangle that contains the point; outside the clips'
	// hull, the nearest point of it, so the character never blends towards a speed no clip covers). The triangles are
	// the Delaunay triangulation of the clip positions (well-shaped ones, no slivers). Collinear
	// clips (a pure 1D tree) blend between the two neighbours along the line.
	class BlendSpace
	{
	public:
		struct Weight { size_t Index = 0; float Value = 0.0f; };

		size_t Add(const GaitInfo& info, glm::vec2 position);   // returns the entry index
		void Replace(size_t i, const GaitInfo& info, glm::vec2 position);   // moves / changes an entry (the triangulation is made again)
		size_t Size() const { return m_Entries.size(); }
		const GaitInfo& Info(size_t i) const { return m_Entries[i].Info; }
		glm::vec2 Position(size_t i) const { return m_Entries[i].Position; }
		// Weights of the entries that take part (sum 1). Empty only for an empty space.
		std::vector<Weight> Evaluate(glm::vec2 query) const;

	private:
		struct Entry { GaitInfo Info; glm::vec2 Position; };
		void BuildTriangles() const;
		std::vector<Entry> m_Entries;
		mutable std::vector<std::array<size_t, 3>> m_Triangles;   // Delaunay triangulation of the positions, built on first use
		mutable bool m_TrianglesReady = false;
	};
}
