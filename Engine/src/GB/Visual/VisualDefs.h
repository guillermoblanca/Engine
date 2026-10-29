#pragma once
#include "GB/Visual/ProcMesh.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// What things look like, as data (the "visuals" section of a content pack, assets/data/visuals.json): procedural models for the resource
	// nodes, the props the tribe carries, the piles in the stores, the creatures and the buildings phase by phase, made of a few shapes (box,
	// cylinder, blob, blade) with materials that use the procedural textures (ProcTexture). Pure data plus the CPU builders that turn a
	// description and a state (how full a node is, which phase a building is in) into meshes; the GL side is VisualCache, the drawing hook
	// SceneVisuals. Nothing here changes the simulation: a node, a building or a member looks like its state, the state does not depend on it.

	struct VisualMaterial
	{
		glm::vec3 Color{ 0.7f };          // linear albedo, multiplied with the texture
		std::string Texture;              // a ProcTexture kind ("bark"); "" = flat colour
		float UVScale = 1.0f;             // texture repeats per metre
		float Roughness = 0.8f;
		float Metallic = 0.0f;
		glm::vec3 Emissive{ 0.0f };
		float Grain = 0.0f;               // the renderer's noise bump (rocks)
		float Wind = 0.0f;                // sway in the wind (crowns, grass)
		bool Seasonal = false;            // takes the season's foliage colour (crowns, bushes)
		bool DoubleSided = false;         // blades of grass and straw
		bool operator==(const VisualMaterial&) const = default;
	};

	// One shape of a model, repeated `Count` times by `Layout`. Its state window: it is drawn while the state fraction f (a node's Amount over
	// its capacity, 1 for a model without state) is in (ShowFrom, ShowTo]; ShowFrom < 0 includes the depleted node (f = 0), so [-1, 0] is a part
	// that only a depleted node has (the stump). With `Progressive` the copies go one by one: copy i is drawn while f > ShowFrom + (ShowTo -
	// ShowFrom) * i / Count, so a bush loses a cluster of berries for every few taken.
	struct VisualPart
	{
		std::string Name;
		std::string Shape = "box";        // "box", "cylinder" (Size = bottom diameter, height, top diameter; top 0 = cone), "blob" (Size = radii),
		                                  // "blade" (Size = width, height, lean)
		glm::vec3 Position{ 0.0f };
		glm::vec3 Size{ 1.0f };
		glm::vec3 Rotation{ 0.0f };       // degrees: pitch (x), yaw (y), roll (z)
		std::string Material;
		int Sides = 8;                    // cylinder
		float Bend = 0.0f;                // cylinder: the top leans this far (metres)
		int Detail = 1;                   // blob: subdivisions (0..3)
		float Rough = 0.25f;              // blob: how lumpy
		float Flat = -2.0f;               // blob: flatten below this (in unit radii; > -1 sits it on the ground)
		int Count = 1;
		std::string Layout = "ring";      // "ring" (Radius, Arc degrees), "row" (along X, Spacing), "stack" (rows of Columns along X, Spacing apart, layers
		                                  // up by Size.y... of logs), "scatter" (a golden-angle disc of Radius)
		float Radius = 0.0f, Arc = 360.0f, Spacing = 0.3f;
		int Columns = 3;
		float Jitter = 0.0f;              // 0..1: copies vary in size, place and turn by the variant's seed
		bool Face = true;                 // ring and scatter copies turn to face out
		float ShowFrom = -1.0f, ShowTo = 1.0f;
		bool Progressive = false;
		bool operator==(const VisualPart&) const = default;
	};

	struct VisualModel
	{
		std::vector<VisualPart> Parts;
		int Variants = 3;                 // instances pick one by a hash of their name: not every tree the same
		std::string Note;
		bool operator==(const VisualModel&) const = default;
	};

	// A building raised in phases (foundation stakes, frame, walls, roof...). The parts of the phases before the current one are all there; those of
	// the current one appear in order as its progress goes from 0 to 1 (copy by copy, part by part), so a site grows stake by stake. A phase past the
	// last is the finished building.
	struct BuildingVisualPhase
	{
		std::string Name;
		std::vector<VisualPart> Parts;
		bool operator==(const BuildingVisualPhase&) const = default;
	};

	struct BuildingVisual
	{
		std::vector<BuildingVisualPhase> Phases;
		glm::vec2 Footprint{ 3.0f };      // metres (x, z): what H8 clears and reserves
		std::string Note;
		bool operator==(const BuildingVisual&) const = default;
	};

	// How a carried thing is drawn and where (see PropVisuals.h): a model and the socket it hangs from, moved by an offset in the socket's frame.
	struct PropLook
	{
		std::string Model;                // a model of VisualLibrary::Models
		glm::vec3 Offset{ 0.0f };         // metres, in the socket's frame
		glm::vec3 Rotation{ 0.0f };       // degrees
		bool operator==(const PropLook&) const = default;
	};

	// A creature drawn by the visuals instead of its primitives: a glTF model with its clips (the fox), or a procedural model (the rabbit).
	struct CreatureVisual
	{
		std::string Model;                // a glTF/FBX path under the assets, or "" for Procedural
		std::string Procedural;           // a model of VisualLibrary::Models
		float Length = 0.6f;              // the model is scaled so its longest side is this (metres)
		float Yaw = 0.0f;                 // degrees that turn the model's front to +Z
		std::string Idle, Walk, Run;      // clip names (glTF)
		float RunSpeed = 1.6f;            // m/s from which it runs
		bool operator==(const CreatureVisual&) const = default;
	};

	// Piles in a store: per item tag (most specific match wins), a model whose copies (Progressive, state = units / Full) show how much there is.
	struct PileVisual
	{
		std::string Model;
		int Full = 20;                    // units at which the pile is drawn whole
		bool operator==(const PileVisual&) const = default;
	};

	// What the tribe carries (PropVisuals.h): the look of each item, where a tool family rests while walking and which family each work uses,
	// what a load in the arms looks like and with which locomotion set it is carried.
	struct CarryRule
	{
		std::vector<std::string> Tags;    // the load is of these (by tag prefix); empty = anything
		std::string Look;                 // a PropLook
		std::string Set;                  // locomotion set while carrying it ("box", "holding", "wheelbarrow")
		int MinUnits = 3;                 // carried in the arms from this many units (fewer go in the bag)
		bool operator==(const CarryRule&) const = default;
	};

	struct PropRules
	{
		std::map<std::string, PropLook> Looks;                // look id -> model, offset
		std::map<std::string, std::string> Items;             // item id -> look id (what it looks like when shown)
		std::map<std::string, std::string> RestSockets;       // tool family / container tag -> socket while not in use ("hip_r", "back")
		std::map<std::string, std::string> WorkTools;         // ability or activity -> tool family taken in the right hand ("Chop" -> Item.Tool.Axe)
		std::map<std::string, std::string> HandLooks;         // activity -> look in the left hand ("stoke" -> firewood, "eat" -> berries)
		std::vector<CarryRule> Carry;                         // first match wins, by order
		std::vector<std::string> CarryJobs{ "deposit", "haul" };   // the jobs that carry the load in the arms
		int MaxShown = 3;                                     // at most this many props at once
		bool operator==(const PropRules&) const = default;
	};

	// Where the hands go during an action (TribeVisuals' reach layer): the contact point of the target, as a height above its origin and a
	// distance from it toward the actor, between two fractions of the montage, with a weight.
	struct ReachRule
	{
		std::string Hand = "right";       // "right", "left" or "both"
		float Height = 1.0f;
		float Toward = 0.2f;              // metres from the target's centre toward whoever acts
		float From = 0.25f, To = 0.75f;   // window of the montage
		float Weight = 0.7f;
		bool operator==(const ReachRule&) const = default;
	};

	struct VisualLibrary
	{
		std::map<std::string, VisualMaterial> Materials;
		std::map<std::string, VisualModel> Models;                    // nodes (by their model id: "tree", "berry_bush"...), props, piles, creatures
		std::map<std::string, BuildingVisual> Buildings;
		std::map<std::string, CreatureVisual> Creatures;              // by species tag ("Animal.Fox")
		std::map<std::string, PileVisual> Piles;                      // by item tag
		std::map<std::string, ReachRule> Reach;                       // by ability or activity
		PropRules Props;
		bool operator==(const VisualLibrary&) const = default;
	};

	// The "visuals" section: { "materials": {id: {...}}, "models": {id: {"variants", "note", "parts": [...]}}, "buildings": {id: {"footprint", "phases":
	// [{"name", "parts"}]}}, "creatures", "piles", "reach", "props": {"looks", "items", "rest", "work", "hand", "carry", "carryJobs", "maxShown"} }.
	// Entries replace those of the same id (a pack can redraw one model); a part's fields that are missing take their defaults. Checks that every
	// part names a known shape and an existing material, every look an existing model and every building phase has parts; on any mistake
	// returns what is wrong and changes nothing.
	std::string LoadVisuals(const std::string& json, VisualLibrary& library);
	std::string LoadVisualsFile(const std::string& path, VisualLibrary& library);
	// The library as a document LoadVisuals reads back to the same library.
	std::string VisualsToJson(const VisualLibrary& library);

	// ---- the CPU builders

	// One mesh per material of the visible parts of a model.
	struct VisualPiece
	{
		MeshData Mesh;
		std::string Material;
	};

	// Which copies of each part are drawn at state fraction `fraction` (see VisualPart): one flag per copy, part after part. Equal masks give the
	// same mesh, so they key the GPU cache.
	std::vector<bool> VisibleCopies(const VisualModel& model, float fraction);
	std::string MaskKey(const std::vector<bool>& mask);
	// The meshes of a model in a state: `variant` (0..Variants-1) seeds the jitter of the copies.
	std::vector<VisualPiece> BuildVisual(const VisualModel& model, const std::vector<bool>& visible, int variant);
	inline std::vector<VisualPiece> BuildVisual(const VisualModel& model, float fraction, int variant) { return BuildVisual(model, VisibleCopies(model, fraction), variant); }
	// The variant of a named instance (a hash of the name modulo the model's variants).
	int VisualVariant(const VisualModel& model, const std::string& instance);

	// A building in a phase with a progress (0..1) through it, as a model: every part of the phases up to `phase` and, in `visible`, which copies
	// are up (all of the phases before, the first `progress` of the current one, copy by copy in the order they are listed). `phase` >= the number
	// of phases is the finished building; a negative phase is an empty site.
	VisualModel BuildingModel(const BuildingVisual& building, int phase, float progress, std::vector<bool>& visible);
	// How many copies of the current phase are up at `progress` (and of all the phase): the adapter's report and the tests' measure.
	int BuildingCopies(const BuildingVisual& building, int phase, float progress, int* total = nullptr);

	// Triangles of a set of pieces (the budget checks).
	size_t VisualTriangles(const std::vector<VisualPiece>& pieces);
}
