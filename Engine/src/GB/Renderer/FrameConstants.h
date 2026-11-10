#pragma once

// The per-frame constants (render M3, T69; DESACOPLE_MATERIALES §2.3): what every program of the shadow and scene passes reads about the frame
// (camera, sun, sky, fog, environment map, weather, time, wind, shadow map and point lights). Renderer::Render fills one each frame, writes it
// into its upload ring and binds it at Binding once; Sky, PBR, Shadow, Grass, Water and Particle read it as the uniform block of `lib/frame`
// (ShaderLib/Pieces.h). Before, each program got these as ~30 loose uniforms, set again per program.
//
// The C++ mirror of that block: std140, the same members in the same order. A vec3 shares its 16 B slot with the float after it; the point
// lights are vec4 (in std140 a vec3 or float array has a 16 B stride). The static_asserts pin every offset to the GLSL layout; the test
// rhi_gl compares them with what the driver reports for the block. CPU only: no GL here.
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace GB
{
	struct FrameConstants
	{
		static constexpr uint32_t Binding = 0;          // layout(std140, binding = 0) uniform FrameBlock
		static constexpr uint32_t MaxPointLights = 32;  // the arrays of the block (Renderer::MaxPointLights)

		glm::mat4 ViewProj{ 1.0f };
		glm::mat4 InvViewProj{ 1.0f };
		glm::mat4 LightVP{ 1.0f };
		glm::vec3 CamPos{ 0.0f };      float EnvIntensity = 0.0f;
		glm::vec3 SunDir{ 0.0f };      float Time = 0.0f;           // SunDir: normalized, the direction the light travels
		glm::vec3 SunColor{ 0.0f };    float ShadowTexel = 0.0f;    // SunColor: color * intensity; ShadowTexel: world size of a shadow map texel
		glm::vec3 SkyTop{ 0.0f };      float FogDensity = 0.0f;
		glm::vec3 SkyHorizon{ 0.0f };  float FogHeight = 0.0f;
		glm::vec3 SkyGround{ 0.0f };   float Wetness = 0.0f;
		glm::vec3 FogColor{ 0.0f };    float SnowCover = 0.0f;
		glm::vec3 Wind{ 0.0f };        float EnvMaxLod = 0.0f;
		glm::vec3 CamRight{ 0.0f };    float EnvRotation = 0.0f;    // CamRight, CamUp: the camera's axes in world space
		glm::vec3 CamUp{ 0.0f };       float EnvExposure = 0.0f;
		glm::vec4 TrailRect{ 0.0f };                                // ground marks window: center x, z, size (m), texel size (m)
		glm::vec2 NearFar{ 0.0f };     float Stars = 0.0f;          float CloudCover = 0.0f;
		int32_t ShadowsOn = 0;         int32_t PointCount = 0;      int32_t Ortho = 0;           int32_t HasEnvMap = 0;
		glm::vec4 PointPosRadius[MaxPointLights] = {};              // xyz position, w radius
		glm::vec4 PointColor[MaxPointLights] = {};                  // rgb color * intensity, w unused
	};

	static_assert(sizeof(glm::vec3) == 12 && sizeof(glm::vec4) == 16 && sizeof(glm::mat4) == 64, "FrameConstants needs tightly packed glm types");
	static_assert(offsetof(FrameConstants, ViewProj) == 0);
	static_assert(offsetof(FrameConstants, InvViewProj) == 64);
	static_assert(offsetof(FrameConstants, LightVP) == 128);
	static_assert(offsetof(FrameConstants, CamPos) == 192 && offsetof(FrameConstants, EnvIntensity) == 204);
	static_assert(offsetof(FrameConstants, SunDir) == 208 && offsetof(FrameConstants, Time) == 220);
	static_assert(offsetof(FrameConstants, SunColor) == 224 && offsetof(FrameConstants, ShadowTexel) == 236);
	static_assert(offsetof(FrameConstants, SkyTop) == 240 && offsetof(FrameConstants, FogDensity) == 252);
	static_assert(offsetof(FrameConstants, SkyHorizon) == 256 && offsetof(FrameConstants, FogHeight) == 268);
	static_assert(offsetof(FrameConstants, SkyGround) == 272 && offsetof(FrameConstants, Wetness) == 284);
	static_assert(offsetof(FrameConstants, FogColor) == 288 && offsetof(FrameConstants, SnowCover) == 300);
	static_assert(offsetof(FrameConstants, Wind) == 304 && offsetof(FrameConstants, EnvMaxLod) == 316);
	static_assert(offsetof(FrameConstants, CamRight) == 320 && offsetof(FrameConstants, EnvRotation) == 332);
	static_assert(offsetof(FrameConstants, CamUp) == 336 && offsetof(FrameConstants, EnvExposure) == 348);
	static_assert(offsetof(FrameConstants, TrailRect) == 352);
	static_assert(offsetof(FrameConstants, NearFar) == 368 && offsetof(FrameConstants, Stars) == 376 && offsetof(FrameConstants, CloudCover) == 380);
	static_assert(offsetof(FrameConstants, ShadowsOn) == 384 && offsetof(FrameConstants, PointCount) == 388 && offsetof(FrameConstants, Ortho) == 392 &&
		offsetof(FrameConstants, HasEnvMap) == 396);
	static_assert(offsetof(FrameConstants, PointPosRadius) == 400);
	static_assert(offsetof(FrameConstants, PointColor) == 912);
	static_assert(sizeof(FrameConstants) == 1424, "3 x 64 + 13 x 16 + 64 x 16 B (std140)");
}
