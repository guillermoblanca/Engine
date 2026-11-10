#pragma once

// Objects and terrain: the shadow pass and the PBR forward pass. Assemble with Shaders::Assemble (Shaders.h).

namespace GB::Shaders
{
	inline constexpr const char* ShadowVS = R"GLSL(#version 450 core
#include "lib/frame"
#include "lib/material"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uModel;
#include "lib/object_skin"
#include "lib/object_height"
out vec2 vUV;
#include "lib/tree_wind"
void main()
{
	vec3 pos = aPos;
	if (HasTex(TEX_HEIGHT))
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * MATERIAL.Uv.x, 0.0).r - 0.5) * MATERIAL.ModelParams.x;
	if (uSkinned != 0)
		pos = (SkinMatrix() * vec4(pos, 1.0)).xyz;
	mat4 model = uInstanced != 0 ? uInstances[gl_InstanceID] : uModel;
	vec4 world = model * vec4(pos, 1.0);
	if (MATERIAL.Wind.x > 0.0)
		world.xyz += WindOffset(aPos, world.xyz, model[3].xyz);
	vUV = aUV * MATERIAL.Uv.x;
	gl_Position = uLightVP * world;
}
)GLSL";

	// Alpha-masked casters (leaf cards) cut their silhouette out of the depth map, so a crown casts leafy shadows, not slabs.
	inline constexpr const char* ShadowFS = R"GLSL(#version 450 core
#include "lib/material"
in vec2 vUV;
layout(binding = 1) uniform sampler2D uAlbedoMap;
void main()
{
	if (MATERIAL.Flags.z == 1u && HasTex(TEX_ALBEDO) && texture(uAlbedoMap, vUV).a * MATERIAL.BaseColor.a < MATERIAL.Emissive.a)
		discard;
}
)GLSL";

	inline constexpr const char* PBRVS = R"GLSL(#version 450 core
#include "lib/frame"
#include "lib/material"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
uniform mat4 uModel;
#include "lib/object_trail"
#include "lib/object_skin"
#include "lib/object_height"
out vec3 vPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
#include "lib/tree_wind"
void main()
{
	vec3 pos = aPos;
	if (HasTex(TEX_HEIGHT))
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * MATERIAL.Uv.x, 0.0).r - 0.5) * MATERIAL.ModelParams.x;
	vec3 nrm = aNormal;
	vec3 tng = aTangent.xyz;
	if (uSkinned != 0)
	{
		mat4 skin = SkinMatrix();
		pos = (skin * vec4(pos, 1.0)).xyz;
		nrm = mat3(skin) * nrm;
		tng = mat3(skin) * tng;
	}
	mat4 model = uInstanced != 0 ? uInstances[gl_InstanceID] : uModel;
	vec4 world = model * vec4(pos, 1.0);
	if (MATERIAL.Wind.x > 0.0)
		world.xyz += WindOffset(aPos, world.xyz, model[3].xyz);
	if (MATERIAL.Layer[SLOT_TRAIL].x > 0.0)
		world.y -= TrailAt(world.xz) * MATERIAL.Layer[SLOT_TRAIL].x;   // footprints, tracks
	mat3 normalMatrix = mat3(transpose(inverse(model)));
	vPos = world.xyz;
	vNormal = normalMatrix * nrm;
	vTangent = vec4(mat3(model) * tng, aTangent.w);
	vUV = aUV * MATERIAL.Uv.x;
	gl_Position = uViewProj * world;
}
)GLSL";

	inline constexpr const char* PBRFS = R"GLSL(#version 450 core
#include "lib/frame"
#include "lib/material"
#include "lib/sky"
in vec3 vPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
#include "lib/scene_targets"

layout(binding = 1) uniform sampler2D uAlbedoMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uHeightMap;   // height doubles as a cavity/AO term (TEX_HEIGHT_MAP)
layout(binding = 4) uniform sampler2D uMRMap;
layout(binding = 5) uniform sampler2D uOcclusionMap;
layout(binding = 6) uniform sampler2D uEmissiveMap;
layout(binding = 7) uniform sampler2D uMacroMap;
layout(binding = 10) uniform sampler2D uTerrainNormals;   // world normals over the macro rect (terrain)
#include "lib/object_trail"

