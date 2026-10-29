#include "Visibility.h"

#include <algorithm>
#include <glm/geometric.hpp>

namespace GB
{
	Frustum MakeFrustum(const glm::mat4& viewProjection)
	{
		Frustum frustum;
		const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
		const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
		const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]);
		const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);

		frustum.Planes[0] = row3 + row0;
		frustum.Planes[1] = row3 - row0;
		frustum.Planes[2] = row3 + row1;
		frustum.Planes[3] = row3 - row1;
		frustum.Planes[4] = row3 + row2;
		frustum.Planes[5] = row3 - row2;
		for (glm::vec4& plane : frustum.Planes)
			plane /= glm::length(glm::vec3(plane));
		return frustum;
	}

	bool BoundsOf(const RenderObject& object, glm::vec3& center, float& radius)
	{
		if (object.BoundingRadius <= 0.0f)
			return false;

		center = glm::vec3(object.Transform * glm::vec4(object.BoundingCenter, 1.0f));
		const float scale = std::max({
			glm::length(glm::vec3(object.Transform[0])),
			glm::length(glm::vec3(object.Transform[1])),
			glm::length(glm::vec3(object.Transform[2]))
		});
		radius = object.BoundingRadius * scale;
		return true;
	}

	bool InFrustum(const Frustum& frustum, const glm::vec3& center, float radius)
	{
		for (const glm::vec4& plane : frustum.Planes)
		{
			if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
				return false;
		}
		return true;
	}
}
