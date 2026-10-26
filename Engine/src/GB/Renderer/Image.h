#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	// CPU-side RGBA8 image, top-left origin unless loaded with flipY.
	struct Image
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;   // Width * Height * 4

		bool IsValid() const { return Width > 0 && Height > 0 && Pixels.size() == static_cast<size_t>(Width) * Height * 4; }

		// Decodes PNG/JPG/... to RGBA8. flipY makes row 0 the bottom row (OpenGL UV convention).
		static Image Load(const std::string& path, bool flipY = true);
<<<<<<< HEAD
		// Same, from an encoded image in memory (e.g. a texture embedded in a .glb).
		static Image LoadFromMemory(const uint8_t* data, size_t size, bool flipY = true);
=======
>>>>>>> origin/master
		static Image Solid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
		// Copy of the rectangle [x, x+w) x [y, y+h) in the image's own row order. Clamped to the bounds.
		Image Crop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) const;
	};
}
