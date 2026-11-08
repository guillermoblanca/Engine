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
		// The objects of a scene with this set and the same mesh share their material too (the trees of one variant): the renderer draws them
		// with one call (instancing) and the first one's material. Not for skinned or translucent objects.
		bool Instanced = false;
		// > 0: only these indices of the mesh are drawn (a tile of a terrain, whose one vertex buffer stays whole for editing)
		uint32_t IndexFirst = 0, IndexCount = 0;
	};
}
