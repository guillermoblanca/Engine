#pragma once

// Fullscreen passes: bloom, SSAO, auto exposure and the final composite. Assemble with Shaders::Assemble (Shaders.h).

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

	// Screen-space ambient occlusion at half resolution: hemisphere samples around each point, rotated per pixel.
	inline constexpr const char* AOFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outAO;
layout(binding = 0) uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform float uRadius;
uniform float uBias;
uniform int uOrtho;

vec3 ViewPos(vec2 uv)
{
	float d = texture(uDepth, uv).r;
	vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
	vec4 v = uInvProj * clip;
	return v.xyz / v.w;
}

float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main()
{
	float d0 = texture(uDepth, vUV).r;
	if (d0 >= 0.99999) { outAO = vec4(1.0); return; }
	vec3 P = ViewPos(vUV);
	vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
	// normal from the neighbours with the smaller depth difference (avoids smearing across edges)
	vec3 Pr = ViewPos(vUV + vec2(texel.x, 0.0)), Pl = ViewPos(vUV - vec2(texel.x, 0.0));
	vec3 Pu = ViewPos(vUV + vec2(0.0, texel.y)), Pd = ViewPos(vUV - vec2(0.0, texel.y));
	vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
	vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
	vec3 N = normalize(cross(dx, dy));
	if (dot(N, -normalize(P)) < 0.0) N = -N;
	float angle = IGN(gl_FragCoord.xy) * 6.2831853;
	vec3 helper = abs(N.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
	vec3 T = normalize(cross(helper, N)), B = cross(N, T);
	float occ = 0.0;
	const int COUNT = 14;
	for (int i = 0; i < COUNT; ++i)
	{
		float u = (float(i) + 0.5) / float(COUNT);
		float z = u;                                   // uniform in height: more samples near the surface plane
		float phi = float(i) * 2.399963 + angle;
		float r = sqrt(1.0 - z * z);
		vec3 dir = T * (cos(phi) * r) + B * (sin(phi) * r) + N * z;
		float scale = mix(0.15, 1.0, u * u);
		vec3 S = P + dir * uRadius * scale;
		vec4 clip = uProj * vec4(S, 1.0);
		vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
		float sceneZ = ViewPos(uv).z;
		float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
		occ += (sceneZ >= S.z + uBias * (1.0 + abs(P.z) * 0.2) ? 1.0 : 0.0) * range;
	}
	float ao = 1.0 - occ / float(COUNT);
	outAO = vec4(ao * ao * (3.0 - 2.0 * ao) * 0.5 + ao * 0.5, 0.0, 0.0, 1.0);
}
)GLSL";

	inline constexpr const char* AOBlurFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outAO;
layout(binding = 0) uniform sampler2D uAO;
layout(binding = 1) uniform sampler2D uDepth;
uniform vec2 uTexel;       // of the AO texture
uniform vec2 uNearFar;
float Lin(float d) { float z = d * 2.0 - 1.0; return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x)); }
void main()
{
	float c = Lin(texture(uDepth, vUV).r);
	float sum = 0.0, wsum = 0.0;
	for (int y = -2; y <= 1; ++y)
		for (int x = -2; x <= 1; ++x)
		{
			vec2 uv = vUV + (vec2(x, y) + 0.5) * uTexel;
			float z = Lin(texture(uDepth, uv).r);
			float w = exp(-abs(z - c) / (0.04 * c + 0.05));   // do not blur across depth edges
			sum += texture(uAO, uv).r * w; wsum += w;
		}
	outAO = vec4(sum / max(wsum, 1e-4), 0.0, 0.0, 1.0);
}
)GLSL";

	// Average colour of the scene on a small grid (32x18 texels, each a 4x4 patch of the full image).
	inline constexpr const char* LumFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outLum;
layout(binding = 0) uniform sampler2D uScene;
void main()
{
	vec3 sum = vec3(0.0);
	vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
	vec2 cell = vec2(1.0 / 32.0, 1.0 / 18.0);
	vec2 base = floor(vUV / cell) * cell;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			sum += min(texture(uScene, base + (vec2(x, y) + 0.5) / 4.0 * cell).rgb, vec3(64.0));
	outLum = vec4(sum / 16.0, 1.0);
}
)GLSL";

	// 1x1: log-average luminance of the grid -> target exposure -> eased toward it over time.
	inline constexpr const char* AdaptFS = R"GLSL(#version 450 core
out vec4 outExposure;
layout(binding = 0) uniform sampler2D uLum;
layout(binding = 1) uniform sampler2D uPrev;
uniform float uKey;
uniform float uMin;
uniform float uMax;
uniform float uBlend;      // 1 - exp(-dt * speed), 1 on the first frame
void main()
{
	float logSum = 0.0;
	for (int y = 0; y < 18; ++y)
		for (int x = 0; x < 32; ++x)
		{
			vec3 c = texelFetch(uLum, ivec2(x, y), 0).rgb;
			logSum += log(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4));
		}
	float avg = exp(logSum / 576.0);
	float target = clamp(uKey / avg, uMin, uMax);
	float prev = texelFetch(uPrev, ivec2(0, 0), 0).r;
	outExposure = vec4(mix(prev, target, uBlend), 0.0, 0.0, 1.0);
}
)GLSL";

	inline constexpr const char* CompositeFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1) uniform sampler2D uBloom;
