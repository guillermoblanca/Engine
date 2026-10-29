#pragma once

#include "RenderObject.h"
#include <glm/glm.hpp>

namespace GB
{
	struct Frustum
	{
		glm::vec4 Planes[6];
	};

	Frustum MakeFrustum(const glm::mat4& viewProjection);

	// Computes the world-space bounding sphere for an object.
	// Returns false when the object does not provide a usable bound.
	bool BoundsOf(const RenderObject& object, glm::vec3& center, float& radius);

	bool InFrustum(const Frustum& frustum, const glm::vec3& center, float radius);
}
