#pragma once

// Built-in GLSL sources. Kept as raw strings so the engine has no runtime file dependencies.
namespace GB::Shaders
{
	inline constexpr const char* FullscreenVS = R"GLSL(#version 450 core
out vec2 vUV;
void main()
{
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	vUV = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

	inline constexpr const char* ShadowVS = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uLightVP;
uniform mat4 uModel;
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(std430, binding = 0) readonly buffer JointBuffer { mat4 uJointMatrices[]; };
uniform int uSkinned;
mat4 SkinMatrix()
{
	return aWeights.x * uJointMatrices[aJoints.x] + aWeights.y * uJointMatrices[aJoints.y]
		+ aWeights.z * uJointMatrices[aJoints.z] + aWeights.w * uJointMatrices[aJoints.w];
}
uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
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
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
	if (uSkinned != 0)
		pos = (SkinMatrix() * vec4(pos, 1.0)).xyz;
	vec4 world = uModel * vec4(pos, 1.0);
	if (uWindStrength > 0.0)
		world.xyz += WindOffset(aPos, world.xyz, uModel[3].xyz);
	gl_Position = uLightVP * world;
}
)GLSL";

	inline constexpr const char* ShadowFS = R"GLSL(#version 450 core
void main() {}
)GLSL";

	inline constexpr const char* PBRVS = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
uniform mat4 uModel;
uniform mat4 uViewProj;
layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size (m), texel size (m)
uniform float uTrailDepth;     // 0 = this material ignores ground marks
uniform vec3 uTrailTint;
uniform float uTrailBoost;
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(std430, binding = 0) readonly buffer JointBuffer { mat4 uJointMatrices[]; };
uniform int uSkinned;
mat4 SkinMatrix()
{
	return aWeights.x * uJointMatrices[aJoints.x] + aWeights.y * uJointMatrices[aJoints.y]
		+ aWeights.z * uJointMatrices[aJoints.z] + aWeights.w * uJointMatrices[aJoints.w];
}
uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
out vec3 vPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
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
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
	vec3 nrm = aNormal;
	vec3 tng = aTangent.xyz;
	if (uSkinned != 0)
	{
		mat4 skin = SkinMatrix();
		pos = (skin * vec4(pos, 1.0)).xyz;
		nrm = mat3(skin) * nrm;
		tng = mat3(skin) * tng;
	}
	vec4 world = uModel * vec4(pos, 1.0);
	if (uWindStrength > 0.0)
		world.xyz += WindOffset(aPos, world.xyz, uModel[3].xyz);
	if (uTrailDepth > 0.0)
		world.y -= TrailAt(world.xz) * uTrailDepth;   // footprints, tracks
	mat3 normalMatrix = mat3(transpose(inverse(uModel)));
	vPos = world.xyz;
	vNormal = normalMatrix * nrm;
	vTangent = vec4(mat3(uModel) * tng, aTangent.w);
	vUV = aUV * uUVScale;
	gl_Position = uViewProj * world;
}
)GLSL";

	// Procedural sky shared by the background and by image based lighting.
	inline constexpr const char* SkyCommon = R"GLSL(
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
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

	inline constexpr const char* SkyFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outColor;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uSunDir;     // direction the light travels
uniform vec3 uSunColor;   // color * intensity
uniform float uEnvIntensity;
uniform float uStars;
uniform float uCloudCover;
uniform float uTime;
uniform vec3 uWind;
)GLSL";

	inline constexpr const char* SkyMain = R"GLSL(

