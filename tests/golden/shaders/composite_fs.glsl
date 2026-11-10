#version 450 core
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

float LinearDepth(float d)
{
	if (uOrtho != 0) return uNearFar.x + d * (uNearFar.y - uNearFar.x);
	float z = d * 2.0 - 1.0;
	return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}

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
