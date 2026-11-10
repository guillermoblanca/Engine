#version 450 core
struct MaterialData
{
	vec4 BaseColor;     // rgb albedo, a opacity
	vec4 Emissive;      // rgb emissive, a alpha cutoff
	vec4 Surface;       // x metallic, y roughness, z ao, w occlusion strength
	vec4 Uv;            // x uv scale, y normal strength, zw normal map sign per axis
	uvec4 Tex;          // x TEX_* bits: the maps bound
	uvec4 Flags;        // x LAYER_* bits, y shading model, z alpha mode (0 opaque, 1 mask, 2 blend), w FLAG_* bits
	vec4 Wind;          // x sway, y height of full sway, z flutter
	vec4 ModelParams;   // standard: x height (displacement) scale, y cavity strength, z index of refraction, w transmission (LAYER_TRANSMISSION)
	vec4 Layer[13];     // the layers' slots (SLOT_*)
};
layout(std430, binding = 3) readonly buffer MaterialBuffer { MaterialData uMaterials[]; };
uniform int uMaterialId;
#define MATERIAL uMaterials[uMaterialId]
const uint TEX_ALBEDO = 1u;
const uint TEX_NORMAL = 2u;
const uint TEX_HEIGHT_MAP = 4u;
const uint TEX_HEIGHT = 8u;
const uint TEX_MR = 16u;
const uint TEX_OCCLUSION = 32u;
const uint TEX_EMISSIVE = 64u;
const uint TEX_MACRO = 128u;
const uint TEX_TERRAIN_NORMALS = 256u;
const uint TEX_INTERIOR = 512u;
const uint FLAG_FOLIAGE_NORMALS = 1u;
const uint FLAG_ALPHA_TO_COVERAGE = 2u;
const uint FLAG_DOUBLE_SIDED = 4u;
const uint LAYER_GRAIN = 1u;
const uint LAYER_MOSS = 2u;
const uint LAYER_SAND = 4u;
const uint LAYER_WATER_LINE = 8u;
const uint LAYER_TRAIL = 16u;
const uint LAYER_MACRO = 32u;
const uint LAYER_TRANSMISSION = 64u;
const uint LAYER_FOLIAGE = 128u;
const uint SLOT_GRAIN = 0u;        // x amount, y scale
const uint SLOT_MOSS = 1u;         // rgb color, w amount
const uint SLOT_SAND = 2u;         // rgb color, w amount
const uint SLOT_WATER_LINE = 3u;   // x water line (-1e9: none), y the sand's base height
const uint SLOT_TRAIL = 4u;        // x depth (0: none), y normal boost
const uint SLOT_TRAIL_TINT = 5u;   // rgb tint
const uint SLOT_MACRO = 6u;        // the macro map's rect
const uint SLOT_TRANSMISSION = 7u; // rgb absorption (1/m), w thickness (m)
const uint SLOT_INTERIOR = 8u;     // rgb interior tint, w strength
const uint SLOT_TRANSMISSION_EXTRA = 9u;   // x interior depth (uv), y scatter, z shadow density, w thickness variation
const uint SLOT_FOLIAGE = 10u;     // rgb transmission tint, w translucency
const uint SLOT_FOLIAGE_EXTRA = 11u;   // x colour variation, y normal blend, z macro variation, w alpha mip scale
const uint SLOT_FOLIAGE_SHADE = 12u;   // x base occlusion, y occlusion height (m), z interior occlusion, w back-light spread
const uint LAYER_SLOTS = 13u;
bool HasTex(uint bit) { return (MATERIAL.Tex.x & bit) != 0u; }
bool HasFlag(uint bit) { return (MATERIAL.Flags.w & bit) != 0u; }
in vec2 vUV;
layout(binding = 1) uniform sampler2D uAlbedoMap;
void main()
{
	if (MATERIAL.Flags.z == 1u && HasTex(TEX_ALBEDO))
	{
		float alpha = texture(uAlbedoMap, vUV).a * MATERIAL.BaseColor.a;
		// T46: foliage with an alpha mip scale: the alpha raised by the mip level, as in PBRFS, so thin cards keep their shadow far away
		if ((MATERIAL.Flags.x & LAYER_FOLIAGE) != 0u)
			alpha *= 1.0 + max(textureQueryLod(uAlbedoMap, vUV).x, 0.0) * MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].w;
		if (alpha < MATERIAL.Emissive.a)
			discard;
	}
	// T101: a transmissive caster (only with a shadow density: RenderList) covers that fraction of the shadow map's texels in a 4x4 ordered
	// dither; the 5x5 PCF of the lookup averages it into a faint, even shadow.
	if (MATERIAL.Flags.z == 2u)
	{
		const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
		ivec2 p = ivec2(gl_FragCoord.xy) & 3;
		if ((bayer[p.y * 4 + p.x] + 0.5) / 16.0 >= MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA].z)
			discard;
	}
}
