#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Renderer/Material.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include "GB/Renderer/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	// The GPU side of a GB::Model (GB/Assets/Model.h, pure CPU): one Mesh per primitive, the model's images decoded and uploaded as textures,
	// and its materials as GB::Material with those textures. The model does not know it exists: the render reads the model, never the other
	// way round (ARQUITECTURA_REVISION.md, H5).
	//
	// Made on the first draw (Of) and kept by the model itself (Model::SetRenderData), so every instance and actor that shares a model shares
	// one upload, and the meshes and textures live exactly as long as the model. A file image used by several models (the pieces of a kit) is
	// one texture for all of them while any of them is alive. Needs an OpenGL context on the thread that draws.
	class ModelGpu final : public ModelRenderData
	{
	public:
		// The GPU copy of `model`, uploaded now if nobody drew it before.
		static const ModelGpu& Of(const Model& model);

		explicit ModelGpu(const Model& model);
		ModelGpu(const ModelGpu&) = delete;
		ModelGpu& operator=(const ModelGpu&) = delete;

		// The mesh of primitive `primitive` of glTF mesh `mesh` (the same order as Model::GetPrimitives).
		const Mesh& GetMesh(int mesh, size_t primitive) const { return *m_Meshes[static_cast<size_t>(mesh)][primitive]; }
		// Material to use for a primitive, with its textures (the glTF default when it has none).
		const Material& GetMaterial(int materialIndex) const;
		const std::vector<Material>& GetMaterials() const { return m_Materials; }
		size_t GetTextureCount() const { return m_Textures.size(); }

	private:
		std::vector<std::vector<std::unique_ptr<Mesh>>> m_Meshes;
		std::vector<std::shared_ptr<Texture2D>> m_Textures;   // one per Model image (null: it could not be decoded)
		std::vector<Material> m_Materials;
		Material m_Default;
	};

	// Appends the instance's current pose to `scene`, one object per mesh primitive, in the model's node order. The joint palettes are owned
	// by the instance, which must outlive the scene's use. `tints` (U4): a colour per material index that the material's albedo is multiplied
	// by (null, or fewer entries: as it is).
	void AddToScene(const ModelInstance& instance, Scene& scene, const glm::mat4& root = glm::mat4(1.0f), const std::vector<glm::vec3>* tints = nullptr);
	// The model in its rest pose, each primitive transformed by `root` * node transform. Skinned meshes are drawn in their bind pose; use a
	// ModelInstance to animate them.
	void AddToScene(const Model& model, Scene& scene, const glm::mat4& root = glm::mat4(1.0f));
}
