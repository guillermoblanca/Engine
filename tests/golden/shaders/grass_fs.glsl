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
in vec3 vWorld;
in float vT;
in float vSeed;
in vec3 vNormal;
in float vSide;
in vec4 vTint;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
layout(binding = 0) uniform sampler2DShadow uShadowMap;
uniform vec3 uBase;
uniform vec3 uTip;
uniform vec3 uDry;
uniform float uDryAmount;


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
	vec3 col = mix(uBase, uTip, pow(max(vT, 0.0), 0.8)) * variation * vTint.rgb;   // vT can interpolate to a hair below 0: pow(<0) is NaN, which bloom/DOF smear over the screen
	float dry = max(step(1.0 - uDryAmount, fract(vSeed * 7.31)), clamp(vTint.a, 0.0, 1.0));
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

	float fogKeep = 1.0;
	if (uFogDensity > 0.0)
	{
		float dist = length(vWorld - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vWorld.y, 0.0));
		fogKeep = exp(-density * density * dist * dist);
		color = mix(uFogColor * uEnvIntensity, color, fogKeep);
	}
	float alpha = 1.0 - smoothstep(0.7, 1.0, abs(vSide));   // soft edges, resolved by alpha-to-coverage
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), alpha);
	outAmbient = vec4(clamp(col * ambient * fogKeep, vec3(0.0), vec3(4096.0)), alpha);
}
