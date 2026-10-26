// CPU-only tests for Image loading/cropping, including the shipped texture atlases.
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int main()
{
	GB::Log::Init();

	GB::Image solid = GB::Image::Solid(4, 2, 10, 20, 30);
	Check(solid.IsValid() && solid.Pixels.size() == 4 * 2 * 4, "solid image has RGBA8 storage");
	GB::Image crop = solid.Crop(1, 1, 10, 10);   // clamped to the image
	Check(crop.Width == 3 && crop.Height == 1 && crop.Pixels[0] == 10 && crop.Pixels[2] == 30, "crop clamps and copies");
	Check(!solid.Crop(9, 0, 1, 1).IsValid(), "crop outside the image is invalid");

	const std::string dir = std::string(GB_ASSET_DIR) + "/textures/";
	for (const char* name : { "atlas_albedo.jpg", "atlas_normal.jpg", "atlas_height.jpg" })
	{
		GB::Image atlas = GB::Image::Load(dir + name);
		Check(atlas.IsValid() && atlas.Width == 512 && atlas.Height == 384, "atlas loads as 512x384");
	}
	Check(!GB::Image::Load(dir + "does_not_exist.png").IsValid(), "missing file reports an invalid image");
	return failures;
}
