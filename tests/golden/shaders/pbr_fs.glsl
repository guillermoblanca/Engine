#version 450 core
layout(std140, binding = 0) uniform FrameBlock
{
	mat4 uViewProj;
	mat4 uInvViewProj;
	mat4 uLightVP;
	vec3 uCamPos;      float uEnvIntensity;
	vec3 uSunDir;      float uTime;           // uSunDir: the direction the light travels; uTime: Environment::Time (s)
	vec3 uSunColor;    float uShadowTexel;    // uSunColor: color * intensity; uShadowTexel: world size of one shadow map texel
	vec3 uSkyTop;      float uFogDensity;
	vec3 uSkyHorizon;  float uFogHeight;
	vec3 uSkyGround;   float uWetness;
	vec3 uFogColor;    float uSnowCover;
	vec3 uWind;        float uEnvMaxLod;
	vec3 uCamRight;    float uEnvRotation;    // uCamRight, uCamUp: the camera's axes in world space (particle billboards)
	vec3 uCamUp;       float uEnvExposure;
	vec4 uTrailRect;                          // ground marks window: center x, z, size (m), texel size (m)
	vec2 uNearFar;     float uStars;  float uCloudCover;
	int uShadowsOn;    int uPointCount;  int uOrtho;  int uHasEnvMap;
	vec4 uPointPosRadius[32];                 // xyz position, w radius (32 = Renderer::MaxPointLights)
	vec4 uPointColor[32];                     // rgb color * intensity
};
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


layout(binding = 9) uniform sampler2D uEnvMap;

vec2 EnvUV(vec3 d)
{
	return vec2(atan(d.z, d.x) / 6.2831853 + 0.5 + uEnvRotation, acos(clamp(d.y, -1.0, 1.0)) / 3.1415927);
}

vec3 SkyColor(vec3 d)
{
	if (uHasEnvMap != 0)
		return textureLod(uEnvMap, EnvUV(d), 0.0).rgb * uEnvExposure;
	float t = clamp(d.y, -1.0, 1.0);
	vec3 up = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
	vec3 down = mix(uSkyHorizon, uSkyGround, pow(max(-t, 0.0), 0.35));
	return t >= 0.0 ? up : down;
}

// Cheap prefiltered reflection: rough surfaces see a blurred, averaged sky (a blurrier mip of the HDRI).
vec3 SkyPrefiltered(vec3 d, float roughness)
{
	if (uHasEnvMap != 0)
		return min(textureLod(uEnvMap, EnvUV(d), roughness * (0.35 + 0.65 * roughness) * uEnvMaxLod * 0.8).rgb, vec3(64.0)) * uEnvExposure;
	vec3 avg = (uSkyTop + uSkyHorizon * 2.0 + uSkyGround) * 0.25;
	return mix(SkyColor(d), avg, roughness * roughness);
}

vec3 SkyIrradiance(vec3 n)
{
	if (uHasEnvMap != 0)
		return min(textureLod(uEnvMap, EnvUV(n), max(uEnvMaxLod - 3.0, 0.0)).rgb, vec3(8.0)) * uEnvExposure;
	float h = smoothstep(-0.6, 0.6, n.y);
	vec3 skyHemisphere = mix(uSkyHorizon, uSkyTop, smoothstep(0.0, 1.0, n.y) * 0.8);
	return mix(uSkyGround, skyHemisphere, h);
}
in vec3 vPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
flat in vec3 vOrigin;   // T46 (LAYER_FOLIAGE only)
#ifdef GB_FOLIAGE
flat in mat3 vFoliageInstance;   // T128: the per-instance colour and the macro patch, from the VS (lib/foliage)
in vec3 vFoliagePatch;
#endif
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)

layout(binding = 1) uniform sampler2D uAlbedoMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uHeightMap;   // height doubles as a cavity/AO term (TEX_HEIGHT_MAP)
layout(binding = 4) uniform sampler2D uMRMap;
layout(binding = 5) uniform sampler2D uOcclusionMap;
layout(binding = 6) uniform sampler2D uEmissiveMap;
layout(binding = 7) uniform sampler2D uMacroMap;
layout(binding = 10) uniform sampler2D uTerrainNormals;   // world normals over the macro rect (terrain)
layout(binding = 11) uniform sampler2D uSceneColor;       // T34: the scene before the translucent objects (LAYER_TRANSMISSION only)
layout(binding = 12) uniform sampler2D uSceneDepth;       // T101: the depth of that scene (LAYER_TRANSMISSION only)
layout(binding = 13) uniform sampler2D uInteriorMap;      // T101: what is inside the ice, rgb colour and a density (TEX_INTERIOR)
uniform float uSceneColorLods;                            // T101: the copy's last mip level with this frame's content (0: only level 0)
layout(binding = 8) uniform sampler2D uTrailMap;
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}

layout(binding = 0) uniform sampler2DShadow uShadowMap;

const float PI = 3.14159265359;

float D_GGX(float NoH, float a)
{
	float a2 = a * a;
	float d = NoH * NoH * (a2 - 1.0) + 1.0;
	return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
	float a2 = a * a;
	float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(float VoH, vec3 F0) { return F0 + (1.0 - F0) * pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0); }

// Karis' analytic split-sum environment BRDF.
vec3 EnvBRDFApprox(vec3 F0, float roughness, float NoV)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

vec3 Shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float a, float metallic)
{
	float NoL = max(dot(N, L), 0.0);
	if (NoL <= 0.0) return vec3(0.0);
	vec3 H = normalize(V + L);
	float NoV = max(dot(N, V), 1e-4);
	float NoH = max(dot(N, H), 0.0);
	float VoH = max(dot(V, H), 0.0);
	vec3 F = F_Schlick(VoH, F0);
	vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
	vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;
	return (diffuse + spec) * radiance * NoL;
}

// T110: Shade with the mirrored part scaled by `specScale` (the crowns shaded with a baked crown normal, see main)
vec3 ShadeSpecScaled(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float a, float metallic, float specScale)
{
	float NoL = max(dot(N, L), 0.0);
	if (NoL <= 0.0) return vec3(0.0);
	vec3 H = normalize(V + L);
	float NoV = max(dot(N, V), 1e-4);
	float NoH = max(dot(N, H), 0.0);
	float VoH = max(dot(V, H), 0.0);
	vec3 F = F_Schlick(VoH, F0);
	vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
	vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;
	return (diffuse + spec * specScale) * radiance * NoL;
}

