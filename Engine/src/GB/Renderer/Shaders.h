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
uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
	gl_Position = uLightVP * uModel * vec4(pos, 1.0);
}
)GLSL";

	inline constexpr const char* ShadowFS = R"GLSL(#version 450 core
void main() {}
)GLSL";

	inline constexpr const char* PBRVS = R"GLSL(#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
<<<<<<< HEAD
layout(location = 3) in vec4 aTangent;
=======
layout(location = 3) in vec3 aTangent;
>>>>>>> origin/master
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
out vec3 vPos;
out vec3 vNormal;
<<<<<<< HEAD
out vec4 vTangent;
=======
out vec3 vTangent;
>>>>>>> origin/master
out vec2 vUV;
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
	vec4 world = uModel * vec4(pos, 1.0);
	mat3 normalMatrix = mat3(transpose(inverse(uModel)));
	vPos = world.xyz;
	vNormal = normalMatrix * aNormal;
<<<<<<< HEAD
	vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
=======
	vTangent = mat3(uModel) * aTangent;
>>>>>>> origin/master
	vUV = aUV * uUVScale;
	gl_Position = uViewProj * world;
}
)GLSL";

	// Procedural sky shared by the background and by image based lighting.
	inline constexpr const char* SkyCommon = R"GLSL(
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;

vec3 SkyColor(vec3 d)
{
	float t = clamp(d.y, -1.0, 1.0);
	vec3 up = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
	vec3 down = mix(uSkyHorizon, uSkyGround, pow(max(-t, 0.0), 0.35));
	return t >= 0.0 ? up : down;
}

// Cheap prefiltered reflection: rough surfaces see a blurred, averaged sky.
vec3 SkyPrefiltered(vec3 d, float roughness)
{
	vec3 avg = (uSkyTop + uSkyHorizon * 2.0 + uSkyGround) * 0.25;
	return mix(SkyColor(d), avg, roughness * roughness);
}

vec3 SkyIrradiance(vec3 n)
{
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
)GLSL";

	inline constexpr const char* SkyMain = R"GLSL(
void main()
{
	vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
	vec3 d = normalize(w.xyz / w.w - uCamPos);
	vec3 col = SkyColor(d) * uEnvIntensity;
	float s = max(dot(d, -uSunDir), 0.0);
	col += uSunColor * (pow(s, 1500.0) * 40.0 + pow(s, 24.0) * 0.06);
	outColor = vec4(col, 1.0);
}
)GLSL";

	inline constexpr const char* PBRFS = R"GLSL(#version 450 core
in vec3 vPos;
in vec3 vNormal;
<<<<<<< HEAD
in vec4 vTangent;
=======
in vec3 vTangent;
>>>>>>> origin/master
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
<<<<<<< HEAD
layout(binding = 4) uniform sampler2D uMRMap;
layout(binding = 5) uniform sampler2D uOcclusionMap;
layout(binding = 6) uniform sampler2D uEmissiveMap;
uniform int uHasMRMap;
uniform int uHasOcclusionMap;
uniform int uHasEmissiveMap;
uniform float uOcclusionStrength;
uniform int uAlphaMode;          // 0 opaque, 1 mask, 2 blend
uniform float uAlphaCutoff;
uniform float uOpacity;
=======
>>>>>>> origin/master
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3 uEmissive;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform int uPointCount;
uniform vec3 uPointPos[8];
uniform vec3 uPointColor[8];

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

void main()
{
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCamPos - vPos);
<<<<<<< HEAD
	if (!gl_FrontFacing) N = -N;   // back faces of double-sided materials
=======
	if (dot(N, V) < 0.0 && !gl_FrontFacing) N = -N;
	vec3 geometricN = N;
>>>>>>> origin/master

	if (uHasNormalMap != 0)
	{
		vec3 tn = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
		tn.xy *= uNormalFlip;
		tn.xy *= uNormalStrength;
<<<<<<< HEAD
		vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
		vec3 B = cross(N, T) * vTangent.w;
=======
		vec3 T = normalize(vTangent - N * dot(N, vTangent));
		vec3 B = cross(N, T);
>>>>>>> origin/master
		N = normalize(mat3(T, B, N) * normalize(tn));
	}

	vec3 albedo = uAlbedo;
<<<<<<< HEAD
	float alpha = uOpacity;
	if (uHasAlbedoMap != 0)
	{
		vec4 t = texture(uAlbedoMap, vUV);
		albedo *= t.rgb;
		alpha *= t.a;
	}
	if (uAlphaMode == 1 && alpha < uAlphaCutoff) discard;
	float ao = uAO;
	if (uHasOcclusionMap != 0)
		ao *= mix(1.0, texture(uOcclusionMap, vUV).r, uOcclusionStrength);
=======
	if (uHasAlbedoMap != 0) albedo *= texture(uAlbedoMap, vUV).rgb;
	float ao = uAO;
>>>>>>> origin/master
	if (uHasHeightMap != 0)
	{
		float h = texture(uHeightMap, vUV).r;
		ao *= mix(1.0, clamp(h * 1.4, 0.0, 1.0), uCavityStrength);
	}
<<<<<<< HEAD
	float metallic = uMetallic;
	float roughness = uRoughness;
	if (uHasMRMap != 0)
	{
		vec3 mr = texture(uMRMap, vUV).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
	metallic = clamp(metallic, 0.0, 1.0);
	roughness = clamp(roughness, 0.045, 1.0);
=======
	float metallic = clamp(uMetallic, 0.0, 1.0);
	float roughness = clamp(uRoughness, 0.045, 1.0);
>>>>>>> origin/master
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
		Lo += Shade(N, V, L, uPointColor[i] / (d2 + 0.01), albedo, F0, a, metallic);
	}

	float NoV = max(dot(N, V), 1e-4);
	vec3 R = reflect(-V, N);
	vec3 F = EnvBRDFApprox(F0, roughness, NoV);
	vec3 kd = (1.0 - F) * (1.0 - metallic);
	vec3 ambientDiffuse = kd * albedo * SkyIrradiance(N);
	vec3 ambientSpec = SkyPrefiltered(R, roughness) * F;
	// Horizon occlusion: reflections under the surface plane fade out.
<<<<<<< HEAD
	float horizon = clamp(1.0 + dot(R, N) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 emissive = uEmissive;
	if (uHasEmissiveMap != 0) emissive *= texture(uEmissiveMap, vUV).rgb;
	vec3 color = Lo + ambient + emissive;
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), uAlphaMode == 2 ? alpha : 1.0);
=======
	float horizon = clamp(1.0 + dot(R, geometricN) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 color = Lo + ambient + uEmissive;
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), 1.0);
>>>>>>> origin/master
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
}
