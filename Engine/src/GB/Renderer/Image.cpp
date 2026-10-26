#include "Image.h"
#include "GB/Log.h"
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace GB
{
	Image Image::Load(const std::string& path, bool flipY)
	{
		stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
		int w = 0, h = 0, channels = 0;
		stbi_uc* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
		if (!data)
		{
			GB_CORE_ERROR("Could not load image '{}': {}", path, stbi_failure_reason());
			return {};
		}
		Image image;
		image.Width = static_cast<uint32_t>(w);
		image.Height = static_cast<uint32_t>(h);
		image.Pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
		stbi_image_free(data);
		return image;
	}

<<<<<<< HEAD
	Image Image::LoadFromMemory(const uint8_t* data, size_t size, bool flipY)
	{
		stbi_set_flip_vertically_on_load(flipY ? 1 : 0);
		int w = 0, h = 0, channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
		if (!pixels)
		{
			GB_CORE_ERROR("Could not decode embedded image: {}", stbi_failure_reason());
			return {};
		}
		Image image;
		image.Width = static_cast<uint32_t>(w);
		image.Height = static_cast<uint32_t>(h);
		image.Pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);
		return image;
	}

=======
>>>>>>> origin/master
	Image Image::Solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		Image image;
		image.Width = width;
		image.Height = height;
		image.Pixels.resize(static_cast<size_t>(width) * height * 4);
		for (size_t i = 0; i < image.Pixels.size(); i += 4)
		{
			image.Pixels[i] = r;
			image.Pixels[i + 1] = g;
			image.Pixels[i + 2] = b;
			image.Pixels[i + 3] = a;
		}
		return image;
	}

	Image Image::Crop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) const
	{
		if (!IsValid() || x >= Width || y >= Height)
			return {};
		w = std::min(w, Width - x);
		h = std::min(h, Height - y);

		Image out;
		out.Width = w;
		out.Height = h;
		out.Pixels.resize(static_cast<size_t>(w) * h * 4);
		for (uint32_t row = 0; row < h; ++row)
			std::memcpy(&out.Pixels[static_cast<size_t>(row) * w * 4],
				&Pixels[(static_cast<size_t>(y + row) * Width + x) * 4], static_cast<size_t>(w) * 4);
		return out;
	}
}