layout(binding = 0) uniform sampler2DShadow uShadowMap;

#include "lib/brdf"

vec3 Shade(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float a, float metallic)
{
	float NoL = max(dot(N, L), 0.0);
	if (NoL <= 0.0) return vec3(0.0);
	vec3 H = normalize(V + L);
	float NoV = max(dot(N, V), 1e-4);
	float NoH = max(dot(N, H), 0.0);
	float VoH = max(dot(V, H), 0.0);
	vec3 F = F_Schlick(VoH, F0);
	vec3 spec = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
	vec3 diffuse = (1.0 - F) * (1.0 - metallic) * albedo / PI;
	return (diffuse + spec) * radiance * NoL;
}

#include "lib/shadow"

#include "lib/noise_pbr"

// Rain rings on a puddle: one expanding ring per random cell, two layers.
vec2 PuddleRings(vec2 p)
{
	vec2 push = vec2(0.0);
	for (int layer = 0; layer < 2; ++layer)
	{
		float size = layer == 0 ? 0.55 : 0.37;
		vec2 q = p / size + float(layer) * 11.3;
		vec2 cell = floor(q);
		float r1 = PHash(cell), r2 = PHash(cell + 17.0);
		vec2 center = cell + 0.2 + 0.6 * vec2(r1, r2);
		float phase = fract(uTime * (0.7 + 0.5 * r2) + r1 * 9.0);
		vec2 d = q - center;
		float dist = length(d) * size;
		float ringR = phase * size * 0.45;
		float ring = exp(-pow((dist - ringR) / 0.012, 2.0)) * (1.0 - phase) * (1.0 - phase);
		push += d / max(length(d), 1e-4) * ring;
	}
	return push;
}

