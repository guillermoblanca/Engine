#pragma once
#include "GB/Gameplay/Ecology.h"
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// Agriculture (E7, H18): crops as data and fields of plots that follow the seasons. Pure CPU and deterministic.
	//
	// A field is a grid of plots with the same cycle as the plots of the vegetable garden (GB/Gameplay/Garden.h): grass -> dug (tilled with a hoe)
	// -> sown (a seed of a crop) -> sprout -> grown -> harvested (back to dug). The growth follows the garden's rule: a plot grows only while it is
	// moist (watered by a tender, or by the rain) and at the pace of the season (SeasonClock::GrowthRate: nothing grows in winter), StageSeconds
	// per stage. What the garden does not have, the field adds: crops with identity (what seed they take, what they yield, in which seasons they
	// can be sown) and the seasons ruining a harvest: the frost of winter kills what is sown, sprouting or ripe and left in the field.
	// The garden's plots are scene entities with abilities (the player's); a clan's field is plain data the tribe works with task lists, so a
	// field of many plots costs nothing per frame. The state names are the garden's (GardenSystem::SoilState).

	struct CropDef
	{
		std::string Id;                          // "grain"
		std::string Seed;                        // the item sown, one per plot ("grain_seed")
		std::map<std::string, int> Yield;        // what a ripe plot gives at harvest ({"grain": 3, "grain_seed": 2})
		float StageSeconds = 90.0f;              // seconds of moist growth at rate 1 from sown to sprout and from sprout to grown
		std::vector<std::string> Sow;            // seasons it can be sown in ("spring", "summer"); empty = any but winter
		// T280 (P-F4 of documentation/01_diseno/AGRICULTURA.md §4.1): the crop's own pace by season ("spring": 1.2, ...), in place of the common
		// curve of SeasonClock::GrowthRate; a season it does not list grows at that curve's rate. Empty = the common curve (as before).
		std::map<std::string, float> Seasons;
		bool operator==(const CropDef&) const = default;
		bool SowsIn(Season s) const;
		float Rate(Season s) const;              // its growth rate in a season (Seasons, else SeasonClock::GrowthRate)
	};

	struct FieldPlot
	{
		std::string State = "grass";             // "grass", "dug", "sown", "sprout", "grown"
		std::string Crop;                        // the crop sown ("" while grass or dug)
		float Growth = 0.0f;                     // 0..1 through the current stage
		float Moisture = 0.0f;                   // seconds it stays moist
		bool operator==(const FieldPlot&) const = default;
	};

	// What happened to a field in one step, for the log and the counters.
	struct FieldEvents
	{
		int Sprouted = 0, Ripened = 0, Ruined = 0;
	};

	class Field
	{
	public:
		std::string Name;                        // "field 1"
		glm::vec2 Origin{ 0.0f };                // the middle of the first plot (x, z)
		int Columns = 3;
		float Spacing = 2.0f;
		std::vector<FieldPlot> Plots;
		// T280: the cell of the grid (row x Columns + column, ascending) of each plot, for a field of any shape (a zone painted as a polygon);
		// empty = plot i is cell i (a rectangle, as every field before)
		std::vector<int> Cells;

		Field() = default;
		Field(const std::string& name, const glm::vec2& origin, int plots, int columns, float spacing);
		glm::vec2 PlotPos(size_t i) const;
		int GridCells() const { return Cells.empty() ? static_cast<int>(Plots.size()) : Cells.back() + 1; }   // the cells of its bounding grid
		glm::vec2 Centre() const;
		// Grows what is moist (StageSeconds per stage at the crop's rate of the season, CropDef::Rate), dries the soil, and in winter the frost ruins what is in the ground.
		// `rain`: it rains, so every plot is watered (as the garden does). Unknown crops do not grow.
		FieldEvents Update(float dt, Season season, bool rain, float moistureSeconds, const std::map<std::string, CropDef>& crops);
		// How many plots are in a state ("grass", "dug", ...), and which ones (in order) up to `most`.
		int Count(const std::string& state) const;
		std::vector<size_t> Find(const std::string& state, size_t most) const;
		// Plots of a crop growing that need water now (sown or sprouting and dry).
		std::vector<size_t> Dry(size_t most) const;
		std::string Report() const;              // "field 1 at 12, -3: 6 plots: 2 grass, 1 dug, 3 sprout (grain)"
		bool operator==(const Field&) const = default;
	};

	std::string FieldsToJson(const std::vector<Field>& fields);
	bool FieldsFromJson(const std::string& json, std::vector<Field>& fields);   // false (and nothing changed) on a mistake
}