float SHash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float SHash3(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float SNoise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(SHash(i), SHash(i + vec2(1, 0)), f.x), mix(SHash(i + vec2(0, 1)), SHash(i + vec2(1, 1)), f.x), f.y);
}
float Fbm(vec2 p)
{
	float v = 0.0, a = 0.5;
	for (int i = 0; i < 5; ++i) { v += a * SNoise(p); p = p * 2.03 + 11.7; a *= 0.5; }
	return v;
}
// Cloud density along a direction (plane projection, drifting with the wind).
float Clouds(vec3 d, out float lit)
{
	lit = 1.0;
	if (d.y < 0.02) return 0.0;
	vec2 p = d.xz / (d.y + 0.15) * 1.6 + uWind.xz * uTime * 0.004;
	float n = Fbm(p);
	float cover = clamp(uCloudCover, 0.0, 1.0);
	float dens = smoothstep(1.0 - cover * 0.9 - 0.12, 1.0 - cover * 0.9 + 0.22, n * 1.25);
	// brighter on the sun side: shift the sample toward the sun and compare
	vec2 toSun = normalize(-uSunDir.xz + 1e-4);
	float n2 = Fbm(p + toSun * 0.12);
	lit = clamp(0.55 + (n - n2) * 5.0, 0.15, 1.0);
	return dens * smoothstep(0.02, 0.2, d.y);
}
void main()
{
	vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
	vec3 d = normalize(w.xyz / w.w - uCamPos);
	vec3 col = SkyColor(d) * uEnvIntensity;
	// Stars (and a moon) where it is dark.
	if (uStars > 0.0 && d.y > -0.05)
	{
		vec3 cell = floor(d * 190.0);
		float h = SHash3(cell);
		vec3 local = fract(d * 190.0) - (0.25 + 0.5 * vec3(SHash3(cell + 3.1), SHash3(cell + 5.7), SHash3(cell + 9.3)));
		float star = step(0.985, h) * smoothstep(0.22, 0.0, length(local)) * smoothstep(0.0, 0.25, d.y + 0.05);
		float tw = 0.7 + 0.3 * sin(uTime * (2.0 + h * 5.0) + h * 40.0);
		col += vec3(0.85, 0.9, 1.0) * star * tw * (0.5 + 3.0 * SHash3(cell + 7.0)) * uStars;
		float band = exp(-pow(dot(d, normalize(vec3(0.3, 0.9, 0.35))) * 3.0, 2.0)) * (0.5 + 0.5 * SNoise(d.xz * 9.0 / (d.y + 0.4)));
		col += vec3(0.05, 0.06, 0.09) * band * uStars;
	}
	float sd = max(dot(d, -uSunDir), 0.0);
	if (uStars > 0.0)
	{
		float moon = smoothstep(0.99955, 0.99975, sd);
		vec3 moonCol = vec3(0.85, 0.9, 1.0) * (0.6 + 0.4 * SNoise(d.xz * 400.0 / (abs(d.y) + 0.2)));
		col += moonCol * moon * 3.0 * uStars + vec3(0.25, 0.3, 0.45) * pow(sd, 90.0) * 0.25 * uStars;
	}
	float cl = 0.0, lit = 1.0;
	if (uCloudCover > 0.0)
	{
		cl = Clouds(d, lit);
		vec3 cloudCol = (SkyIrradiance(vec3(0, 1, 0)) * 0.9 + uSunColor * 0.045 * lit + uSkyHorizon * 0.3) * uEnvIntensity;
		cloudCol = mix(cloudCol, cloudCol * 0.45, clamp(uCloudCover - 0.6, 0.0, 0.4) * 2.0);
		col = mix(col, cloudCol, cl * 0.92);
	}
	float fogAmount = clamp(uFogDensity * 30.0, 0.0, 1.0);
	col = mix(col, uFogColor, fogAmount * (1.0 - smoothstep(0.0, 0.6, d.y)));
	float s = sd;
	col += uSunColor * (pow(s, 1500.0) * 40.0 + pow(s, 24.0) * 0.06) * (1.0 - fogAmount * 0.85) * (1.0 - cl * 0.85);
	outColor = vec4(col, 1.0);
}
)GLSL";

	inline constexpr const char* PBRFS = R"GLSL(#version 450 core
in vec3 vPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
out vec4 outColor;

uniform vec3 uCamPos;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasHeightMap;       // height doubles as a cavity/AO term
uniform float uNormalStrength;
uniform vec2 uNormalFlip;        // per-axis sign: y = -1 for DirectX style maps
uniform float uCavityStrength;
layout(binding = 1) uniform sampler2D uAlbedoMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uHeightMap;
layout(binding = 4) uniform sampler2D uMRMap;
layout(binding = 5) uniform sampler2D uOcclusionMap;
layout(binding = 6) uniform sampler2D uEmissiveMap;
layout(binding = 7) uniform sampler2D uMacroMap;
uniform int uHasMacroMap;
uniform float uGrain;
uniform float uGrainScale;
uniform float uMoss;
uniform vec3 uMossColor;
uniform float uSand;
uniform vec3 uSandColor;
uniform float uBaseY;
uniform float uWaterLine;
uniform vec4 uMacroRect;
layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size (m), texel size (m)
uniform float uTrailDepth;     // 0 = this material ignores ground marks
uniform vec3 uTrailTint;
uniform float uTrailBoost;
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}

uniform int uHasMRMap;
uniform int uHasOcclusionMap;
uniform int uHasEmissiveMap;
uniform float uOcclusionStrength;
uniform int uAlphaMode;          // 0 opaque, 1 mask, 2 blend
uniform float uAlphaCutoff;
uniform float uOpacity;
uniform int uNoNormalFlip;       // foliage: cards use crown normals on both faces
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3 uEmissive;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform int uPointCount;
uniform vec3 uPointPos[32];
uniform vec3 uPointColor[32];
uniform float uPointRadius[32];

uniform int uShadowsOn;
uniform mat4 uLightVP;
uniform float uShadowTexel;   // world-space size of one shadow map texel
layout(binding = 0) uniform sampler2DShadow uShadowMap;

uniform float uEnvIntensity;

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

uniform float uTime;
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

