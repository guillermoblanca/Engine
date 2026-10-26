#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// A glTF 2.0 asset (.gltf or .glb, e.g. exported from Blender) uploaded to the GPU.
	//
	// Supported: meshes (triangles; positions, normals, tangents, UV0), node hierarchy and transforms,
	// metallic-roughness materials with base color / metallic-roughness / normal / occlusion / emissive
	// textures, alpha modes (opaque, mask, blend), double-sided, KHR_materials_emissive_strength.
	// Not yet: skinning/animations, morph targets, cameras, lights, UV sets > 0, KHR_texture_transform.
	//
	// The Model owns the meshes and textures; keep it alive while Scenes built from it are rendered.
	class Model
	{
	public:
		// Returns nullptr (and logs why) if the file cannot be read or is not valid glTF.
		static std::unique_ptr<Model> Load(const std::string& path);

		// Appends one RenderObject per primitive instance, each transformed by `root` * node transform.
		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

		// Axis-aligned bounds of the whole model in model space (all node transforms applied).
		glm::vec3 GetBoundsMin() const { return m_BoundsMin; }
		glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
		glm::vec3 GetBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
		float GetBoundsRadius() const { return glm::length(m_BoundsMax - m_BoundsMin) * 0.5f; }

		size_t GetMeshCount() const { return m_Meshes.size(); }
		size_t GetInstanceCount() const { return m_Instances.size(); }
		size_t GetMaterialCount() const { return m_Materials.size(); }
		size_t GetTextureCount() const { return m_Textures.size(); }
		size_t GetTriangleCount() const { return m_TriangleCount; }
		const std::vector<Material>& GetMaterials() const { return m_Materials; }

	private:
		Model() = default;
		friend struct ModelLoader;

		struct Primitive
		{
			std::unique_ptr<Mesh> MeshData;
			int MaterialIndex = -1;   // -1 = default material
		};
		struct Instance
		{
			int MeshIndex;
			glm::mat4 Transform;
		};

		std::vector<std::vector<Primitive>> m_Meshes;   // glTF meshes -> primitives
		std::vector<Instance> m_Instances;               // one per node that references a mesh
		std::vector<Material> m_Materials;
		std::vector<std::unique_ptr<Texture2D>> m_Textures;
		glm::vec3 m_BoundsMin{ 0.0f }, m_BoundsMax{ 0.0f };
		size_t m_TriangleCount = 0;
	};
}