float Shadow(vec3 worldPos, vec3 N, vec3 L)
{
	if (uShadowsOn == 0) return 1.0;
	float NoL = max(dot(N, L), 0.0);
	vec3 p = worldPos + N * uShadowTexel * (1.0 + 2.0 * (1.0 - NoL));
	vec4 lp = uLightVP * vec4(p, 1.0);
	vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
	if (c.z > 1.0 || c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) return 1.0;
	c.z -= 0.0004;
	vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
	float sum = 0.0;
	for (int x = -2; x <= 2; ++x)
		for (int y = -2; y <= 2; ++y)
			sum += texture(uShadowMap, vec3(c.xy + vec2(x, y) * texel, c.z));
	return sum / 25.0;
}
// T128: Shadow with 9 taps two texels apart instead of 25 one apart. The comparison sampler filters 2x2 texels per tap, so the footprint
// is about the same 5x5. For the foliage program (the crowns' overdraw makes the taps most of their cost); the others keep Shadow.
float Shadow9(vec3 worldPos, vec3 N, vec3 L)
{
	if (uShadowsOn == 0) return 1.0;
	float NoL = max(dot(N, L), 0.0);
	vec3 p = worldPos + N * uShadowTexel * (1.0 + 2.0 * (1.0 - NoL));
	vec4 lp = uLightVP * vec4(p, 1.0);
	vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
	if (c.z > 1.0 || c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) return 1.0;
	c.z -= 0.0004;
	vec2 texel = 2.0 / vec2(textureSize(uShadowMap, 0));
	float sum = 0.0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			sum += texture(uShadowMap, vec3(c.xy + vec2(x, y) * texel, c.z));
	return sum / 9.0;
}

