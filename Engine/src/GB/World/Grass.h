#pragma once
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <map>
#include <vector>

namespace GB
{
	struct GrassParams
	{
		uint32_t Seed = 1;
		glm::vec2 Center{ 0.0f, 0.0f };
		float Size = 160.0f;               // meters per side of the covered square
		float Density = 45.0f;             // blades per square meter (before the draw-distance thinning)
		glm::vec2 Height{ 0.25f, 0.62f };  // blade height range (m)
		float Width = 0.04f;               // blade width at the root (m)
		float Lean = 0.22f;                // natural forward curve, fraction of the height
		glm::vec3 BaseColor{ 0.07f, 0.17f, 0.04f };
		glm::vec3 TipColor{ 0.34f, 0.52f, 0.13f };
		glm::vec3 DryColor{ 0.60f, 0.52f, 0.24f };   // some blades dry out toward this
		float DryAmount = 0.22f;
		float ChunkSize = 12.0f;           // blades are grouped in chunks so far ones can be skipped
		float DrawDistance = 34.0f;        // beyond this nothing is drawn; blades thin out before it
		float MaxSlopeDegrees = 38.0f;     // no grass on steeper ground
		float TrailBend = 1.0f;            // how strongly marks in the ground map flatten the grass (0 = ignore)
		std::function<float(float, float)> HeightFn;    // ground height at (x, z); default 0
		std::function<float(float, float)> DensityFn;   // optional 0..1 multiplier (0 = bare ground, e.g. sand, water)
		std::function<glm::vec3(float, float)> NormalFn;   // optional ground normal, for the slope limit
		// Optional per-place look: rgb multiplies the blade colors, w (0..1) dries the blade toward DryColor (on top of DryAmount's random
		// share). Unset: every blade is tinted (1, 1, 1, 0), the look of before.
		std::function<glm::vec4(float, float)> TintFn;
		std::function<float(float, float)> HeightScaleFn;   // optional multiplier of the blade height (tall clumps, short turf)
	};

	struct GrassBlade
	{
		glm::vec4 PositionYaw;    // x, y (ground), z, yaw (radians)
		glm::vec4 Shape;          // height, width, seed (0..1), lean
		glm::vec4 Tint{ 1.0f, 1.0f, 1.0f, 0.0f };   // color multiplier, dryness
	};

	struct GrassChunk
	{
		glm::vec3 Center{ 0.0f };
		float Radius = 0.0f;
		uint32_t First = 0;       // index of the first blade in the field's blade list
		uint32_t Count = 0;
	};

	// A field of grass blades, generated on the CPU and drawn instanced by the Renderer. Wind comes from the
	// environment, bending from Scene::Ground, snow/wetness from the environment. Needs no GL to generate.
	class GrassField
	{
	public:
		explicit GrassField(const GrassParams& params = {});
		~GrassField();
		GrassField(const GrassField&) = delete;
		GrassField& operator=(const GrassField&) = delete;

		GrassParams Params;
		size_t GetBladeCount() const { return m_Blades.size(); }
		const std::vector<GrassBlade>& GetBlades() const { return m_Blades; }
		const std::vector<GrassChunk>& GetChunks() const { return m_Chunks; }
		// Number of blades inside chunks that are within the draw distance of a point.
		size_t CountVisibleBlades(const glm::vec3& cameraPosition) const;
		// Moves the covered square (Size per side) to be centered on a point, on the field's own chunk grid: chunks that stay are kept as they
		// are, new ones are grown with the seed of their place, so the same spot always has the same blades. A big world keeps a field of
		// grass around the camera this way. Returns whether anything changed (the GPU buffer is then uploaded again on next use).
		bool Recenter(const glm::vec2& center);
		// Grows every chunk of the window again (after what DensityFn or the other functions read changed: a village trod the grass down).
		void Regrow();
		// ... only the chunks of the window that touch one of the rectangles (min x, min z, max x, max z). Returns whether any did (T5c: a trail
		// appeared or faded).
		bool RegrowAreas(const std::vector<glm::vec4>& rects);

		// GPU side (called by the Renderer; needs a GL context). Uploads on first use.
		uint32_t GetBuffer() const;

	private:
		struct ChunkBlades { GrassChunk Chunk; std::vector<GrassBlade> Blades; bool Empty = true; };
		void Generate();
		ChunkBlades GrowChunk(int cx, int cz) const;
		void Assemble();
		std::vector<GrassBlade> m_Blades;
		std::vector<GrassChunk> m_Chunks;
		glm::vec2 m_Anchor{ 0.0f };     // world x, z of chunk (0, 0)'s corner: where the field first was
		int m_ChunksPerSide = 1;
		glm::ivec2 m_First{ 0 };        // chunk index of the window's first corner
		std::map<std::pair<int, int>, ChunkBlades> m_Cache;
		mutable uint32_t m_Buffer = 0;
		mutable bool m_BufferStale = false;
	};
}