void main()
{
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCamPos - vPos);
	if (!gl_FrontFacing && uNoNormalFlip == 0) N = -N;   // back faces of double-sided materials
	float trailT = 0.0;
	if (uTrailDepth > 0.0)
	{
		// The surface is lower where the map is high: its normal tilts toward the dent's rim.
		float e = uTrailRect.w;
		trailT = TrailAt(vPos.xz);
		vec2 grad = vec2(TrailAt(vPos.xz + vec2(e, 0.0)) - TrailAt(vPos.xz - vec2(e, 0.0)),
		                 TrailAt(vPos.xz + vec2(0.0, e)) - TrailAt(vPos.xz - vec2(0.0, e))) / (2.0 * e);
		N = normalize(N + vec3(grad.x, 0.0, grad.y) * uTrailDepth * uTrailBoost);
	}

	if (uHasNormalMap != 0)
	{
		vec3 tn = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
		tn.xy *= uNormalFlip;
		tn.xy *= uNormalStrength;
		vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
		vec3 B = cross(N, T) * vTangent.w;
		N = normalize(mat3(T, B, N) * normalize(tn));
	}

	vec3 albedo = uAlbedo;
	float alpha = uOpacity;
	if (uHasAlbedoMap != 0)
	{
		vec4 t = texture(uAlbedoMap, vUV);
		albedo *= t.rgb;
		alpha *= t.a;
	}
	if (uHasMacroMap != 0)
		albedo *= texture(uMacroMap, (vPos.xz - uMacroRect.xy) / uMacroRect.zw).rgb;
	albedo *= mix(vec3(1.0), uTrailTint, clamp(trailT * 1.3, 0.0, 1.0));   // compacted / wetter ground
	if (uAlphaMode == 1 && alpha < uAlphaCutoff) discard;
	// Procedural surface layers: grain, moss on top, sand at the base, a wet band at the water line.
	float mossMask = 0.0, sandMask = 0.0, bandWet = 0.0;
	if (uGrain > 0.0 || uMoss > 0.0 || uSand > 0.0 || uWaterLine > -1e8)
	{
		vec3 gp = vPos * uGrainScale;
		float g1 = FBM3(gp), g2 = FBM3(vPos * 11.0 + 5.0);
		albedo *= mix(1.0, 0.55 + 0.9 * g1, uGrain);
		if (uGrain > 0.0)
		{
			float e = 0.05;
			vec3 grad = vec3(FBM3(gp + vec3(e, 0, 0)) - g1, FBM3(gp + vec3(0, e, 0)) - g1, FBM3(gp + vec3(0, 0, e)) - g1) / e;
			N = normalize(N - (grad - N * dot(grad, N)) * 0.06 * uGrain);
		}
		float up = clamp(normalize(vNormal).y, 0.0, 1.0);
		if (uMoss > 0.0)
		{
			float v = up * 0.55 + FBM3(vPos * 1.9 + 3.0) * 0.7 + (uMoss - 0.5) * 0.9 - 0.45;
			mossMask = smoothstep(0.3, 0.55, v) * smoothstep(0.0, 0.12, uMoss);
			albedo = mix(albedo, uMossColor * (0.55 + 0.9 * g2), mossMask);
		}
		if (uSand > 0.0)
		{
			float h = vPos.y - uBaseY;
			sandMask = (1.0 - smoothstep(0.0, 0.5, h + (g2 - 0.5) * 0.3)) * uSand * (0.55 + 0.45 * up);
			albedo = mix(albedo, uSandColor * (0.85 + 0.3 * g2), sandMask);
		}
		if (uWaterLine > -1e8)
		{
			bandWet = 1.0 - smoothstep(uWaterLine, uWaterLine + 0.3, vPos.y + (g2 - 0.5) * 0.1);
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
		N = normalize(N + vec3(PuddleRings(vPos.xz).x, 0.0, PuddleRings(vPos.xz).y) * 0.6 * puddle * smoothstep(0.4, 0.9, uWetness));
		albedo *= mix(1.0, 0.55, puddle);
	}
	float ao = uAO;
	if (uHasOcclusionMap != 0)
		ao *= mix(1.0, texture(uOcclusionMap, vUV).r, uOcclusionStrength);
	if (uHasHeightMap != 0)
	{
		float h = texture(uHeightMap, vUV).r;
		ao *= mix(1.0, clamp(h * 1.4, 0.0, 1.0), uCavityStrength);
	}
	float metallic = uMetallic;
	float roughness = uRoughness;
	if (uHasMRMap != 0)
	{
		vec3 mr = texture(uMRMap, vUV).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
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

	vec3 Lo = vec3(0.0);

	vec3 Ls = normalize(-uSunDir);
	Lo += Shade(N, V, Ls, uSunColor, albedo, F0, a, metallic) * Shadow(vPos, N, Ls);

	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPos[i] - vPos;
		float d2 = dot(toLight, toLight);
		vec3 L = toLight * inversesqrt(d2);
		float window = 1.0;
		if (uPointRadius[i] > 0.0)
		{
			float r = d2 / (uPointRadius[i] * uPointRadius[i]);
			window = clamp(1.0 - r * r, 0.0, 1.0);
			window *= window;
		}
		Lo += Shade(N, V, L, uPointColor[i] * (window / (d2 + 0.01)), albedo, F0, a, metallic);
	}

	float NoV = max(dot(N, V), 1e-4);
	vec3 R = reflect(-V, N);
	vec3 F = EnvBRDFApprox(F0, roughness, NoV);
	vec3 kd = (1.0 - F) * (1.0 - metallic);
	vec3 ambientDiffuse = kd * albedo * SkyIrradiance(N);
	vec3 ambientSpec = SkyPrefiltered(R, roughness) * F;
	// Horizon occlusion: reflections under the surface plane fade out.
	float horizon = clamp(1.0 + dot(R, N) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 emissive = uEmissive;
	if (uHasEmissiveMap != 0) emissive *= texture(uEmissiveMap, vUV).rgb;
	vec3 color = Lo + ambient + emissive;
	if (uFogDensity > 0.0)
	{
		float dist = length(vPos - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		color = mix(uFogColor * uEnvIntensity, color, exp(-density * density * dist * dist));
	}
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), uAlphaMode == 2 ? alpha : 1.0);
}
)GLSL";

	inline constexpr const char* BloomDownFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uSrc;
uniform vec2 uTexel;       // 1 / source size
uniform int uPrefilter;
uniform float uThreshold;
uniform float uKnee;

