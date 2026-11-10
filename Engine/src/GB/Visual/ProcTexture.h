#pragma once
#include "GB/Scene/Image.h"
#include <cstdint>
#include <string>
#include <vector>

namespace GB
{
	// Procedural textures generated on the CPU at load (no files, no GL): bark, stone, flint, copper ore, thatch, worked planks, tilled soil, hide,
	// leaves, (T91) sand, brick, cut stone, layered rock, wood grain, ice and frosted glass, and (T102) faceted ice and wet glass: the ones the visual descriptions name
	// (VisualMaterial::Texture). Every one tiles (the noise is periodic over the image) and is
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
		// T91: the relief and the shine of the same pattern, as data maps (upload them Linear). The kinds of T91 (sand, brick, brick_worn,
		// ashlar, rock, wood, ice, ice_frost, glass_frosted) and of T102 (ice_faceted: chiselled facets; glass_wet: drops and rivulets, which
		// run toward -v, down the side of a ProcMesh box or ball) are written as a surface (albedo, height, roughness) and give their own relief;
		// the older kinds take it from the albedo's luminance and have a flat roughness. An unknown kind gives a flat map.
		// A tangent-space normal map: +X along u, +Y along v (the bitangent of ProcMesh's UVs), Z out. The relief is in the texture's own
		// scale (a brick's mortar sinks about a centimetre at UVScale 1), so a smaller size gives the same slopes, only blurrier.
		Image GenerateNormal(const std::string& kind, uint32_t size, uint32_t seed = 0);
		// A glTF metallic-roughness map: G is the roughness factor (multiplies the material's roughness, which is then the maximum); R and B
		// are 255 (no occlusion here, the metalness is the material's).
		Image GenerateRoughness(const std::string& kind, uint32_t size, uint32_t seed = 0);
		// T102: the inside of clear ice, for a parallax interior layer: RGBA, rgb the colour of what is in the ice (whitish fracture planes,
		// bright bubbles, milky veils) and a its density, straight (not premultiplied); 0 is clean ice. Upload it SRGB (the alpha stays linear).
		// Its fracture planes are on other scales than the surface's facets (3 and 8 cells a tile), so they do not line up with them. Made for
		// `ice` and `ice_faceted`; any other kind gives a transparent size x size image (all zero), so a caller binds it without a special case.
		Image GenerateInterior(const std::string& kind, uint32_t size, uint32_t seed = 0);
		// The size to make a texture at for a quality: 256 normally, 64 with `lowRes` (the editor's `quality minimum`).
		inline uint32_t Size(bool lowRes) { return lowRes ? 64u : 256u; }
		// A 64-bit hash of the pixels (the determinism check).
		uint64_t Hash(const Image& image);
	}
}
