#pragma once
#include "GB/Scene/Image.h"
#include "GB/Scene/MeshData.h"
#include "GB/World/Scatter.h"
#include "GB/World/Terrain.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
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
		int Style = -1;                 // habit within the species (broadleaf: spreading / ovate / columnar; conifer: spruce / pine;
		                                // palm: coconut / date); -1 = derived from the seed
		float Fruit = 0.35f;            // palms: amount of coconuts / dates under the crown (0 = none)
		int Detail = 1;                 // 0 = low (about half the foliage; far trees), 1 = normal, 2 = high
	};

	// One generated tree, on the CPU (no OpenGL, any thread): a bark mesh and (except for Bare trees) a foliage mesh, with the measures the
	// placement and the picking read. GB/Renderer/TreesGpu.h uploads it and gives it its materials (docs/ARQUITECTURA_REVISION.md, H5).
	struct Tree
	{
		TreeParams Params;                 // what it was made from (the materials are derived from it)
		MeshData Trunk;
		MeshData Foliage;                  // empty for bare trees
		float Height = 0.0f;
		float CrownRadius = 0.0f;          // largest horizontal distance of any vertex from the trunk base
		float BoundsHeight = 0.0f;         // highest vertex
		size_t TriangleCount = 0;
		size_t LeafCardCount = 0;
	};

	// The procedural bark / leaf / needle / frond images every tree's materials sample, painted once in the process.
	enum class TreeTexture { Bark, BarkPalm, Leaf, LeafRound, LeafMaple, Needle, Frond, Count };
	struct TreeImages { Image Images[static_cast<size_t>(TreeTexture::Count)]; const Image& Get(TreeTexture t) const { return Images[static_cast<size_t>(t)]; } };

	// Generates trees on the CPU (deterministic by TreeParams).
	class TreeFactory
	{
	public:
		// Paints the bark and leaf images (no OpenGL, any thread): a world built in the background calls it there, so making a TreesGpu on the main
		// thread only uploads them. Painted once (seconds of work), on whichever thread asks first.
		static void PrepareTextures();
		static const TreeImages& Textures();   // painted on the first call
		static std::unique_ptr<Tree> Create(const TreeParams& params);
		// The texture each part of a tree of these params samples.
		static TreeTexture TrunkTexture(const TreeParams& params);
		static TreeTexture FoliageTexture(const TreeParams& params);
	};
}
