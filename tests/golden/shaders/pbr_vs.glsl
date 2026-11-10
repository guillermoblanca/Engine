#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
uniform mat4 uModel;
uniform mat4 uViewProj;
layout(binding = 8) uniform sampler2D uTrailMap;
uniform vec4 uTrailRect;       // window center x, z, size (m), texel size (m)
uniform float uTrailDepth;     // 0 = this material ignores ground marks
uniform vec3 uTrailTint;
uniform float uTrailBoost;
float TrailAt(vec2 xz)
{
	float edge = max(abs(xz.x - uTrailRect.x), abs(xz.y - uTrailRect.y)) / uTrailRect.z;
	return textureLod(uTrailMap, xz / uTrailRect.z, 0.0).r * (1.0 - smoothstep(0.38, 0.5, edge));
}
layout(location = 4) in uvec4 aJoints;
layout(location = 5) in vec4 aWeights;
layout(std430, binding = 0) readonly buffer JointBuffer { mat4 uJointMatrices[]; };
layout(std430, binding = 2) readonly buffer InstanceBuffer { mat4 uInstances[]; };
uniform int uInstanced;   // 1: one draw of many copies of the object, their matrices in uInstances (uModel unused)
uniform int uSkinned;
mat4 SkinMatrix()
{
	return aWeights.x * uJointMatrices[aJoints.x] + aWeights.y * uJointMatrices[aJoints.y]
		+ aWeights.z * uJointMatrices[aJoints.z] + aWeights.w * uJointMatrices[aJoints.w];
}
uniform int uHasHeight;
uniform float uHeightScale;
uniform float uUVScale;
layout(binding = 3) uniform sampler2D uHeightMap;
out vec3 vPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
uniform float uWindStrength;      // 0 = rigid; meters of sway at the top of the object per unit of wind
uniform float uWindHeight;        // object-space height at which the sway is full
uniform float uWindFlutter;       // small fast leaf motion (meters)
uniform float uTime;
uniform vec3 uWind;
vec3 WindOffset(vec3 localPos, vec3 worldPos, vec3 origin)
{
	float hw = clamp(localPos.y / max(uWindHeight, 1e-3), 0.0, 1.0);
	hw *= hw;
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(origin.xz, wd) * 0.12 - uTime * (0.8 + 0.1 * ws);
	float sway = (0.05 + 0.03 * ws) * (0.75 + 0.3 * sin(phase) + 0.2 * sin(phase * 2.3 + 1.0));
	vec3 off = vec3(wd.x, 0.0, wd.y) * sway * uWindStrength * hw;
	float fl = uWindFlutter * (0.35 + 0.08 * ws) * hw;
	off += vec3(sin(uTime * 4.1 + worldPos.z * 4.0), 0.4 * sin(uTime * 5.3 + worldPos.x * 3.0), sin(uTime * 3.7 + worldPos.y * 5.0)) * fl;
	return off;
}
void main()
{
	vec3 pos = aPos;
	if (uHasHeight != 0)
		pos += normalize(aNormal) * (textureLod(uHeightMap, aUV * uUVScale, 0.0).r - 0.5) * uHeightScale;
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
	if (uWindStrength > 0.0)
		world.xyz += WindOffset(aPos, world.xyz, model[3].xyz);
	if (uTrailDepth > 0.0)
		world.y -= TrailAt(world.xz) * uTrailDepth;   // footprints, tracks
	mat3 normalMatrix = mat3(transpose(inverse(model)));
	vPos = world.xyz;
	vNormal = normalMatrix * nrm;
	vTangent = vec4(mat3(model) * tng, aTangent.w);
	vUV = aUV * uUVScale;
	gl_Position = uViewProj * world;
}
