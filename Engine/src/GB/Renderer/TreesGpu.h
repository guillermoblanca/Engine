#pragma once
#include "GB/Renderer/Material.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include "GB/Renderer/Texture.h"
#include "GB/World/Trees.h"
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>

namespace GB
{
	// One tree variant on the GPU: the meshes of a GB::Tree (GB/World/Trees.h, pure CPU) and their materials, ready to be placed in a Scene any
	// number of times. Made by a TreesGpu, whose textures its materials point to: it must not outlive it.
	struct TreeGpu
	{
		std::unique_ptr<Mesh> Trunk;
		std::unique_ptr<Mesh> Foliage;     // null for bare trees
		std::unique_ptr<Mesh> Cuts;        // T113: the end grain (stubs, stump, logs, chips); null when the tree has none
		std::unique_ptr<Mesh> Soil;        // T113: the planted patch or the root ball; null when it has none
		Material TrunkMaterial;
		Material FoliageMaterial;
		Material CutMaterial;
		Material SoilMaterial;
		float Height = 0.0f;               // the measures of the Tree it was made from
		float CrownRadius = 0.0f;
		float BoundsHeight = 0.0f;
		size_t TriangleCount = 0;
		size_t LeafCardCount = 0;

		// Adds the tree at a position (on the ground) with a yaw and uniform scale.
		void AddToScene(Scene& scene, const glm::vec3& position, float yawRadians = 0.0f, float scale = 1.0f, float maxDistance = 0.0f) const;   // maxDistance > 0: culled beyond it
	};

	// The GPU side of the trees: the bark and leaf textures (painted by TreeFactory::PrepareTextures, on any thread; here only uploaded), shared
	// by every variant it uploads. The trees do not know it exists: the render reads the world, never the other way round
	// (docs/ARQUITECTURA_REVISION.md, H5). Needs an OpenGL context; owned by whoever draws the trees (the editor's WorldInstance, the Sandbox,
	// the tests), and the scene objects AddToScene made point at its textures and the variants' meshes.
	class TreesGpu
	{
	public:
		TreesGpu();
		~TreesGpu();
		TreesGpu(const TreesGpu&) = delete;
		TreesGpu& operator=(const TreesGpu&) = delete;

		// Uploads a tree's meshes and gives them their materials (derived from tree.Params).
		std::unique_ptr<TreeGpu> Upload(const Tree& tree) const;
		// TreeFactory::Create and Upload in one go.
		std::unique_ptr<TreeGpu> Create(const TreeParams& params) const { return Upload(*TreeFactory::Create(params)); }
		const Texture2D* GetTexture(TreeTexture texture) const { return m_Textures[static_cast<size_t>(texture)].get(); }

	private:
		std::unique_ptr<Texture2D> m_Textures[static_cast<size_t>(TreeTexture::Count)];
	};
}
