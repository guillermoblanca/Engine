#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// An HDR equirectangular environment (HDRI): the sky background, diffuse ambient and specular reflections.
	// Stored as RGBA16F with a full mip chain: rough surfaces and the ambient term read blurrier mips.
	class EnvironmentMap
	{
	public:
		// Radiance .hdr (or any format stb_image reads as float). Returns null when the file cannot be read.
		static std::unique_ptr<EnvironmentMap> Load(const std::string& path);
		// Linear RGB floats, width * height * 3, first row = zenith.
		static std::unique_ptr<EnvironmentMap> FromPixels(uint32_t width, uint32_t height, const std::vector<float>& rgb);

		~EnvironmentMap();
		EnvironmentMap(const EnvironmentMap&) = delete;
		EnvironmentMap& operator=(const EnvironmentMap&) = delete;

		bool IsValid() const { return m_Texture != 0; }
		uint32_t GetID() const { return m_Texture; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		uint32_t GetMipCount() const { return m_Levels; }

		float Rotation = 0.0f;     // radians around the vertical axis
		float Exposure = 1.0f;     // multiplies the map

	private:
		EnvironmentMap() = default;
		uint32_t m_Texture = 0;
		uint32_t m_Width = 0, m_Height = 0, m_Levels = 1;
	};
}
