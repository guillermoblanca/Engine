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
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
	if (uSkinned != 0)
		pos = (SkinMatrix() * vec4(pos, 1.0)).xyz;
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
	float fogAmount = clamp(uFogDensity * 30.0, 0.0, 1.0);
	col = mix(col, uFogColor, fogAmount * (1.0 - smoothstep(0.0, 0.6, d.y)));
	float s = max(dot(d, -uSunDir), 0.0);
	col += uSunColor * (pow(s, 1500.0) * 40.0 + pow(s, 24.0) * 0.06) * (1.0 - fogAmount * 0.85);
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
	if (!gl_FrontFacing) N = -N;   // back faces of double-sided materials
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
	roughness = clamp(roughness, 0.045, 1.0);
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
}
