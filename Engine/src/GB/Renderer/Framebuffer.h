#pragma once
#include <cstdint>
#include <vector>

namespace GB
{
	enum class ColorFormat { None, RGBA8, RGBA16F };

	struct FramebufferSpec
	{
		uint32_t Width = 1;
		uint32_t Height = 1;
		ColorFormat Format = ColorFormat::RGBA8;
		uint32_t Samples = 1;          // >1 = multisampled (resolve with Framebuffer::Blit)
		bool Depth = true;             // 24-bit depth
		bool DepthAsTexture = false;   // depth stored in a texture instead of a renderbuffer
		bool DepthCompare = true;      // with DepthAsTexture: comparison sampler (shadow maps); false = plain sampler2D
	};

	// Offscreen render target.
	class Framebuffer
	{
	public:
		explicit Framebuffer(const FramebufferSpec& spec);
		// RGBA8 + depth shortcut.
		Framebuffer(uint32_t width, uint32_t height);
		~Framebuffer();
		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;

		bool IsComplete() const { return m_Complete; }
		void Bind() const;   // also sets the viewport
		static void BindDefault(uint32_t viewportWidth, uint32_t viewportHeight);

		uint32_t GetWidth() const { return m_Spec.Width; }
		uint32_t GetHeight() const { return m_Spec.Height; }
		uint32_t GetID() const { return m_FBO; }
		uint32_t GetColorTexture() const { return m_Color; }
		uint32_t GetDepthTexture() const { return m_DepthTexture; }

		// Copies the color buffer (e.g. resolves MSAA into a single-sample target).
		static void BlitColor(const Framebuffer& src, const Framebuffer& dst);
		// Copies the depth buffer (e.g. resolves an MSAA depth buffer). Both need a 24-bit depth attachment.
		static void BlitDepth(const Framebuffer& src, const Framebuffer& dst);

		// Single-sample RGBA8 only. Top-left origin, width*height*4 bytes.
		std::vector<uint8_t> ReadPixels() const;
		// ReadPixels + PNG on disk. Returns false if the write failed.
		bool SavePNG(const char* path) const;

	private:
		void Create();
		FramebufferSpec m_Spec;
		uint32_t m_FBO = 0, m_Color = 0, m_Depth = 0, m_DepthTexture = 0;
		bool m_Complete = false;
	};

	// Writes RGBA8 pixels (top-left origin) to a PNG file.
	bool SavePNG(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba);
	// Saves the currently bound default framebuffer (back buffer) as PNG.
	bool SaveBackbufferPNG(const char* path, uint32_t width, uint32_t height);
}
