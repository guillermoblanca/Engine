#pragma once
#include "Material.h"
#include "Mesh.h"
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	struct RenderObject
	{
		const Mesh* MeshPtr = nullptr;
		Material Mat;
		glm::mat4 Transform{ 1.0f };
		const std::vector<glm::mat4>* JointMatrices = nullptr;
		glm::vec3 BoundingCenter{ 0.0f };
		float BoundingRadius = 0.0f;
		float MaxDistance = 0.0f;
	};
}
