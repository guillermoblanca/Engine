#pragma once
#include "Image.h"
#include <cstdint>

namespace GB
{
	enum class TextureEncoding
	{
		SRGB,     // color data (albedo): decoded to linear by the sampler
		Linear    // data textures (normal, height, roughness)
	};

	// 2D RGBA8 texture with mipmaps, repeat wrapping and anisotropic filtering.
	class Texture2D
	{
	public:
		Texture2D(const Image& image, TextureEncoding encoding);
		~Texture2D();
		Texture2D(const Texture2D&) = delete;
		Texture2D& operator=(const Texture2D&) = delete;

		bool IsValid() const { return m_Texture != 0; }
		uint32_t GetID() const { return m_Texture; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }
		void Bind(uint32_t unit) const;

	private:
		uint32_t m_Texture = 0;
		uint32_t m_Width = 0, m_Height = 0;
	};
}
