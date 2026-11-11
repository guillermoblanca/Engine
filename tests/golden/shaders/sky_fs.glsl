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
in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)


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
