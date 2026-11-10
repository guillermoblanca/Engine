#version 450 core
in vec3 vPos;
in vec2 vFlow;
in float vBlend;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
layout(binding = 1) uniform sampler2D uSceneDepth;
layout(binding = 2) uniform sampler2D uRipples;
layout(binding = 3) uniform sampler2D uSceneColor;   // opaque scene so far (refraction)
uniform vec3 uCamPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uEnvIntensity;
uniform vec2 uNearFar;
uniform int uOrtho;
uniform vec3 uShallow;
uniform vec3 uDeep;
uniform float uClarity;
uniform float uRoughness;
uniform float uFoam;
uniform float uFoamWidth;
uniform float uRippleStrength;
uniform vec4 uRippleRect;     // window center x, z, size, texel size (size 0 = off)
uniform mat4 uViewProj;
uniform int uPointCount;
uniform vec3 uPointPos[32];
uniform vec3 uPointColor[32];
uniform float uPointRadius[32];
uniform float uSSR;           // 0 = sky only, 1 = screen-space reflections
uniform float uOriginY;
uniform vec3 uOriginXZ;     // water center
uniform float uMask;          // > 0: discard beyond this radius

uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyGround;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogHeight;
uniform float uWetness;
uniform float uSnowCover;

layout(binding = 9) uniform sampler2D uEnvMap;
uniform int uHasEnvMap;
uniform float uEnvMaxLod;
uniform float uEnvRotation;
uniform float uEnvExposure;

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

uniform float uTime;
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

float LinearDepth(float d)
{
	if (uOrtho != 0) return uNearFar.x + d * (uNearFar.y - uNearFar.x);
	float z = d * 2.0 - 1.0;
	return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}

// Screen-space reflection: march the reflected ray through the depth buffer. Returns rgb + confidence.
vec4 TraceReflection(vec3 origin, vec3 dir, float jitter)
{
	float stepLen = 0.35 + 0.002 * length(origin - uCamPos);
	vec3 p = origin + dir * stepLen * jitter;
	for (int i = 0; i < 40; ++i)
	{
		p += dir * stepLen;
		stepLen *= 1.12;
		vec4 clip = uViewProj * vec4(p, 1.0);
		if (clip.w <= 0.0) break;
		vec3 ndc = clip.xyz / clip.w;
		vec2 uv = ndc.xy * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
		float sceneZ = LinearDepth(texture(uSceneDepth, uv).r);
		float rayZ = LinearDepth(ndc.z * 0.5 + 0.5);
		float diff = rayZ - sceneZ;
		if (diff > 0.0 && diff < stepLen * 2.5 + 0.3)
		{
			// refine between the last two samples
			vec3 a = p - dir * stepLen / 1.12, b = p;
			for (int k = 0; k < 5; ++k)
			{
				vec3 m = (a + b) * 0.5;
				vec4 cm = uViewProj * vec4(m, 1.0);
				vec2 uvm = cm.xy / cm.w * 0.5 + 0.5;
				float z = LinearDepth(texture(uSceneDepth, uvm).r);
				if (LinearDepth(cm.z / cm.w * 0.5 + 0.5) > z) b = m; else a = m;
			}
			vec4 ch = uViewProj * vec4(b, 1.0);
			vec2 uvh = ch.xy / ch.w * 0.5 + 0.5;
			if (LinearDepth(texture(uSceneDepth, uvh).r) > uNearFar.y * 0.95) return vec4(0.0);   // that is the sky: use the environment instead
			float edge = smoothstep(0.0, 0.08, min(min(uvh.x, 1.0 - uvh.x), min(uvh.y, 1.0 - uvh.y)));
			return vec4(texture(uSceneColor, uvh).rgb, edge * (1.0 - float(i) / 40.0));
		}
	}
	return vec4(0.0);
}

float Hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float Noise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1, 0)), f.x), mix(Hash(i + vec2(0, 1)), Hash(i + vec2(1, 1)), f.x), f.y);
}

vec2 Detail(vec2 q, float t)
{
	vec2 n1 = vec2(Noise(q * 1.7 + vec2(t * 0.35, t * 0.2)), Noise(q * 1.7 + 17.0 + vec2(-t * 0.25, t * 0.3))) - 0.5;
	vec2 n2 = vec2(Noise(q * 4.3 + vec2(-t * 0.5, t * 0.4)), Noise(q * 4.3 + 31.0 + vec2(t * 0.45, -t * 0.35))) - 0.5;
	return n1 * 0.14 + n2 * 0.07;
}

