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
in vec4 vColor;
in vec3 vWorld;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
layout(binding = 0) uniform sampler2D uSprite;
layout(binding = 1) uniform sampler2D uSceneDepth;
uniform float uSoft;            // soft particle fade distance (world units), 0 = off

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
	outAmbient = vec4(0.0, 0.0, 0.0, a);   // no ambient of its own: blended, it hides the one behind (additive: unchanged)
}
