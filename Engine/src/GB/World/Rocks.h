#pragma once
#include "GB/Scene/MeshData.h"
#include "GB/Scene/Vertex.h"
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// How the blocks of one rock are laid out (documentation/02_tecnico/motor/ROCAS.md, phase 3): a single convex block, or several blocks of the
	// same recipe fused into one mesh, after the formations of the reference kit (art-book/concept/approved/era1_env_rock_kit_v001.jpg).
	//   Single   one block (what every rock was before)
	//   Cluster  a main block with smaller ones leaning on it
	//   Slab     a wide, low slab cracked into pieces
	//   Spire    a tall pointed block with shorter ones at its foot
	//   Stack    a cairn: flat blocks piled up, smaller and smaller
	//   Ledge    a low line of blocks, a broken ledge or ridge
	// Every formation stays inside the footprint of its recipe's Size (the navigation blocks Size x scale x 1.3 around a rock).
	enum class RockArchetype { Single, Cluster, Slab, Spire, Stack, Ledge };
	const char* RockArchetypeName(RockArchetype archetype);
	bool ParseRockArchetype(const std::string& name, RockArchetype& out);

	struct RockFormation
	{
		RockArchetype Archetype = RockArchetype::Single;
		int Blocks = 0;            // blocks besides the main one (0 = the archetype's own count)
		float Spread = 1.0f;       // how far the blocks reach, relative to the footprint
		float Variation = 0.3f;    // 0..1: how much the size of each block varies
		int Detail = -1;           // icosphere subdivisions of the smaller blocks (-1 = one less than the recipe's, at least 1)
	};

	struct RockParams
	{
		uint32_t Seed = 1;
		float Size = 1.0f;                         // radius in meters before Stretch
		glm::vec3 Stretch{ 1.0f, 0.7f, 1.0f };     // per-axis scale: flat slabs, tall pillars
		float Roughness = 0.35f;                   // displacement amplitude (relative to the radius)
		float Frequency = 1.6f;                    // noise cycles over the rock: lumpy (1) to craggy (3)
		float Facets = 0.6f;                       // 0 = smooth pebble, 1 = flat-shaded cut faces
		float Flatten = 0.35f;                     // 0..1: how flat the base is (so it sits on the ground)
		float Strata = 0.5f;                       // bedding grooves (layers), 0..1
		float Cracks = 0.5f;                       // narrow cracks, 0..1
		float Erosion = 0.5f;                      // fine pitting and rounded edges, 0..1
		int Detail = 4;                            // icosphere subdivisions (4 = 5120 triangles)
		// The cut faces: Planes random planes (0 = 8 to 12, as before) at a distance between CutNear and CutFar of the radius. Fewer, deeper cuts
		// give the broad flat faces of the reference kit.
		int Planes = 0;
		float CutNear = 0.70f, CutFar = 0.92f;
		glm::vec3 Color{ 0.42f, 0.40f, 0.37f };   // linear RGB albedo
		// Surface layers (see Material): moss on top, sand at the base, wet band at the water line.
		float Moss = 0.0f;
		// Linear RGB. Not read by the render yet: GB/Renderer/RocksGpu.cpp leaves Material::MossColor at its default (ROCAS.md, phase 1).
		glm::vec3 MossColor{ 0.20f, 0.36f, 0.10f };
		float Sand = 0.0f;
		RockFormation Formation;
	};

	// A procedural rock on the CPU (no OpenGL, any thread): a noise-displaced icosphere with a flat base, and its measures. The procedural
	// grain/bump of its material is derived from the params by GB/Renderer/RocksGpu.h, which uploads it (docs/ARQUITECTURA_REVISION.md, H5).
	struct Rock
	{
		RockParams Params;
		MeshData Geometry;
		float Radius = 1.0f;                       // horizontal radius of the mesh around its axis
		float Height = 1.0f;                       // top of the mesh above its base
		size_t TriangleCount = 0;
	};

	class RockFactory
	{
	public:
		static std::unique_ptr<Rock> Create(const RockParams& params);
		// The geometry alone (vertices and indices): one block, or the blocks of params.Formation fused into one mesh, its base at y = 0.
		static void BuildGeometry(const RockParams& params, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
	};

	// ---- the rocks of the world as data: assets/data/rocks.json (ROCAS.md, phase 3)
	// A recipe is a kind of stone: its shape and material (all of RockParams but the seed and the formation). A formation is how its blocks are
	// laid out. A kit is the rock variants a type of world places, in order (a placement's Variant indexes it): each a recipe in a formation, with
	// what it changes of the recipe (its size, its stretch...) already applied in Params.
	struct RockVariantDef
	{
		std::string Recipe, Formation = "single";
		RockParams Params;   // the recipe with the variant's own keys on top (Seed and Formation are filled by RockLibrary::Resolve)
	};

	struct RockLibrary
	{
		std::map<std::string, RockParams> Recipes;
		std::map<std::string, RockFormation> Formations;
		std::map<std::string, std::vector<RockVariantDef>> Kits;   // by type of world: "temperate", "alpine", "desert"
		std::string Source;                                        // the file it was read from ("" = built in)

		// A recipe in a formation ("" = single), its size as the recipe gives it: false, and `out` untouched, when either id is unknown.
		bool Resolve(const std::string& recipe, const std::string& formation, RockParams& out) const;
		// One variant of a kit; false when its formation is unknown.
		bool Resolve(const RockVariantDef& variant, RockParams& out) const;
		// The kit of a type of world ("temperate", "alpine", "desert"); null when the library has none.
		const std::vector<RockVariantDef>* Kit(const std::string& worldType) const;
	};

	// Reads a rocks document (the format of assets/data/rocks.json) into `out`; the error, or "" when it loaded. A mistake (an unknown key, a
	// kit naming a recipe or formation that does not exist) is an error, and `out` is left as it was: nothing is half-loaded.
	std::string LoadRockLibrary(const std::string& json, RockLibrary& out);
	std::string LoadRockLibraryFile(const std::string& path, RockLibrary& out);
	// The recipes every world used before they were data (generators 1-4 up to 2026-11-08): six single blocks per type of world. What a world
	// gets when rocks.json is missing or does not load.
	const RockLibrary& BuiltinRockLibrary();
	// The shipped library (assets/data/rocks.json), read again whenever the file changes, so an edit shows in the next world generated without
	// rebuilding; the built-in one, with an error in the log, when the file is missing or wrong. Thread-safe.
	std::shared_ptr<const RockLibrary> ShippedRockLibrary();
	// Points ShippedRockLibrary at another folder (a test, a tool); "" goes back to the engine's assets/data.
	void SetRockLibraryDir(const std::string& dir);
}
