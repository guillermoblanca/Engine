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

	// lib/fog_uniforms: exponential height fog. Inside lib/sky; Particle FS on its own.
	// The fog formula itself is still written out in each main (different variable names): unified in M8.
	inline constexpr const char* FogUniforms = R"GLSL(uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
)GLSL";

	// lib/sky (the old SkyCommon): procedural sky and environment map, shared by the background and image based lighting.
	// Sky, PBR, Water and Grass FS. Starts with a blank line, as SkyCommon did.
	inline constexpr const char* Sky = R"GLSL(
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;
#include "lib/fog_uniforms"
uniform float uWetness;
uniform float uSnowCover;

layout(binding = 9) uniform sampler2D uEnvMap;
uniform int uHasEnvMap;
uniform float uEnvMaxLod;
uniform float uEnvRotation;
uniform float uEnvExposure;

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

	// lib/trail_at: ground-mark lookup (footprints, tracks). Needs uTrailMap and uTrailRect declared before it.
	// PBR VS and FS (through lib/object_trail) and Grass VS (with its own uniforms).
	inline constexpr const char* TrailAt = R"GLSL(float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}
)GLSL";

	// lib/object_trail: the ground-mark uniforms of objects and terrain, plus TrailAt. PBR VS and FS.
	inline constexpr const char* ObjectTrail = R"GLSL(layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size (m), texel size (m)
uniform float uTrailDepth;     // 0 = this material ignores ground marks
uniform vec3 uTrailTint;
uniform float uTrailBoost;
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

	// lib/object_height: height-map displacement uniforms. Shadow and PBR VS.
	inline constexpr const char* ObjectHeight = R"GLSL(uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
)GLSL";

	// lib/tree_wind: tree and foliage sway, WindOffset(). Shadow and PBR VS. The grass wind is another formula (Grass VS).
	inline constexpr const char* TreeWind = R"GLSL(// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
uniform float uWindStrength;      // 0 = rigid; meters of sway at the top of the object per unit of wind
uniform float uWindHeight;        // object-space height at which the sway is full
uniform float uWindFlutter;       // small fast leaf motion (meters)
uniform float uTime;
uniform vec3 uWind;
vec3 WindOffset(vec3 localPos, vec3 worldPos, vec3 origin)
{
	float hw = clamp(localPos.y / max(uWindHeight, 1e-3), 0.0, 1.0);
	hw *= hw;
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(origin.xz, wd) * 0.12 - uTime * (0.8 + 0.1 * ws);
	float sway = (0.05 + 0.03 * ws) * (0.75 + 0.3 * sin(phase) + 0.2 * sin(phase * 2.3 + 1.0));
	vec3 off = vec3(wd.x, 0.0, wd.y) * sway * uWindStrength * hw;
	float fl = uWindFlutter * (0.35 + 0.08 * ws) * hw;
	off += vec3(sin(uTime * 4.1 + worldPos.z * 4.0), 0.4 * sin(uTime * 5.3 + worldPos.x * 3.0), sin(uTime * 3.7 + worldPos.y * 5.0)) * fl;
	return off;
}
)GLSL";

	// lib/point_lights: the point light uniforms (32 max, Renderer::MaxPointLights). PBR and Water FS.
	inline constexpr const char* PointLights = R"GLSL(uniform int uPointCount;
uniform vec3 uPointPos[32];
uniform vec3 uPointColor[32];
uniform float uPointRadius[32];
)GLSL";

	// lib/depth: LinearDepth() from a depth-buffer value (perspective or ortho). Needs uNearFar and uOrtho.
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

	// lib/shadow: the 5x5 PCF sun shadow of objects, Shadow(). Needs uShadowsOn, uLightVP, uShadowTexel and uShadowMap.
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

	// lib/water_waves: the four Gerstner waves of WaterSurface::HeightAt. Water VS and FS. The @WAVESET@ line is replaced
	// after assembly by InjectWaveSet (World/Water/WaterWaves.h) with the wave constants.
	inline constexpr const char* WaterWaves = R"GLSL(
uniform float uTime;
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