vec3 Prefilter(vec3 c)
{
	if (uPrefilter == 0) return c;
	c = clamp(c, vec3(0.0), vec3(32.0));
	float brightness = max(c.r, max(c.g, c.b));
	float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
	soft = soft * soft / (4.0 * uKnee + 1e-4);
	float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-4);
	return c * contribution;
}

vec3 T(vec2 o) { return Prefilter(texture(uSrc, vUV + o * uTexel).rgb); }

void main()
{
	vec3 a = T(vec2(-2, 2)), b = T(vec2(0, 2)), c = T(vec2(2, 2));
	vec3 d = T(vec2(-2, 0)), e = T(vec2(0, 0)), f = T(vec2(2, 0));
	vec3 g = T(vec2(-2, -2)), h = T(vec2(0, -2)), i = T(vec2(2, -2));
	vec3 j = T(vec2(-1, 1)), k = T(vec2(1, 1)), l = T(vec2(-1, -1)), m = T(vec2(1, -1));
	vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	outColor = vec4(col, 1.0);
}
)GLSL";

	inline constexpr const char* BloomUpFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uSrc;
uniform vec2 uTexel;       // 1 / source size
uniform float uRadius;

void main()
{
	vec2 o = uTexel * uRadius;
	vec3 col = texture(uSrc, vUV + vec2(-o.x, o.y)).rgb + texture(uSrc, vUV + vec2(0, o.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(o.x, o.y)).rgb;
	col += texture(uSrc, vUV + vec2(-o.x, 0)).rgb * 2.0 + texture(uSrc, vUV).rgb * 4.0 + texture(uSrc, vUV + vec2(o.x, 0)).rgb * 2.0;
	col += texture(uSrc, vUV + vec2(-o.x, -o.y)).rgb + texture(uSrc, vUV + vec2(0, -o.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(o.x, -o.y)).rgb;
	outColor = vec4(col / 16.0, 1.0);
}
)GLSL";

	inline constexpr const char* CompositeFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1) uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uVignette;

vec3 ACES(vec3 x)
{
	const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 LinearToSRGB(vec3 c)
{
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main()
{
	vec3 hdr = max(texture(uScene, vUV).rgb + texture(uBloom, vUV).rgb * uBloomIntensity, vec3(0.0));
	vec3 ldr = ACES(hdr * uExposure);
	vec2 q = vUV - 0.5;
	ldr *= 1.0 - uVignette * dot(q, q) * 1.6;
	outColor = vec4(LinearToSRGB(ldr), 1.0);
}
)GLSL";

	inline constexpr const char* ParticleVS = R"GLSL(#version 450 core
layout(location = 0) in vec4 aPosSize;     // xyz position, w size
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aVelRot;      // xyz velocity, w rotation
layout(location = 3) in vec4 aFrame;       // x = flipbook frame
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uStretch;
uniform vec2 uSheet;                       // columns, rows
out vec2 vUV;
out vec4 vColor;
out vec3 vWorld;
void main()
{
	// Triangle strip corners: (-1,-1) (1,-1) (-1,1) (1,1)
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	float size = aPosSize.w;
	vec2 offset;
	vec2 v = vec2(dot(aVelRot.xyz, uCamRight), dot(aVelRot.xyz, uCamUp));
	float speed = length(v);
	if (uStretch > 0.0 && speed > 1e-4)
	{
		vec2 dir = v / speed;
		vec2 perp = vec2(-dir.y, dir.x);
		offset = dir * corner.x * size * (1.0 + uStretch * speed) + perp * corner.y * size;
	}
	else
	{
		float c = cos(aVelRot.w), s = sin(aVelRot.w);
		offset = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y) * size;
	}
	vec3 world = aPosSize.xyz + uCamRight * offset.x + uCamUp * offset.y;
	gl_Position = uViewProj * vec4(world, 1.0);
	vWorld = world;

	vec2 uv = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);   // top-left origin
	float frame = aFrame.x;
	float col = mod(frame, uSheet.x);
	float row = floor(frame / uSheet.x);
	vUV = (vec2(col, row) + uv) / uSheet;
	vColor = aColor;
}
)GLSL";

	inline constexpr const char* ParticleFS = R"GLSL(#version 450 core
in vec2 vUV;
in vec4 vColor;
in vec3 vWorld;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uSprite;
layout(binding = 1) uniform sampler2D uSceneDepth;
uniform float uSoft;            // soft particle fade distance (world units), 0 = off
uniform vec2 uNearFar;
uniform int uOrtho;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;

float LinearDepth(float d)
{
	if (uOrtho != 0) return uNearFar.x + d * (uNearFar.y - uNearFar.x);
	float z = d * 2.0 - 1.0;
	return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}

void main()
{
	vec4 t = texture(uSprite, vUV);
	float a = vColor.a * t.a;
	if (uSoft > 0.0)
	{
		vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneDepth, 0));
		float diff = LinearDepth(texture(uSceneDepth, uv).r) - LinearDepth(gl_FragCoord.z);
		a *= clamp(diff / uSoft, 0.0, 1.0);
	}
	if (uFogDensity > 0.0)
	{
		float dist = length(vWorld - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vWorld.y, 0.0));
		a *= exp(-density * density * dist * dist);   // distant particles melt into the fog
	}
	outColor = vec4(vColor.rgb * t.rgb, a);
}
)GLSL";

	// Shared by the water VS and FS: the same four Gerstner waves as WaterSurface::HeightAt.
	inline constexpr const char* WaterWaves = R"GLSL(
