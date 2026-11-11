#pragma once

// The sky background. Assemble with Shaders::Assemble (Shaders.h).

namespace GB::Shaders
{
	// Sky background: the lib/sky colour plus stars, moon, clouds and the sun disc (with FullscreenVS).
	inline constexpr const char* SkyFS = R"GLSL(#version 450 core
#include "lib/frame"
in vec2 vUV;
#include "lib/scene_targets"
#include "lib/sky"


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
	vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 0.0, 1.0);   // a point on the far plane (reversed Z: depth 0, T137)
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
	outAmbient = vec4(0.0);
}
)GLSL";
}
