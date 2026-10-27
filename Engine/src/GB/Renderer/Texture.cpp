#include "Texture.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>

namespace GB
{
	Texture2D::Texture2D(const Image& image, TextureEncoding encoding)
	{
		if (!image.IsValid())
		{
			GB_CORE_ERROR("Texture2D created from an invalid image");
			return;
		}
		m_Width = image.Width;
		m_Height = image.Height;

		const GLsizei levels = 1 + static_cast<GLsizei>(std::floor(std::log2(static_cast<float>(std::max(m_Width, m_Height)))));
		glCreateTextures(GL_TEXTURE_2D, 1, &m_Texture);
		glTextureStorage2D(m_Texture, levels, encoding == TextureEncoding::SRGB ? GL_SRGB8_ALPHA8 : GL_RGBA8,
			static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height));
		glTextureSubImage2D(m_Texture, 0, 0, 0, static_cast<GLsizei>(m_Width), static_cast<GLsizei>(m_Height),
			GL_RGBA, GL_UNSIGNED_BYTE, image.Pixels.data());
		glGenerateTextureMipmap(m_Texture);

		glTextureParameteri(m_Texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(m_Texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_Texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_Texture, GL_TEXTURE_WRAP_T, GL_REPEAT);

		if (GLAD_GL_EXT_texture_filter_anisotropic)
		{
			GLfloat maxAniso = 1.0f;
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
			glTextureParameterf(m_Texture, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(16.0f, maxAniso));
		}
	}

	Texture2D::~Texture2D()
	{
		glDeleteTextures(1, &m_Texture);
	}

	void Texture2D::Update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t* rgba)
	{
		if (!m_Texture || x + w > m_Width || y + h > m_Height) return;
		glTextureSubImage2D(m_Texture, 0, static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		glGenerateTextureMipmap(m_Texture);
	}

	void Texture2D::Bind(uint32_t unit) const
	{
		glBindTextureUnit(unit, m_Texture);
	}
}