uniform float uTime;
uniform vec2 uWaveDir;
uniform float uWaveHeight;
uniform float uWaveLength;
uniform float uSteep;
uniform float uChop;

const float kAngle[4] = float[4](0.0, 0.42, -0.58, 0.95);
const float kLen[4] = float[4](1.0, 0.62, 0.38, 0.24);
const float kAmp[4] = float[4](1.0, 0.55, 0.30, 0.16);

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

	inline constexpr const char* WaterVS = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aTangent;   // rivers: xy = flow direction, z = speed
uniform mat4 uViewProj;
uniform vec3 uOrigin;
uniform vec3 uCamPos;
uniform int uRiver;
out vec3 vPos;
out vec2 vFlow;
)GLSL";

	inline constexpr const char* WaterVSMain = R"GLSL(
void main()
{
	vec3 world = aPos + uOrigin;
	float fade = 1.0 - smoothstep(30.0, 120.0, length(world.xz - uCamPos.xz));
	vec2 slope;
	vec3 w = Waves(world.xz, fade, slope);
	world += vec3(w.x, w.y, w.z);
	vPos = world;
	vFlow = uRiver != 0 ? aTangent.xy * aTangent.z : vec2(0.0);
	gl_Position = uViewProj * vec4(world, 1.0);
}
)GLSL";

	inline constexpr const char* WaterFS = R"GLSL(#version 450 core
in vec3 vPos;
in vec2 vFlow;
out vec4 outColor;
layout(binding = 1) uniform sampler2D uSceneDepth;
layout(binding = 2) uniform sampler2D uRipples;
layout(binding = 3) uniform sampler2D uSceneColor;   // opaque scene so far (refraction)
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uEnvIntensity;
uniform vec2 uNearFar;
uniform int uOrtho;
uniform vec3 uShallow;
uniform vec3 uDeep;
uniform float uClarity;
uniform float uRoughness;
uniform float uFoam;
uniform float uFoamWidth;
uniform float uRippleStrength;
uniform vec4 uRippleRect;     // window center x, z, size, texel size (size 0 = off)
uniform mat4 uViewProj;
uniform int uPointCount;
uniform vec3 uPointPos[32];
uniform vec3 uPointColor[32];
uniform float uPointRadius[32];
uniform float uSSR;           // 0 = sky only, 1 = screen-space reflections
uniform float uOriginY;
uniform vec3 uOriginXZ;     // water center
uniform float uMask;          // > 0: discard beyond this radius
)GLSL";

	inline constexpr const char* WaterFSMain = R"GLSL(
float LinearDepth(float d)
{
	if (uOrtho != 0) return uNearFar.x + d * (uNearFar.y - uNearFar.x);
	float z = d * 2.0 - 1.0;
	return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}

// Screen-space reflection: march the reflected ray through the depth buffer. Returns rgb + confidence.
vec4 TraceReflection(vec3 origin, vec3 dir, float jitter)
{
	float stepLen = 0.35 + 0.002 * length(origin - uCamPos);
	vec3 p = origin + dir * stepLen * jitter;
	for (int i = 0; i < 40; ++i)
	{
		p += dir * stepLen;
		stepLen *= 1.12;
		vec4 clip = uViewProj * vec4(p, 1.0);
		if (clip.w <= 0.0) break;
		vec3 ndc = clip.xyz / clip.w;
		vec2 uv = ndc.xy * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
		float sceneZ = LinearDepth(texture(uSceneDepth, uv).r);
		float rayZ = LinearDepth(ndc.z * 0.5 + 0.5);
		float diff = rayZ - sceneZ;
		if (diff > 0.0 && diff < stepLen * 2.5 + 0.3)
		{
			// refine between the last two samples
			vec3 a = p - dir * stepLen / 1.12, b = p;
			for (int k = 0; k < 5; ++k)
			{
				vec3 m = (a + b) * 0.5;
				vec4 cm = uViewProj * vec4(m, 1.0);
				vec2 uvm = cm.xy / cm.w * 0.5 + 0.5;
				float z = LinearDepth(texture(uSceneDepth, uvm).r);
				if (LinearDepth(cm.z / cm.w * 0.5 + 0.5) > z) b = m; else a = m;
			}
			vec4 ch = uViewProj * vec4(b, 1.0);
			vec2 uvh = ch.xy / ch.w * 0.5 + 0.5;
			if (LinearDepth(texture(uSceneDepth, uvh).r) > uNearFar.y * 0.95) return vec4(0.0);   // that is the sky: use the environment instead
			float edge = smoothstep(0.0, 0.08, min(min(uvh.x, 1.0 - uvh.x), min(uvh.y, 1.0 - uvh.y)));
			return vec4(texture(uSceneColor, uvh).rgb, edge * (1.0 - float(i) / 40.0));
		}
	}
	return vec4(0.0);
}

float Hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float Noise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1, 0)), f.x), mix(Hash(i + vec2(0, 1)), Hash(i + vec2(1, 1)), f.x), f.y);
}

