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
in vec3 vPos;
in vec4 vRiver;
in vec2 vFlowDir;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outAmbient;   // the ambient (indirect) part of outColor, for the SSAO (Renderer::m_SceneAmbient)
layout(binding = 1) uniform sampler2D uSceneDepth;
layout(binding = 2) uniform sampler2D uRipples;
layout(binding = 3) uniform sampler2D uSceneColor;   // opaque scene so far (refraction)
uniform vec3 uShallow;
uniform vec3 uDeep;
uniform float uClarity;
uniform float uRoughness;
uniform float uFoam;
uniform float uFoamWidth;
uniform float uRippleStrength;
uniform vec4 uRippleRect;     // window center x, z, size, texel size (size 0 = off)
uniform float uSSR;           // 0 = sky only, 1 = screen-space reflections
uniform float uOriginY;
uniform vec3 uOriginXZ;     // water center
uniform float uMask;          // > 0: discard beyond this radius


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
	if (uOrtho != 0) return uNearFar.y - d * (uNearFar.y - uNearFar.x);
	return uNearFar.x * uNearFar.y / (uNearFar.x + d * (uNearFar.y - uNearFar.x));
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
		float rayZ = LinearDepth(ndc.z);   // (reversed Z in [0, 1]: ndc z is the depth, T137)
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
				if (LinearDepth(cm.z / cm.w) > z) b = m; else a = m;
			}
			vec4 ch = uViewProj * vec4(b, 1.0);
			vec2 uvh = ch.xy / ch.w * 0.5 + 0.5;
			if (LinearDepth(texture(uSceneDepth, uvh).r) > uNearFar.y * 0.95) return vec4(0.0);   // that is the sky: use the environment instead
			// fade at the screen's edges and smoothly toward the end of the march (a step index would draw bands)
			float edge = smoothstep(0.0, 0.08, min(min(uvh.x, 1.0 - uvh.x), min(uvh.y, 1.0 - uvh.y)));
			float reach = 1.0 - smoothstep(0.55, 1.0, (float(i) + jitter) / 40.0);
			return vec4(texture(uSceneColor, uvh).rgb, edge * reach);
		}
	}
	return vec4(0.0);
}

