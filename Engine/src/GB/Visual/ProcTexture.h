#pragma once
#include "GB/Renderer/Image.h"
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	// Procedural textures generated on the CPU at load (no files, no GL): bark, stone, flint, copper ore, thatch, worked planks, tilled soil, hide,
	// leaves and the rest the visual descriptions name (VisualMaterial::Texture). Every one tiles (the noise is periodic over the image) and is
	// deterministic: the same kind, size and seed give the same pixels on every run and machine (integer hashing, no std::rand). The colour is
	// sRGB (an albedo map), meant to be multiplied by the material's colour, so they are mostly light and neutral in hue where a tint is expected.
	// `size` is a power of two from 16 to 1024; `quality minimum` asks for a quarter of the normal size (ProcTextureSize).
	namespace ProcTexture
	{
		// The kinds this build can make, in a fixed order.
		const std::vector<std::string>& Kinds();
		bool Known(const std::string& kind);
		// The texture; an unknown kind gives a flat mid-grey image (so a newer content file still draws).
		Image Generate(const std::string& kind, uint32_t size, uint32_t seed = 0);
		// The size to make a texture at for a quality: 256 normally, 64 with `lowRes` (the editor's `quality minimum`).
		inline uint32_t Size(bool lowRes) { return lowRes ? 64u : 256u; }
		// A 64-bit hash of the pixels (the determinism check).
		uint64_t Hash(const Image& image);
	}
}
