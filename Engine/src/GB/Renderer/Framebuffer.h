#pragma once
#include <cstdint>
#include <vector>

namespace GB
{
	// RGBA8 color + 24-bit depth offscreen target.
	class Framebuffer
	{
	public:
		Framebuffer(uint32_t width, uint32_t height);
		~Framebuffer();
		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;

		bool IsComplete() const { return m_Complete; }
		void Bind() const;   // also sets the viewport
		static void BindDefault(uint32_t viewportWidth, uint32_t viewportHeight);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		// Top-left origin RGBA8, width*height*4 bytes.
		std::vector<uint8_t> ReadPixels() const;
		// Convenience: ReadPixels + PNG on disk. Returns false if the write failed.
		bool SavePNG(const char* path) const;

	private:
		uint32_t m_FBO = 0, m_Color = 0, m_Depth = 0;
		uint32_t m_Width, m_Height;
		bool m_Complete = false;
	};

	// Writes RGBA8 pixels (top-left origin) to a PNG file.
	bool SavePNG(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba);
}

namespace GB
{
	// Saves the currently bound default framebuffer (back buffer) as PNG.
	bool SaveBackbufferPNG(const char* path, uint32_t width, uint32_t height);
}
