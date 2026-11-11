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
flat in vec3 vRoot;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
layout(binding = 0) uniform sampler2DShadow uShadowMap;
uniform vec3 uBase;
uniform vec3 uTip;
uniform vec3 uDry;
uniform float uDryAmount;
// T46: the foliage terms of the PBR's LAYER_FOLIAGE, per field (GrassGpu::Look; all 0: the grass as before)
uniform float uTranslucency;
uniform vec3 uTransmissionTint;
uniform float uColorVariation;
uniform float uMacroVariation;
uniform vec4 uFoliageShade;   // T128: base occlusion, its height (m; 0: no occlusion), interior occlusion, backlight spread (GrassGpu::Look)


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
float FHash3(vec3 p) { p = fract(p * vec3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return fract((p.x + p.y) * p.z); }
float FNoise2(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(FHash3(vec3(i, 0.0)), FHash3(vec3(i + vec2(1, 0), 0.0)), f.x), mix(FHash3(vec3(i + vec2(0, 1), 0.0)), FHash3(vec3(i + vec2(1, 1), 0.0)), f.x), f.y);
}
vec3 FoliageVary(vec3 c, vec3 origin, vec3 world, float amount, float macro)
{
	if (amount > 0.0)
	{
		float h1 = FHash3(origin * 1.37 + 0.11), h2 = FHash3(origin.zxy * 2.11 + 5.3), h3 = FHash3(origin.yzx * 3.07 + 9.1);
		float a = (0.3 - h1) * amount * 0.55;   // radians around the grey axis: negative turns green toward yellow, positive toward a deeper green
		const vec3 k = vec3(0.57735027);
		c = c * cos(a) + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - cos(a));
		float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
		c = mix(vec3(lum), c, 1.0 + (0.4 - h2) * amount * 0.6);   // less saturated (olive) or a little more
		c *= 1.0 + (h3 - 0.5) * amount * 0.7;
	}
	if (macro > 0.0)
	{
		float m = FNoise2(world.xz * 0.07) * 0.65 + FNoise2(world.xz * 0.19 + 7.0) * 0.35;
		vec3 patchTint = mix(vec3(1.16, 1.06, 0.62), vec3(0.78, 0.84, 0.62), smoothstep(0.3, 0.7, m));
		c *= mix(vec3(1.0), patchTint, macro);
	}
	return max(c, vec3(0.0));
}
// T128: FoliageVary in two parts the PBR VS works out, so the FS only multiplies:
// max(FoliagePatchTint(world, macro) * (FoliageInstanceMatrix(origin, amount) * c), 0) == FoliageVary(c, origin, world, amount, macro).
// The hue turn, the saturation and the brightness are linear in the colour and depend only on the origin: one matrix per instance (flat).
// The macro patch depends only on the world position, at the scale of tens of metres: per vertex.
mat3 FoliageInstanceMatrix(vec3 origin, float amount)
{
	if (amount <= 0.0)
		return mat3(1.0);
	float h1 = FHash3(origin * 1.37 + 0.11), h2 = FHash3(origin.zxy * 2.11 + 5.3), h3 = FHash3(origin.yzx * 3.07 + 9.1);
	float a = (0.3 - h1) * amount * 0.55;
	const vec3 k = vec3(0.57735027);
	float ca = cos(a), sa = sin(a);
	mat3 R = ca * mat3(1.0) + sa * mat3(0.0, k.z, -k.y, -k.z, 0.0, k.x, k.y, -k.x, 0.0) + (1.0 - ca) * outerProduct(k, k);   // cross(k, c) as a matrix
	float s = 1.0 + (0.4 - h2) * amount * 0.6;
	mat3 S = s * mat3(1.0) + (1.0 - s) * outerProduct(vec3(1.0), vec3(0.2126, 0.7152, 0.0722));   // mix(vec3(lum), c, s)
	return (1.0 + (h3 - 0.5) * amount * 0.7) * S * R;
}
vec3 FoliagePatchTint(vec3 world, float macro)
{
	if (macro <= 0.0)
		return vec3(1.0);
	float m = FNoise2(world.xz * 0.07) * 0.65 + FNoise2(world.xz * 0.19 + 7.0) * 0.35;
	return mix(vec3(1.0), mix(vec3(1.16, 1.06, 0.62), vec3(0.78, 0.84, 0.62), smoothstep(0.3, 0.7, m)), macro);
}
float FoliageThrough(vec3 N, vec3 V, vec3 L)
{
	return pow(clamp(dot(V, -L), 0.0, 1.0), 4.0) * 3.0 + clamp(dot(-N, L), 0.0, 1.0);
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
	if (uColorVariation > 0.0 || uMacroVariation > 0.0)   // T46: by clumps of ~0.6 m (the root's cell), and the world's macro patches
		col = FoliageVary(col, floor(vRoot * 1.6), vWorld, uColorVariation, uMacroVariation);
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
	// T128: the PBR foliage's occlusion from the pivot (ObjectShaders.h, T107), the blade's root as the pivot: dark where it meets the
	// ground, fading out by `height` up, and near the root's vertical; it replaces the blade's own darker root, dims the ambient and the light
	// through the blade fully and the sun half. Without it (height 0) the grass is as before.
	float ambientOcc = 0.4 + 0.6 * vT;   // darker at the root
	float leafOcc = 1.0;
	if (uFoliageShade.y > 0.0)
	{
		vec3 rel = vWorld - vRoot;
		float up = clamp(rel.y / uFoliageShade.y, 0.0, 1.0);
		float baseOcc = 1.0 - up * up * (3.0 - 2.0 * up);
		float core = (1.0 - smoothstep(0.0, uFoliageShade.y, length(rel.xz))) * (1.0 - smoothstep(uFoliageShade.y, 3.0 * uFoliageShade.y, rel.y));
		leafOcc = (1.0 - uFoliageShade.x * baseOcc) * (1.0 - uFoliageShade.z * core);
		ambientOcc = leafOcc;
	}
	vec3 direct = uSunColor * (wrap * 0.9 + trans) * (shadow * (0.5 + 0.5 * leafOcc)) / 3.14159;
	vec3 ambient = SkyIrradiance(N) * uEnvIntensity * ambientOcc;
	vec3 color = col * (direct + ambient);
	if (uTranslucency > 0.0)   // T46: the PBR foliage's diffuse transmission, on top of the blades' own `trans`, stronger toward the tip
	{
		float through;
		if (uFoliageShade.w > 0.0)
		{
			// T128: the PBR foliage's spread (T107): the sun's direction bent by the normal and a wider lobe, and the grazing edges
			float spread = uFoliageShade.w;
			vec3 Lb = normalize(L + N * (0.6 * spread));
			float edge = pow(1.0 - abs(dot(N, V)), 3.0) * (0.5 + 0.5 * dot(V, -L));
			through = pow(clamp(dot(V, -Lb), 0.0, 1.0), 4.0 - 2.5 * spread) * 3.0 + clamp(dot(-N, L), 0.0, 1.0) + edge * 1.5 * spread;
		}
		else
			through = FoliageThrough(N, V, L);
		color += col * uTransmissionTint * (uTranslucency * vT * leafOcc) * uSunColor * (shadow / 3.14159) * through;
	}

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
