#include "Framebuffer.h"
#include "GB/Log.h"
#include <glad/gl.h>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FALLBACK
#include <stb_image_write.h>

namespace GB
{
	Framebuffer::Framebuffer(uint32_t width, uint32_t height)
		: m_Width(width), m_Height(height)
	{
		glCreateFramebuffers(1, &m_FBO);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_Color);
		glTextureStorage2D(m_Color, 1, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
		glTextureParameteri(m_Color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_Color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glNamedFramebufferTexture(m_FBO, GL_COLOR_ATTACHMENT0, m_Color, 0);

		glCreateRenderbuffers(1, &m_Depth);
		glNamedRenderbufferStorage(m_Depth, GL_DEPTH_COMPONENT24, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
		glNamedFramebufferRenderbuffer(m_FBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_Depth);

		m_Complete = glCheckNamedFramebufferStatus(m_FBO, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		if (!m_Complete)
			GB_CORE_ERROR("Framebuffer {}x{} is incomplete", width, height);
	}

	Framebuffer::~Framebuffer()
	{
		glDeleteFramebuffers(1, &m_FBO);
		glDeleteTextures(1, &m_Color);
		glDeleteRenderbuffers(1, &m_Depth);
	}

	void Framebuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glViewport(0, 0, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));
	}

	void Framebuffer::BindDefault(uint32_t w, uint32_t h)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	}

	std::vector<uint8_t> Framebuffer::ReadPixels() const
	{
		const size_t stride = static_cast<size_t>(m_Width) * 4;
		std::vector<uint8_t> bottomUp(stride * m_Height);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
			GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());

		std::vector<uint8_t> topDown(bottomUp.size());
		for (uint32_t y = 0; y < m_Height; ++y)
			std::memcpy(&topDown[y * stride], &bottomUp[(m_Height - 1 - y) * stride], stride);
		return topDown;
	}

	bool Framebuffer::SavePNG(const char* path) const
	{
		return GB::SavePNG(path, m_Width, m_Height, ReadPixels().data());
	}

	bool SavePNG(const char* path, uint32_t width, uint32_t height, const uint8_t* rgba)
	{
		const bool ok = stbi_write_png(path, static_cast<int>(width), static_cast<int>(height), 4,
			rgba, static_cast<int>(width) * 4) != 0;
		if (!ok)
			GB_CORE_ERROR("Could not write PNG '{}'", path);
		return ok;
	}

	bool SaveBackbufferPNG(const char* path, uint32_t width, uint32_t height)
	{
		const size_t stride = static_cast<size_t>(width) * 4;
		std::vector<uint8_t> bottomUp(stride * height), topDown(stride * height);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glReadBuffer(GL_BACK);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
			GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
		for (uint32_t y = 0; y < height; ++y)
			std::memcpy(&topDown[y * stride], &bottomUp[(height - 1 - y) * stride], stride);
		return SavePNG(path, width, height, topDown.data());
	}
}
