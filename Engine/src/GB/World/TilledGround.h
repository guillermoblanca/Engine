#pragma once
// T332: the tilled ground of a field as one procedural mesh, the map of its plots' states laid over it, and where each plot's plants and the
// stones of its edge stand.
//
// A field (GB/Gameplay/Farming.h: plots 2 m apart on a grid, Field::Cells for a field of any shape) is drawn as a sheet of earth that follows the
// terrain (the height function) a little above it and carries furrows along the longer side of the field (a ripple in the geometry, in phase with
// the furrows of the "tilled_soil" texture through the UVs). Its edge follows the field's outline, not the grid: a painted zone's perimeter grown
// by half a plot (it runs through the middles of the edge plots), or for a field of the AI the union of its cells filtered bilinearly (straight
// sides, rounded corners). The vertices just outside are moved onto that line, so the edge is a clean line where the earth meets the terrain. A
// band inside it is the field's margin (a rim of trodden earth, flat, without furrows) with a few small stones along it.
//
// The plots' states (dug, sown, fallow, wet...) are a small RGBA map over the field that the renderer multiplies on the soil's colour
// (Material::MacroMap): a few texels a plot along the furrows and several across each furrow, so it also darkens the bottom of every furrow, the
// ground under each plant and the rim (the occlusion the material has no other way to get).
//
// Pure CPU and deterministic (no GL, no Gameplay types): the presentation (Visual/TribeVisuals.cpp) turns a Field into a TilledShape, builds the
// mesh only when the shape or the ground changes and the map only when a state changes. The look's numbers come from visuals.json ("fields").
#include "GB/Scene/Image.h"
#include "GB/Scene/MeshData.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace GB
{
	// visuals.json "fields": how a field is drawn and how much grass the fields and the buildings clear (TrailsView in the editor).
	struct TilledLook
	{
		std::string Material = "soil";                 // the visuals material of the tilled earth (its texture's furrows run along v)
		std::string FrostMaterial = "frost";           // the whole field in winter
		std::vector<std::string> GroundParts{ "soil" };   // the parts of the plot models made of these materials are not drawn (the mesh is the soil)
		float Lift = 0.05f;           // m above the terrain inside the field
		float Sink = 0.08f;           // m under the terrain, away from the edge (what is outside the line hides)
		float EdgeNoise = 0.04f;      // 0..0.45: how much the edge wobbles about the outline (a share of a plot)
		float FurrowSpacing = 0.45f;  // m between two furrows
		float FurrowDepth = 0.06f;    // m from a ridge to a trough
		int FurrowSamples = 4;        // vertices across one furrow (and texels of the map across one)
		float Step = 0.5f;            // m between vertices along the furrows
		int Texels = 8;               // texels of the state map a plot along the furrows
		float TintNoise = 0.12f;      // 0..1: the texels' brightness varies by this (clods)
		std::map<std::string, glm::vec3> Tints;   // per state ("grass", "dug", "sown", "sprout", "grown", "fallow"): multiplies the soil; 0..2
		glm::vec3 WetTint{ 0.8f, 0.78f, 0.76f };  // times the tint where a plot is moist (watered or rained on, FieldPlot::Moisture)
		// the margin: a band of trodden earth RimWidth m inside the edge, of RimTint, where the furrows start; a stone (the model StoneModel) every
		// StoneSpacing m along it with the chance StoneChance, scaled by StoneScale (min, max)
		float RimWidth = 0.3f;
		std::string RimMaterial = "trodden";           // the rim's own material (no furrows in its texture): its triangles come first in the mesh
		glm::vec3 RimTint{ 1.0f };
		std::string StoneModel = "field_stone";
		float StoneSpacing = 1.2f, StoneChance = 0.45f;
		glm::vec2 StoneScale{ 0.6f, 1.3f };
		// the occlusion in the map: the bottom of a furrow darkened by FurrowShade, the ground under a plant of the ShadeStates by PlantShade
		// within PlantShadeRadius of a plot of its middle
		float FurrowShade = 0.35f, PlantShade = 0.35f, PlantShadeRadius = 0.32f;
		std::vector<std::string> ShadeStates{ "sprout", "grown" };
		// the plants: each plot's model moved up to Jitter of a plot from its middle, turned at random, tilted up to Tilt degrees, scaled by
		// Scale (min, max) and widened by Spread on the ground so the copies of neighbouring plots meet
		float Jitter = 0.18f;
		glm::vec2 Scale{ 0.87f, 1.13f };
		float Tilt = 5.0f;
		float Spread = 1.25f;
		float PlantDistance = 150.0f;   // m: plants and stones farther than this from the camera are not drawn
		// the grass cleared under a field (each plot's square grown by ClearField m) and a building or a site (its footprint grown by
		// ClearBuilding m), fading back over ClearFeather m
		float ClearField = 0.0f, ClearBuilding = 0.6f, ClearFeather = 0.6f;
		bool operator==(const TilledLook&) const = default;
	};

	// A field's cells: plot i stands on grid cell Cells[i] (row x Columns + column; empty = plot i is cell i), its middle at Origin + (column,
	// row) x Spacing (Field::PlotPos).
	struct TilledShape
	{
		glm::vec2 Origin{ 0.0f };
		int Columns = 1;
		float Spacing = 2.0f;
		int Plots = 0;
		std::vector<int> Cells;
		// the perimeter the player painted (FarmZones::Zone::Outline; empty for a field of the AI): the sheet stops half a plot beyond it (the
		// perimeter runs through the middles of its edge plots), so a straight side keeps its plots whole and a slanted one is a straight edge
		std::vector<glm::vec2> Outline;
		int CellOf(int plot) const { return Cells.empty() || plot >= static_cast<int>(Cells.size()) ? plot : Cells[static_cast<size_t>(plot)]; }
		int Rows() const;
		bool AlongX() const;   // the furrows run along x (the field is at least as long in x as in z)
		uint64_t Hash() const;
	};

	// Signed distance from p to a closed polygon (x, z points in order): positive inside, negative outside.
	float OutlineDistance(const std::vector<glm::vec2>& outline, const glm::vec2& p);
	// How far inside the field's edge p is (m; negative outside): the line the sheet ends at, with its wobble.
	float TilledInside(const TilledShape& shape, const TilledLook& look, uint32_t seed, const glm::vec2& p);

	using HeightFn = std::function<float(float, float)>;

	// The sheet of tilled earth over the field, in world space, on `ground` (null: y = 0). Empty when the field has no plot. The triangles of the
	// rim (every corner within RimWidth of the edge) come first: `rimIndices` gets how many indices they take (drawn with RimMaterial).
	MeshData BuildTilledGround(const TilledShape& shape, const TilledLook& look, const HeightFn& ground, uint32_t seed, size_t* rimIndices = nullptr);

	// The map of the plots' tints: `tints` one id per plot (a key of TilledLook::Tints; unknown = white), `wet` one flag per plot (empty: none);
	// the cells round the field take the tint of their nearest plot, so the filter does not darken the edge. `rect` gets where it lies (min x,
	// min z, size x, size z: Material::MacroRect). Each texel is the colour halved (0..2 in 0..255): the material's albedo is doubled to match.
	Image BuildTilledTints(const TilledShape& shape, const std::vector<std::string>& tints, const std::vector<uint8_t>& wet, const TilledLook& look, uint32_t seed,
		glm::vec4& rect);

	// Where plot `plot`'s plants stand: an offset from its middle (x, z), a yaw (radians), a tilt (radians, about the horizontal axis at
	// TiltAxis radians) and a scale; and a variant of the model (0..variants-1).
	struct TilledPlant
	{
		glm::vec2 Offset{ 0.0f };
		float Yaw = 0.0f;
		float Tilt = 0.0f, TiltAxis = 0.0f;
		float Scale = 1.0f;
		int Variant = 0;
	};
	TilledPlant PlantOf(const TilledLook& look, float spacing, uint32_t seed, int plot, int variants);

	// The stones of the field's margin: x, y (on `ground`), z and w a number 0..1 that picks their yaw, scale and variant.
	std::vector<glm::vec4> TilledStones(const TilledShape& shape, const TilledLook& look, const HeightFn& ground, uint32_t seed);

	// A seed for a field from its name and place (fields of two clans may share a name).
	uint32_t TilledSeed(const std::string& name, const glm::vec2& origin);
}
