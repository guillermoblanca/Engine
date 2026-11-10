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
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;    // rivers: x = opacity (fades out where the river runs into a lake)
layout(location = 3) in vec4 aTangent;   // rivers: xy = flow direction, z = speed
uniform vec3 uOrigin;
uniform int uRiver;
out vec3 vPos;
out vec2 vFlow;
out float vBlend;

uniform vec2 uWaveDir;
uniform float uWaveHeight;
uniform float uWaveLength;
uniform float uSteep;
uniform float uChop;

const float kAngle[4] = float[4](0.0, 0.419999987, -0.579999983, 0.949999988);
const float kLen[4] = float[4](1.0, 0.620000005, 0.379999995, 0.239999995);
const float kAmp[4] = float[4](1.0, 0.550000012, 0.300000012, 0.159999996);

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

void main()
{
	vec3 world = aPos + uOrigin;
	float fade = 1.0 - smoothstep(30.0, 120.0, length(world.xz - uCamPos.xz));
	vec2 slope;
	vec3 w = Waves(world.xz, fade, slope);
	world += vec3(w.x, w.y, w.z);
	vPos = world;
	vFlow = uRiver != 0 ? aTangent.xy * aTangent.z : vec2(0.0);
	vBlend = uRiver != 0 ? aNormal.x : 1.0;
	gl_Position = uViewProj * vec4(world, 1.0);
}
