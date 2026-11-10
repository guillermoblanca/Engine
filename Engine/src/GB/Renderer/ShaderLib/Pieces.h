#pragma once

// Shared GLSL pieces, the `lib/*` of the shader assembler (ShaderAssembler.h; registry in Shaders.h).
// Each piece is verbatim GLSL: a program that includes it is byte for byte what it was when the text was pasted in (T68, test
// shader_assembly). Only text that was identical in every program shares a piece; copies that differ stay in their program until M8
// unifies them (DESACOPLE_MATERIALES.md, sections 1.4 and 2.7).

namespace GB::Shaders::Lib
{
	// lib/scene_targets: the two color targets of the scene pass (color and its ambient part, for the SSAO).
	// Sky, PBR, Particle, Water and Grass FS.
	inline constexpr const char* SceneTargets = R"GLSL(layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
)GLSL";

	// lib/frame: the per-frame block (render M3, T69): camera, sun, sky, fog, environment, weather, time, wind, shadow and point lights,
	// written once per frame by Renderer::Render into its upload ring and bound at FrameConstants::Binding. Mirror of GB::FrameConstants
	// (FrameConstants.h): same members, same order, std140; a change here is a change there. The members keep the names the loose uniforms
	// had, so the code that reads them did not change; only the point lights are packed (a vec3 or float array has a 16 B stride in std140).
	// Every program of the scene pass and the shadow pass (Sky, PBR, Shadow, Grass, Water, Particle); not the post-processing.
	inline constexpr const char* Frame = R"GLSL(layout(std140, binding = 0) uniform FrameBlock
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
)GLSL";

	// lib/material: the per-material block (render M4, T36): one MaterialData per distinct material in a storage buffer at
	// MaterialData::Binding, written by the renderer's MaterialBuffer; a draw reads its entry, MATERIAL, at uMaterialId. Mirror of
	// GB::MaterialData (MaterialData.h): same members, same order, std430; the constants are its Tex*, Flag*, Layer* and Slot* (the test
	// material_block checks the offsets against the driver and these lines against the C++). PBR and Shadow VS and FS.
	inline constexpr const char* Material = R"GLSL(struct MaterialData
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
)GLSL";

	// lib/sky (the old SkyCommon): procedural sky and environment map, shared by the background and image based lighting.
	// Sky, PBR, Water and Grass FS. Its values (sky colors, fog, environment map) are in lib/frame. Starts with a blank line, as SkyCommon did.
	inline constexpr const char* Sky = R"GLSL(
#include "lib/frame"

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
)GLSL";

	// lib/trail_at: ground-mark lookup (footprints, tracks). Needs uTrailMap declared before it (uTrailRect is in lib/frame).
	// PBR VS and FS (through lib/object_trail) and Grass VS (with its own uniforms).
	inline constexpr const char* TrailAt = R"GLSL(#include "lib/frame"
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}
)GLSL";

	// lib/object_trail: the ground-mark map of the objects, plus TrailAt (the window, uTrailRect, is in lib/frame; the material's depth, tint
	// and boost in lib/material, SLOT_TRAIL and SLOT_TRAIL_TINT). PBR VS and FS.
	inline constexpr const char* ObjectTrail = R"GLSL(layout(binding = 8) uniform sampler2D uTrailMap;
#include "lib/trail_at"
)GLSL";

	// lib/object_skin: skinning inputs and joint buffer, the instance buffer, and SkinMatrix(). Shadow and PBR VS.
	inline constexpr const char* ObjectSkin = R"GLSL(layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(std430, binding = 0) readonly buffer JointBuffer { mat4 uJointMatrices[]; };
layout(std430, binding = 2) readonly buffer InstanceBuffer { mat4 uInstances[]; };
uniform int uInstanced;   // 1: one draw of many copies of the object, their matrices in uInstances (uModel unused)
uniform int uSkinned;
mat4 SkinMatrix()
{
	return aWeights.x * uJointMatrices[aJoints.x] + aWeights.y * uJointMatrices[aJoints.y]
		+ aWeights.z * uJointMatrices[aJoints.z] + aWeights.w * uJointMatrices[aJoints.w];
}
)GLSL";

	// lib/object_height: the height map of the displacement (whether it is on, TEX_HEIGHT, and its scale are in lib/material). Shadow and PBR VS.
	inline constexpr const char* ObjectHeight = R"GLSL(layout(binding = 3) uniform sampler2D uHeightMap;
)GLSL";

	// lib/tree_wind: tree and foliage sway, WindOffset(). Shadow and PBR VS. The grass wind is another formula (Grass VS).
	// The material's MATERIAL.Wind: x sway (0 = rigid; meters at the top of the object per unit of wind), y the object-space height at which
	// the sway is full, z flutter (small fast leaf motion, meters).
	inline constexpr const char* TreeWind = R"GLSL(// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
