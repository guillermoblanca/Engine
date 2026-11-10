#pragma once

// Billboard particles. Assemble with Shaders::Assemble (Shaders.h).

namespace GB::Shaders
{
	inline constexpr const char* ParticleVS = R"GLSL(#version 450 core
#include "lib/frame"
layout(location = 0) in vec4 aPosSize;     // xyz position, w size
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aVelRot;      // xyz velocity, w rotation
layout(location = 3) in vec4 aFrame;       // x = flipbook frame
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
#include "lib/frame"
in vec2 vUV;
in vec4 vColor;
in vec3 vWorld;
#include "lib/scene_targets"
layout(binding = 0) uniform sampler2D uSprite;
layout(binding = 1) uniform sampler2D uSceneDepth;
uniform float uSoft;            // soft particle fade distance (world units), 0 = off

#include "lib/depth"

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
)GLSL";
}
