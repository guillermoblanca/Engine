#pragma once
#include "GB/Renderer/Mesh.h"
#include <cstdint>
#include <vector>

namespace GB::detail
{
	// Shared by the glTF and FBX loaders: per-vertex normals from the faces, and tangents from the UVs.
	void GenerateNormals(std::vector<Vertex>& v, const std::vector<uint32_t>& idx);
	void GenerateTangents(std::vector<Vertex>& v, const std::vector<uint32_t>& idx);
}
