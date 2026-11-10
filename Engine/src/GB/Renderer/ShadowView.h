#pragma once
// The sun's shadow map view-projection (CPU only, no GL): an orthographic box of `radius` around `center`, snapped to the map's texel grid.
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	// `sunDir` is the direction the light travels. As `center` follows the camera, the snapped map moves in whole texels, so a point of the
	// world always lands on the same spot inside its texel and shadow edges stay put instead of shimmering. The projection is orthographic,
	// so shifting it by where the world origin lands is enough.
	inline glm::mat4 ShadowViewProj(const glm::vec3& center, const glm::vec3& sunDir, float radius, unsigned mapSize)
	{
		const glm::vec3 dir = glm::normalize(sunDir);
		const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
		const glm::mat4 view = glm::lookAt(center - dir * radius * 2.0f, center, up);
		glm::mat4 vp = glm::ortho(-radius, radius, -radius, radius, 0.0f, radius * 4.0f) * view;
		const float half = 0.5f * static_cast<float>(mapSize);
		const glm::vec4 origin = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const glm::vec2 inTexels = glm::vec2(origin) * half;
		const glm::vec2 snap = (glm::round(inTexels) - inTexels) / half;
		vp[3][0] += snap.x;
		vp[3][1] += snap.y;
		return vp;
	}
}
