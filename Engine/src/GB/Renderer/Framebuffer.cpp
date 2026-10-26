#include "Framebuffer.h"
#include "GB/Log.h"
#include <glad/gl.h>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_FALLBACK
#include <stb_image_write.h>

namespace GB
{
	Framebuffer::Framebuffer(const FramebufferSpec& spec) : m_Spec(spec) { Create(); }

	Framebuffer::Framebuffer(uint32_t width, uint32_t height)
	{
		m_Spec.Width = width;
		m_Spec.Height = height;
		Create();
	}

	void Framebuffer::Create()
	{
		const GLsizei w = static_cast<GLsizei>(m_Spec.Width);
		const GLsizei h = static_cast<GLsizei>(m_Spec.Height);
		const bool multisampled = m_Spec.Samples > 1;
		const GLsizei samples = static_cast<GLsizei>(m_Spec.Samples);

		glCreateFramebuffers(1, &m_FBO);

		if (m_Spec.Format != ColorFormat::None)
		{
			const GLenum internal = m_Spec.Format == ColorFormat::RGBA16F ? GL_RGBA16F : GL_RGBA8;
			glCreateTextures(multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, 1, &m_Color);
			if (multisampled)
				glTextureStorage2DMultisample(m_Color, samples, internal, w, h, GL_TRUE);
			else
			{
				glTextureStorage2D(m_Color, 1, internal, w, h);
				glTextureParameteri(m_Color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTextureParameteri(m_Color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTextureParameteri(m_Color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTextureParameteri(m_Color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			glNamedFramebufferTexture(m_FBO, GL_COLOR_ATTACHMENT0, m_Color, 0);
		}
		else
		{
			glNamedFramebufferDrawBuffer(m_FBO, GL_NONE);
			glNamedFramebufferReadBuffer(m_FBO, GL_NONE);
		}

		if (m_Spec.Depth)
		{
			if (m_Spec.DepthAsTexture)
			{
				glCreateTextures(GL_TEXTURE_2D, 1, &m_DepthTexture);
				glTextureStorage2D(m_DepthTexture, 1, GL_DEPTH_COMPONENT24, w, h);
				glTextureParameteri(m_DepthTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTextureParameteri(m_DepthTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTextureParameteri(m_DepthTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
				glTextureParameteri(m_DepthTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
				const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
				glTextureParameterfv(m_DepthTexture, GL_TEXTURE_BORDER_COLOR, border);
				if (m_Spec.DepthCompare)
				{
					glTextureParameteri(m_DepthTexture, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
					glTextureParameteri(m_DepthTexture, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
				}
				else
				{
					glTextureParameteri(m_DepthTexture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTextureParameteri(m_DepthTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				}
				glNamedFramebufferTexture(m_FBO, GL_DEPTH_ATTACHMENT, m_DepthTexture, 0);
			}
			else
			{
				glCreateRenderbuffers(1, &m_Depth);
				if (multisampled)
					glNamedRenderbufferStorageMultisample(m_Depth, samples, GL_DEPTH_COMPONENT24, w, h);
				else
					glNamedRenderbufferStorage(m_Depth, GL_DEPTH_COMPONENT24, w, h);
				glNamedFramebufferRenderbuffer(m_FBO, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_Depth);
			}
		}

		m_Complete = glCheckNamedFramebufferStatus(m_FBO, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
		if (!m_Complete)
			GB_CORE_ERROR("Framebuffer {}x{} is incomplete", m_Spec.Width, m_Spec.Height);
	}

	Framebuffer::~Framebuffer()
	{
		glDeleteFramebuffers(1, &m_FBO);
		glDeleteTextures(1, &m_Color);
		glDeleteTextures(1, &m_DepthTexture);
		glDeleteRenderbuffers(1, &m_Depth);
	}

	void Framebuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
		glViewport(0, 0, static_cast<GLsizei>(m_Spec.Width), static_cast<GLsizei>(m_Spec.Height));
	}

	void Framebuffer::BlitColor(const Framebuffer& src, const Framebuffer& dst)
	{
		glBlitNamedFramebuffer(src.m_FBO, dst.m_FBO,
			0, 0, static_cast<GLint>(src.m_Spec.Width), static_cast<GLint>(src.m_Spec.Height),
			0, 0, static_cast<GLint>(dst.m_Spec.Width), static_cast<GLint>(dst.m_Spec.Height),
			GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}

	void Framebuffer::BindDefault(uint32_t w, uint32_t h)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
	}

	void Framebuffer::BlitDepth(const Framebuffer& src, const Framebuffer& dst)
	{
		glBlitNamedFramebuffer(src.m_FBO, dst.m_FBO,
			0, 0, static_cast<GLint>(src.m_Spec.Width), static_cast<GLint>(src.m_Spec.Height),
			0, 0, static_cast<GLint>(dst.m_Spec.Width), static_cast<GLint>(dst.m_Spec.Height),
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}

	std::vector<uint8_t> Framebuffer::ReadPixels() const
	{
		const size_t stride = static_cast<size_t>(m_Spec.Width) * 4;
		std::vector<uint8_t> bottomUp(stride * m_Spec.Height);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, static_cast<GLsizei>(m_Spec.Width), static_cast<GLsizei>(m_Spec.Height),
			GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());

		std::vector<uint8_t> topDown(bottomUp.size());
		for (uint32_t y = 0; y < m_Spec.Height; ++y)
			std::memcpy(&topDown[y * stride], &bottomUp[(m_Spec.Height - 1 - y) * stride], stride);
		return topDown;
	}

	bool Framebuffer::SavePNG(const char* path) const
	{
		return GB::SavePNG(path, m_Spec.Width, m_Spec.Height, ReadPixels().data());
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
