#version 450 core
in vec2 vUV;
out vec4 outAO;
layout(binding = 0) uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform float uRadius;
uniform float uBias;
uniform int uOrtho;

vec3 ViewPos(vec2 uv)
{
	float d = texture(uDepth, uv).r;
	vec4 clip = vec4(uv * 2.0 - 1.0, d, 1.0);   // (reversed Z in [0, 1], and uInvProj its inverse: T137)
	vec4 v = uInvProj * clip;
	return v.xyz / v.w;
}

float IGN(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main()
{
	float d0 = texture(uDepth, vUV).r;
	if (d0 <= 0.00001) { outAO = vec4(1.0); return; }   // the sky and the farthest ground (reversed Z: 1 - the 0.99999 of before)
	vec3 P = ViewPos(vUV);
	vec2 texel = 1.0 / vec2(textureSize(uDepth, 0));
	// normal from the neighbours with the smaller depth difference (avoids smearing across edges)
	vec3 Pr = ViewPos(vUV + vec2(texel.x, 0.0)), Pl = ViewPos(vUV - vec2(texel.x, 0.0));
	vec3 Pu = ViewPos(vUV + vec2(0.0, texel.y)), Pd = ViewPos(vUV - vec2(0.0, texel.y));
	vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
	vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
	vec3 N = normalize(cross(dx, dy));
	if (dot(N, -normalize(P)) < 0.0) N = -N;
	float angle = IGN(gl_FragCoord.xy) * 6.2831853;
	vec3 helper = abs(N.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0);
	vec3 T = normalize(cross(helper, N)), B = cross(N, T);
	float occ = 0.0;
	const int COUNT = 14;
	for (int i = 0; i < COUNT; ++i)
	{
		float u = (float(i) + 0.5) / float(COUNT);
		float z = u;                                   // uniform in height: more samples near the surface plane
		float phi = float(i) * 2.399963 + angle;
		float r = sqrt(1.0 - z * z);
		vec3 dir = T * (cos(phi) * r) + B * (sin(phi) * r) + N * z;
		float scale = mix(0.15, 1.0, u * u);
		vec3 S = P + dir * uRadius * scale;
		vec4 clip = uProj * vec4(S, 1.0);
		vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
		if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
		float sceneZ = ViewPos(uv).z;
		float range = smoothstep(0.0, 1.0, uRadius / max(abs(P.z - sceneZ), 1e-4));
		occ += (sceneZ >= S.z + uBias * (1.0 + abs(P.z) * 0.2) ? 1.0 : 0.0) * range;
	}
	float ao = 1.0 - occ / float(COUNT);
	outAO = vec4(ao * ao * (3.0 - 2.0 * ao) * 0.5 + ao * 0.5, 0.0, 0.0, 1.0);
}