// Integer lattice hash (stable at world coordinates of thousands of meters, where a fract-of-product hash goes regular).
float Hash(vec2 p)
{
	uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334673u, 3812015801u);
	uint n = (q.x ^ q.y) * 1597334673u;
	return float(n) * (1.0 / 4294967295.0);
}
float Noise(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(Hash(i), Hash(i + vec2(1, 0)), f.x), mix(Hash(i + vec2(0, 1)), Hash(i + vec2(1, 1)), f.x), f.y);
}
// Value noise and its gradient (x: value 0..1, yz: d/dp), quintic: the gradient is continuous, so normals made of it are.
vec3 NoiseD(vec2 p)
{
	vec2 i = floor(p), f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
	float a = Hash(i), b = Hash(i + vec2(1, 0)), c = Hash(i + vec2(0, 1)), d = Hash(i + vec2(1, 1));
	float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
	return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y, du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

// The pixel's footprint on the water (m) and how much of a band of wavelength `lambda` it can still show: all of it from
// eight pixels a wavelength, none under three (value noise and sharp crests hold detail finer than their period, and the
// sun's highlight turns any of it that aliases into glints).
float gFootprint;
float Band(float lambda) { return smoothstep(3.0, 8.0, lambda / gFootprint); }

// How coarse the depth buffer is at view depth z (one step, in meters). T137: reversed Z in 32-bit float stores about
// n / z, with a relative step of at most 2^-23, so a step is z * 2^-23 of view depth: 0.27 mm at 2.25 km. (The 24-bit
// buffer of before had z^2 (f - n) / (n f) / 2^24, meters far away, and SceneDepth averaged the pixels around in four or
// eight gathers to smooth the staircase it drew on a shallow bed; with steps this fine one sample is the bed.)
float DepthStep(float z)
{
	if (uOrtho != 0) return (uNearFar.y - uNearFar.x) * 5.9604645e-8;
	return z * 1.1920929e-7;
}

// Slope of the Gerstner set (the waves of lib/water_waves), band-limited; `lost` gathers the slope variance filtered out.
vec2 WaveSlope(vec2 p, inout float lost)
{
	vec2 slope = vec2(0.0);
	for (int i = 0; i < 4; ++i)
	{
		float c = cos(kAngle[i]), s = sin(kAngle[i]);
		vec2 d = vec2(uWaveDir.x * c - uWaveDir.y * s, uWaveDir.x * s + uWaveDir.y * c);
		float k = 6.2831853 / (uWaveLength * kLen[i]);
		float amp = uWaveHeight * kAmp[i] * (i == 0 ? 1.0 : uChop);
		float ph = k * dot(d, p) - k * sqrt(9.81 / k) * uTime;
		float w = Band(uWaveLength * kLen[i]);
		slope += d * (amp * k * cos(ph) * w);
		lost += 0.5 * (amp * k) * (amp * k) * (1.0 - w * w);
	}
	return slope;
}

// One band of noise slope: `g` the gradient of NoiseD in its own coordinates, `scale` meters per unit of each coordinate,
// `gain` the slope it stands for. Returns the slope (in those axes) band-limited by its smallest scale.
vec2 NoiseBand(vec2 g, vec2 scale, float gain, inout float lost)
{
	float w = Band(min(scale.x, scale.y));
	lost += gain * gain * 0.3 * (1.0 - w * w);   // value noise: mean square gradient ~0.3 per unit
	return g / scale * gain * w;
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
	// the footprint: the longer side of the pixel on the surface (grazing views stretch it along the view)
	vec2 dpx = dFdx(vPos.xz), dpy = dFdy(vPos.xz);
	gFootprint = max(max(length(dpx), length(dpy)), 1e-4);
	vec2 q = vPos.xz;
	float t = uTime;
	float lost = 0.0;   // slope variance the filtering removed (goes into the roughness)

	float speed = vRiver.z;
	// A current and still water are cross-faded by the speed (no seam where a river slows down into a lake).
	float flowing = smoothstep(0.0, 0.35, speed);

	// Wind: its direction steers the ripples; slow patches of gusts (tens of meters, drifting downwind) make some water
	// rougher than the rest, so no pattern repeats over a lake.
	float windSpeed = length(uWind.xz);
	vec2 wd = windSpeed > 0.01 ? uWind.xz / windSpeed : normalize(uWaveDir + vec2(1e-4));
	vec2 ws = vec2(-wd.y, wd.x);
	float wind = clamp(windSpeed / 2.0, 0.35, 1.6);
	vec2 drift = wd * t * 0.9;
	float gust = Noise((q - drift) / 47.0) * 0.65 + Noise((q - drift * 1.3) / 17.0 + 13.0) * 0.35;
	gust = mix(0.35, 1.35, smoothstep(0.2, 0.8, gust));

	// Swell: the Gerstner set, gentler on a current. Its shading reads it through a slow warp of a few meters, so the four
	// straight wave trains do not draw a regular lattice of highlights from a hundred meters (the geometry keeps them
	// straight: its heights are the CPU's, WaterSurface::HeightAt).
	vec2 warp = NoiseD(q / 23.0 + 3.0).yz * uWaveLength * 0.3;
	vec2 slope = WaveSlope(q + warp, lost) * mix(0.75 + 0.35 * gust, 0.3, flowing);
	lost *= mix(1.0, 0.1, flowing);

	float foamRapids = 0.0, foamBank = 0.0;   // rivers: the white water carried by the current (in the rapids, along the banks)
	if (flowing < 1.0)
	{
		// wind ripples: crests across the wind, travelling downwind at about the speed of waves that long
		vec2 c = vec2(dot(q, wd), dot(q, ws));
		// (a band the pixel cannot show is not sampled: only its variance is kept)
		float l = 0.0;
		vec2 rs = NoiseBand(NoiseD(vec2(c.x - t * 1.25, c.y * 0.55) / 1.6).yz, vec2(1.6, 1.6 / 0.55), 0.10, l);
		if (Band(0.55) > 0.0) rs += NoiseBand(NoiseD(vec2(c.x - t * 0.85, c.y * 0.6 + 7.0) / 0.55 + 31.0).yz, vec2(0.55, 0.55 / 0.6), 0.07, l);
		else l += 0.07 * 0.07 * 0.3;
		if (Band(0.21) > 0.0) rs += NoiseBand(NoiseD(vec2(c.x * 0.9 + c.y * 0.45 - t * 0.6, c.y * 0.9 - c.x * 0.45) / 0.21 + 57.0).yz, vec2(0.21), 0.05, l);
		else l += 0.05 * 0.05 * 0.3;
		float amount = uChop * wind * gust * (1.0 - flowing);
		slope += (wd * rs.x + ws * rs.y) * amount;
		lost += l * amount * amount;
	}
	if (flowing > 0.0)
	{
		// The current: a pattern stretched along the river, drawn in the ribbon's own coordinates (arc length, across) so it
		// follows the bends, and advected by the speed of each point (faster in the middle). Two copies half a cycle apart
		// take turns (each fades out as it jumps back), with the cycle's phase shifted by a slow noise so the river does
		// not pulse all at once.
		const float across = 3.0;   // meters per unit of `across` (the ribbon's half-width is a few meters)
		vec2 rc = vec2(vRiver.x, vRiver.y * across);
		const float cycle = 4.0;   // long enough that a patch is seen to travel before it fades
		float offset = Noise(rc * vec2(0.07, 0.25) + 3.0);
		float p0 = fract(t / cycle + offset), p1 = fract(p0 + 0.5);
		float w0 = 1.0 - abs(2.0 * p0 - 1.0), w1 = 1.0 - w0;
		float norm = inversesqrt(w0 * w0 + w1 * w1);   // keeps the contrast of the normals steady through the blend
		float fine = Band(0.4);
		vec2 fs = vec2(0.0);
		float l = 0.0;
		for (int k = 0; k < 2; ++k)
		{
			float ph = k == 0 ? p0 : p1;
			float wk = k == 0 ? w0 : w1;
			vec2 pc = rc - vec2(speed * cycle * (ph - 0.5), 0.0) + float(k) * vec2(17.3, 5.1);
			vec3 a = NoiseD(pc / vec2(3.2, 1.1));
			vec3 b = NoiseD(pc / vec2(1.1, 0.42) + 41.0);
			float lk = 0.0;
			vec2 fk = NoiseBand(a.yz, vec2(3.2, 1.1), 0.09, lk) + NoiseBand(b.yz, vec2(1.1, 0.42), 0.06, lk);
			if (Band(0.28) > 0.0) fk += NoiseBand(NoiseD(pc / vec2(0.36, 0.28) + 77.0).yz, vec2(0.36, 0.28), 0.03, lk);
			else lk += 0.03 * 0.03 * 0.3;
			fs += wk * norm * fk;
			l += wk * wk * norm * norm * lk;
			// the white water of this copy: thresholded here, so each copy's patches drift with it and fade, and the blend
			// never grows or shrinks a patch in place
			float s = a.x * 0.55 + b.x * 0.45;
			// ridged, turned off the axes (value noise lines up with its lattice): a net of bubbles
			float net = 0.6;
			if (fine > 0.0)
			{
				vec2 pn = mat2(0.8, -0.6, 0.6, 0.8) * (pc / vec2(0.3, 0.22));
				float r = 1.0 - abs(Noise(pn + 13.0) + Noise(pn * 1.9 + 29.0) * 0.6 - 0.8) * 1.25;
				net = mix(0.6, mix(0.3, 1.0, smoothstep(0.35, 0.85, r)), fine);
			}
			foamRapids += wk * smoothstep(0.55, 0.8, s) * net;
			foamBank += wk * smoothstep(0.45, 0.72, s) * net;
		}
		float rough = clamp(0.45 + speed * 0.35, 0.45, 1.6) * uChop * flowing;
		vec2 side = vec2(-vFlowDir.y, vFlowDir.x);
		slope += (vFlowDir * fs.x + side * fs.y) * rough;
		lost += l * rough * rough;
	}
	vec3 N = normalize(vec3(-slope.x, 1.0, -slope.y));

	// Ripples from rain, feet and swimmers.
	float ripple = 0.0, ringLight = 0.0;
	if (uRippleRect.z > 0.0)
	{
		float e = uRippleRect.w;
		float hL = RippleAt(vPos.xz - vec2(e, 0.0)), hR = RippleAt(vPos.xz + vec2(e, 0.0));
		float hD = RippleAt(vPos.xz - vec2(0.0, e)), hU = RippleAt(vPos.xz + vec2(0.0, e));
		vec2 g = vec2(hR - hL, hU - hD) / (2.0 * e);
		float wr = Band(e * 8.0);   // the rings are a few texels wide
		N = normalize(N + vec3(-g.x, 0.0, -g.y) * (uRippleStrength * 4.0 * wr));
		lost += dot(g, g) * (uRippleStrength * 4.0) * (uRippleStrength * 4.0) * (1.0 - wr * wr);
		ripple = RippleAt(vPos.xz) * wr;
		ringLight = clamp(length(g) * uRippleStrength * 5.0, 0.0, 1.0) * wr;
	}
	if (dot(N, V) < 0.0) N = reflect(N, vec3(0, 1, 0)); // keeps the horizon from flipping

	// Toksvig: the slope the pixel could not show widens the highlight instead of glittering.
	float a2 = uRoughness * uRoughness + lost;
	float alpha = sqrt(a2);

	// Thickness of the water: the scene's depth behind the surface along the view ray, turned into the vertical depth.
	vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSceneDepth, 0));
	// the surface's own view depth from its position (exact: no depth buffer rounding)
	float surfZ = dot(vPos - uCamPos, normalize(cross(uCamUp, uCamRight)));
	float dz = DepthStep(surfZ);
	float sceneZ = LinearDepth(texture(uSceneDepth, uv).r);
	float thickSigned = sceneZ - surfZ;
	float thick = max(thickSigned, 0.0);
	float rayLen = thick * camDist / max(surfZ, 1e-3);   // view z to distance along the ray
	float vdepth = rayLen * clamp(V.y, 0.0, 1.0);
	// How far the shore is, across the surface: the depth over how fast it changes (a flat shallow shelf is not all shore).
	float shelf = max(length(vec2(dFdx(vdepth), dFdy(vdepth))) / gFootprint, 0.03);
	float shoreDist = vdepth / shelf;
	// Where the depth buffer cannot tell the bed from the surface (meters of view depth per step), nothing that hangs on
	// the exact depth (the foam line, a hard edge) is drawn: the water there is only its colour. T137: with float depth
	// that is never the case in perspective, but the shore's foam band (at most ~1.5 m across) still falls under a pixel
	// far away and would draw a dotted line: it fades out as the pixel's footprint grows past a meter.
	float certain = (1.0 - smoothstep(0.1, 0.6, dz)) * (1.0 - smoothstep(0.8, 1.6, gFootprint));

	// Light reaching the bed and coming back: down through the depth, then up along the view ray, refracted (n = 1.33).
	float cosT = sqrt(1.0 - (1.0 - V.y * V.y) / 1.7689);
	float path = vdepth * (1.0 + 1.0 / max(cosT, 0.25));
	vec3 L = normalize(-uSunDir);
	vec3 amb = SkyIrradiance(vec3(0, 1, 0)) * uEnvIntensity * 0.55;
	// What the water scatters back: its shallow colour where it is thin, the deep colour where it is deep (fine silt
	// and plankton keep it from being a black hole).
	float absorb = 1.0 - exp(-vdepth / max(uClarity, 0.05));
	vec3 body = mix(uShallow, uDeep, pow(absorb, 0.8)) * (amb + uSunColor * max(L.y, 0.0) * 0.06);

	// Refraction: look through the surface along a bent ray. Only take samples that lie behind the water
	// (objects in front of the surface must not smear into it); the bend grows with the water depth.
	vec2 bend = N.xz * clamp(vdepth, 0.0, 1.5) * 0.12 / max(surfZ, 1.0) * 6.0;
	vec2 uvR = uv + bend;
	float zR = LinearDepth(texture(uSceneDepth, uvR).r);
	if (zR < surfZ) uvR = uv;
	vec3 seen = texture(uSceneColor, uvR).rgb;
	// Beer-Lambert along that path: red goes first, then green, blue survives longest; what is lost is replaced by the
	// light the water itself scatters toward the eye.
	vec3 T = exp(-vec3(0.9, 0.25, 0.12) * path / max(uClarity, 0.05));
	vec3 transmitted = seen * T + body * (1.0 - T);

	// Fresnel (F0 = 0.02, water), a little less at grazing angles on rough water; the sky prefiltered by the roughness.
	float cosV = clamp(dot(N, V), 0.0, 1.0);
	float F = 0.02 + (max(1.0 - alpha, 0.02) - 0.02) * pow(1.0 - cosV, 5.0);
	vec3 R = reflect(-V, N);
	R.y = abs(R.y);
	vec3 refl = SkyPrefiltered(R, min(alpha * 3.0, 1.0)) * uEnvIntensity;
	if (uSSR > 0.0)
	{
		vec4 hit = TraceReflection(vPos + N * 0.02, R, Hash(gl_FragCoord.xy));
		refl = mix(refl, hit.rgb, hit.a * uSSR * (1.0 - smoothstep(0.2, 0.5, alpha)));
	}
	// The sun: GGX at the filtered roughness, so a far lake shows a broad glitter patch rather than single glints.
	vec3 H = normalize(L + V);
	float nl = max(dot(N, L), 0.0), nv = max(cosV, 1e-3);
	float nh = max(dot(N, H), 0.0);
	float dd = nh * nh * (a2 - 1.0) + 1.0;
	float D = a2 / (3.14159265 * dd * dd);
	float vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2) + 1e-5);
	float Fh = 0.02 + 0.98 * pow(1.0 - max(dot(V, H), 0.0), 5.0);
	vec3 glint = uSunColor * min(D * vis * nl * Fh, 40.0) * step(0.0, L.y);
	// Lamps and torches glitter on the surface too.
	for (int i = 0; i < uPointCount; ++i)
	{
		vec3 toLight = uPointPosRadius[i].xyz - vPos;
		float d2 = dot(toLight, toLight);
		vec3 Lp = toLight * inversesqrt(d2);
		float w = 1.0;
		if (uPointPosRadius[i].w > 0.0) { float r = d2 / (uPointPosRadius[i].w * uPointPosRadius[i].w); w = clamp(1.0 - r * r, 0.0, 1.0); w *= w; }
		float nhp = max(dot(N, normalize(Lp + V)), 0.0);
		float dp = nhp * nhp * (a2 - 1.0) + 1.0;
		float sp = min(a2 / (3.14159265 * dp * dp) * 0.25 * max(dot(N, Lp), 0.0), 40.0);
		glint += uPointColor[i].rgb * (sp * w / (d2 + 0.01)) * F * step(0.0, Lp.y);
	}

	// Foam. At the shore: a broken, bubbly line where the water is shallow, with bands lapping in toward the land, and
	// whole stretches without it. On a river: white water in the rapids, and along the banks where the current is fast.
	float foam = 0.0;
	// (the noise is only worth sampling near a shore, on a crest or a ripple; the rest of a lake skips it)
	float foamy = (1.0 - smoothstep(0.0, uFoamWidth * 1.4, vdepth)) * (1.0 - smoothstep(0.4, 1.5, shoreDist)) * certain
		+ step(0.8, (vPos.y - uOriginY) / max(uWaveHeight * 2.2, 1e-3)) * (1.0 - flowing) + step(0.015, abs(ripple));
	if (uFoam > 0.0 && foamy > 0.0)
	{
		float fine = Band(0.4);
		float bubbles = Noise(q * 2.3 + vec2(t * 0.21, -t * 0.17)) * mix(1.0, 0.6, fine) + (Noise(q * 6.1 - t * 0.33) - 0.5) * 0.4 * fine + 0.2 * (1.0 - fine);
		bubbles = clamp(bubbles, 0.0, 1.0);
		// inside the foam, a net of bubbles with the water showing between them
		vec2 pn = mat2(0.8, -0.6, 0.6, 0.8) * (q * 4.0) + vec2(-t * 0.5, t * 0.35);
		float net = 1.0 - abs(Noise(pn + 21.0) + Noise(pn * 1.9 + 37.0) * 0.6 - 0.8) * 1.25;
		float gaps = smoothstep(0.38, 0.6, Noise(q / 9.0 + t * 0.015 + 5.0));
		float width = uFoamWidth * (0.5 + 0.9 * gaps);
		// bands lapping in toward the land (their phase falls with the depth as time runs)
		float lap = 0.5 + 0.5 * sin(6.2831853 * (vdepth / max(uFoamWidth * 1.4, 0.05) + t * 0.22) + bubbles * 2.5);
		lap = mix(0.5, lap, Band(uFoamWidth * 1.4));
		// the shore: shallow, and near the waterline across the surface too (at most ~1.5 m of a flat shelf)
		float shore = (1.0 - smoothstep(0.0, width, vdepth)) * (1.0 - smoothstep(0.4, 1.5, shoreDist));
		// (on a current the still-water bubbles would not move with it: the banks' white water below stands in for them)
		float cover = shore * mix(0.55, 1.0, lap) * mix(0.1, 1.0, gaps) * certain * (1.0 - 0.7 * flowing);
		// crests of still water only: a river's ribbon lies in world space (its origin is 0), its height is not a wave's
		float crest = smoothstep(0.8, 1.0, (vPos.y - uOriginY) / max(uWaveHeight * 2.2, 1e-3)) * smoothstep(0.45, 0.9, bubbles) * Band(uWaveLength * 0.6) * (1.0 - flowing);
		float rip = smoothstep(0.015, 0.06, abs(ripple)) * 0.5;
		cover = clamp((cover + crest * 0.6 + rip) * uFoam, 0.0, 1.0);
		// the foam shows where the bubbles rise over what the cover leaves: a ragged edge, softer as the pixel grows
		float soft = 0.25 + 0.3 * (1.0 - fine);
		foam = smoothstep(1.0 - cover, 1.0 - cover + soft, bubbles + 0.2 * cover) * smoothstep(0.0, 0.1, cover);
		// foam is a lace of bubbles, not a sheet: the water shows through it, less where it is thick
		foam *= mix(0.6, 0.95, smoothstep(0.4, 0.9, cover)) * mix(1.0, mix(0.25, 1.0, smoothstep(0.35, 0.85, net)), fine);
		foam *= clamp(uFoamWidth * 3.0 / gFootprint, 0.0, 1.0);   // a line thinner than a pixel covers part of it
	}
	if (uFoam > 0.0 && flowing > 0.0)
	{
		// on a river: white water carried by the current where it is fast, and along its banks (less tied to the foam
		// amount, which is the shore's)
		float bank = smoothstep(0.75, 1.0, abs(vRiver.y)) * smoothstep(0.8, 2.6, speed) * foamBank;
		float rapids = smoothstep(1.4, 3.4, speed) * foamRapids;
		foam = max(foam, max(bank, rapids) * flowing * (0.5 + 0.4 * min(uFoam, 1.0)));
	}
	foam = clamp(foam, 0.0, 1.0);
	vec3 foamColor = (amb + uSunColor * max(L.y, 0.0) * 0.1) * vec3(1.6);

	vec3 color = mix(transmitted, refl, F) + glint;
	color += (uSkyHorizon * uEnvIntensity * 0.8 + 0.06) * ringLight * 0.8;   // rings catch the light of the sky
	color = mix(color, foamColor, foam);
	// no hard edge where the water meets the shore; far away the edge is as soft as the depth buffer is coarse
	float opacity = smoothstep(0.3 * dz, 0.05 + 1.5 * dz, thickSigned);
	opacity = max(opacity, foam * smoothstep(0.0, 0.01, thick));
	opacity *= vRiver.w;   // a river dissolves into the lake it flows into

	if (uFogDensity > 0.0)
	{
		float density = uFogDensity * exp(-uFogHeight * max(vPos.y, 0.0));
		color = mix(uFogColor * uEnvIntensity, color, exp(-density * density * camDist * camDist));
	}
	outColor = vec4(min(color, vec3(64.0)), opacity);
	outAmbient = vec4(0.0, 0.0, 0.0, opacity);   // no SSAO on the water: it hides the ambient of the bed behind it
}
