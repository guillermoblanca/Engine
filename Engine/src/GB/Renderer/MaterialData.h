#pragma once

// The per-material block (render M4, T36; DESACOPLE_MATERIALES §2.3): what the PBR and shadow programs read about the surface they draw
// (base color, metal and roughness, emissive, alpha, the maps it has, wind, displacement and the layers' parameters). One entry per distinct
// material in a storage buffer at Binding, indexed by the draw's `uMaterialId` (MaterialTable gives the ids; MaterialBuffer uploads them).
// Before, each draw set these as ~40 loose uniforms.
//
// The C++ mirror of `struct MaterialData` in `lib/material` (ShaderLib/Pieces.h): std430, the same members in the same order, vec4 and uvec4
// only, so there is no padding and the struct can be hashed and compared as bytes. The static_asserts pin every offset; the test
// material_block compares them with what the driver reports. The bits and slot numbers below are written out as GLSL constants in
// `lib/material`; material_block also checks that text. CPU only: no GL here.
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace GB
{
	struct MaterialData
	{
		static constexpr uint32_t Binding = 3;      // layout(std430, binding = 3) readonly buffer MaterialBuffer
		static constexpr uint32_t LayerSlots = 8;   // Layer[]: fixed slots, one or two per layer (below)

		// Tex.x: the loose maps the draw binds (until M6 puts them in arrays and Tex.yzw holds their layers)
		static constexpr uint32_t TexAlbedo = 1u << 0;           // unit 1
		static constexpr uint32_t TexNormal = 1u << 1;           // unit 2
		static constexpr uint32_t TexHeightMap = 1u << 2;        // unit 3: the cavity term of the fragment shader
		static constexpr uint32_t TexHeight = 1u << 3;           // ... and the displacement of the vertex shaders (a map and a scale != 0)
		static constexpr uint32_t TexMR = 1u << 4;               // unit 4
		static constexpr uint32_t TexOcclusion = 1u << 5;        // unit 5
		static constexpr uint32_t TexEmissive = 1u << 6;         // unit 6
		static constexpr uint32_t TexMacro = 1u << 7;            // unit 7
		static constexpr uint32_t TexTerrainNormals = 1u << 8;   // unit 10 (only with a macro map)

		// Flags.w
		static constexpr uint32_t FlagFoliageNormals = 1u << 0;   // the cards keep the crown normal on both faces (no flip on back faces)
		static constexpr uint32_t FlagAlphaToCoverage = 1u << 1;  // masked alpha leaves as coverage (a mask on a multisampled target)
		static constexpr uint32_t FlagDoubleSided = 1u << 2;      // drawn without culling (the cull is set per draw from it)

		// Flags.x: the layers the material has on (informative until M7 branches on it; the shader still tests the values, as before)
		static constexpr uint32_t LayerGrain = 1u << 0;
		static constexpr uint32_t LayerMoss = 1u << 1;
		static constexpr uint32_t LayerSand = 1u << 2;
		static constexpr uint32_t LayerWaterLine = 1u << 3;
		static constexpr uint32_t LayerTrail = 1u << 4;
		static constexpr uint32_t LayerMacro = 1u << 5;

		// Layer[] slots
		static constexpr uint32_t SlotGrain = 0;       // x amount, y scale (noise frequency per metre)
		static constexpr uint32_t SlotMoss = 1;        // rgb color, w amount
		static constexpr uint32_t SlotSand = 2;        // rgb color, w amount
		static constexpr uint32_t SlotWaterLine = 3;   // x water line (-1e9: none), y the sand's base height
		static constexpr uint32_t SlotTrail = 4;       // x depth (0: no ground marks, or no ground), y normal boost
		static constexpr uint32_t SlotTrailTint = 5;   // rgb tint of the marked ground
		static constexpr uint32_t SlotMacro = 6;       // the macro map's rect: x, z of its corner, width, depth
		// slot 7: free

		glm::vec4 BaseColor{ 0.0f };     // rgb albedo (linear), a opacity
		glm::vec4 Emissive{ 0.0f };      // rgb emissive, a alpha cutoff
		glm::vec4 Surface{ 0.0f };       // x metallic, y roughness, z ao, w occlusion strength
		glm::vec4 Uv{ 0.0f };            // x uv scale, y normal strength, zw normal map sign per axis (+-1)
		glm::uvec4 Tex{ 0u };            // x Tex* bits; yzw: M6
		glm::uvec4 Flags{ 0u };          // x Layer* bits, y shading model (0: standard), z alpha mode (0 opaque, 1 mask, 2 blend), w Flag* bits
		glm::vec4 Wind{ 0.0f };          // x sway, y height of full sway, z flutter, w unused
		glm::vec4 ModelParams{ 0.0f };   // standard: x height (displacement) scale, y cavity strength
		glm::vec4 Layer[LayerSlots] = {};
	};

	static_assert(sizeof(glm::vec4) == 16 && sizeof(glm::uvec4) == 16, "MaterialData needs tightly packed glm types");
	static_assert(offsetof(MaterialData, BaseColor) == 0);
	static_assert(offsetof(MaterialData, Emissive) == 16);
	static_assert(offsetof(MaterialData, Surface) == 32);
	static_assert(offsetof(MaterialData, Uv) == 48);
	static_assert(offsetof(MaterialData, Tex) == 64);
	static_assert(offsetof(MaterialData, Flags) == 80);
	static_assert(offsetof(MaterialData, Wind) == 96);
	static_assert(offsetof(MaterialData, ModelParams) == 112);
	static_assert(offsetof(MaterialData, Layer) == 128);
	static_assert(sizeof(MaterialData) == 256, "8 x 16 + 8 slots x 16 B (std430, array stride 256)");
}
