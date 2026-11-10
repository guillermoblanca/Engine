#include "EnvironmentMap.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
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
		RHI::Device& device = RHI::GetDevice();
		RHI::TextureDesc desc;
		desc.Width = width;
		desc.Height = height;
		desc.Levels = map->m_Levels;
		desc.Fmt = RHI::Format::RGB16F;
		const RHI::TextureHandle texture = device.CreateTexture(desc);
		map->m_Texture = texture.Id;
		device.UploadTexture(texture, 0, 0, width, height, RHI::PixelData::RGBFloat, rgb.data());
		device.GenerateMips(texture);
		RHI::SamplerDesc sampler;
		sampler.Min = RHI::Filter::LinearMipLinear;
		sampler.Mag = RHI::Filter::Linear;
		sampler.WrapS = RHI::Wrap::Repeat;
		sampler.WrapT = RHI::Wrap::ClampToEdge;
		device.SetSampler(texture, sampler);
		return map;
	}

	EnvironmentMap::~EnvironmentMap()
	{
		RHI::GetDevice().DestroyTexture({ m_Texture });
	}
}
