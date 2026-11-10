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
layout(location = 0) in vec4 aPosYaw;   // x, y, z of the root, yaw
layout(location = 1) in vec4 aShape;    // height, width, seed, lean
layout(location = 2) in vec4 aTint;     // color multiplier, dryness
uniform float uDrawDistance;
out vec3 vWorld;
out float vT;
out float vSeed;
out vec3 vNormal;
out float vSide;
out vec4 vTint;
flat out vec3 vRoot;   // T46: the blade's root in the world (the colour variation's hash)
const int SEGMENTS = 4;

layout(binding = 8) uniform sampler2D uTrailMap;
uniform float uTrailBend;      // 0 = ignore marks
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}

void main()
{
	int seg = gl_VertexID >> 1;
	float side = float(gl_VertexID & 1) * 2.0 - 1.0;
	float t = float(seg) / float(SEGMENTS);
	vec3 root = aPosYaw.xyz;
	float yaw = aPosYaw.w;
	float height = aShape.x, width = aShape.y, seed = aShape.z, lean = aShape.w;
	vRoot = root;

	// Far away, blades thin out (the survivors get wider so the ground stays covered) and then vanish.
	float dist = length(root - uCamPos);
	float fade = smoothstep(0.5, 1.0, dist / uDrawDistance);
	if (seed < fade)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		vWorld = root; vT = 0.0; vSeed = seed; vNormal = vec3(0, 1, 0); vSide = 0.0; vTint = aTint;
		return;
	}
	width *= 1.0 + dist * 0.03;

	// Ground marks: footprints flatten the grass and push it away from the dent.
	float trail = 0.0;
	vec2 push = vec2(0.0);
	if (uTrailBend > 0.0 && uTrailRect.z > 0.0)
	{
		float e = max(uTrailRect.w, 0.05);
		trail = TrailAt(root.xz);
		vec2 grad = vec2(TrailAt(root.xz + vec2(e, 0.0)) - TrailAt(root.xz - vec2(e, 0.0)),
		                 TrailAt(root.xz + vec2(0.0, e)) - TrailAt(root.xz - vec2(0.0, e)));
		float gl = length(grad);
		push = gl > 1e-5 ? -grad / gl : vec2(0.0);
		trail = clamp(trail * uTrailBend * 1.4, 0.0, 1.0);
	}

	float h = height * (1.0 - 0.7 * uSnowCover) * (1.0 - 0.8 * trail);
	vec3 bladeRight = vec3(cos(yaw), 0.0, sin(yaw));
	vec3 forward = vec3(-sin(yaw), 0.0, cos(yaw));

	// Wind: a travelling wave plus flutter. Weak at the root, strong at the tip.
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(root.xz, wd) * 0.32 - uTime * (1.2 + 0.18 * ws) + seed * 6.2831;
	float gust = 0.55 + 0.45 * sin(phase) + 0.25 * sin(phase * 2.7 + 1.3);
	float flutter = sin(uTime * 6.5 + seed * 40.0 + root.x * 1.7) * 0.12;
	float bend = (0.05 + 0.055 * ws) * gust + flutter * 0.06 * (0.4 + ws * 0.2);
	vec3 windOffset = vec3(wd.x, 0.0, wd.y) * bend * h;

	float t2 = t * t;
	vec3 pos = root + vec3(0.0, h * t, 0.0) + forward * (lean * h * t2) + windOffset * t2
	         + vec3(push.x, 0.0, push.y) * trail * h * 0.7 * t2;

	// Ribbon turned partly toward the camera so blades do not vanish edge-on.
	vec3 toCam = normalize(vec3(uCamPos.x - root.x, 0.0, uCamPos.z - root.z) + vec3(1e-4));
	vec3 camRight = normalize(cross(vec3(0.0, 1.0, 0.0), toCam));
	if (dot(camRight, bladeRight) < 0.0) camRight = -camRight;
	vec3 right = normalize(mix(bladeRight, camRight, 0.45));
	float halfWidth = 0.5 * width * (1.0 - pow(t, 1.4));
	pos += right * side * halfWidth;

	vec3 tangent = normalize(vec3(0.0, h, 0.0) + forward * (2.0 * lean * h * t) + windOffset * 2.0 * t);
	vWorld = pos;
	vT = t;
	vSeed = seed;
	vSide = side;
	vTint = aTint;
	vNormal = normalize(cross(right, tangent));
	gl_Position = uViewProj * vec4(pos, 1.0);
}
