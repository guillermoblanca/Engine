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
	if (MATERIAL.Flags.z == 1u && HasTex(TEX_ALBEDO))
	{
		float alpha = texture(uAlbedoMap, vUV).a * MATERIAL.BaseColor.a;
		// T46: foliage with an alpha mip scale: the alpha raised by the mip level, as in PBRFS, so thin cards keep their shadow far away
		if ((MATERIAL.Flags.x & LAYER_FOLIAGE) != 0u)
			alpha *= 1.0 + max(textureQueryLod(uAlbedoMap, vUV).x, 0.0) * MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].w;
		if (alpha < MATERIAL.Emissive.a)
			discard;
	}
	// T101: a transmissive caster (only with a shadow density: RenderList) covers that fraction of the shadow map's texels in a 4x4 ordered
	// dither; the 5x5 PCF of the lookup averages it into a faint, even shadow.
	if (MATERIAL.Flags.z == 2u)
	{
		const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
		ivec2 p = ivec2(gl_FragCoord.xy) & 3;
		if ((bayer[p.y * 4 + p.x] + 0.5) / 16.0 >= MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA].z)
			discard;
	}
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
flat out vec3 vOrigin;   // T46: the object's (or the instance's) origin in the world: the foliage's per-instance hash and crown centre
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
	vOrigin = model[3].xyz;
	gl_Position = uViewProj * world;
}
)GLSL";

	// (Two raw literals joined: one MSVC string literal holds at most 16 KB. The text is the same as one.)
	// T46: compiled twice. With GB_FOLIAGE (Renderer's foliage program, for the materials with MaterialData::LayerFoliage: FoliageOn in
	// MaterialTable.h) it adds the crown normal blend, the alpha mip scale, the per-instance colour and the light through the leaf. In the
	// base program those blocks are compiled out: as branches on the layer bit they cost every object ~6% of the opaque pass on the
	// reference scene (register pressure; measured, MATERIALES.md "Follaje"), even with no foliage in view.
	// T115: compiled a third time with GB_TERRAIN_LAYERS (the Renderer's terrain program, for a material with Material::TerrainLayers: a
	// partition terrain): the colour, normal, roughness and occlusion come from the terrain's material layers (lib/terrain_layers), and the
	// transmission is compiled out. The other two programs compile none of it.
	inline constexpr const char* PBRFS = R"GLSL(#version 450 core
#include "lib/frame"
#include "lib/material"
#include "lib/sky"
in vec3 vPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
flat in vec3 vOrigin;   // T46 (LAYER_FOLIAGE only)
#include "lib/scene_targets"

layout(binding = 1) uniform sampler2D uAlbedoMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uHeightMap;   // height doubles as a cavity/AO term (TEX_HEIGHT_MAP)
layout(binding = 4) uniform sampler2D uMRMap;
layout(binding = 5) uniform sampler2D uOcclusionMap;
layout(binding = 6) uniform sampler2D uEmissiveMap;
layout(binding = 7) uniform sampler2D uMacroMap;
layout(binding = 10) uniform sampler2D uTerrainNormals;   // world normals over the macro rect (terrain)
layout(binding = 11) uniform sampler2D uSceneColor;       // T34: the scene before the translucent objects (LAYER_TRANSMISSION only)
layout(binding = 12) uniform sampler2D uSceneDepth;       // T101: the depth of that scene (LAYER_TRANSMISSION only)
layout(binding = 13) uniform sampler2D uInteriorMap;      // T101: what is inside the ice, rgb colour and a density (TEX_INTERIOR)
uniform float uSceneColorLods;                            // T101: the copy's last mip level with this frame's content (0: only level 0)
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

// T110: Shade with the mirrored part scaled by `specScale` (the crowns shaded with a baked crown normal, see main)
vec3 ShadeSpecScaled(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, vec3 F0, float a, float metallic, float specScale)
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
	return (diffuse + spec * specScale) * radiance * NoL;
}

#include "lib/shadow"

#include "lib/noise_pbr"
#ifdef GB_FOLIAGE
#include "lib/foliage"
#endif
#ifdef GB_TERRAIN_LAYERS
#include "lib/terrain_layers"
#endif

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

