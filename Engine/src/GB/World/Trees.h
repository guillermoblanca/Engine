#pragma once
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include "GB/World/Terrain.h"
#include <memory>
#include <vector>

namespace GB
{
	enum class TreeSpecies
	{
		Broadleaf,   // oak-like: thick trunk, spreading branches, round crown of leaves
		Conifer,     // pine-like: straight trunk, drooping whorls of needles in a cone
		Palm,        // curved trunk with a crown of long fronds
		Bare         // dead / winter tree: branches only
	};

	struct TreeParams
	{
		TreeSpecies Species = TreeSpecies::Broadleaf;
		uint32_t Seed = 1;
		float Height = 8.0f;            // approximate total height (m)
		float TrunkRadius = 0.0f;       // 0 = derive from the height
		glm::vec3 LeafTint{ 1.0f };     // multiplies the leaf texture (autumn colors, etc.)
		float LeafDensity = 1.0f;       // 1 = full crown
	};

	// One generated tree: a bark mesh and (except for Bare trees) a foliage mesh, ready to be placed in a
	// Scene any number of times. Owned by a TreeFactory, whose textures its materials point to.
	struct Tree
	{
		std::unique_ptr<Mesh> Trunk;
		std::unique_ptr<Mesh> Foliage;     // null for bare trees
		Material TrunkMaterial;
		Material FoliageMaterial;
		float Height = 0.0f;
		float CrownRadius = 0.0f;
		size_t TriangleCount = 0;
		size_t LeafCardCount = 0;

		// Adds the tree at a position (on the ground) with a yaw and uniform scale.
		void AddToScene(Scene& scene, const glm::vec3& position, float yawRadians = 0.0f, float scale = 1.0f) const;
	};

	// Generates trees. Owns the procedural bark / leaf / needle / frond textures.
	class TreeFactory
	{
	public:
		TreeFactory();
		~TreeFactory();
		std::unique_ptr<Tree> Create(const TreeParams& params) const;

	private:
		std::unique_ptr<Texture2D> m_Bark, m_Leaf, m_Needle, m_Frond, m_BarkPalm;
	};

	struct TreePlacement
	{
		int Variant = 0;            // index into the list of trees given to Scatter
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		float Scale = 1.0f;
	};

	// Scatters `count` trees over a terrain with a minimum spacing. Rejects steep ground, positions outside
	// [minHeight, maxHeight] and those where `allow(x, z)` (optional) is false. Deterministic by seed.
	std::vector<TreePlacement> ScatterTrees(const Terrain& terrain, int count, int variants, float minSpacing, uint32_t seed,
		float minHeight, float maxHeight, float maxSlopeDegrees = 28.0f, const std::function<bool(float, float)>& allow = {});
}
