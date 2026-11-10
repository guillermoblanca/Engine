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

	// T113: how far a tree has grown. Every stage grows from the same skeleton (the same seed draws the same trunk and limbs), so a tree keeps
	// its identity as it grows: the young one is the adult's trunk and main branches, shorter and thinner, without the finest branches; the
	// mature and old ones are the adult thicker and wider, the old one with dead and broken limbs and gaps in the crown. Palms ignore it.
	enum class TreeStage
	{
		Sprout,    // a germinated seed: a stem of 0.15-0.3 m with 2-4 leaves
		Sapling,   // 1-2 m: a thin trunk and a few branches
		Young,     // 3-5 m
		Adult,     // the tree as TreeParams::Height asks (the default: what every tree was before T113)
		Mature,    // a little taller, a thicker trunk, a wider crown, stronger roots
		Old        // ancient: a very thick, twisted trunk, dead and broken limbs, gaps in the crown
	};

	// T113: which piece of a tree TreeFactory::Create makes. The pieces of a felled tree come from the same skeleton as the standing one (its
	// stage included) and keep its measures: the stump has the radius of its foot, the fallen trunk the length it had standing.
	enum class TreePart
	{
		Standing,      // the tree (with TreeParams::BranchesTaken, Planted, Uprooted)
		Stump,         // what stays in the ground after felling: the foot, its roots, the cut face on top and chips around it
		FallenTrunk,   // the trunk and its main branches lying on the ground along FallAzimuth, cut at the stump's height
		Bucked         // the trunk cut into logs and the main branches cut and piled beside them, along FallAzimuth
	};

	struct TreeParams
	{
		TreeSpecies Species = TreeSpecies::Broadleaf;
		uint32_t Seed = 1;
		float Height = 8.0f;            // approximate total height (m) as an adult (TreeFactory::StageHeight gives every stage's)
		float TrunkRadius = 0.0f;       // 0 = derive from the height
		glm::vec3 LeafTint{ 1.0f };     // multiplies the leaf texture (autumn colors, etc.)
		float LeafDensity = 1.0f;       // 1 = full crown
		int Style = -1;                 // habit within the species (broadleaf: spreading / ovate / columnar; conifer: spruce / pine;
		                                // palm: coconut / date); -1 = derived from the seed
		float Fruit = 0.35f;            // palms: amount of coconuts / dates under the crown (0 = none)
		int Detail = 1;                 // 0 = low (about half the foliage; far trees), 1 = normal, 2 = high
		// T113: growth and what was done to the tree. The defaults change nothing (the same mesh, bit for bit, as before T113).
		TreeStage Stage = TreeStage::Adult;
		float BranchesTaken = 0.0f;     // 0..1: the share of the branches within reach (under 9 cm, leaving their parent below 2.2 m or a
		                                // finer one hanging below 1.8 m) taken, each cut to a stub with its cut face; a branch taken at 0.5
		                                // is taken at 1 too
		bool Planted = false;           // with a patch of turned earth round its foot (Tree::Soil): just planted
		bool Uprooted = false;          // with its root ball (Tree::Soil, under the foot): dug up to be carried or planted elsewhere
		TreePart Part = TreePart::Standing;
		float FallAzimuth = 0.0f;       // radians in the XZ plane (0 = +X, pi/2 = +Z): where a felled tree lies (FallenTrunk, Bucked)
	};

	// One generated tree, on the CPU (no OpenGL, any thread): a bark mesh and (except for Bare trees) a foliage mesh, with the measures the
	// placement and the picking read. GB/Renderer/TreesGpu.h uploads it and gives it its materials (docs/ARQUITECTURA_REVISION.md, H5).
	struct Tree
	{
		TreeParams Params;                 // what it was made from (the materials are derived from it)
		MeshData Trunk;
		MeshData Foliage;                  // empty for bare trees
		MeshData Cuts;                     // T113: the end grain (cut faces of stubs, stump and logs; chips): UV 0.5 +- 0.49 over each face
		MeshData Soil;                     // T113: earth (the planted patch or the root ball); UV in metres
		float Height = 0.0f;               // the stage's height (a piece: its top)
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
		// T113: the height a tree of these params reaches at its stage (Height for the adult; a sprout 0.15-0.3 m).
		static float StageHeight(const TreeParams& params);
		// The texture each part of a tree of these params samples.
		static TreeTexture TrunkTexture(const TreeParams& params);
		static TreeTexture FoliageTexture(const TreeParams& params);
	};
}