// T34: what a transmissive surface (glass, ice) lets through: the scene behind it, from the copy at unit 11, looked up where the ray refracted
// by the surface leaves it after `thickness` metres (screen-space refraction), blurred by the roughness and dimmed by Beer-Lambert absorption
// over the same thickness. The copy already carries the fog between the camera and what is behind. N and roughness are the pixel's, after
// the normal and metal-roughness maps: a smooth drop on misted glass refracts sharp, the mist around it blurs.
// T101: the blur reads the copy's mip chain (uSceneColorLods levels this frame): the level whose texel is half the blur radius and four taps
// at half the radius around it, smooth and stable where the 12-tap disk of T34 was grainy. And a depth test: where the opaque scene at the
// refracted point is nearer than this surface, what is there is in front of the glass, not behind it, so the lookup falls back to the point
// straight behind (no leak of the things in front into the glass).
vec3 Transmitted(vec3 N, vec3 V, float roughness, float thickness)
{
	vec3 r = refract(-V, N, 1.0 / max(MATERIAL.ModelParams.z, 1.0));
	if (dot(r, r) < 1e-6) r = -V;
	vec4 clip = uViewProj * vec4(vPos + r * thickness, 1.0);
	vec2 size = vec2(textureSize(uSceneColor, 0));
	vec2 uv = clamp(clip.xy / max(clip.w, 1e-4) * 0.5 + 0.5, vec2(0.0), vec2(1.0));
	if (textureLod(uSceneDepth, uv, 0.0).r < gl_FragCoord.z)
		uv = gl_FragCoord.xy / size;
	float radius = roughness * roughness * 0.3 / max(clip.w, 0.25);   // in screen heights: frosted ice blurs ~0.1 of the screen at 1 m
	float pixels = radius * size.y;
	vec3 behind;
	if (pixels > 1.0)
	{
		float lod = clamp(log2(pixels) - 1.0, 0.0, uSceneColorLods);
		vec2 scale = vec2(radius * size.y / size.x, radius) * 0.5;
		behind = textureLod(uSceneColor, uv, lod).rgb * 2.0;
		const vec2 taps[4] = vec2[4](vec2(0.94, 0.34), vec2(-0.34, 0.94), vec2(-0.94, -0.34), vec2(0.34, -0.94));
		for (int i = 0; i < 4; ++i)
			behind += textureLod(uSceneColor, clamp(uv + taps[i] * scale, vec2(0.0), vec2(1.0)), lod).rgb;
		behind /= 6.0;
	}
	else
		behind = textureLod(uSceneColor, uv, 0.0).rgb;
	return behind * exp(-MATERIAL.Layer[SLOT_TRANSMISSION].rgb * thickness);
}
)GLSL" R"GLSL(
void main()
{
	vec3 N = normalize(vNormal);
	vec3 V = normalize(uCamPos - vPos);
	if (!gl_FrontFacing && !HasFlag(FLAG_FOLIAGE_NORMALS)) N = -N;   // back faces of double-sided materials
	vec3 Ng = N;   // T101: the interpolated normal, before any map (the interior's parallax and the scatter's edges)
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
#ifdef GB_FOLIAGE
	// T46: foliage. The card's normal mixed with the normal of a sphere around the object's pivot (on the ground: the sphere leans up), so
	// the inside of a bush sees the sky; before the normal map, which perturbs the blended normal (the card's relief still shows).
	if (MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].y > 0.0)
	{
		vec3 crown = vPos - vOrigin;
		vec3 S = dot(crown, crown) > 1e-8 ? normalize(crown) : vec3(0.0, 1.0, 0.0);
		vec3 n = mix(N, S, MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].y);
		N = dot(n, n) > 1e-6 ? normalize(n) : S;
	}
#endif

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
#ifdef GB_FOLIAGE
		// T46: the mip levels average the mask's alpha down, so thin cards thin out and flicker far away: raise it by the level
		if (MATERIAL.Flags.z == 1u)
			alpha *= 1.0 + max(textureQueryLod(uAlbedoMap, vUV).x, 0.0) * MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].w;
#endif
	}
#ifdef GB_FOLIAGE
	// T46: per-instance colour (a hash of the instance's origin) and the world's macro patches
	albedo = FoliageVary(albedo, vOrigin, vPos, MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].x, MATERIAL.Layer[SLOT_FOLIAGE_EXTRA].z);
