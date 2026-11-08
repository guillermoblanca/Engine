#pragma once
#include "GB/Scene/Vertex.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// An indexed triangle mesh on the CPU (no GL): what procedural geometry (Visual/ProcMesh.h) builds and what the render uploads.
	// Deterministic: the same operations give the same vertices bit for bit, so a test can hash them and the GPU cache can key them.
	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;

		size_t Triangles() const { return Indices.size() / 3; }
		bool Empty() const { return Indices.empty(); }
		// Adds `other` moved by `transform` (normals and tangents follow; a mirroring transform flips the winding back).
		void Append(const MeshData& other, const glm::mat4& transform = glm::mat4(1.0f));
		// Tangents from the UVs (needed by normal mapping and the grain bump); the shape builders call it.
		void ComputeTangents();
		// Axis-aligned bounds of the vertices (zero when empty).
		void Bounds(glm::vec3& min, glm::vec3& max) const;
		// A 64-bit FNV hash of the positions, normals, UVs and indices: the determinism checks compare it.
		uint64_t Hash() const;
	};
}
