#pragma once
#include <glm/glm.hpp>

namespace GB
{
	// One vertex of a triangle mesh: plain data, no OpenGL. Anything that only needs to describe geometry (a river
	// ribbon, a generated terrain) includes this instead of the GPU Mesh.
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 UV;
		glm::vec4 Tangent;   // xyz = direction of increasing U, w = bitangent handedness (+1/-1)
	};
}