#include "lib/material"
#include "lib/frame"
vec3 WindOffset(vec3 localPos, vec3 worldPos, vec3 origin)
{
	float hw = clamp(localPos.y / max(MATERIAL.Wind.y, 1e-3), 0.0, 1.0);
	hw *= hw;
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(origin.xz, wd) * 0.12 - uTime * (0.8 + 0.1 * ws);
	float sway = (0.05 + 0.03 * ws) * (0.75 + 0.3 * sin(phase) + 0.2 * sin(phase * 2.3 + 1.0));
	vec3 off = vec3(wd.x, 0.0, wd.y) * sway * MATERIAL.Wind.x * hw;
	float fl = MATERIAL.Wind.z * (0.35 + 0.08 * ws) * hw;
	off += vec3(sin(uTime * 4.1 + worldPos.z * 4.0), 0.4 * sin(uTime * 5.3 + worldPos.x * 3.0), sin(uTime * 3.7 + worldPos.y * 5.0)) * fl;
	return off;
}
)GLSL";

	// lib/depth: LinearDepth() from a depth-buffer value (perspective or ortho). Needs uNearFar and uOrtho (lib/frame in Particle and Water,
	// its own uniforms in Composite).
	// Composite, Particle and Water FS. AOBlur's Lin() has no ortho branch: it stays apart (M8).
	inline constexpr const char* Depth = R"GLSL(float LinearDepth(float d)
{
	if (uOrtho != 0) return uNearFar.x + d * (uNearFar.y - uNearFar.x);
	float z = d * 2.0 - 1.0;
	return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
)GLSL";

	// lib/brdf: PI, GGX distribution, correlated Smith visibility, Schlick Fresnel and Karis' environment BRDF. PBR FS.
	inline constexpr const char* Brdf = R"GLSL(const float PI = 3.14159265359;

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
)GLSL";

	// lib/shadow: the 5x5 PCF sun shadow of objects, Shadow(). Needs uShadowMap and lib/frame (uShadowsOn, uLightVP, uShadowTexel).
	// PBR FS. Grass has its own 3x3 filter (GrassShadow).
	inline constexpr const char* Shadow = R"GLSL(float Shadow(vec3 worldPos, vec3 N, vec3 L)
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
)GLSL";

	// lib/noise_pbr: value noise 2D/3D and FBM3 with PBR's names (PHash, PNoise, PHash3, PNoise3). PBR FS.
	// Sky (SHash, SNoise) and Water (Hash, Noise) have the same formulas under other names: unified in M8.
	inline constexpr const char* NoisePbr = R"GLSL(float PHash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
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
)GLSL";

	// lib/foliage (T46): what leaves and grass share (MATERIALES.md, "Follaje"). PBR FS (LAYER_FOLIAGE) and Grass FS (its uniforms).
	// - FoliageVary: the colour of one instance of a plant, from a hash of its origin in the world: hue turned around the grey axis (mostly
	//   toward yellow, some toward a deeper green), saturation down toward olive or a little up, and brightness up or down, by `amount`;
	//   and, by `macro`, a low-frequency world patch between a
	//   yellowish and an olive green. Self-contained (its own hash, not lib/noise_pbr's, which the grass does not have).
	// - FoliageThrough: how much of the sun gets through a leaf toward the eye, before its tint and shadow: a lobe around the sun seen
	//   through the leaf (V . -L) and a diffuse part on the face away from the sun (-N . L), as a wrap.
	inline constexpr const char* Foliage = R"GLSL(float FHash3(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
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
float FoliageThrough(vec3 N, vec3 V, vec3 L)
{
	return pow(clamp(dot(V, -L), 0.0, 1.0), 4.0) * 3.0 + clamp(dot(-N, L), 0.0, 1.0);
}
)GLSL";

	// lib/water_waves: the four Gerstner waves of WaterSurface::HeightAt. Water VS and FS. The @WAVESET@ line is replaced
	// after assembly by InjectWaveSet (World/Water/WaterWaves.h) with the wave constants.
	inline constexpr const char* WaterWaves = R"GLSL(
#include "lib/frame"
uniform vec2 uWaveDir;
uniform float uWaveHeight;
uniform float uWaveLength;
uniform float uSteep;
uniform float uChop;

@WAVESET@

// xz offset in .xz, height in .y; the surface slope (dh/dx, dh/dz) in `slope`. `fade` removes the small waves with distance.
vec3 Waves(vec2 p, float fade, out vec2 slope)
{
	vec3 o = vec3(0.0);
	slope = vec2(0.0);
	for (int i = 0; i < 4; ++i)
	{
		float c = cos(kAngle[i]), s = sin(kAngle[i]);
		vec2 d = vec2(uWaveDir.x * c - uWaveDir.y * s, uWaveDir.x * s + uWaveDir.y * c);
		float k = 6.2831853 / (uWaveLength * kLen[i]);
		float amp = uWaveHeight * kAmp[i] * (i == 0 ? 1.0 : uChop * mix(1.0, fade, float(i) / 3.0));
		float ph = k * dot(d, p) - k * sqrt(9.81 / k) * uTime;
		float sn = sin(ph), cs = cos(ph);
		float q = uSteep / max(k * uWaveHeight * 4.0, 1e-3);   // keeps loops from forming
		q = min(q, 1.0) * 0.5;
		o.xz += d * (q * amp * cs);
		o.y += amp * sn;
		slope += d * (amp * k * cs);
	}
	return o;
}
)GLSL";
}
