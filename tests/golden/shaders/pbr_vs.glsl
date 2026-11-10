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
struct MaterialData
{
	vec4 BaseColor;     // rgb albedo, a opacity
	vec4 Emissive;      // rgb emissive, a alpha cutoff
	vec4 Surface;       // x metallic, y roughness, z ao, w occlusion strength
	vec4 Uv;            // x uv scale, y normal strength, zw normal map sign per axis
	uvec4 Tex;          // x TEX_* bits: the maps bound
	uvec4 Flags;        // x LAYER_* bits, y shading model, z alpha mode (0 opaque, 1 mask, 2 blend), w FLAG_* bits
	vec4 Wind;          // x sway, y height of full sway, z flutter
	vec4 ModelParams;   // standard: x height (displacement) scale, y cavity strength, z index of refraction, w transmission (LAYER_TRANSMISSION)
	vec4 Layer[12];     // the layers' slots (SLOT_*)
};
layout(std430, binding = 3) readonly buffer MaterialBuffer { MaterialData uMaterials[]; };
uniform int uMaterialId;
#define MATERIAL uMaterials[uMaterialId]
const uint TEX_ALBEDO = 1u;
const uint TEX_NORMAL = 2u;
const uint TEX_HEIGHT_MAP = 4u;
const uint TEX_HEIGHT = 8u;
const uint TEX_MR = 16u;
const uint TEX_OCCLUSION = 32u;
const uint TEX_EMISSIVE = 64u;
const uint TEX_MACRO = 128u;
const uint TEX_TERRAIN_NORMALS = 256u;
const uint TEX_INTERIOR = 512u;
const uint FLAG_FOLIAGE_NORMALS = 1u;
const uint FLAG_ALPHA_TO_COVERAGE = 2u;
const uint FLAG_DOUBLE_SIDED = 4u;
const uint LAYER_GRAIN = 1u;
const uint LAYER_MOSS = 2u;
const uint LAYER_SAND = 4u;
const uint LAYER_WATER_LINE = 8u;
const uint LAYER_TRAIL = 16u;
const uint LAYER_MACRO = 32u;
const uint LAYER_TRANSMISSION = 64u;
const uint LAYER_FOLIAGE = 128u;
const uint SLOT_GRAIN = 0u;        // x amount, y scale
const uint SLOT_MOSS = 1u;         // rgb color, w amount
const uint SLOT_SAND = 2u;         // rgb color, w amount
const uint SLOT_WATER_LINE = 3u;   // x water line (-1e9: none), y the sand's base height
const uint SLOT_TRAIL = 4u;        // x depth (0: none), y normal boost
const uint SLOT_TRAIL_TINT = 5u;   // rgb tint
const uint SLOT_MACRO = 6u;        // the macro map's rect
const uint SLOT_TRANSMISSION = 7u; // rgb absorption (1/m), w thickness (m)
const uint SLOT_INTERIOR = 8u;     // rgb interior tint, w strength
const uint SLOT_TRANSMISSION_EXTRA = 9u;   // x interior depth (uv), y scatter, z shadow density, w thickness variation
const uint SLOT_FOLIAGE = 10u;     // rgb transmission tint, w translucency
const uint SLOT_FOLIAGE_EXTRA = 11u;   // x colour variation, y normal blend, z macro variation, w alpha mip scale
const uint LAYER_SLOTS = 12u;
bool HasTex(uint bit) { return (MATERIAL.Tex.x & bit) != 0u; }
bool HasFlag(uint bit) { return (MATERIAL.Flags.w & bit) != 0u; }
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;
uniform mat4 uModel;
layout(binding = 8) uniform sampler2D uTrailMap;
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
layout(binding = 3) uniform sampler2D uHeightMap;
out vec3 vPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
flat out vec3 vOrigin;   // T46: the object's (or the instance's) origin in the world: the foliage's per-instance hash and crown centre
// Tree / foliage sway: bend grows with height, a travelling gust wave, and high-frequency flutter.
vec3 WindOffset(vec3 localPos, vec3 worldPos, vec3 origin)
{
	float hw = clamp(localPos.y / max(MATERIAL.Wind.y, 1e-3), 0.0, 1.0);
	hw *= hw;
	float ws = length(uWind.xz);
	vec2 wd = ws > 0.01 ? uWind.xz / ws : vec2(1.0, 0.0);
	float phase = dot(origin.xz, wd) * 0.12 - uTime * (0.8 + 0.1 * ws);
	float sway = (0.05 + 0.03 * ws) * (0.75 + 0.3 * sin(phase) + 0.2 * sin(phase * 2.3 + 1.0));
	vec3 off = vec3(wd.x, 0.0, wd.y) * sway * MATERIAL.Wind.x * hw;
	float fl = MATERIAL.Wind.z * (0.35 + 0.08 * ws) * hw;
	off += vec3(sin(uTime * 4.1 + worldPos.z * 4.0), 0.4 * sin(uTime * 5.3 + worldPos.x * 3.0), sin(uTime * 3.7 + worldPos.y * 5.0)) * fl;
	return off;
}
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
