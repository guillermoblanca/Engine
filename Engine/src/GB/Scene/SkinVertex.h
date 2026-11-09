#pragma once
#include <cstdint>

namespace GB
{
	// Per-vertex skinning data (glTF JOINTS_0 / WEIGHTS_0). Joint indices refer to the skin's joint list. Plain CPU data: the model loaders
	// fill it (GB/Assets/Model.h) and the render uploads it (GB/Renderer/Mesh.h).
	struct SkinVertex
	{
		uint32_t Joints[4] = { 0, 0, 0, 0 };
		float Weights[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	};
}