void main()
{
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCamPos - vPos);
	if (!gl_FrontFacing && !HasFlag(FLAG_FOLIAGE_NORMALS)) N = -N;   // back faces of double-sided materials
	if (HasTex(TEX_TERRAIN_NORMALS))   // per-pixel smooth ground normal instead of the interpolated (faceted) vertex normal
		N = normalize(texture(uTerrainNormals, clamp((vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw, vec2(0.0004), vec2(0.9996))).xyz * 2.0 - 1.0);
	float trailT = 0.0;
	if (MATERIAL.Layer[SLOT_TRAIL].x > 0.0)
	{
		// The surface is lower where the map is high: its normal tilts toward the dent's rim.
		float e = uTrailRect.w;
		trailT = TrailAt(vPos.xz);
		vec2 grad = vec2(TrailAt(vPos.xz + vec2(e, 0.0)) - TrailAt(vPos.xz - vec2(e, 0.0)),
		                 TrailAt(vPos.xz + vec2(0.0, e)) - TrailAt(vPos.xz - vec2(0.0, e))) / (2.0 * e);
		N = normalize(N + vec3(grad.x, 0.0, grad.y) * MATERIAL.Layer[SLOT_TRAIL].x * MATERIAL.Layer[SLOT_TRAIL].y);
	}

	if (HasTex(TEX_NORMAL))
	{
		vec3 tn = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
		tn.xy *= MATERIAL.Uv.zw;
		tn.xy *= MATERIAL.Uv.y;
		vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
		vec3 B = cross(N, T) * vTangent.w;
		N = normalize(mat3(T, B, N) * normalize(tn));
	}

	vec3 albedo = MATERIAL.BaseColor.rgb;
	float alpha = MATERIAL.BaseColor.a;
	if (HasTex(TEX_ALBEDO))
	{
		vec4 t = texture(uAlbedoMap, vUV);
		albedo *= t.rgb;
		alpha *= t.a;
	}
	if (HasTex(TEX_MACRO))
		albedo *= texture(uMacroMap, (vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw).rgb;
	albedo *= mix(vec3(1.0), MATERIAL.Layer[SLOT_TRAIL_TINT].rgb, clamp(trailT * 1.3, 0.0, 1.0));   // compacted / wetter ground
	if (MATERIAL.Flags.z == 1u)
	{
		// Mip levels blur the mask: rescale around the cutoff by the screen-space change of alpha (soft, sharp edge).
		alpha = clamp((alpha - MATERIAL.Emissive.a) / max(fwidth(alpha), 1e-3) + 0.5, 0.0, 1.0);
		if (alpha < (HasFlag(FLAG_ALPHA_TO_COVERAGE) ? 0.02 : 0.5)) discard;
	}
	// Procedural surface layers: grain, moss on top, sand at the base, a wet band at the water line.
	float mossMask = 0.0, sandMask = 0.0, bandWet = 0.0;
	float grain = MATERIAL.Layer[SLOT_GRAIN].x, moss = MATERIAL.Layer[SLOT_MOSS].w, sand = MATERIAL.Layer[SLOT_SAND].w;
	float waterLine = MATERIAL.Layer[SLOT_WATER_LINE].x;
	if (grain > 0.0 || moss > 0.0 || sand > 0.0 || waterLine > -1e8)
	{
		// With grain == 0 the mottling is mix(1, x, 0) == 1 exactly: the grain noise (g1) is only paid when grain is on. g2 is the
		// fine noise of moss, sand and the wet band, so it is only paid when one of those is on.
		if (grain != 0.0)
		{
			vec3 gp = vPos * MATERIAL.Layer[SLOT_GRAIN].y;
			float g1 = FBM3(gp);
			albedo *= mix(1.0, 0.55 + 0.9 * g1, grain);
			if (grain > 0.0)
			{
				float e = 0.05;
				vec3 grad = vec3(FBM3(gp + vec3(e, 0, 0)) - g1, FBM3(gp + vec3(0, e, 0)) - g1, FBM3(gp + vec3(0, 0, e)) - g1) / e;
				N = normalize(N - (grad - N * dot(grad, N)) * 0.06 * grain);
			}
		}
		float g2 = 0.0;
		if (moss > 0.0 || sand > 0.0 || waterLine > -1e8)
			g2 = FBM3(vPos * 11.0 + 5.0);
		float up = clamp(normalize(vNormal).y, 0.0, 1.0);
		if (moss > 0.0)
		{
			float v = up * 0.55 + FBM3(vPos * 1.9 + 3.0) * 0.7 + (moss - 0.5) * 0.9 - 0.45;
			mossMask = smoothstep(0.3, 0.55, v) * smoothstep(0.0, 0.12, moss);
			albedo = mix(albedo, MATERIAL.Layer[SLOT_MOSS].rgb * (0.55 + 0.9 * g2), mossMask);
		}
		if (sand > 0.0)
		{
			float h = vPos.y - MATERIAL.Layer[SLOT_WATER_LINE].y;
			sandMask = (1.0 - smoothstep(0.0, 0.5, h + (g2 - 0.5) * 0.3)) * sand * (0.55 + 0.45 * up);
			albedo = mix(albedo, MATERIAL.Layer[SLOT_SAND].rgb * (0.85 + 0.3 * g2), sandMask);
		}
		if (waterLine > -1e8)
		{
			bandWet = 1.0 - smoothstep(waterLine, waterLine + 0.3, vPos.y + (g2 - 0.5) * 0.1);
			albedo *= mix(1.0, 0.5, bandWet);
		}
	}
	albedo *= mix(1.0, 0.55, uWetness);   // wet surfaces are darker...
	// Snow settles on surfaces that face up (with a little noise so the edge is not a clean line).
	float snow = 0.0;
	if (uSnowCover > 0.0)
	{
		float upFacing = clamp(normalize(vNormal).y, 0.0, 1.0);
		float grain = fract(sin(dot(floor(vPos.xz * 6.0), vec2(12.9898, 78.233))) * 43758.5453) * 0.12;
		snow = smoothstep(0.55, 0.85, upFacing + grain - (1.0 - uSnowCover) * 0.35) * uSnowCover;
		albedo = mix(albedo, vec3(0.92, 0.94, 0.97), snow);
	}
	// Puddles: where the ground is flat and low-frequency noise says so, rain collects into mirror-like patches.
	float puddle = 0.0;
	if (uWetness > 0.3)
	{
		float flatness = smoothstep(0.9, 0.98, normalize(vNormal).y);
		float pn = PNoise(vPos.xz * 0.22) * 0.65 + PNoise(vPos.xz * 0.95 + 7.0) * 0.35;
		float cover = smoothstep(0.3, 1.0, uWetness) * 0.5;
		puddle = smoothstep(1.0 - cover - 0.05, 1.0 - cover + 0.02, pn) * flatness * (1.0 - snow);
		N = normalize(mix(N, normalize(vNormal), puddle * 0.9));
		vec2 rings = PuddleRings(vPos.xz);
		N = normalize(N + vec3(rings.x, 0.0, rings.y) * 0.6 * puddle * smoothstep(0.4, 0.9, uWetness));
		albedo *= mix(1.0, 0.55, puddle);
	}
	float ao = MATERIAL.Surface.z;
	if (HasTex(TEX_OCCLUSION))
		ao *= mix(1.0, texture(uOcclusionMap, vUV).r, MATERIAL.Surface.w);
	if (HasTex(TEX_HEIGHT_MAP))
	{
		float h = texture(uHeightMap, vUV).r;
		ao *= mix(1.0, clamp(h * 1.4, 0.0, 1.0), MATERIAL.ModelParams.y);
	}
	float metallic = MATERIAL.Surface.x;
	float roughness = MATERIAL.Surface.y;
	if (HasTex(TEX_MR))
	{
		vec3 mr = texture(uMRMap, vUV).rgb;
		roughness *= mr.g;
		metallic *= mr.b;
	}
	metallic = clamp(metallic, 0.0, 1.0);
	metallic *= 1.0 - snow;
	roughness = mix(roughness, roughness * 0.25, uWetness * (1.0 - metallic) * (1.0 - snow));   // ...and glossier
	roughness = mix(roughness, 0.85, snow);
	roughness = mix(roughness, 0.9, max(mossMask, sandMask));
	roughness = mix(roughness, 0.12, bandWet);
	roughness = mix(roughness, 0.03, puddle);
	roughness = clamp(roughness, 0.03, 1.0);
	float a = roughness * roughness;
	vec3 F0 = mix(vec3(0.04), albedo, metallic);

	vec3 Lo = vec3(0.0);

	vec3 Ls = normalize(-uSunDir);
	Lo += Shade(N, V, Ls, uSunColor, albedo, F0, a, metallic) * Shadow(vPos, N, Ls);

	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPosRadius[i].xyz - vPos;
		float d2 = dot(toLight, toLight);
		vec3 L = toLight * inversesqrt(d2);
		float window = 1.0;
		if (uPointPosRadius[i].w > 0.0)
		{
			float r = d2 / (uPointPosRadius[i].w * uPointPosRadius[i].w);
			window = clamp(1.0 - r * r, 0.0, 1.0);
			window *= window;
		}
		Lo += Shade(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic);
	}

	float NoV = max(dot(N, V), 1e-4);
	vec3 R = reflect(-V, N);
	vec3 F = EnvBRDFApprox(F0, roughness, NoV);
	vec3 kd = (1.0 - F) * (1.0 - metallic);
	vec3 ambientDiffuse = kd * albedo * SkyIrradiance(N);
	vec3 ambientSpec = SkyPrefiltered(R, roughness) * F;
	// Horizon occlusion: reflections under the surface plane fade out.
	float horizon = clamp(1.0 + dot(R, N) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 emissive = MATERIAL.Emissive.rgb;
	if (HasTex(TEX_EMISSIVE)) emissive *= texture(uEmissiveMap, vUV).rgb;
	vec3 color = Lo + ambient + emissive;
	float fogKeep = 1.0;   // the part of the surface's light that gets through the fog
	if (uFogDensity > 0.0)
	{
		float dist = length(vPos - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		fogKeep = exp(-density * density * dist * dist);
		color = mix(uFogColor * uEnvIntensity, color, fogKeep);
	}
	float outAlpha = (MATERIAL.Flags.z == 2u || (MATERIAL.Flags.z == 1u && HasFlag(FLAG_ALPHA_TO_COVERAGE))) ? alpha : 1.0;
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), outAlpha);
	outAmbient = vec4(clamp(ambient * fogKeep, vec3(0.0), vec3(4096.0)), outAlpha);   // blended like the color
}
)GLSL";
}
