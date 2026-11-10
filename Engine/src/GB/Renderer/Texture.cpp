#include "Texture.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>

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

		RHI::Device& device = RHI::GetDevice();
		RHI::TextureDesc desc;
		desc.Width = m_Width;
		desc.Height = m_Height;
		desc.Levels = 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(m_Width, m_Height)))));
		desc.Fmt = encoding == TextureEncoding::SRGB ? RHI::Format::SRGB8A8 : RHI::Format::RGBA8;
		m_Texture = device.CreateTexture(desc);
		device.UploadTexture(m_Texture, 0, 0, m_Width, m_Height, RHI::PixelData::RGBA8, image.Pixels.data());
		device.GenerateMips(m_Texture);

		RHI::SamplerDesc sampler;
		sampler.Min = RHI::Filter::LinearMipLinear;
		sampler.Mag = RHI::Filter::Linear;
		sampler.WrapS = sampler.WrapT = RHI::Wrap::Repeat;
		sampler.MaxAnisotropy = 16.0f;
		device.SetSampler(m_Texture, sampler);
	}

	Texture2D::~Texture2D()
	{
		RHI::GetDevice().DestroyTexture(m_Texture);
	}

	void Texture2D::Update(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint8_t* rgba)
	{
		if (!m_Texture || x + w > m_Width || y + h > m_Height) return;
		RHI::Device& device = RHI::GetDevice();
		device.UploadTexture(m_Texture, x, y, w, h, RHI::PixelData::RGBA8, rgba);
		device.GenerateMips(m_Texture);
	}

	void Texture2D::Bind(uint32_t unit) const
	{
		RHI::GetDevice().Commands().BindTexture(unit, m_Texture);
	}
}