vec2 Detail(vec2 q, float t)
{
	vec2 n1 = vec2(Noise(q * 1.7 + vec2(t * 0.35, t * 0.2)), Noise(q * 1.7 + 17.0 + vec2(-t * 0.25, t * 0.3))) - 0.5;
	vec2 n2 = vec2(Noise(q * 4.3 + vec2(-t * 0.5, t * 0.4)), Noise(q * 4.3 + 31.0 + vec2(t * 0.45, -t * 0.35))) - 0.5;
	return n1 * 0.14 + n2 * 0.07;
}

float RippleAt(vec2 xz)
{
	float edge = max(abs(xz.x - uRippleRect.x), abs(xz.y - uRippleRect.y)) / uRippleRect.z;
	return textureLod(uRipples, xz / uRippleRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}

void main()
{
	if (uMask > 0.0 && length(vPos.xz - uOriginXZ.xz) > uMask)
		discard;
	vec3 V = normalize(uCamPos - vPos);
	float camDist = length(uCamPos - vPos);
	float fade = 1.0 - smoothstep(30.0, 120.0, length(vPos.xz - uCamPos.xz));
	vec2 slope;
	Waves(vPos.xz - vec2(0.0), fade, slope);   // slope of the wave sum (position offsets are tiny for shading)
	vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));

	// Fine detail: two noise layers, fading with distance. Still water scrolls them slowly; a current advects
	// them along the flow (two phases cross-faded so the pattern never visibly resets).
	vec2 q = vPos.xz;
	float t = uTime;
	float speed = length(vFlow);
	vec2 detail;
	if (speed > 0.02)
	{
		float p0 = fract(t * 0.55), p1 = fract(t * 0.55 + 0.5);
		float wgt = abs(2.0 * p0 - 1.0);
		vec2 da = Detail(q - vFlow * p0 * 1.8, 0.0), db = Detail(q - vFlow * p1 * 1.8, 0.0);
		detail = mix(da, db, wgt) * (1.0 + speed * 0.5) * 2.2 * uChop * fade;
	}
	else
		detail = Detail(q, t) * uChop * fade;
	N = normalize(N + vec3(detail.x, 0.0, detail.y));

	// Ripples from rain, feet and swimmers.
	float ripple = 0.0, ringLight = 0.0;
	if (uRippleRect.z > 0.0)
	{
		float e = uRippleRect.w;
		float hL = RippleAt(vPos.xz - vec2(e, 0.0)), hR = RippleAt(vPos.xz + vec2(e, 0.0));
		float hD = RippleAt(vPos.xz - vec2(0.0, e)), hU = RippleAt(vPos.xz + vec2(0.0, e));
		vec2 g = vec2(hR - hL, hU - hD) / (2.0 * e);
		N = normalize(N + vec3(-g.x, 0.0, -g.y) * (uRippleStrength * 4.0));
		ripple = RippleAt(vPos.xz);
		ringLight = clamp(length(g) * uRippleStrength * 5.0, 0.0, 1.0);
	}
	if (dot(N, V) < 0.0) N = reflect(N, vec3(0, 1, 0)); // keeps the horizon from flipping

	// Thickness of the water along the view ray.
	vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneDepth, 0));
	float sceneZ = LinearDepth(texture(uSceneDepth, uv).r);
	float surfZ = LinearDepth(gl_FragCoord.z);
	float thick = max(sceneZ - surfZ, 0.0);
	// Vertical depth is what colors the water; the slanted view ray only changes how hidden the bottom is.
	float vdepth = thick * clamp(V.y, 0.05, 1.0);

	float absorb = 1.0 - exp(-vdepth / max(uClarity, 0.05));
	vec3 body = mix(uShallow, uDeep, pow(absorb, 0.8));
	vec3 L = normalize(-uSunDir);
	vec3 amb = SkyIrradiance(vec3(0, 1, 0)) * uEnvIntensity * 0.55;
	body *= amb + uSunColor * max(dot(vec3(0, 1, 0), L), 0.0) * 0.06;

	// Refraction: look through the surface along a bent ray. Only take samples that lie behind the water
	// (objects in front of the surface must not smear into it); the bend grows with the water depth.
	vec2 bend = N.xz * clamp(vdepth, 0.0, 1.5) * 0.12 / max(surfZ, 1.0) * 6.0;
	vec2 uvR = uv + bend;
	float zR = LinearDepth(texture(uSceneDepth, uvR).r);
	if (zR < surfZ) uvR = uv;
	vec3 seen = texture(uSceneColor, uvR).rgb;
	// Beer-Lambert: red goes first, then green, blue survives longest; what is lost is replaced by scattered body color.
	vec3 T = exp(-vec3(0.9, 0.25, 0.12) * vdepth / max(uClarity, 0.05));
	vec3 transmitted = seen * T + body * (1.0 - dot(T, vec3(0.3333)));

	// Fresnel sky reflection with a sharp sun glint.
	float cosT = clamp(dot(N, V), 0.0, 1.0);
	float F = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);
	F = clamp(F, 0.0, 1.0);
	vec3 R = reflect(-V, N);
	R.y = abs(R.y);
	vec3 refl = SkyPrefiltered(R, uRoughness * 3.0) * uEnvIntensity;
	if (uSSR > 0.0)
	{
		vec4 hit = TraceReflection(vPos + N * 0.02, R, Hash(gl_FragCoord.xy));
		refl = mix(refl, hit.rgb, hit.a * uSSR);
	}
	vec3 H = normalize(L + V);
	float a2 = max(uRoughness * uRoughness, 1e-3);
	float nh = max(dot(N, H), 0.0);
	float spec = min(pow(nh, 2.0 / a2 - 1.0) * (2.0 / a2) * 0.05, 60.0);
	vec3 glint = uSunColor * spec * step(0.0, L.y) * F;
	// Lamps and torches glitter on the surface too.
	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPos[i] - vPos;
		float d2 = dot(toLight, toLight);
		vec3 Lp = toLight * inversesqrt(d2);
		float w = 1.0;
		if (uPointRadius[i] > 0.0) { float r = d2 / (uPointRadius[i] * uPointRadius[i]); w = clamp(1.0 - r * r, 0.0, 1.0); w *= w; }
		float nhp = max(dot(N, normalize(Lp + V)), 0.0);
		float sp = min(pow(nhp, 2.0 / a2 - 1.0) * (2.0 / a2) * 0.05, 40.0);
		glint += uPointColor[i] * (sp * w / (d2 + 0.01)) * F * step(0.0, Lp.y);
	}

	// Foam at the shore, on crests and where ripples are strong.
	float foam = 0.0;
	if (uFoam > 0.0)
	{
		float nz = Noise(q * 3.0 + vec2(t * 0.3, -t * 0.2)) * 0.6 + Noise(q * 9.0 - t * 0.5) * 0.4;
		float shore = 1.0 - smoothstep(0.0, uFoamWidth * (0.6 + 0.8 * nz), vdepth);
		float wave = 0.5 + 0.5 * sin(vdepth * 7.0 - t * 1.6 + nz * 5.0);   // lapping bands
		shore *= mix(0.55, 1.0, wave);
		float crest = smoothstep(0.8, 1.0, (vPos.y - uOriginY) / max(uWaveHeight * 2.2, 1e-3)) * smoothstep(0.45, 0.9, nz);
		float rip = smoothstep(0.015, 0.06, abs(ripple)) * 0.5;
		if (speed > 0.02)
		{
			float p0 = fract(t * 0.55), p1 = fract(t * 0.55 + 0.5);
			float fa = Noise((q - vFlow * p0 * 1.8) * vec2(2.2, 2.2)), fb = Noise((q - vFlow * p1 * 1.8) * 2.2);
			float streak = mix(fa, fb, abs(2.0 * p0 - 1.0));
			rip += smoothstep(1.7, 3.6, speed) * smoothstep(0.55, 0.85, streak) * 0.9;   // white water in the rapids
		}
		foam = clamp((shore + crest * 0.6 + rip) * uFoam, 0.0, 1.0) * smoothstep(0.35, 0.65, nz + shore * 0.5);
	}
	vec3 foamColor = (amb + uSunColor * max(L.y, 0.0) * 0.1) * vec3(1.6);

	vec3 color = mix(transmitted, refl, F) + glint;
	color += (uSkyHorizon * uEnvIntensity * 0.8 + 0.06) * ringLight * 0.8;   // rings catch the light of the sky
	color = mix(color, foamColor, foam);
	float alpha = smoothstep(0.0, 0.05, thick);   // no hard edge where the water meets the shore
	alpha = max(alpha, foam * smoothstep(0.0, 0.01, thick));

	if (uFogDensity > 0.0)
	{
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		color = mix(uFogColor * uEnvIntensity, color, exp(-density * density * camDist * camDist));
	}
	outColor = vec4(min(color, vec3(64.0)), alpha);
}
)GLSL";

	// Shared by the grass VS/FS: ground-mark lookup (same map the terrain uses).
	inline constexpr const char* GrassTrailCommon = R"GLSL(
layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size, texel size
uniform float uTrailBend;      // 0 = ignore marks
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}
)GLSL";

	inline constexpr const char* GrassVS = R"GLSL(#version 450 core
layout(location = 0) in vec4 aPosYaw;   // x, y, z of the root, yaw
layout(location = 1) in vec4 aShape;    // height, width, seed, lean
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uWind;
uniform float uDrawDistance;
uniform float uSnowCover;
out vec3 vWorld;
out float vT;
out float vSeed;
out vec3 vNormal;
out float vSide;
const int SEGMENTS = 4;
)GLSL";

	inline constexpr const char* GrassVSMain = R"GLSL(
void main()
{
	int seg = gl_VertexID >> 1;
	float side = float(gl_VertexID & 1) * 2.0 - 1.0;
	float t = float(seg) / float(SEGMENTS);
	vec3 root = aPosYaw.xyz;
	float yaw = aPosYaw.w;
	float height = aShape.x, width = aShape.y, seed = aShape.z, lean = aShape.w;

	// Far away, blades thin out (the survivors get wider so the ground stays covered) and then vanish.
	float dist = length(root - uCamPos);
	float fade = smoothstep(0.5, 1.0, dist / uDrawDistance);
	if (seed < fade)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		vWorld = root; vT = 0.0; vSeed = seed; vNormal = vec3(0, 1, 0); vSide = 0.0;
		return;
	}
	width *= 1.0 + dist * 0.03;

	// Ground marks: footprints flatten the grass and push it away from the dent.
	float trail = 0.0;
	vec2 push = vec2(0.0);
	if (uTrailBend > 0.0 && uTrailRect.z > 0.0)
	{
		float e = max(uTrailRect.w, 0.05);
		trail = TrailAt(root.xz);
		vec2 grad = vec2(TrailAt(root.xz + vec2(e, 0.0)) - TrailAt(root.xz - vec2(e, 0.0)),
		                 TrailAt(root.xz + vec2(0.0, e)) - TrailAt(root.xz - vec2(0.0, e)));
		float gl = length(grad);
		push = gl > 1e-5 ? -grad / gl : vec2(0.0);
		trail = clamp(trail * uTrailBend * 1.4, 0.0, 1.0);
	}

	float h = height * (1.0 - 0.7 * uSnowCover) * (1.0 - 0.8 * trail);
	vec3 bladeRight = vec3(cos(yaw), 0.0, sin(yaw));
	vec3 forward = vec3(-sin(yaw), 0.0, cos(yaw));

	// Wind: a travelling wave plus flutter. Weak at the root, strong at the tip.
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(root.xz, wd) * 0.32 - uTime * (1.2 + 0.18 * ws) + seed * 6.2831;
	float gust = 0.55 + 0.45 * sin(phase) + 0.25 * sin(phase * 2.7 + 1.3);
	float flutter = sin(uTime * 6.5 + seed * 40.0 + root.x * 1.7) * 0.12;
	float bend = (0.05 + 0.055 * ws) * gust + flutter * 0.06 * (0.4 + ws * 0.2);
	vec3 windOffset = vec3(wd.x, 0.0, wd.y) * bend * h;

	float t2 = t * t;
	vec3 pos = root + vec3(0.0, h * t, 0.0) + forward * (lean * h * t2) + windOffset * t2
	         + vec3(push.x, 0.0, push.y) * trail * h * 0.7 * t2;

	// Ribbon turned partly toward the camera so blades do not vanish edge-on.
	vec3 toCam = normalize(vec3(uCamPos.x - root.x, 0.0, uCamPos.z - root.z) + vec3(1e-4));
	vec3 camRight = normalize(cross(vec3(0.0, 1.0, 0.0), toCam));
	if (dot(camRight, bladeRight) < 0.0) camRight = -camRight;
	vec3 right = normalize(mix(bladeRight, camRight, 0.45));
	float halfWidth = 0.5 * width * (1.0 - pow(t, 1.4));
	pos += right * side * halfWidth;

	vec3 tangent = normalize(vec3(0.0, h, 0.0) + forward * (2.0 * lean * h * t) + windOffset * 2.0 * t);
	vWorld = pos;
	vT = t;
	vSeed = seed;
	vSide = side;
	vNormal = normalize(cross(right, tangent));
	gl_Position = uViewProj * vec4(pos, 1.0);
}
)GLSL";

	inline constexpr const char* GrassFS = R"GLSL(#version 450 core
