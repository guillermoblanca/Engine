#include "Mesh.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace GB
{
	Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
		: m_IndexCount(static_cast<uint32_t>(indices.size()))
	{
		glCreateBuffers(1, &m_VBO);
		glNamedBufferStorage(m_VBO, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);
		glCreateBuffers(1, &m_IBO);
		glNamedBufferStorage(m_IBO, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);

		glCreateVertexArrays(1, &m_VAO);
		glVertexArrayVertexBuffer(m_VAO, 0, m_VBO, 0, sizeof(Vertex));
		glVertexArrayElementBuffer(m_VAO, m_IBO);

		glEnableVertexArrayAttrib(m_VAO, 0);
		glVertexArrayAttribFormat(m_VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Position));
		glVertexArrayAttribBinding(m_VAO, 0, 0);
		glEnableVertexArrayAttrib(m_VAO, 1);
		glVertexArrayAttribFormat(m_VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Normal));
		glVertexArrayAttribBinding(m_VAO, 1, 0);
		glEnableVertexArrayAttrib(m_VAO, 2);
		glVertexArrayAttribFormat(m_VAO, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, UV));
		glVertexArrayAttribBinding(m_VAO, 2, 0);
		glEnableVertexArrayAttrib(m_VAO, 3);
		glVertexArrayAttribFormat(m_VAO, 3, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Tangent));
		glVertexArrayAttribBinding(m_VAO, 3, 0);
	}

	Mesh::~Mesh()
	{
		glDeleteVertexArrays(1, &m_VAO);
		glDeleteBuffers(1, &m_VBO);
		glDeleteBuffers(1, &m_IBO);
	}

	Mesh::Mesh(Mesh&& other) noexcept
		: m_VAO(std::exchange(other.m_VAO, 0)), m_VBO(std::exchange(other.m_VBO, 0)),
		  m_IBO(std::exchange(other.m_IBO, 0)), m_IndexCount(std::exchange(other.m_IndexCount, 0))
	{
	}

	Mesh& Mesh::operator=(Mesh&& other) noexcept
	{
		if (this != &other)
		{
			glDeleteVertexArrays(1, &m_VAO);
			glDeleteBuffers(1, &m_VBO);
			glDeleteBuffers(1, &m_IBO);
			m_VAO = std::exchange(other.m_VAO, 0);
			m_VBO = std::exchange(other.m_VBO, 0);
			m_IBO = std::exchange(other.m_IBO, 0);
			m_IndexCount = std::exchange(other.m_IndexCount, 0);
		}
		return *this;
	}

	void Mesh::Draw() const
	{
		glBindVertexArray(m_VAO);
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_IndexCount), GL_UNSIGNED_INT, nullptr);
	}

	Mesh Mesh::CreateSphere(float radius, uint32_t segments, uint32_t rings)
	{
		constexpr float pi = 3.14159265358979f;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t y = 0; y <= rings; ++y)
		{
			const float v = static_cast<float>(y) / static_cast<float>(rings);
			const float phi = v * pi;
			for (uint32_t x = 0; x <= segments; ++x)
			{
				const float u = static_cast<float>(x) / static_cast<float>(segments);
				const float theta = u * 2.0f * pi;
				const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
				const glm::vec3 t(-std::sin(theta), 0.0f, std::cos(theta));
				vertices.push_back({ n * radius, n, { u, 1.0f - v }, t });
			}
		}
		const uint32_t stride = segments + 1;
		for (uint32_t y = 0; y < rings; ++y)
		{
			for (uint32_t x = 0; x < segments; ++x)
			{
				const uint32_t i0 = y * stride + x, i1 = i0 + 1, i2 = i0 + stride, i3 = i2 + 1;
				indices.insert(indices.end(), { i0, i1, i2, i1, i3, i2 });
			}
		}
		return Mesh(vertices, indices);
	}

	Mesh Mesh::CreatePlane(float size, float uvScale)
	{
		const float h = size * 0.5f;
		const glm::vec3 n(0, 1, 0);
		std::vector<Vertex> vertices = {
			{ { -h, 0,  h }, n, { 0, 0 }, { 1, 0, 0 } },
			{ {  h, 0,  h }, n, { uvScale, 0 }, { 1, 0, 0 } },
			{ {  h, 0, -h }, n, { uvScale, uvScale }, { 1, 0, 0 } },
			{ { -h, 0, -h }, n, { 0, uvScale }, { 1, 0, 0 } },
		};
		return Mesh(vertices, { 0, 1, 2, 0, 2, 3 });
	}

	Mesh Mesh::CreateGrid(float size, uint32_t subdivisions)
	{
		subdivisions = std::max(subdivisions, 1u);
		const uint32_t stride = subdivisions + 1;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(static_cast<size_t>(stride) * stride);
		indices.reserve(static_cast<size_t>(subdivisions) * subdivisions * 6);

		// Same orientation as CreatePlane: u grows along +X, v grows along -Z.
		for (uint32_t j = 0; j <= subdivisions; ++j)
		{
			const float v = static_cast<float>(j) / static_cast<float>(subdivisions);
			for (uint32_t i = 0; i <= subdivisions; ++i)
			{
				const float u = static_cast<float>(i) / static_cast<float>(subdivisions);
				vertices.push_back({ { (u - 0.5f) * size, 0.0f, (0.5f - v) * size }, { 0, 1, 0 }, { u, v }, { 1, 0, 0 } });
			}
		}
		for (uint32_t j = 0; j < subdivisions; ++j)
		{
			for (uint32_t i = 0; i < subdivisions; ++i)
			{
				const uint32_t i0 = j * stride + i, i1 = i0 + 1, i2 = i0 + stride, i3 = i2 + 1;
				indices.insert(indices.end(), { i0, i1, i3, i0, i3, i2 });
			}
		}
		return Mesh(vertices, indices);
	}

	Mesh Mesh::CreateCube(float size)
	{
		const float h = size * 0.5f;
		struct Face { glm::vec3 N, U, V; };
		const Face faces[6] = {
			{ {  1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
			{ { -1, 0, 0 }, { 0, 0,  1 }, { 0, 1, 0 } },
			{ { 0,  1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
			{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0,  1 } },
			{ { 0, 0,  1 }, { 1, 0, 0 }, { 0, 1, 0 } },
			{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
		};
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		for (const Face& f : faces)
		{
			const uint32_t base = static_cast<uint32_t>(vertices.size());
			vertices.push_back({ (f.N - f.U - f.V) * h, f.N, { 0, 0 }, f.U });
			vertices.push_back({ (f.N + f.U - f.V) * h, f.N, { 1, 0 }, f.U });
			vertices.push_back({ (f.N + f.U + f.V) * h, f.N, { 1, 1 }, f.U });
			vertices.push_back({ (f.N - f.U + f.V) * h, f.N, { 0, 1 }, f.U });
			indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		}
		return Mesh(vertices, indices);
	}
}
