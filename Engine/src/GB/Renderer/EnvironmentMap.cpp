#include "EnvironmentMap.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <stb_image.h>

namespace GB
{
	std::unique_ptr<EnvironmentMap> EnvironmentMap::Load(const std::string& path)
	{
		int w = 0, h = 0, n = 0;
		float* data = stbi_loadf(path.c_str(), &w, &h, &n, 3);
		if (!data)
		{
			GB_CORE_ERROR("Could not load environment map '{}': {}", path, stbi_failure_reason());
			return nullptr;
		}
		std::vector<float> rgb(data, data + static_cast<size_t>(w) * h * 3);
		stbi_image_free(data);
		return FromPixels(static_cast<uint32_t>(w), static_cast<uint32_t>(h), rgb);
	}

	std::unique_ptr<EnvironmentMap> EnvironmentMap::FromPixels(uint32_t width, uint32_t height, const std::vector<float>& rgb)
	{
		if (width < 2 || height < 2 || rgb.size() != static_cast<size_t>(width) * height * 3)
		{
			GB_CORE_ERROR("EnvironmentMap: invalid pixel data");
			return nullptr;
		}
		std::unique_ptr<EnvironmentMap> map(new EnvironmentMap());
		map->m_Width = width;
		map->m_Height = height;
		map->m_Levels = 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));
		glCreateTextures(GL_TEXTURE_2D, 1, &map->m_Texture);
		glTextureStorage2D(map->m_Texture, static_cast<GLsizei>(map->m_Levels), GL_RGB16F, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
		glTextureSubImage2D(map->m_Texture, 0, 0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGB, GL_FLOAT, rgb.data());
		glGenerateTextureMipmap(map->m_Texture);
		glTextureParameteri(map->m_Texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(map->m_Texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(map->m_Texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(map->m_Texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return map;
	}

	EnvironmentMap::~EnvironmentMap()
	{
		glDeleteTextures(1, &m_Texture);
	}
}