in vec3 vWorld;
in float vT;
in float vSeed;
in vec3 vNormal;
in float vSide;
out vec4 outColor;
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uEnvIntensity;
uniform int uShadowsOn;
uniform mat4 uLightVP;
uniform float uShadowTexel;
layout(binding = 0) uniform sampler2DShadow uShadowMap;
uniform vec3 uBase;
uniform vec3 uTip;
uniform vec3 uDry;
uniform float uDryAmount;
)GLSL";

	inline constexpr const char* GrassFSMain = R"GLSL(
float GrassShadow(vec3 p)
{
	if (uShadowsOn == 0) return 1.0;
	vec4 lp = uLightVP * vec4(p + vec3(0.0, uShadowTexel, 0.0), 1.0);
	vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
	if (c.z > 1.0 || c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0) return 1.0;
	c.z -= 0.0006;
	vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
	float sum = 0.0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			sum += texture(uShadowMap, vec3(c.xy + vec2(x, y) * texel, c.z));
	return sum / 9.0;
}

void main()
{
	vec3 N = normalize(vNormal);
	if (!gl_FrontFacing) N = -N;
	N = normalize(mix(vec3(0.0, 1.0, 0.0), N, 0.55));   // fields shade like a soft carpet

	float variation = 0.78 + 0.44 * fract(vSeed * 13.7);
	vec3 col = mix(uBase, uTip, pow(vT, 0.8)) * variation;
	float dry = step(1.0 - uDryAmount, fract(vSeed * 7.31));
	col = mix(col, uDry * (0.55 + 0.45 * vT), dry * 0.85);
	col = mix(col, vec3(0.9, 0.93, 0.96), uSnowCover * 0.85 * smoothstep(0.05, 0.8, vT));
	col *= 1.0 - 0.3 * uWetness;

	vec3 L = normalize(-uSunDir);
	vec3 V = normalize(uCamPos - vWorld);
	float wrap = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
	wrap *= wrap;
	float trans = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * 0.55 * vT;   // sun shining through the blades
	float shadow = GrassShadow(vWorld);
	vec3 direct = uSunColor * (wrap * 0.9 + trans) * shadow / 3.14159;
	vec3 ambient = SkyIrradiance(N) * uEnvIntensity * (0.4 + 0.6 * vT);   // darker at the root (occlusion)
	vec3 color = col * (direct + ambient);

	if (uFogDensity > 0.0)
	{
		float dist = length(vWorld - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vWorld.y, 0.0));
		color = mix(uFogColor * uEnvIntensity, color, exp(-density * density * dist * dist));
	}
	float alpha = 1.0 - smoothstep(0.7, 1.0, abs(vSide));   // soft edges, resolved by alpha-to-coverage
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), alpha);
}
)GLSL";
}
