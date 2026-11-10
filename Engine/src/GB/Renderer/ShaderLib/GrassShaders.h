#pragma once

// Grass blades. Assemble with Shaders::Assemble (Shaders.h).

namespace GB::Shaders
{
	// The grass ground-mark uniforms (uTrailBend, comment without units) differ from lib/object_trail: only TrailAt is shared.
	inline constexpr const char* GrassVS = R"GLSL(#version 450 core
layout(location = 0) in vec4 aPosYaw;   // x, y, z of the root, yaw
layout(location = 1) in vec4 aShape;    // height, width, seed, lean
layout(location = 2) in vec4 aTint;     // color multiplier, dryness
uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform float uTime;
uniform vec3 uWind;
uniform float uDrawDistance;
uniform float uSnowCover;
out vec3 vWorld;
out float vT;
out float vSeed;
out vec3 vNormal;
out float vSide;
out vec4 vTint;
const int SEGMENTS = 4;

layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size, texel size
uniform float uTrailBend;      // 0 = ignore marks
#include "lib/trail_at"

void main()
{
	int seg = gl_VertexID >> 1;
	float side = float(gl_VertexID & 1) * 2.0 - 1.0;
	float t = float(seg) / float(SEGMENTS);
	vec3 root = aPosYaw.xyz;
	float yaw = aPosYaw.w;
	float height = aShape.x, width = aShape.y, seed = aShape.z, lean = aShape.w;

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
)GLSL";

	inline constexpr const char* GrassFS = R"GLSL(#version 450 core
in vec3 vWorld;
in float vT;
in float vSeed;
in vec3 vNormal;
in float vSide;
in vec4 vTint;
#include "lib/scene_targets"
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uEnvIntensity;
uniform int uShadowsOn;
uniform mat4 uLightVP;
uniform float uShadowTexel;
layout(binding = 0) uniform sampler2DShadow uShadowMap;
uniform vec3 uBase;
uniform vec3 uTip;
uniform vec3 uDry;
uniform float uDryAmount;
#include "lib/sky"

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
)GLSL";
}