#endif
	if (HasTex(TEX_MACRO))
		albedo *= texture(uMacroMap, (vPos.xz - MATERIAL.Layer[SLOT_MACRO].xy) / MATERIAL.Layer[SLOT_MACRO].zw).rgb;
#ifdef GB_TERRAIN_LAYERS
	// T115: a partition terrain: its material layers, modulated by the macro colour above (lib/terrain_layers)
	TerrainSurface terrainLayers = TerrainLayersAt(vPos, N, albedo);
	albedo = terrainLayers.Albedo;
	N = terrainLayers.N;
#endif
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
#ifdef GB_TERRAIN_LAYERS
	roughness = terrainLayers.Roughness;
	ao *= terrainLayers.AO;
#endif
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
	// T34: a transmissive surface reflects like a dielectric of its index of refraction and does not diffuse the light that goes through.
#ifdef GB_TERRAIN_LAYERS
	float transmission = 0.0;   // T115: a terrain is opaque (and the transmission's three samplers stay out of this program)
#else
	float transmission = (MATERIAL.Flags.x & LAYER_TRANSMISSION) != 0u ? MATERIAL.ModelParams.w : 0.0;
#endif
	if (transmission > 0.0)
	{
		float f0 = (MATERIAL.ModelParams.z - 1.0) / (MATERIAL.ModelParams.z + 1.0);
		F0 = mix(vec3(f0 * f0), albedo, metallic);
		albedo *= 1.0 - transmission;
	}
	// T101: the thickness Beer-Lambert absorbs over, changed by the height map (or else the interior's density) by the variation, and the
	// interior map at two depths under the surface: the uv shifted along the view in tangent space (uv - V.xy / V.z * depth), so the cracks
	// and bubbles move against the surface (parallax). The deeper sample reads another part of the map at another scale.
	float thickness = MATERIAL.Layer[SLOT_TRANSMISSION].w;
	vec4 inner0 = vec4(0.0), inner1 = vec4(0.0);
	if (transmission > 0.0)
	{
		vec4 extra = MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA];
		float h = -1.0;
		if (HasTex(TEX_INTERIOR))
		{
			vec3 T = normalize(vTangent.xyz - Ng * dot(Ng, vTangent.xyz));
			vec3 B = cross(Ng, T) * vTangent.w;
			vec3 Vt = vec3(dot(V, T), dot(V, B), dot(V, Ng));
			vec2 shift = Vt.xy / max(Vt.z, 0.25);   // grazing views clamped: the layer would slide off
			inner0 = texture(uInteriorMap, vUV - shift * extra.x * 0.4);
			inner1 = texture(uInteriorMap, vUV * 0.71 + vec2(0.37, 0.61) - shift * extra.x);
			h = 0.25 + 0.5 * (inner0.a + inner1.a);
		}
		if (HasTex(TEX_HEIGHT_MAP))
			h = texture(uHeightMap, vUV).r;
		if (h >= 0.0)
			thickness *= mix(1.0, 0.35 + 1.3 * clamp(h, 0.0, 1.0), extra.w);
	}

)GLSL" R"GLSL(
	vec3 Lo = vec3(0.0);

	// T110: a crown that keeps its baked crown normal on both faces (FLAG_FOLIAGE_NORMALS: the tree crowns) mirrors the sun and the sky
	// only as much as the card really faces that way: the crown normal is the volume's, not the leaf's, and on its own it put white glints
	// over the tops of the crowns against a low sun (the whole top of a crown "facing" the half vector). The specular is scaled by the
	// fourth power of the agreement between the card's own face (from the screen derivatives) and the shading normal.
	// The other materials keep their code untouched (bit for bit the same pictures).
	bool crownNormals = HasFlag(FLAG_FOLIAGE_NORMALS);
	float specScale = 1.0;
	if (crownNormals)
	{
		vec3 face = cross(dFdx(vPos), dFdy(vPos));
		float agree = dot(face, face) > 1e-20 ? abs(dot(normalize(face), N)) : 1.0;
		float agree2 = agree * agree;
		specScale = agree2 * agree2;
	}

	vec3 Ls = normalize(-uSunDir);
	float sunShadow = Shadow(vPos, N, Ls);
#ifdef GB_FOLIAGE
	// T107: occlusion from the pivot, no maps: dark where the plant meets the ground, fading out by `height` over the pivot, and in its core,
	// near the vertical axis (within `height` of it, fading out by three times `height` up). It dims the ambient and the light through the
	// leaf fully and the sun half (the ground and the inner leaves still see some of it).
	vec4 shade = MATERIAL.Layer[SLOT_FOLIAGE_SHADE];
	float leafOcc = 1.0;
	if (shade.y > 0.0)
	{
		vec3 rel = vPos - vOrigin;
		float up = clamp(rel.y / shade.y, 0.0, 1.0);
		float baseOcc = 1.0 - up * up * (3.0 - 2.0 * up);
		float core = (1.0 - smoothstep(0.0, shade.y, length(rel.xz))) * (1.0 - smoothstep(shade.y, 3.0 * shade.y, rel.y));
		leafOcc = (1.0 - shade.x * baseOcc) * (1.0 - shade.z * core);
		ao *= leafOcc;
		sunShadow *= 0.5 + 0.5 * leafOcc;
	}
	// T107: with the spread on, a face the sun does not reach (its interpolated normal turned away from the sun; the crown normal can
	// still lean toward it) takes the sun as diffuse only: that light came through the leaf, it is not mirrored (no white glints on
	// the back-lit edges)
	if (shade.w > 0.0 && dot(Ng, Ls) < 0.0)
		Lo += albedo * ((1.0 - metallic) / PI * max(dot(N, Ls), 0.0)) * uSunColor * sunShadow;
	else
#endif
	{
		if (crownNormals)
			Lo += ShadeSpecScaled(N, V, Ls, uSunColor, albedo, F0, a, metallic, specScale) * sunShadow;
		else
			Lo += Shade(N, V, Ls, uSunColor, albedo, F0, a, metallic) * sunShadow;
	}
#ifdef GB_FOLIAGE
	// T46: the sun through the leaf (diffuse transmission), tinted by the albedo times the transmission tint. Its shadow is looked up
	// offset toward the sun, not along N: on the face away from the sun N points away from it and the leaf would shadow itself; the
	// leaves between this one and the sun still do (a leaf in the shadow of another does not glow).
	float leafT = MATERIAL.Layer[SLOT_FOLIAGE].w;
	vec3 leafTint = MATERIAL.Layer[SLOT_FOLIAGE].rgb * albedo * leafT;
	if (leafT > 0.0)
	{
		float through;
		if (shade.w > 0.0)
		{
			// T107: the spread. The sun's direction bent by the normal (as thin-surface scattering does) and a wider lobe, so the glow
			// is seen around the sun and not only right behind; and the grazing edges of the silhouette lit where the sun reaches them
			// (the thinnest part of the leaf, seen edge-on).
			vec3 Lb = normalize(Ls + N * (0.6 * shade.w));
			float edge = pow(1.0 - abs(dot(N, V)), 3.0) * (0.5 + 0.5 * dot(V, -Ls));
			through = pow(clamp(dot(V, -Lb), 0.0, 1.0), 4.0 - 2.5 * shade.w) * 3.0 + clamp(dot(-N, Ls), 0.0, 1.0) + edge * 1.5 * shade.w;
		}
		else
			through = FoliageThrough(N, V, Ls);
		Lo += uSunColor / PI * Shadow(vPos, Ls, Ls) * through * leafTint * leafOcc;
	}
#endif

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
		if (crownNormals)
			Lo += ShadeSpecScaled(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic, specScale);
		else
			Lo += Shade(N, V, L, uPointColor[i].rgb * (window / (d2 + 0.01)), albedo, F0, a, metallic);
	}

	float NoV = max(dot(N, V), 1e-4);
	vec3 R = reflect(-V, N);
	vec3 F = EnvBRDFApprox(F0, roughness, NoV);
	vec3 kd = (1.0 - F) * (1.0 - metallic);
	vec3 ambientDiffuse = kd * albedo * SkyIrradiance(N);
	vec3 ambientSpec = SkyPrefiltered(R, roughness) * F;
	if (crownNormals)
		ambientSpec *= specScale;
	// Horizon occlusion: reflections under the surface plane fade out.
	float horizon = clamp(1.0 + dot(R, N) * 1.5, 0.0, 1.0);
	vec3 ambient = (ambientDiffuse + ambientSpec * horizon) * ao * uEnvIntensity;
