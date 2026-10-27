#pragma once
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
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
	};

	struct GrassBlade
	{
		glm::vec4 PositionYaw;    // x, y (ground), z, yaw (radians)
		glm::vec4 Shape;          // height, width, seed (0..1), lean
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

		// GPU side (called by the Renderer; needs a GL context). Uploads on first use.
		uint32_t GetBuffer() const;

	private:
		void Generate();
		std::vector<GrassBlade> m_Blades;
		std::vector<GrassChunk> m_Chunks;
		mutable uint32_t m_Buffer = 0;
	};
}
