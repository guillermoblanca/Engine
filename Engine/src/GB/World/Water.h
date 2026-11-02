#pragma once
#include "GB/Water/WaterGeometry.h"
#include "GB/World/RiverData.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	struct WaterParams
	{
		glm::vec3 ShallowColor{ 0.10f, 0.38f, 0.40f };
		glm::vec3 DeepColor{ 0.01f, 0.07f, 0.16f };
		float Clarity = 2.5f;          // meters of water until the bottom is hidden
		float WaveHeight = 0.12f;      // meters, crest to rest
		float WaveLength = 7.0f;       // meters of the longest wave
		float Steepness = 0.6f;        // 0 = sine, 1 = sharp crests
		float Choppiness = 1.0f;       // scales the small waves and the surface detail
		glm::vec2 Direction{ 1.0f, 0.35f };   // where the waves travel (xz)
		float Roughness = 0.08f;
		float Foam = 1.0f;             // shoreline and crest foam amount
		float FoamWidth = 0.35f;       // meters of foam along the shore
		float RippleStrength = 1.0f;   // normal perturbation from WaterRipples
	};

	// A patch of water (waves are displaced in the shader): its look (Params), where it is (Position; Position.y is the
	// water level) and its CPU geometry. It holds no GPU object: the renderer uploads the geometry the first time it
	// draws it (WaterRenderer), so this is built, queried and tested with no OpenGL context. Drawn translucent after
	// the opaque geometry; tints by depth, reflects the sky, foams at the shore.
	class WaterSurface
	{
	public:
		// A flat grid (a lake, see Radius).
		explicit WaterSurface(float size = 64.0f, uint32_t subdivisions = 192);
		// A ribbon of flowing water along a river (see MakeRiverGeometry for how it is resampled).
		explicit WaterSurface(const RiverData& river, float widthScale = 1.5f);
		static constexpr int RiverColumns = kRiverColumns;
		static constexpr float RiverStep = kRiverStep;

		bool IsRiver() const { return m_Geometry->IsRiver(); }
		size_t GetRiverVertexCount() const { return m_Geometry->RiverVertices.size(); }
		const std::vector<Vertex>& GetRiverVertices() const { return m_Geometry->RiverVertices; }   // CPU copy of the ribbon (for tests)

		WaterParams Params;
		glm::vec3 Position{ 0.0f };
		float Radius = 0.0f;           // > 0: only a disc of this radius around Position is drawn (lakes)

		float GetSize() const { return m_Geometry->GridSize; }
		// The geometry the renderer uploads. Shared and immutable: a renderer may key its GPU mesh on it and notice when it is gone.
		const std::shared_ptr<const WaterGeometry>& Geometry() const { return m_Geometry; }

		// Vertical displacement of the waves at a world xz and time (same waves as the shader).
		float HeightAt(float x, float z, float time) const;
		float SurfaceY(float x, float z, float time) const { return Position.y + HeightAt(x, z, time); }

	private:
		std::shared_ptr<const WaterGeometry> m_Geometry;
	};

	// The shared wave set, so CPU queries match the GPU (4 Gerstner waves around the main direction). The shader's copy of these
	// numbers is generated from this struct (WaveSetGlsl, GB/Water/WaterWaves.h): there is one place to change them.
	struct WaveSet
	{
		static constexpr int Count = 4;
		static constexpr float Angle[Count] = { 0.0f, 0.42f, -0.58f, 0.95f };
		static constexpr float Length[Count] = { 1.0f, 0.62f, 0.38f, 0.24f };
		static constexpr float Amp[Count] = { 1.0f, 0.55f, 0.30f, 0.16f };
	};
}