#ifdef GB_FOLIAGE
	if (leafT > 0.0)   // T46: the sky behind the leaf, through it (a quarter of what the face toward it gets)
		ambient += SkyIrradiance(-N) * leafTint * (0.25 * ao * uEnvIntensity);
#endif

	// RGBA16F overflows at 65504; a tiny GGX lobe can exceed that and the bloom would spread the inf.
	vec3 emissive = MATERIAL.Emissive.rgb;
	if (HasTex(TEX_EMISSIVE)) emissive *= texture(uEmissiveMap, vUV).rgb;
	vec3 color = Lo + ambient + emissive;
	// T34: the part of what is behind that gets through: not reflected (the Fresnel term of the ambient light), not absorbed. The reflection
	// is not dimmed by any opacity; the transmitted light is, by (1 - F) and the amount.
	vec3 through = vec3(0.0), passing = vec3(0.0);
	if (transmission > 0.0)
	{
		passing = (1.0 - F) * (1.0 - metallic) * transmission;
		through = Transmitted(N, V, roughness, thickness) * passing;
		vec3 absorb = MATERIAL.Layer[SLOT_TRANSMISSION].rgb * thickness;
		float scatter = MATERIAL.Layer[SLOT_TRANSMISSION_EXTRA].y;
		float wrap = clamp((dot(N, Ls) + 0.6) / 1.6, 0.0, 1.0);
		if (HasTex(TEX_INTERIOR))
		{
			// T101: the inclusions scatter the light that reaches them (the sky and the wrapped, shadowed sun), dimmed by the ice above them,
			// and hide what is behind by their density: the deep layer first, the shallow one over it.
			vec4 inside = MATERIAL.Layer[SLOT_INTERIOR];
			vec3 lit = (SkyIrradiance(N) * uEnvIntensity + uSunColor / PI * sunShadow * (0.25 + 0.75 * wrap)) * inside.rgb * passing;
			through = mix(through, inner1.rgb * lit * exp(-absorb * 0.6), clamp(inner1.a * inside.w, 0.0, 1.0));
			through = mix(through, inner0.rgb * lit * exp(-absorb * 0.25), clamp(inner0.a * inside.w, 0.0, 1.0));
		}
		if (scatter > 0.0)
		{
			// T101: light through the thin parts, cheap: the silhouette (grazing view, the ice is thin there) and the sun seen through the
			// piece (back light around -Ls, bent by the normal), plus a little wrap; tinted by a shorter path, so the edges are a paler cyan.
			float edge = pow(1.0 - clamp(dot(Ng, V), 0.0, 1.0), 3.0);
			float back = pow(clamp(dot(V, -normalize(Ls + N * 0.4)), 0.0, 1.0), 6.0);
			vec3 thin = exp(-absorb * 0.3);
			color += (uSunColor / PI * sunShadow * (back * 2.0 + edge * wrap * 1.2 + wrap * 0.15) + SkyIrradiance(N) * uEnvIntensity * edge * 0.5)
				* thin * scatter * transmission;
		}
	}
	float fogKeep = 1.0;   // the part of the surface's light that gets through the fog
	if (uFogDensity > 0.0)
	{
		float dist = length(vPos - uCamPos);
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		fogKeep = exp(-density * density * dist * dist);
		if (transmission > 0.0)   // the fog on the surface's own light only: the transmitted light has it already (the copy is fogged)
			color = mix(uFogColor * uEnvIntensity * (1.0 - dot(passing, vec3(1.0 / 3.0))), color, fogKeep);
		else
			color = mix(uFogColor * uEnvIntensity, color, fogKeep);
	}
	color += through;
	float outAlpha = (MATERIAL.Flags.z == 2u || (MATERIAL.Flags.z == 1u && HasFlag(FLAG_ALPHA_TO_COVERAGE))) ? alpha : 1.0;
	if (transmission > 0.0) outAlpha = 1.0;   // what is behind is in the color already: blending it again would count it twice
	outColor = vec4(clamp(color, vec3(0.0), vec3(4096.0)), outAlpha);
	outAmbient = vec4(clamp(ambient * fogKeep, vec3(0.0), vec3(4096.0)), outAlpha);   // blended like the color
}
)GLSL";
}