layout(binding = 2) uniform sampler2D uAO;
layout(binding = 3) uniform sampler2D uDepth;
layout(binding = 4) uniform sampler2D uExposureTex;
layout(binding = 5) uniform sampler2D uAmbient;   // the ambient part of uScene (with the SSAO on)
uniform float uExposure;
uniform float uBloomIntensity;
uniform float uVignette;
uniform int uTonemap;          // 0 ACES, 1 Neutral, 2 Filmic
uniform float uAOStrength;     // 0 = no AO texture
uniform float uDOF;            // max blur radius in percent of the screen height, 0 = off
uniform float uFocus;
uniform float uFocusRange;
uniform vec2 uNearFar;
uniform int uOrtho;
uniform int uAutoExposure;
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uTint;
uniform float uCA;
uniform float uGrain;
uniform float uTime;

vec3 ACES(vec3 x)
{
	const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Khronos PBR Neutral: keeps hues and the base colours of materials, compresses only the highlights.
vec3 Neutral(vec3 color)
{
	const float startCompression = 0.8 - 0.04, desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression) return color;
	float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;
	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return mix(color, vec3(newPeak), g);
}

vec3 Hable(vec3 x)
{
	const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
	return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 LinearToSRGB(vec3 c)
{
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

#include "lib/depth"

float CoC(vec2 uv)
{
	float z = LinearDepth(texture(uDepth, uv).r);
	return clamp((abs(z - uFocus) - uFocusRange * 0.5) / max(uFocus * 0.6, 1.0), 0.0, 1.0);
}

vec3 SceneColor(vec2 uv)
{
	if (uDOF <= 0.0) return texture(uScene, uv).rgb;
	float coc = CoC(uv);
	if (coc < 0.01) return texture(uScene, uv).rgb;
	// golden-angle disc; samples that are sharper than the centre do not bleed into it
	float aspect = float(textureSize(uScene, 0).x) / float(textureSize(uScene, 0).y);
	float radius = coc * uDOF * 0.01;
	vec3 sum = texture(uScene, uv).rgb; float wsum = 1.0;
	for (int i = 0; i < 20; ++i)
	{
		float r = sqrt((float(i) + 0.5) / 20.0), a = float(i) * 2.399963;
		vec2 off = vec2(cos(a) / aspect, sin(a)) * r * radius;
		float cocS = CoC(uv + off);
		float w = max(cocS, coc * 0.35) * (1.0 + 0.0);
		sum += min(texture(uScene, uv + off).rgb, vec3(24.0)) * w; wsum += w;
	}
	return sum / wsum;
}

void main()
{
	vec2 q = vUV - 0.5;
	vec2 uv = vUV;
	vec3 hdr;
	if (uCA > 0.0)
	{
		vec2 shift = q * dot(q, q) * uCA * 0.06;
		hdr = vec3(SceneColor(uv + shift).r, SceneColor(uv).g, SceneColor(uv - shift).b);
	}
	else
		hdr = SceneColor(uv);
	// The SSAO occludes the sky light only: take its occluded share off the ambient part, the sun and the lamps stay.
	if (uAOStrength > 0.0)
		hdr = max(hdr - texture(uAmbient, vUV).rgb * ((1.0 - texture(uAO, vUV).r) * uAOStrength), vec3(0.0));
	hdr = max(hdr + texture(uBloom, vUV).rgb * uBloomIntensity, vec3(0.0));
	float exposure = uExposure * (uAutoExposure != 0 ? texelFetch(uExposureTex, ivec2(0, 0), 0).r : 1.0);
	vec3 x = hdr * exposure;
	vec3 ldr;
	if (uTonemap == 1) ldr = clamp(Neutral(x), 0.0, 1.0);
	else if (uTonemap == 2) ldr = clamp(Hable(x * 2.0) / Hable(vec3(11.2)), 0.0, 1.0);
	else ldr = ACES(x);
	// grading
	float luma = dot(ldr, vec3(0.2126, 0.7152, 0.0722));
	ldr = mix(vec3(luma), ldr, uSaturation);
	ldr = clamp((ldr - 0.5) * uContrast + 0.5, 0.0, 1.0) * uTint;
	ldr *= 1.0 - uVignette * dot(q, q) * 1.6;
	vec3 srgb = LinearToSRGB(clamp(ldr, 0.0, 1.0));
	if (uGrain > 0.0)
	{
		float n = fract(sin(dot(vUV * vec2(1280.0, 720.0) + fract(uTime) * 91.7, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
		srgb += n * uGrain * 0.08 * (1.0 - luma * 0.6);
	}
	outColor = vec4(srgb, 1.0);
}
)GLSL";
}
