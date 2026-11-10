#pragma once
#include "GB/Renderer/RHI/RHI.h"
#include "GB/Scene/SkinVertex.h"
#include "GB/Scene/Vertex.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Immutable indexed triangle mesh stored on the GPU.
	class Mesh
	{
	public:
		// `skin` (optional) must have one entry per vertex.
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
			const std::vector<SkinVertex>* skin = nullptr);
		~Mesh();
		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;
		Mesh(Mesh&& other) noexcept;
		Mesh& operator=(Mesh&& other) noexcept;

		void Draw() const;
		void DrawInstanced(uint32_t count) const;   // `count` copies in one call (gl_InstanceID tells them apart)
		void DrawRange(uint32_t firstIndex, uint32_t indexCount) const;   // part of the triangles (a terrain's tile)
		// Overwrites `count` vertices starting at `first` (e.g. terrain that erodes). The mesh keeps its topology.
		void UpdateVertices(uint32_t first, const Vertex* vertices, uint32_t count);
		uint32_t GetIndexCount() const { return m_IndexCount; }
		bool HasSkin() const { return static_cast<bool>(m_SkinVBO); }

		// Procedural primitives (counter-clockwise, outward normals).
		static Mesh CreateSphere(float radius = 0.5f, uint32_t segments = 64, uint32_t rings = 32);
		static Mesh CreatePlane(float size = 1.0f, float uvScale = 1.0f);
		// Plane in XZ (normal +Y) with (subdivisions+1)^2 vertices, UV 0..1; for displacement mapping.
		static Mesh CreateGrid(float size, uint32_t subdivisions);
		static Mesh CreateCube(float size = 1.0f);

	private:
		void Release();
		RHI::VertexLayoutHandle m_VAO;
		RHI::BufferHandle m_VBO, m_IBO, m_SkinVBO;
		uint32_t m_IndexCount = 0;
	};
}
