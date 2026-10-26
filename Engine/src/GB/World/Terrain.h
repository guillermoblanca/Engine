#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	// Regular grid of heights covering a square of the XZ plane, centered on `Center`.
	class Heightmap
	{
	public:
		Heightmap(uint32_t resolution, float size, const glm::vec2& center = { 0.0f, 0.0f });

		uint32_t GetResolution() const { return m_Resolution; }   // samples per side
		float GetSize() const { return m_Size; }                  // meters per side
		float GetSpacing() const { return m_Size / static_cast<float>(m_Resolution - 1); }
		glm::vec2 GetMin() const { return m_Center - m_Size * 0.5f; }
		glm::vec2 GetMax() const { return m_Center + m_Size * 0.5f; }
		bool Contains(float x, float z) const;

		float& At(uint32_t i, uint32_t j) { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		float At(uint32_t i, uint32_t j) const { return m_Heights[static_cast<size_t>(j) * m_Resolution + i]; }
		glm::vec2 SamplePosition(uint32_t i, uint32_t j) const;   // world x,z of a grid point

		// Bilinear height at a world position; clamps to the border outside the map.
		float GetHeight(float x, float z) const;
		// Smooth surface normal (central differences) at a world position.
		glm::vec3 GetNormal(float x, float z) const;
		float GetMinHeight() const;
		float GetMaxHeight() const;

	private:
		uint32_t m_Resolution;
		float m_Size;
		glm::vec2 m_Center;
		std::vector<float> m_Heights;
	};

	enum class TerrainStyle { Desert, Snow, Meadow };

	struct DuneParams
	{
		uint32_t Seed = 1;
		float WindAngleDegrees = 30.0f;   // direction the wind blows toward, measured from +X toward +Z
		float Wavelength = 45.0f;         // meters between crests along the wind
		float Height = 6.0f;              // typical crest height above the trough
		float CrestPosition = 0.78f;      // fraction of the wavelength taken by the gentle windward slope
		float CrestSharpness = 0.55f;     // 0 = rounded cosine crest, 1 = knife edge
		float Warp = 0.5f;                // how much the crest lines meander (in wavelengths)
		float WarpScale = 110.0f;         // meters; size of the meanders
		float SizeVariation = 0.45f;      // some dunes are taller than others
		float BaseHills = 2.5f;           // meters of gentle large-scale undulation underneath
	};

	struct HillParams
	{
		uint32_t Seed = 1;
		float Height = 14.0f;
		float Scale = 140.0f;             // meters of the largest features
		int Octaves = 5;
		float Flatten = 0.25f;            // pushes valleys flatter (0 = none)
	};

	// A drawable heightfield with a matching material (grain, wind ripples, slope-based coloring).
	class Terrain
	{
	public:
		static std::unique_ptr<Terrain> CreateDunes(const DuneParams& dunes = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Desert);
		static std::unique_ptr<Terrain> CreateHills(const HillParams& hills = {}, float size = 256.0f, uint32_t resolution = 513,
			TerrainStyle style = TerrainStyle::Meadow);

		const Heightmap& GetHeightmap() const { return m_Heightmap; }
		float GetHeight(float x, float z) const { return m_Heightmap.GetHeight(x, z); }
		const Material& GetMaterial() const { return m_Material; }
		Material& GetMaterial() { return m_Material; }
		const Mesh& GetMesh() const { return *m_Mesh; }
		TerrainStyle GetStyle() const { return m_Style; }
		float GetWindAngle() const { return m_WindAngle; }   // radians, dune terrains only

		// A function usable as ParticleEmitterDesc::GroundHeightFn. The Terrain must outlive it.
		std::function<float(float, float)> MakeGroundFunction() const;

		// Points along the dune crests (local maxima across the wind), for effects such as blowing sand.
		const std::vector<glm::vec3>& GetCrestPoints() const { return m_Crests; }

		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

	private:
		Terrain(uint32_t resolution, float size) : m_Heightmap(resolution, size) {}
		void Finish(TerrainStyle style, float wavelengthMeters);

		Heightmap m_Heightmap;
		TerrainStyle m_Style = TerrainStyle::Desert;
		float m_WindAngle = 0.0f;
		std::unique_ptr<Mesh> m_Mesh;
		std::unique_ptr<Texture2D> m_Grain, m_Ripples, m_Macro;
		Material m_Material;
		std::vector<glm::vec3> m_Crests;
	};
}
