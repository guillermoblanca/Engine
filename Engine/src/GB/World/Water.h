#pragma once
#include "GB/Renderer/Mesh.h"
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

	// A flat patch of water (waves are displaced in the shader). Place it with Position (Position.y is the
	// water level). Drawn translucent after the opaque geometry; tints by depth, reflects the sky, foams at the shore.
	class WaterSurface
	{
	public:
		explicit WaterSurface(float size = 64.0f, uint32_t subdivisions = 192);
		// A ribbon of flowing water along a river (see HillParams::RiverPath): the surface follows the water level,
		// the shader advects the surface detail along the current and adds foam in the rapids.
		explicit WaterSurface(const RiverData& river, float widthScale = 1.5f);

		bool IsRiver() const { return !m_RiverVertices.empty(); }
		size_t GetRiverVertexCount() const { return m_RiverVertices.size(); }

		WaterParams Params;
		glm::vec3 Position{ 0.0f };
		float Radius = 0.0f;           // > 0: only a disc of this radius around Position is drawn (lakes)

		float GetSize() const { return m_Size; }
		const Mesh& GetMesh() const;   // built on first use, so the CPU queries work without an OpenGL context

		// Vertical displacement of the waves at a world xz and time (same waves as the shader).
		float HeightAt(float x, float z, float time) const;
		float SurfaceY(float x, float z, float time) const { return Position.y + HeightAt(x, z, time); }

	private:
		float m_Size;
		uint32_t m_Subdivisions = 0;
		std::vector<Vertex> m_RiverVertices;
		std::vector<uint32_t> m_RiverIndices;
		mutable std::unique_ptr<Mesh> m_Mesh;
	};

	// The shared wave set, so CPU queries match the GPU (4 Gerstner waves around the main direction).
	struct WaveSet
	{
		static constexpr int Count = 4;
		static constexpr float Angle[Count] = { 0.0f, 0.42f, -0.58f, 0.95f };
		static constexpr float Length[Count] = { 1.0f, 0.62f, 0.38f, 0.24f };
		static constexpr float Amp[Count] = { 1.0f, 0.55f, 0.30f, 0.16f };
	};
}