float PHash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float PNoise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(PHash(i), PHash(i + vec2(1, 0)), f.x), mix(PHash(i + vec2(0, 1)), PHash(i + vec2(1, 1)), f.x), f.y);
}
float PHash3(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float PNoise3(vec3 p)
{
	vec3 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(PHash3(i), PHash3(i + vec3(1, 0, 0)), f.x), mix(PHash3(i + vec3(0, 1, 0)), PHash3(i + vec3(1, 1, 0)), f.x), f.y),
	           mix(mix(PHash3(i + vec3(0, 0, 1)), PHash3(i + vec3(1, 0, 1)), f.x), mix(PHash3(i + vec3(0, 1, 1)), PHash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
float FBM3(vec3 p) { return PNoise3(p) * 0.55 + PNoise3(p * 2.07 + 3.1) * 0.3 + PNoise3(p * 4.3 + 7.7) * 0.15; }
#ifdef GB_FOLIAGE
float FHash3(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float FNoise2(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(FHash3(vec3(i, 0.0)), FHash3(vec3(i + vec2(1, 0), 0.0)), f.x), mix(FHash3(vec3(i + vec2(0, 1), 0.0)), FHash3(vec3(i + vec2(1, 1), 0.0)), f.x), f.y);
}
vec3 FoliageVary(vec3 c, vec3 origin, vec3 world, float amount, float macro)
{
	if (amount > 0.0)
	{
		float h1 = FHash3(origin * 1.37 + 0.11), h2 = FHash3(origin.zxy * 2.11 + 5.3), h3 = FHash3(origin.yzx * 3.07 + 9.1);
		float a = (0.3 - h1) * amount * 0.55;   // radians around the grey axis: negative turns green toward yellow, positive toward a deeper green
		const vec3 k = vec3(0.57735027);
		c = c * cos(a) + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cos(a));
		float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
		c = mix(vec3(lum), c, 1.0 + (0.4 - h2) * amount * 0.6);   // less saturated (olive) or a little more
		c *= 1.0 + (h3 - 0.5) * amount * 0.7;
	}
	if (macro > 0.0)
	{
		float m = FNoise2(world.xz * 0.07) * 0.65 + FNoise2(world.xz * 0.19 + 7.0) * 0.35;
		vec3 patchTint = mix(vec3(1.16, 1.06, 0.62), vec3(0.78, 0.84, 0.62), smoothstep(0.3, 0.7, m));
		c *= mix(vec3(1.0), patchTint, macro);
	}
	return max(c, vec3(0.0));
}
// T128: FoliageVary in two parts the PBR VS works out, so the FS only multiplies:
// max(FoliagePatchTint(world, macro) * (FoliageInstanceMatrix(origin, amount) * c), 0) == FoliageVary(c, origin, world, amount, macro).
// The hue turn, the saturation and the brightness are linear in the colour and depend only on the origin: one matrix per instance (flat).
// The macro patch depends only on the world position, at the scale of tens of metres: per vertex.
mat3 FoliageInstanceMatrix(vec3 origin, float amount)
{
	if (amount <= 0.0)
		return mat3(1.0);
	float h1 = FHash3(origin * 1.37 + 0.11), h2 = FHash3(origin.zxy * 2.11 + 5.3), h3 = FHash3(origin.yzx * 3.07 + 9.1);
	float a = (0.3 - h1) * amount * 0.55;
	const vec3 k = vec3(0.57735027);
	float ca = cos(a), sa = sin(a);
	mat3 R = ca * mat3(1.0) + sa * mat3(0.0, k.z, -k.y, -k.z, 0.0, k.x, k.y, -k.x, 0.0) + (1.0 - ca) * outerProduct(k, k);   // cross(k, c) as a matrix
	float s = 1.0 + (0.4 - h2) * amount * 0.6;
	mat3 S = s * mat3(1.0) + (1.0 - s) * outerProduct(vec3(1.0), vec3(0.2126, 0.7152, 0.0722));   // mix(vec3(lum), c, s)
	return (1.0 + (h3 - 0.5) * amount * 0.7) * S * R;
}
vec3 FoliagePatchTint(vec3 world, float macro)
{
	if (macro <= 0.0)
		return vec3(1.0);
	float m = FNoise2(world.xz * 0.07) * 0.65 + FNoise2(world.xz * 0.19 + 7.0) * 0.35;
	return mix(vec3(1.0), mix(vec3(1.16, 1.06, 0.62), vec3(0.78, 0.84, 0.62), smoothstep(0.3, 0.7, m)), macro);
}
float FoliageThrough(vec3 N, vec3 V, vec3 L)
{
	return pow(clamp(dot(V, -L), 0.0, 1.0), 4.0) * 3.0 + clamp(dot(-N, L), 0.0, 1.0);
}
#endif
#ifdef GB_TERRAIN_LAYERS
layout(binding = 14) uniform sampler2DArray uSplat;          // the weights by heightmap sample: kind k in channel k % 4 of layer k / 4
layout(binding = 15) uniform sampler2DArray uLayerAlbedo;    // by kind: rgb albedo, a height
layout(binding = 16) uniform sampler2DArray uLayerSurface;   // by kind: rg normal xy, b roughness, a occlusion
uniform vec4 uSplatRect;          // xy the first sample's position, z the spacing, w the resolution
uniform vec4 uLayerParams[13];    // by kind: x 1 / metres per tile, y flags (1 triplanar, 2 broken tiles, 4 no surface map), z edge contrast, w roughness without a surface map
uniform vec4 uLayerMacro[13];     // by kind: rgb the flat colour the macro map paints for it (linear)

struct TerrainSurface { vec3 Albedo; vec3 N; float Roughness; float AO; };

bool TlFlag(int kind, int flag) { return (int(uLayerParams[kind].y + 0.5) & flag) != 0; }

// A surface texel's normal as the height's slopes along its u and v (-dh/du, -dh/dv)
vec2 TlSlope(vec4 s)
{
	vec2 n = s.rg * 2.0 - 1.0;
	return n / max(sqrt(max(1.0 - dot(n, n), 0.0)), 0.2);
}

// One lookup of a kind's maps at uv (tiles; dx, dy its screen derivatives), the uv moved and turned by variant `level` (level < 0: as it
// is). slope: along the uv given.
void TlRead(float layer, vec2 uv, vec2 dx, vec2 dy, float level, bool bare, float rough, out vec4 albedo, out vec4 surface, out vec2 slope)
{
	mat2 R = mat2(1.0);
	if (level >= 0.0)
	{
		uv += fract(sin(vec2(12.9898, 78.233) * (level + 1.0) + layer) * 43758.5453);
		float a = level * 2.39996323;   // (the golden angle: no two of the 8 alike)
		float c = cos(a), s = sin(a);
		R = mat2(c, s, -s, c);
		uv = R * uv; dx = R * dx; dy = R * dy;
	}
	albedo = textureGrad(uLayerAlbedo, vec3(uv, layer), dx, dy);
	if (bare) { surface = vec4(0.5, 0.5, rough, 1.0); slope = vec2(0.0); return; }
	surface = textureGrad(uLayerSurface, vec3(uv, layer), dx, dy);
	slope = transpose(R) * TlSlope(surface);
}

// A kind's maps at uv, its tiles broken when `vary` (see above): the variant of the noise's level, two of them in the band between levels
void TlLookup(int kind, vec2 uv, vec2 dx, vec2 dy, bool vary, out vec4 albedo, out vec4 surface, out vec2 slope)
{
	float layer = float(kind);
	bool bare = TlFlag(kind, 4);   // (no surface map)
	float rough = uLayerParams[kind].w;
	// (two call sites of TlRead only: every one more is inlined and the registers it takes slow the whole program, not just the pixels
	// that read it; measured, T132)
	float level = -1.0, t = 0.0;
	if (vary)
	{
		float k = PNoise(uv * 0.4 + layer * 7.13) * 8.0;
		level = floor(k);
		t = smoothstep(0.35, 0.65, k - level);
	}
	TlRead(layer, uv, dx, dy, t >= 1.0 ? level + 1.0 : level, bare, rough, albedo, surface, slope);
	if (t <= 0.0 || t >= 1.0) return;
	vec4 a1, s1;
	vec2 l1;
	TlRead(layer, uv, dx, dy, level + 1.0, bare, rough, a1, s1, l1);
	// the higher of the two takes the middle of the band (0 and 1 at its ends: no seam where one lookup takes over)
	float b = clamp(t + (a1.a - albedo.a) * 4.0 * t * (1.0 - t), 0.0, 1.0);
	albedo = mix(albedo, a1, b);
	surface = mix(surface, s1, b);
	slope = mix(slope, l1, b);
}

// One kind from above at world xz (gx, gy: the screen derivatives of xz): albedo and height, surface, and slopes along world x and z
void TlPlanar(int kind, vec2 xz, vec2 gx, vec2 gy, out vec4 albedo, out vec4 surface, out vec3 pert)
{
	float scale = uLayerParams[kind].x;
	vec2 slope;
	TlLookup(kind, xz * scale, gx * scale, gy * scale, TlFlag(kind, 2), albedo, surface, slope);
	pert = vec3(slope.x, 0.0, slope.y);
}

// One kind along the three axes (walls): each projection weighted by how much the surface faces it; v runs up the walls
void TlTriplanar(int kind, vec3 p, vec3 n, vec3 gx, vec3 gy, out vec4 albedo, out vec4 surface, out vec3 pert)
{
	float scale = uLayerParams[kind].x;
	// (T132: a sharper blend than pow(|n|, 4): most pixels read one or two projections)
	vec3 w = max(abs(n) - 0.3, 0.0);
	w *= w;
	w /= dot(w, vec3(1.0));
	albedo = vec4(0.0); surface = vec4(0.0); pert = vec3(0.0);
	float sx = n.x < 0.0 ? -1.0 : 1.0, sz = n.z < 0.0 ? -1.0 : 1.0;
	float used = 0.0;
	vec4 a, s;
	vec2 sl;
	if (w.x > 0.03)
	{
		TlLookup(kind, vec2(p.z * sx, p.y) * scale, vec2(gx.z * sx, gx.y) * scale, vec2(gy.z * sx, gy.y) * scale, false, a, s, sl);
		albedo += a * w.x;
		surface += s * w.x;
		pert += (vec3(0.0, 0.0, sx) * sl.x + vec3(0.0, 1.0, 0.0) * sl.y) * w.x;
		used += w.x;
	}
	if (w.z > 0.03)
	{
		TlLookup(kind, vec2(-p.x * sz, p.y) * scale, vec2(-gx.x * sz, gx.y) * scale, vec2(-gy.x * sz, gy.y) * scale, false, a, s, sl);
		albedo += a * w.z;
		surface += s * w.z;
		pert += (vec3(-sz, 0.0, 0.0) * sl.x + vec3(0.0, 1.0, 0.0) * sl.y) * w.z;
		used += w.z;
	}
	if (w.y > 0.03)
	{
		TlLookup(kind, p.xz * scale, gx.xz * scale, gy.xz * scale, false, a, s, sl);
		albedo += a * w.y;
		surface += s * w.y;
		pert += vec3(sl.x, 0.0, sl.y) * w.y;
		used += w.y;
	}
	float inv = 1.0 / max(used, 1e-4);
	albedo *= inv; surface *= inv; pert *= inv;
}

// pos: the world position; N: the smooth ground normal; macro: the macro map's colour there (linear)
TerrainSurface TerrainLayersAt(vec3 pos, vec3 N, vec3 macro)
{
	// (the derivatives in uniform control flow: the lookups below are in branches)
	vec3 gx = dFdx(pos), gy = dFdy(pos);
	float dist = length(pos - uCamPos);
	bool closeUp = dist < 90.0;
	// Far away (300 m and beyond) the ground is the macro map, as the terrain was before the layers (it paints every layer's flat colour,
	// the snow's edge and the trails): no lookup at all; over 280-340 m the layers fade into it.
	float toMacro = smoothstep(280.0, 340.0, dist);
	TerrainSurface o;
	o.Albedo = macro; o.N = N; o.Roughness = 0.9; o.AO = 1.0;
	if (toMacro >= 1.0) return o;

	// The weights, through a warp of about a sample (its fine octave only close up)
	vec2 cell = (pos.xz - uSplatRect.xy) / uSplatRect.z;
	vec2 warp = vec2(PNoise(pos.xz * 0.09), PNoise(pos.xz * 0.09 + 31.7)) - 0.5;
	if (closeUp) warp += (vec2(PNoise(pos.xz * 0.31 + 3.3), PNoise(pos.xz * 0.31 + 11.9)) - 0.5) * 0.5;
	vec2 uv = (cell + warp * 1.4 + 0.5) / uSplatRect.w;
	// (level 0: after the far return this is not uniform control flow; within 340 m a sample of a few metres is bigger than a pixel anyway)
	vec4 s0 = textureLod(uSplat, vec3(uv, 0.0), 0.0), s1 = textureLod(uSplat, vec3(uv, 1.0), 0.0), s2 = textureLod(uSplat, vec3(uv, 2.0), 0.0), s3 = textureLod(uSplat, vec3(uv, 3.0), 0.0);
	float w[13] = float[13](s0.x, s0.y, s0.z, s0.w, s1.x, s1.y, s1.z, s1.w, s2.x, s2.y, s2.z, s2.w, s3.x);

	// The three largest, and the flat colour the macro map has there
	int ids[3] = int[3](2, 2, 2);
	float ws[3] = float[3](0.0, 0.0, 0.0);
	vec3 flatColor = vec3(0.0);
	float total = 0.0;
	for (int k = 0; k < 13; ++k)
	{
		float v = w[k];
		flatColor += uLayerMacro[k].rgb * v;
		total += v;
		if (v > ws[2])
		{
			if (v > ws[1])
			{
				ws[2] = ws[1]; ids[2] = ids[1];
				if (v > ws[0]) { ws[1] = ws[0]; ids[1] = ids[0]; ws[0] = v; ids[0] = k; }
				else { ws[1] = v; ids[1] = k; }
			}
			else { ws[2] = v; ids[2] = k; }
		}
	}
	float top = max(ws[0] + ws[1] + ws[2], 1e-4);
	float wmax = ws[0] / max(total, 1e-4);
	float contrast = 0.0;
	for (int i = 0; i < 3; ++i) { ws[i] /= top; contrast += ws[i] * uLayerParams[ids[i]].z; }
	if (ws[0] <= 0.0) { ws[0] = 1.0; }   // (no weight at all, outside the samples: grass)

	// Each one's material, and the height blend
	vec4 A[3], S[3];
	vec3 P[3];
	float score[3];
	float best = -1e9;
	for (int i = 0; i < 3; ++i)
	{
		A[i] = vec4(0.0); S[i] = vec4(0.0); P[i] = vec3(0.0); score[i] = -1e9;
		if (ws[i] <= 0.003 || (i == 2 && (dist > 50.0 || ws[2] < 0.1))) continue;   // (the third only within 50 m and where it shows)
		int kind = ids[i];
		if (TlFlag(kind, 1)) TlTriplanar(kind, pos, N, gx, gy, A[i], S[i], P[i]);
		else TlPlanar(kind, pos.xz, gx.xz, gy.xz, A[i], S[i], P[i]);
		score[i] = ws[i] + (A[i].a - 0.5) * 0.6 * min(ws[i] * 4.0, 1.0);
		best = max(best, score[i]);
	}
	float depth = mix(0.30, 0.05, contrast);
	float b[3];
	float bsum = 0.0;
	for (int i = 0; i < 3; ++i) { b[i] = max(score[i] - (best - depth), 0.0); bsum += b[i]; }
	TerrainSurface layered;
	layered.Albedo = vec3(0.0); layered.Roughness = 0.0; layered.AO = 0.0;
	vec3 pert = vec3(0.0);
	for (int i = 0; i < 3; ++i)
	{
		float k = b[i] / max(bsum, 1e-5);
		layered.Albedo += A[i].rgb * k;
		layered.Roughness += S[i].b * k;
		layered.AO += S[i].a * k;
		pert += P[i] * k;
	}
	pert -= N * dot(pert, N);
	layered.N = normalize(N + pert);

	// The macro map's own detail (its colour over the layers' flat colours there), inside a layer only
	vec3 ratio = clamp(macro / max(flatColor, vec3(1e-3)), vec3(0.6), vec3(1.5));
	layered.Albedo *= mix(vec3(1.0), ratio, 0.8 * smoothstep(0.55, 0.9, wmax));
	// The forest seen from afar is its canopy (the macro map paints it where the trees are no longer drawn), not the floor under them
	float canopy = smoothstep(60.0, 220.0, dist);
	layered.Albedo = mix(layered.Albedo, macro, canopy * clamp(w[5] / max(total, 1e-4), 0.0, 1.0));

	o.Albedo = mix(layered.Albedo, o.Albedo, toMacro);
	o.N = normalize(mix(layered.N, o.N, toMacro));
	o.Roughness = mix(layered.Roughness, o.Roughness, toMacro);
	o.AO = mix(layered.AO, o.AO, toMacro);
	return o;
}
#endif

// Rain rings on a puddle: one expanding ring per random cell, two layers.
vec2 PuddleRings(vec2 p)
{
	vec2 push = vec2(0.0);
	for (int layer = 0; layer < 2; ++layer)
	{
		float size = layer == 0 ? 0.55 : 0.37;
		vec2 q = p / size + float(layer) * 11.3;
		vec2 cell = floor(q);
		float r1 = PHash(cell), r2 = PHash(cell + 17.0);
		vec2 center = cell + 0.2 + 0.6 * vec2(r1, r2);
		float phase = fract(uTime * (0.7 + 0.5 * r2) + r1 * 9.0);
		vec2 d = q - center;
		float dist = length(d) * size;
		float ringR = phase * size * 0.45;
		float ring = exp(-pow((dist - ringR) / 0.012, 2.0)) * (1.0 - phase) * (1.0 - phase);
		push += d / max(length(d), 1e-4) * ring;
	}
	return push;
}

// T34: what a transmissive surface (glass, ice) lets through: the scene behind it, from the copy at unit 11, looked up where the ray refracted
// by the surface leaves it after `thickness` metres (screen-space refraction), blurred by the roughness and dimmed by Beer-Lambert absorption
// over the same thickness. The copy already carries the fog between the camera and what is behind. N and roughness are the pixel's, after
// the normal and metal-roughness maps: a smooth drop on misted glass refracts sharp, the mist around it blurs.
// T101: the blur reads the copy's mip chain (uSceneColorLods levels this frame): the level whose texel is half the blur radius and four taps
// at half the radius around it, smooth and stable where the 12-tap disk of T34 was grainy. And a depth test: where the opaque scene at the
// refracted point is nearer than this surface, what is there is in front of the glass, not behind it, so the lookup falls back to the point
// straight behind (no leak of the things in front into the glass).
vec3 Transmitted(vec3 N, vec3 V, float roughness, float thickness)
{
	vec3 r = refract(-V, N, 1.0 / max(MATERIAL.ModelParams.z, 1.0));
	if (dot(r, r) < 1e-6) r = -V;
	vec4 clip = uViewProj * vec4(vPos + r * thickness, 1.0);
	vec2 size = vec2(textureSize(uSceneColor, 0));
	vec2 uv = clamp(clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5, vec2(0.0), vec2(1.0));
	if (textureLod(uSceneDepth, uv, 0.0).r < gl_FragCoord.z)
		uv = gl_FragCoord.xy / size;
	float radius = roughness * roughness * 0.3 / max(clip.w, 0.25);   // in screen heights: frosted ice blurs ~0.1 of the screen at 1 m
	float pixels = radius * size.y;
	vec3 behind;
	if (pixels > 1.0)
	{
		float lod = clamp(log2(pixels) - 1.0, 0.0, uSceneColorLods);
		vec2 scale = vec2(radius * size.y / size.x, radius) * 0.5;
		behind = textureLod(uSceneColor, uv, lod).rgb * 2.0;
		const vec2 taps[4] = vec2[4](vec2(0.94, 0.34), vec2(-0.34, 0.94), vec2(-0.94, -0.34), vec2(0.34, -0.94));
		for (int i = 0; i < 4; ++i)
			behind += textureLod(uSceneColor, clamp(uv + taps[i] * scale, vec2(0.0), vec2(1.0)), lod).rgb;
		behind /= 6.0;
	}
	else
		behind = textureLod(uSceneColor, uv, 0.0).rgb;
	return behind * exp(-MATERIAL.Layer[SLOT_TRANSMISSION].rgb * thickness);
}

void main()
{
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCamPos - vPos);
	if (!gl_FrontFacing && !HasFlag(FLAG_FOLIAGE_NORMALS)) N = -N;   // back faces of double-sided materials
	vec3 Ng = N;   // T101: the interpolated normal, before any map (the interior's parallax and the scatter's edges)
	if (HasTex(TEX_TERRAIN_NORMALS))   // per-pixel smooth ground normal instead of the interpolated (faceted) vertex normal
		N = normalize(texture(uTerrainNormals, clamp((vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw, vec2(0.0004), vec2(0.9996))).xyz * 2.0 - 1.0);
	float trailT = 0.0;
	if (MATERIAL.Layer[SLOT_TRAIL].x > 0.0)
	{
		// The surface is lower where the map is high: its normal tilts toward the dent's rim.
		float e = uTrailRect.w;
		trailT = TrailAt(vPos.xz);
		vec2 grad = vec2(TrailAt(vPos.xz + vec2(e, 0.0)) - TrailAt(vPos.xz - vec2(e, 0.0)),
		                 TrailAt(vPos.xz + vec2(0.0, e)) - TrailAt(vPos.xz - vec2(0.0, e))) / (2.0 * e);
		N = normalize(N + vec3(grad.x, 0.0, grad.y) * MATERIAL.Layer[SLOT_TRAIL].x * MATERIAL.Layer[SLOT_TRAIL].y);
	}
#ifdef GB_FOLIAGE
	// T46: foliage. The card's normal mixed with the normal of a sphere around the object's pivot (on the ground: the sphere leans up), so
	// the inside of a bush sees the sky; before the normal map, which perturbs the blended normal (the card's relief still shows).
	if (MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].y > 0.0)
	{
		vec3 crown = vPos - vOrigin;
		vec3 S = dot(crown, crown) > 1e-8 ? normalize(crown) : vec3(0.0, 1.0, 0.0);
		vec3 n = mix(N, S, MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].y);
		N = dot(n, n) > 1e-6 ? normalize(n) : S;
	}
#endif

	if (HasTex(TEX_NORMAL))
	{
		vec3 tn = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
		tn.xy *= MATERIAL.Uv.zw;
		tn.xy *= MATERIAL.Uv.y;
		vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
		vec3 B = cross(N, T) * vTangent.w;
		N = normalize(mat3(T, B, N) * normalize(tn));
	}

	vec3 albedo = MATERIAL.BaseColor.rgb;
	float alpha = MATERIAL.BaseColor.a;
	if (HasTex(TEX_ALBEDO))
	{
		vec4 t = texture(uAlbedoMap, vUV);
		albedo *= t.rgb;
		alpha *= t.a;
#ifdef GB_FOLIAGE
		// T46: the mip levels average the mask's alpha down, so thin cards thin out and flicker far away: raise it by the level
		if (MATERIAL.Flags.z == 1u)
			alpha *= 1.0 + max(textureQueryLod(uAlbedoMap, vUV).x, 0.0) * MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].w;
#endif
	}
