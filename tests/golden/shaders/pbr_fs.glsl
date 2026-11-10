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
	vec4 Layer[10];     // the layers' slots (SLOT_*)
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
const uint LAYER_SLOTS = 10u;
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
	}
	if (HasTex(TEX_MACRO))
		albedo *= texture(uMacroMap, (vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw).rgb;
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
	float transmission = (MATERIAL.Flags.x & LAYER_TRANSMISSION) != 0u ? MATERIAL.ModelParams.w : 0.0;
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

	vec3 Ls = normalize(-uSunDir);
	float sunShadow = Shadow(vPos, N, Ls);
	Lo += Shade(N, V, Ls, uSunColor, albedo, F0, a, metallic) * sunShadow;

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
		Lo += Shade(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic);
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
