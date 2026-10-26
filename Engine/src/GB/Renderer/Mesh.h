#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 UV;
<<<<<<< HEAD
		glm::vec4 Tangent;   // xyz = direction of increasing U, w = bitangent handedness (+1/-1)
=======
		glm::vec3 Tangent;   // direction of increasing U
>>>>>>> origin/master
	};

	// Immutable indexed triangle mesh stored on the GPU.
	class Mesh
	{
	public:
		Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
		~Mesh();
		Mesh(const Mesh&) = delete;
		Mesh& operator=(const Mesh&) = delete;
		Mesh(Mesh&& other) noexcept;
		Mesh& operator=(Mesh&& other) noexcept;

		void Draw() const;
		uint32_t GetIndexCount() const { return m_IndexCount; }

		// Procedural primitives (counter-clockwise, outward normals).
		static Mesh CreateSphere(float radius = 0.5f, uint32_t segments = 64, uint32_t rings = 32);
		static Mesh CreatePlane(float size = 1.0f, float uvScale = 1.0f);
		// Plane in XZ (normal +Y) with (subdivisions+1)^2 vertices, UV 0..1; for displacement mapping.
		static Mesh CreateGrid(float size, uint32_t subdivisions);
		static Mesh CreateCube(float size = 1.0f);

	private:
		uint32_t m_VAO = 0, m_VBO = 0, m_IBO = 0;
		uint32_t m_IndexCount = 0;
	};
}