#ifdef GB_FOLIAGE
	// T46: per-instance colour (a hash of the instance's origin) and the world's macro patches; T128: worked out in the VS as a matrix
	albedo = max(vFoliagePatch * (vFoliageInstance * albedo), vec3(0.0));
#endif
	if (HasTex(TEX_MACRO))
		albedo *= texture(uMacroMap, (vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw).rgb;
#ifdef GB_TERRAIN_LAYERS
	// T115: a partition terrain: its material layers, modulated by the macro colour above (lib/terrain_layers)
	TerrainSurface terrainLayers = TerrainLayersAt(vPos, N, albedo);
	albedo = terrainLayers.Albedo;
	N = terrainLayers.N;
#endif
	albedo *= mix(vec3(1.0), MATERIAL.Layer[SLOT_TRAIL_TINT].rgb, clamp(trailT * 1.3, 0.0, 1.0));   // compacted / wetter ground
	if (MATERIAL.Flags.z == 1u)
	{
		// Mip levels blur the mask: rescale around the cutoff by the screen-space change of alpha (soft, sharp edge).
		alpha = clamp((alpha - MATERIAL.Emissive.a) / max(fwidth(alpha), 1e-3) + 0.5, 0.0, 1.0);
		if (alpha < (HasFlag(FLAG_ALPHA_TO_COVERAGE) ? 0.02 : 0.5)) discard;
	}
	// Procedural surface layers: grain, moss on top, sand at the base, a wet band at the water line.
	float mossMask = 0.0, sandMask = 0.0, bandWet = 0.0;
	float grain = MATERIAL.Layer[SLOT_GRAIN].x, moss = MATERIAL.Layer[SLOT_MOSS].w, sand = MATERIAL.Layer[SLOT_SAND].w;
	float waterLine = MATERIAL.Layer[SLOT_WATER_LINE].x;
	if (grain > 0.0 || moss > 0.0 || sand > 0.0 || waterLine > -1e8)
	{
		// With grain == 0 the mottling is mix(1, x, 0) == 1 exactly: the grain noise (g1) is only paid when grain is on. g2 is the
		// fine noise of moss, sand and the wet band, so it is only paid when one of those is on.
		if (grain != 0.0)
		{
			vec3 gp = vPos * MATERIAL.Layer[SLOT_GRAIN].y;
			float g1 = FBM3(gp);
			albedo *= mix(1.0, 0.55 + 0.9 * g1, grain);
			if (grain > 0.0)
			{
				float e = 0.05;
				vec3 grad = vec3(FBM3(gp + vec3(e, 0, 0)) - g1, FBM3(gp + vec3(0, e, 0)) - g1, FBM3(gp + vec3(0, 0, e)) - g1) / e;
				N = normalize(N - (grad - N * dot(grad, N)) * 0.06 * grain);
			}
		}
		float g2 = 0.0;
		if (moss > 0.0 || sand > 0.0 || waterLine > -1e8)
			g2 = FBM3(vPos * 11.0 + 5.0);
		float up = clamp(normalize(vNormal).y, 0.0, 1.0);
		if (moss > 0.0)
		{
			float v = up * 0.55 + FBM3(vPos * 1.9 + 3.0) * 0.7 + (moss - 0.5) * 0.9 - 0.45;
			mossMask = smoothstep(0.3, 0.55, v) * smoothstep(0.0, 0.12, moss);
			albedo = mix(albedo, MATERIAL.Layer[SLOT_MOSS].rgb * (0.55 + 0.9 * g2), mossMask);
		}
		if (sand > 0.0)
		{
			float h = vPos.y - MATERIAL.Layer[SLOT_WATER_LINE].y;
			sandMask = (1.0 - smoothstep(0.0, 0.5, h + (g2 - 0.5) * 0.3)) * sand * (0.55 + 0.45 * up);
			albedo = mix(albedo, MATERIAL.Layer[SLOT_SAND].rgb * (0.85 + 0.3 * g2), sandMask);
		}
		if (waterLine > -1e8)
		{
			bandWet = 1.0 - smoothstep(waterLine, waterLine + 0.3, vPos.y + (g2 - 0.5) * 0.1);
			albedo *= mix(1.0, 0.5, bandWet);
		}
	}
	albedo *= mix(1.0, 0.55, uWetness);   // wet surfaces are darker...
	// Snow settles on surfaces that face up (with a little noise so the edge is not a clean line).
	float snow = 0.0;
	if (uSnowCover > 0.0)
	{
		float upFacing = clamp(normalize(vNormal).y, 0.0, 1.0);
		float grain = fract(sin(dot(floor(vPos.xz * 6.0), vec2(12.9898, 78.233))) * 43758.5453) * 0.12;
		snow = smoothstep(0.55, 0.85, upFacing + grain - (1.0 - uSnowCover) * 0.35) * uSnowCover;
		albedo = mix(albedo, vec3(0.92, 0.94, 0.97), snow);
	}
	// Puddles: where the ground is flat and low-frequency noise says so, rain collects into mirror-like patches.
	float puddle = 0.0;
	if (uWetness > 0.3)
	{
		float flatness = smoothstep(0.9, 0.98, normalize(vNormal).y);
		float pn = PNoise(vPos.xz * 0.22) * 0.65 + PNoise(vPos.xz * 0.95 + 7.0) * 0.35;
		float cover = smoothstep(0.3, 1.0, uWetness) * 0.5;
		puddle = smoothstep(1.0 - cover - 0.05, 1.0 - cover + 0.02, pn) * flatness * (1.0 - snow);
		N = normalize(mix(N, normalize(vNormal), puddle * 0.9));
		vec2 rings = PuddleRings(vPos.xz);
		N = normalize(N + vec3(rings.x, 0.0, rings.y) * 0.6 * puddle * smoothstep(0.4, 0.9, uWetness));
		albedo *= mix(1.0, 0.55, puddle);
	}
	float ao = MATERIAL.Surface.z;
	if (HasTex(TEX_OCCLUSION))
		ao *= mix(1.0, texture(uOcclusionMap, vUV).r, MATERIAL.Surface.w);
	if (HasTex(TEX_HEIGHT_MAP))
	{
		float h = texture(uHeightMap, vUV).r;
		ao *= mix(1.0, clamp(h * 1.4, 0.0, 1.0), MATERIAL.ModelParams.y);
	}
	float metallic = MATERIAL.Surface.x;
	float roughness = MATERIAL.Surface.y;
	if (HasTex(TEX_MR))
	{
		vec3 mr = texture(uMRMap, vUV).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
#ifdef GB_TERRAIN_LAYERS
	roughness = terrainLayers.Roughness;
	ao *= terrainLayers.AO;
#endif
	metallic = clamp(metallic, 0.0, 1.0);
	metallic *= 1.0 - snow;
	roughness = mix(roughness, roughness * 0.25, uWetness * (1.0 - metallic) * (1.0 - snow));   // ...and glossier
	roughness = mix(roughness, 0.85, snow);
	roughness = mix(roughness, 0.9, max(mossMask, sandMask));
	roughness = mix(roughness, 0.12, bandWet);
	roughness = mix(roughness, 0.03, puddle);
	roughness = clamp(roughness, 0.03, 1.0);
	float a = roughness * roughness;
	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	// T34: a transmissive surface reflects like a dielectric of its index of refraction and does not diffuse the light that goes through.
#ifdef GB_TERRAIN_LAYERS
	float transmission = 0.0;   // T115: a terrain is opaque (and the transmission's three samplers stay out of this program)
#else
	float transmission = (MATERIAL.Flags.x & LAYER_TRANSMISSION) != 0u ? MATERIAL.ModelParams.w : 0.0;
#endif
	if (transmission > 0.0)
	{
		float f0 = (MATERIAL.ModelParams.z - 1.0) / (MATERIAL.ModelParams.z + 1.0);
		F0 = mix(vec3(f0 * f0), albedo, metallic);
		albedo *= 1.0 - transmission;
	}
	// T101: the thickness Beer-Lambert absorbs over, changed by the height map (or else the interior's density) by the variation, and the
	// interior map at two depths under the surface: the uv shifted along the view in tangent space (uv - V.xy / V.z * depth), so the cracks
	// and bubbles move against the surface (parallax). The deeper sample reads another part of the map at another scale.
	float thickness = MATERIAL.Layer[SLOT_TRANSMISSION].w;
	vec4 inner0 = vec4(0.0), inner1 = vec4(0.0);
	if (transmission > 0.0)
	{
		vec4 extra = MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA];
		float h = -1.0;
		if (HasTex(TEX_INTERIOR))
		{
			vec3 T = normalize(vTangent.xyz - Ng * dot(Ng, vTangent.xyz));
			vec3 B = cross(Ng, T) * vTangent.w;
			vec3 Vt = vec3(dot(V, T), dot(V, B), dot(V, Ng));
			vec2 shift = Vt.xy / max(Vt.z, 0.25);   // grazing views clamped: the layer would slide off
			inner0 = texture(uInteriorMap, vUV - shift * extra.x * 0.4);
			inner1 = texture(uInteriorMap, vUV * 0.71 + vec2(0.37, 0.61) - shift * extra.x);
			h = 0.25 + 0.5 * (inner0.a + inner1.a);
		}
		if (HasTex(TEX_HEIGHT_MAP))
			h = texture(uHeightMap, vUV).r;
		if (h >= 0.0)
			thickness *= mix(1.0, 0.35 + 1.3 * clamp(h, 0.0, 1.0), extra.w);
	}


	vec3 Lo = vec3(0.0);

	// T110: a crown that keeps its baked crown normal on both faces (FLAG_FOLIAGE_NORMALS: the tree crowns) mirrors the sun and the sky
	// only as much as the card really faces that way: the crown normal is the volume's, not the leaf's, and on its own it put white glints
	// over the tops of the crowns against a low sun (the whole top of a crown "facing" the half vector). The specular is scaled by the
	// fourth power of the agreement between the card's own face (from the screen derivatives) and the shading normal.
	// The other materials keep their code untouched (bit for bit the same pictures).
	bool crownNormals = HasFlag(FLAG_FOLIAGE_NORMALS);
	float specScale = 1.0;
	if (crownNormals)
	{
		vec3 face = cross(dFdx(vPos), dFdy(vPos));
		float agree = dot(face, face) > 1e-20 ? abs(dot(normalize(face), N)) : 1.0;
		float agree2 = agree * agree;
		specScale = agree2 * agree2;
	}

	vec3 Ls = normalize(-uSunDir);
#ifdef GB_FOLIAGE
	// T128: one shadow lookup for the sun on the face and through the leaf, offset along the side of the card that faces the sun (on a
	// back-lit face, -N): the face toward the sun gets the offset it always had, and the back-lit one no longer shadows itself. With 9
	// taps (lib/shadow, Shadow9): the crowns' overdraw made the 25 taps most of their cost.
	float sunShadow = Shadow9(vPos, dot(N, Ls) < 0.0 ? -N : N, Ls);
	float leafShadow = sunShadow;
#else
	float sunShadow = Shadow(vPos, N, Ls);
#endif
#ifdef GB_FOLIAGE
	// T107: occlusion from the pivot, no maps: dark where the plant meets the ground, fading out by `height` over the pivot, and in its core,
	// near the vertical axis (within `height` of it, fading out by three times `height` up). It dims the ambient and the light through the
	// leaf fully and the sun half (the ground and the inner leaves still see some of it).
	vec4 shade = MATERIAL.Layer[SLOT_FOLIAGE_SHADE];
	float leafOcc = 1.0;
	if (shade.y > 0.0)
	{
		vec3 rel = vPos - vOrigin;
		float up = clamp(rel.y / shade.y, 0.0, 1.0);
		float baseOcc = 1.0 - up * up * (3.0 - 2.0 * up);
		float core = (1.0 - smoothstep(0.0, shade.y, length(rel.xz))) * (1.0 - smoothstep(shade.y, 3.0 * shade.y, rel.y));
		leafOcc = (1.0 - shade.x * baseOcc) * (1.0 - shade.z * core);
		ao *= leafOcc;
		sunShadow *= 0.5 + 0.5 * leafOcc;
	}
	// T107: with the spread on, a face the sun does not reach (its interpolated normal turned away from the sun; the crown normal can
	// still lean toward it) takes the sun as diffuse only: that light came through the leaf, it is not mirrored (no white glints on
	// the back-lit edges)
	if (shade.w > 0.0 && dot(Ng, Ls) < 0.0)
		Lo += albedo * ((1.0 - metallic) / PI * max(dot(N, Ls), 0.0)) * uSunColor * sunShadow;
	else
#endif
	{
		if (crownNormals)
			Lo += ShadeSpecScaled(N, V, Ls, uSunColor, albedo, F0, a, metallic, specScale) * sunShadow;
		else
			Lo += Shade(N, V, Ls, uSunColor, albedo, F0, a, metallic) * sunShadow;
	}
#ifdef GB_FOLIAGE
	// T46: the sun through the leaf (diffuse transmission), tinted by the albedo times the transmission tint. Its shadow is the sun's
	// lookup (T128), before the occlusion scales it, offset to the sun's side of the card: the leaf does not shadow itself, and the
	// leaves between this one and the sun still do (a leaf in the shadow of another does not glow).
	float leafT = MATERIAL.Layer[SLOT_FOLIAGE].w;
	vec3 leafTint = MATERIAL.Layer[SLOT_FOLIAGE].rgb * albedo * leafT;
	if (leafT > 0.0)
	{
		float through;
		if (shade.w > 0.0)
		{
			// T107: the spread. The sun's direction bent by the normal (as thin-surface scattering does) and a wider lobe, so the glow
			// is seen around the sun and not only right behind; and the grazing edges of the silhouette lit where the sun reaches them
			// (the thinnest part of the leaf, seen edge-on).
			vec3 Lb = normalize(Ls + N * (0.6 * shade.w));
			float edge = pow(1.0 - abs(dot(N, V)), 3.0) * (0.5 + 0.5 * dot(V, -Ls));
			through = pow(clamp(dot(V, -Lb), 0.0, 1.0), 4.0 - 2.5 * shade.w) * 3.0 + clamp(dot(-N, Ls), 0.0, 1.0) + edge * 1.5 * shade.w;
		}
		else
			through = FoliageThrough(N, V, Ls);
		Lo += uSunColor / PI * leafShadow * through * leafTint * leafOcc;
	}
#endif

	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPosRadius[i].xyz - vPos;
		float d2 = dot(toLight, toLight);
		vec3 L = toLight * inversesqrt(d2);
		float window = 1.0;
		if (uPointPosRadius[i].w > 0.0)
		{
			float r = d2 / (uPointPosRadius[i].w * uPointPosRadius[i].w);
			window = clamp(1.0 - r * r, 0.0, 1.0);
			window *= window;
		}
		if (crownNormals)
			Lo += ShadeSpecScaled(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic, specScale);
		else
			Lo += Shade(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic);
	}

	float NoV = max(dot(N, V), 1e-4);
	vec3 R = reflect(-V, N);
	vec3 F = EnvBRDFApprox(F0, roughness, NoV);
	vec3 kd = (1.0 - F) * (1.0 - metallic);
	vec3 ambientDiffuse = kd * albedo * SkyIrradiance(N);
	vec3 ambientSpec = SkyPrefiltered(R, roughness) * F;
	if (crownNormals)
		ambientSpec *= specScale;
	// Horizon occlusion: reflections under the surface plane fade out.
	float horizon = clamp(1.0 + dot(R, N) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;
#ifdef GB_FOLIAGE
	if (leafT > 0.0)   // T46: the sky behind the leaf, through it (a quarter of what the face toward it gets)
		ambient += SkyIrradiance(-N) * leafTint * (0.25 * ao * uEnvIntensity);
#endif

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 emissive = MATERIAL.Emissive.rgb;
	if (HasTex(TEX_EMISSIVE)) emissive *= texture(uEmissiveMap, vUV).rgb;
	vec3 color = Lo + ambient + emissive;
	// T34: the part of what is behind that gets through: not reflected (the Fresnel term of the ambient light), not absorbed. The reflection
	// is not dimmed by any opacity; the transmitted light is, by (1 - F) and the amount.
	vec3 through = vec3(0.0), passing = vec3(0.0);
	if (transmission > 0.0)
	{
		passing = (1.0 - F) * (1.0 - metallic) * transmission;
		through = Transmitted(N, V, roughness, thickness) * passing;
		vec3 absorb = MATERIAL.Layer[SLOT_TRANSMISSION].rgb * thickness;
		float scatter = MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA].y;
		float wrap = clamp((dot(N, Ls) + 0.6) / 1.6, 0.0, 1.0);
		if (HasTex(TEX_INTERIOR))
		{
			// T101: the inclusions scatter the light that reaches them (the sky and the wrapped, shadowed sun), dimmed by the ice above them,
			// and hide what is behind by their density: the deep layer first, the shallow one over it.
			vec4 inside = MATERIAL.Layer[SLOT_INTERIOR];
			vec3 lit = (SkyIrradiance(N) * uEnvIntensity + uSunColor / PI * sunShadow * (0.25 + 0.75 * wrap)) * inside.rgb * passing;
			through = mix(through, inner1.rgb * lit * exp(-absorb * 0.6), clamp(inner1.a * inside.w, 0.0, 1.0));
			through = mix(through, inner0.rgb * lit * exp(-absorb * 0.25), clamp(inner0.a * inside.w, 0.0, 1.0));
		}
		if (scatter > 0.0)
		{
			// T101: light through the thin parts, cheap: the silhouette (grazing view, the ice is thin there) and the sun seen through the
			// piece (back light around -Ls, bent by the normal), plus a little wrap; tinted by a shorter path, so the edges are a paler cyan.
			float edge = pow(1.0 - clamp(dot(Ng, V), 0.0, 1.0), 3.0);
			float back = pow(clamp(dot(V, -normalize(Ls + N * 0.4)), 0.0, 1.0), 6.0);
			vec3 thin = exp(-absorb * 0.3);
			color += (uSunColor / PI * sunShadow * (back * 2.0 + edge * wrap * 1.2 + wrap * 0.15) + SkyIrradiance(N) * uEnvIntensity * edge * 0.5)
				* thin * scatter * transmission;
		}
	}
	float fogKeep = 1.0;   // the part of the surface's light that gets through the fog
	if (uFogDensity > 0.0)
	{
		float dist = length(vPos - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		fogKeep = exp(-density * density * dist * dist);
		if (transmission > 0.0)   // the fog on the surface's own light only: the transmitted light has it already (the copy is fogged)
			color = mix(uFogColor * uEnvIntensity * (1.0 - dot(passing, vec3(1.0 / 3.0))), color, fogKeep);
		else
			color = mix(uFogColor * uEnvIntensity, color, fogKeep);
	}
	color += through;
	float outAlpha = (MATERIAL.Flags.z == 2u || (MATERIAL.Flags.z == 1u && HasFlag(FLAG_ALPHA_TO_COVERAGE))) ? alpha : 1.0;
	if (transmission > 0.0) outAlpha = 1.0;   // what is behind is in the color already: blending it again would count it twice
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), outAlpha);
	outAmbient = vec4(clamp(ambient * fogKeep, vec3(0.0), vec3(4096.0)), outAlpha);   // blended like the color
}