float RippleAt(vec2 xz)
{
	float edge = max(abs(xz.x - uRippleRect.x), abs(xz.y - uRippleRect.y)) / uRippleRect.z;
	return textureLod(uRipples, xz / uRippleRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}

void main()
{
	if (uMask > 0.0 && length(vPos.xz - uOriginXZ.xz) > uMask)
		discard;
	vec3 V = normalize(uCamPos - vPos);
	float camDist = length(uCamPos - vPos);
	float fade = 1.0 - smoothstep(30.0, 120.0, length(vPos.xz - uCamPos.xz));
	vec2 slope;
	Waves(vPos.xz - vec2(0.0), fade, slope);   // slope of the wave sum (position offsets are tiny for shading)
	vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));

	// Fine detail: two noise layers, fading with distance. Still water scrolls them slowly; a current advects
	// them along the flow (two phases cross-faded so the pattern never visibly resets).
	vec2 q = vPos.xz;
	float t = uTime;
	float speed = length(vFlow);
	// The two are cross-faded by the current's speed (no seam where a river slows down into the still water of a lake).
	float flowing = smoothstep(0.0, 0.35, speed);
	vec2 detail = Detail(q, t) * uChop * fade * (1.0 - flowing);
	if (flowing > 0.0)
	{
		float p0 = fract(t * 0.55), p1 = fract(t * 0.55 + 0.5);
		float wgt = abs(2.0 * p0 - 1.0);
		vec2 da = Detail(q - vFlow * p0 * 1.8, 0.0), db = Detail(q - vFlow * p1 * 1.8, 0.0);
		detail += mix(da, db, wgt) * (1.0 + speed * 0.5) * 2.2 * uChop * fade * flowing;
	}
	N = normalize(N + vec3(detail.x, 0.0, detail.y));

	// Ripples from rain, feet and swimmers.
	float ripple = 0.0, ringLight = 0.0;
	if (uRippleRect.z > 0.0)
	{
		float e = uRippleRect.w;
		float hL = RippleAt(vPos.xz - vec2(e, 0.0)), hR = RippleAt(vPos.xz + vec2(e, 0.0));
		float hD = RippleAt(vPos.xz - vec2(0.0, e)), hU = RippleAt(vPos.xz + vec2(0.0, e));
		vec2 g = vec2(hR - hL, hU - hD) / (2.0 * e);
		N = normalize(N + vec3(-g.x, 0.0, -g.y) * (uRippleStrength * 4.0));
		ripple = RippleAt(vPos.xz);
		ringLight = clamp(length(g) * uRippleStrength * 5.0, 0.0, 1.0);
	}
	if (dot(N, V) < 0.0) N = reflect(N, vec3(0, 1, 0)); // keeps the horizon from flipping

	// Thickness of the water along the view ray.
	vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneDepth, 0));
	float sceneZ = LinearDepth(texture(uSceneDepth, uv).r);
	float surfZ = LinearDepth(gl_FragCoord.z);
	float thick = max(sceneZ - surfZ, 0.0);
	// Vertical depth is what colors the water; the slanted view ray only changes how hidden the bottom is.
	float vdepth = thick * clamp(V.y, 0.05, 1.0);

	float absorb = 1.0 - exp(-vdepth / max(uClarity, 0.05));
	vec3 body = mix(uShallow, uDeep, pow(absorb, 0.8));
	vec3 L = normalize(-uSunDir);
	vec3 amb = SkyIrradiance(vec3(0, 1, 0)) * uEnvIntensity * 0.55;
	body *= amb + uSunColor * max(dot(vec3(0, 1, 0), L), 0.0) * 0.06;

	// Refraction: look through the surface along a bent ray. Only take samples that lie behind the water
	// (objects in front of the surface must not smear into it); the bend grows with the water depth.
	vec2 bend = N.xz * clamp(vdepth, 0.0, 1.5) * 0.12 / max(surfZ, 1.0) * 6.0;
	vec2 uvR = uv + bend;
	float zR = LinearDepth(texture(uSceneDepth, uvR).r);
	if (zR < surfZ) uvR = uv;
	vec3 seen = texture(uSceneColor, uvR).rgb;
	// Beer-Lambert: red goes first, then green, blue survives longest; what is lost is replaced by scattered body color.
	vec3 T = exp(-vec3(0.9, 0.25, 0.12) * vdepth / max(uClarity, 0.05));
	vec3 transmitted = seen * T + body * (1.0 - dot(T, vec3(0.3333)));

	// Fresnel sky reflection with a sharp sun glint.
	float cosT = clamp(dot(N, V), 0.0, 1.0);
	float F = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);
	F = clamp(F, 0.0, 1.0);
	vec3 R = reflect(-V, N);
	R.y = abs(R.y);
	vec3 refl = SkyPrefiltered(R, uRoughness * 3.0) * uEnvIntensity;
	if (uSSR > 0.0)
	{
		vec4 hit = TraceReflection(vPos + N * 0.02, R, Hash(gl_FragCoord.xy));
		refl = mix(refl, hit.rgb, hit.a * uSSR);
	}
	vec3 H = normalize(L + V);
	float a2 = max(uRoughness * uRoughness, 1e-3);
	float nh = max(dot(N, H), 0.0);
	float spec = min(pow(nh, 2.0 / a2 - 1.0) * (2.0 / a2) * 0.05, 60.0);
	vec3 glint = uSunColor * spec * step(0.0, L.y) * F;
	// Lamps and torches glitter on the surface too.
	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPos[i] - vPos;
		float d2 = dot(toLight, toLight);
		vec3 Lp = toLight * inversesqrt(d2);
		float w = 1.0;
		if (uPointRadius[i] > 0.0) { float r = d2 / (uPointRadius[i] * uPointRadius[i]); w = clamp(1.0 - r * r, 0.0, 1.0); w *= w; }
		float nhp = max(dot(N, normalize(Lp + V)), 0.0);
		float sp = min(pow(nhp, 2.0 / a2 - 1.0) * (2.0 / a2) * 0.05, 40.0);
		glint += uPointColor[i] * (sp * w / (d2 + 0.01)) * F * step(0.0, Lp.y);
	}

	// Foam at the shore, on crests and where ripples are strong.
	float foam = 0.0;
	if (uFoam > 0.0)
	{
		float nz = Noise(q * 3.0 + vec2(t * 0.3, -t * 0.2)) * 0.6 + Noise(q * 9.0 - t * 0.5) * 0.4;
		float shore = 1.0 - smoothstep(0.0, uFoamWidth * (0.6 + 0.8 * nz), vdepth);
		float wave = 0.5 + 0.5 * sin(vdepth * 7.0 - t * 1.6 + nz * 5.0);   // lapping bands
		shore *= mix(0.55, 1.0, wave);
		float crest = smoothstep(0.8, 1.0, (vPos.y - uOriginY) / max(uWaveHeight * 2.2, 1e-3)) * smoothstep(0.45, 0.9, nz);
		float rip = smoothstep(0.015, 0.06, abs(ripple)) * 0.5;
		if (speed > 0.02)
		{
			float p0 = fract(t * 0.55), p1 = fract(t * 0.55 + 0.5);
			float fa = Noise((q - vFlow * p0 * 1.8) * vec2(2.2, 2.2)), fb = Noise((q - vFlow * p1 * 1.8) * 2.2);
			float streak = mix(fa, fb, abs(2.0 * p0 - 1.0));
			rip += smoothstep(1.7, 3.6, speed) * smoothstep(0.55, 0.85, streak) * 0.9;   // white water in the rapids
		}
		foam = clamp((shore + crest * 0.6 + rip) * uFoam, 0.0, 1.0) * smoothstep(0.35, 0.65, nz + shore * 0.5);
	}
	vec3 foamColor = (amb + uSunColor * max(L.y, 0.0) * 0.1) * vec3(1.6);

	vec3 color = mix(transmitted, refl, F) + glint;
	color += (uSkyHorizon * uEnvIntensity * 0.8 + 0.06) * ringLight * 0.8;   // rings catch the light of the sky
	color = mix(color, foamColor, foam);
	float alpha = smoothstep(0.0, 0.05, thick);   // no hard edge where the water meets the shore
	alpha = max(alpha, foam * smoothstep(0.0, 0.01, thick));
	alpha *= vBlend;   // a river dissolves into the lake it flows into

	if (uFogDensity > 0.0)
	{
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		color = mix(uFogColor * uEnvIntensity, color, exp(-density * density * camDist * camDist));
	}
	outColor = vec4(min(color, vec3(64.0)), alpha);
	outAmbient = vec4(0.0, 0.0, 0.0, alpha);   // no SSAO on the water: it hides the ambient of the bed behind it
}
