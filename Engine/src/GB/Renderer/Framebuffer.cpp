#include "Framebuffer.h"
#include "GB/Log.h"
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
		const auto format = [](ColorFormat f)
		{
			return f == ColorFormat::RGBA16F ? RHI::Format::RGBA16F : f == ColorFormat::R11G11B10F ? RHI::Format::R11G11B10F
				: f == ColorFormat::RGBA8 ? RHI::Format::RGBA8 : RHI::Format::None;
		};
		RHI::RenderTargetDesc desc;
		desc.Width = m_Spec.Width;
		desc.Height = m_Spec.Height;
		desc.Samples = m_Spec.Samples;
		desc.Color = format(m_Spec.Format);
		desc.Extra = format(m_Spec.Extra);
		desc.Depth = m_Spec.Depth;
		desc.DepthAsTexture = m_Spec.DepthAsTexture;
		desc.DepthCompare = m_Spec.DepthCompare;
		desc.Levels = m_Spec.Levels;
		m_Target = RHI::GetDevice().CreateRenderTarget(desc);
		m_Complete = m_Target.Complete;
	}

	Framebuffer::~Framebuffer()
	{
		RHI::GetDevice().DestroyRenderTarget(m_Target);
	}

	RHI::PassDesc Framebuffer::Pass(const char* name) const
	{
		RHI::PassDesc pass;
		pass.Target = m_Target.Handle;
		pass.Width = m_Spec.Width;
		pass.Height = m_Spec.Height;
		pass.Name = name;
		return pass;
	}

	void Framebuffer::Bind() const
	{
		RHI::GetDevice().Commands().BeginPass(Pass());
	}

	void Framebuffer::BlitColor(const Framebuffer& src, const Framebuffer& dst)
	{
		RHI::GetDevice().Commands().BlitColor(src.m_Target, src.m_Spec.Width, src.m_Spec.Height, dst.m_Target, dst.m_Spec.Width, dst.m_Spec.Height,
			0, RHI::BlitFilter::Linear);
	}

	void Framebuffer::SetExtraEnabled(bool on) const
	{
		if (!m_Target.Extra) return;
		RHI::GetDevice().Commands().SetDrawAttachments(m_Target.Handle, on ? 2 : 1);
	}

	void Framebuffer::BlitExtra(const Framebuffer& src, const Framebuffer& dst)
	{
		if (!src.m_Target.Extra) return;
		RHI::GetDevice().Commands().BlitColor(src.m_Target, src.m_Spec.Width, src.m_Spec.Height, dst.m_Target, dst.m_Spec.Width, dst.m_Spec.Height,
			1, RHI::BlitFilter::Nearest);
	}

	void Framebuffer::BindDefault(uint32_t w, uint32_t h)
	{
		RHI::PassDesc pass;
		pass.Width = w;
		pass.Height = h;
		RHI::GetDevice().Commands().BeginPass(pass);
	}

	void Framebuffer::BlitDepth(const Framebuffer& src, const Framebuffer& dst)
	{
		RHI::GetDevice().Commands().BlitDepth(src.m_Target, src.m_Spec.Width, src.m_Spec.Height, dst.m_Target, dst.m_Spec.Width, dst.m_Spec.Height);
	}

	std::vector<uint8_t> Framebuffer::ReadPixels() const
	{
		const size_t stride = static_cast<size_t>(m_Spec.Width) * 4;
		std::vector<uint8_t> bottomUp(stride * m_Spec.Height);
		RHI::GetDevice().ReadPixels(m_Target.Handle, m_Spec.Width, m_Spec.Height, bottomUp.data());

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
		RHI::GetDevice().ReadPixels({}, width, height, bottomUp.data());
		for (uint32_t y = 0; y < height; ++y)
			std::memcpy(&topDown[y * stride], &bottomUp[(height - 1 - y) * stride], stride);
		return SavePNG(path, width, height, topDown.data());
	}
}
