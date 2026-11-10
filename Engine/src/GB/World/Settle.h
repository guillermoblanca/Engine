#pragma once
#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/TgField.h"
#include "GB/World/Water/WaterField.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// Settlement sites, the trails between them and the levelling of both (GRAFO_TERRENO, "Asentamientos y sendas"). Pure CPU, deterministic,
	// in GB_WorldGen: the terrain graph nodes settlement_sites, route_network and path_grade (Graph/Nodes/SettleNodes.h) are thin wrappers
	// around the three functions below. The criteria are those of ESCENARIOS_JUGABLES (S1 water, S2 slope, S7 edge, R2 separation, X3 steep
	// ground, X4 flood) and the hard filters of the game's own picker for a big world (EditorApp::PickVillageSites: roughness in 22 m, water
	// at 35 m or more, a margin from the border), so the graph and the game agree on what a site is.

	// ------------------------------------------------------------------ what the game reads (GeneratedWorld::Settlements / Trails)
	struct SettlementSite
	{
		glm::vec2 Position{ 0.0f };
		float Radius = 30.0f;          // metres: the village (its platform)
		float Score = 0.0f;            // the weighted sum of the parts below
		int Rank = 0;                  // 0 = the best: the order of preference
		float Ground = 0.0f;           // the ground at the centre before levelling
		float PlatformLevel = 0.0f;    // the level of its platform (path_grade); Ground before that
		// measures
		float WaterDistance = 0.0f;    // metres to the nearest water (lake or river ribbon)
		float AboveWater = 0.0f;       // metres above the level of that water (the flood criterion X4: not the HAND along the flow)
		float CampSlopeMax = 0.0f;     // degrees, the steepest sample within the camp's radius (6 m, S2)
		float CampSlopeMean = 0.0f;    // degrees, the mean there
		float VillageSlopeMean = 0.0f; // degrees, the mean over the village's disc
		float FlatShare = 0.0f;        // 0..1 of the disc at 8 degrees or less
		float SteepShare = 0.0f;       // 0..1 of the disc over 14 degrees (X3)
		float Roughness = 0.0f;        // metres: the most the ground departs from the centre's 22 m away (PickVillageSites)
		float EdgeDistance = 0.0f;
		// the parts of the score, each 0..1 before its weight
		float Water = 0.0f, Flat = 0.0f, Area = 0.0f, Dry = 0.0f, Low = 0.0f, Forest = 0.0f, Stone = 0.0f;
		// path_grade: the earth its platform moved (m3)
		float Cut = 0.0f, Fill = 0.0f;
	};

	enum class TrailKind : int { Link = 0, Extra = 1, Water = 2, Col = 3, Ford = 4 };
	const char* TrailKindName(TrailKind kind);

	struct Trail
	{
		TrailKind Kind = TrailKind::Link;
		int From = -1, To = -1;          // site indices (into the site list); -1 at a point of interest (the water, a col, a ford)
		std::vector<glm::vec3> Points;   // x, height, z: the ground along it (after path_grade: the levelled ground)
		float Width = 4.0f;              // metres of levelled tread
		float Length = 0.0f;             // metres along it
		float Straight = 0.0f;           // metres in a straight line between its ends
		float Cost = 0.0f;               // the route's cost (metres weighted by the slope)
		// path_grade
		float Cut = 0.0f, Fill = 0.0f;   // m3
		float WithinGrade = 0.0f;        // 0..1 of its length at the maximum grade or less, measured on the levelled ground
		float MaxGrade = 0.0f;           // the steepest 9 m of it
		int Fords = 0;                   // river crossings on it
	};

	struct Ford
	{
		glm::vec2 Position{ 0.0f };
		float Level = 0.0f;              // the water there
		float Depth = 0.0f;              // metres from the water to the deepest bed across the section (today's river: river_carve)
		float HalfWidth = 0.0f;
		int Used = 0;                    // trails that cross here
		bool Built = false;              // path_grade raised the bed to FordDepth (the river had no reach that shallow)
	};

	struct Col
	{
		glm::vec2 Position{ 0.0f };
		float Height = 0.0f;
		int BasinA = -1, BasinB = -1;    // the drainage basins it joins (internal ids)
		int Site = -1;                   // the site whose basin it leaves, -1 none
		float Grade = 0.0f;              // the steepest 9 m of the trail to it (0 without one)
	};

	// ------------------------------------------------------------------ the graph's encoding (TgPaths), the one place it is written
	// A site or a record goes as one TgPath whose Points are the position repeated once per value and whose Values are the record, so every
	// TgPath keeps one value per point (what the editor and mask_distance_to_path expect). A trail is a TgPath of its points with the height
	// in Values; what it joins is a record in a second list, one per trail in the same order.
	TgPaths EncodeSites(const std::vector<SettlementSite>& sites);
	bool DecodeSites(const TgPaths& paths, std::vector<SettlementSite>& out);
	void EncodeTrails(const std::vector<Trail>& trails, TgPaths& geometry, TgPaths& info);
	bool DecodeTrails(const TgPaths& geometry, const TgPaths& info, std::vector<Trail>& out);
	TgPaths EncodeFords(const std::vector<Ford>& fords);
	bool DecodeFords(const TgPaths& paths, std::vector<Ford>& out);
	TgPaths EncodeCols(const std::vector<Col>& cols);
	bool DecodeCols(const TgPaths& paths, std::vector<Col>& out);

	// ------------------------------------------------------------------ 1. where to settle
	struct SiteParams
	{
		int Count = 8;                   // the most sites
		float Separation = 375.0f;       // metres between two sites (R2; the valley's max(250, size / 8))
		float Radius = 30.0f;            // the village and its platform
		float CampRadius = 6.0f;         // the camp's ground (S2)
		float CampMaxSlope = 14.0f;      // degrees, nowhere on the camp's ground (S2)
		float CampMeanSlope = 7.0f;      // degrees, the mean there (S2: flat >= 0.5)
		float WaterMin = 35.0f, WaterBest = 60.0f, WaterMax = 150.0f;   // S1 in the valley
		float FloodHeight = 1.5f;        // metres above the nearest water (X4)
		float Edge = 150.0f;             // S7 in the valley
		float RoughRadius = 22.0f, MaxRough = 2.5f;   // PickVillageSites
		float SteepShareMax = 0.2f;      // X3: of the disc over 14 degrees
		float WallDistance = 15.0f;      // X3: no ground over 40 degrees this close
		float FlatShareMin = 0.6f;       // of the disc at 8 degrees or less
		float MaxRelativeHeight = 0.8f;  // of the map's height range: above it is snow (biomes' SnowRelative)
		float Step = 10.0f;              // metres between candidates
		float ForestRadius = 150.0f, StoneRadius = 300.0f;
		float StoneSlope = 35.0f;        // degrees: ground this steep is bare rock (a proxy for stone)
		// weights
		float WWater = 1.0f, WFlat = 0.8f, WArea = 0.6f, WDry = 0.4f, WLow = 0.8f, WForest = 0.3f, WStone = 0.2f;
	};
	struct SiteResult
	{
		std::vector<SettlementSite> Sites;   // best first (Rank = index)
		std::vector<float> Score;            // N*N: the score of the nearest candidate, 0 where it is refused
		std::vector<float> WaterDistance;    // N*N
		size_t Candidates = 0, Accepted = 0;
	};
	// `forest` (0..1, >= 0.5 is forest; null = none) and the water are optional.
	SiteResult FindSettlementSites(const TgField& height, const WaterField* water, const TgField* forest, const SiteParams& params);

	// ------------------------------------------------------------------ 2. the trails
	struct RouteParams
	{
		float Cell = 6.0f;               // metres: the routing grid is the height's every round(Cell / spacing)-th sample
		float MaxGrade = 0.14f;          // rise per run along a trail (era 1 path)
		float GradeWeight = 2.0f;        // the cost of climbing at the maximum grade (x the distance)
		float SteepWeight = 25.0f;       // per unit of grade over the maximum
		float CliffGrade = 0.85f;        // never steeper than this along the move (NavProfile::MaxSlope)
		float SideFree = 0.27f;          // cross slope (tan, 15 deg) that costs nothing
		float SideWeight = 3.0f;         // per unit of tan over SideFree
		float SideMax = 0.58f;           // ground over this (tan 30 deg): a 30 deg talus cannot meet it, the trail avoids it
		float SideMaxWeight = 30.0f;
		float FordDepth = 1.0f;          // a river reach this shallow is a natural ford (NavProfile::MaxWadeDepth)
		float FordSpacing = 300.0f;      // metres between two fords
		int MaxFords = 8;
		float FordCost = 3.0f;           // x the distance in the water of a ford
		int ShapeFords = 1;              // 1: when no reach is shallow enough, the shallowest sections are fords all the same and path_grade raises their bed
		float ExtraRatio = 1.3f;         // an extra link is worth it when the network's way is this many times longer
		int ExtraLinks = 2;
		float WaterSpurMax = 300.0f;     // a trail to the nearest water when it is this near (metres along)
		float ColBasinShare = 0.03f;     // basins smaller than this share of the map have no col
		int MaxCols = 4;
		float ColSpurMax = 1500.0f;
		float FordSpurMax = 600.0f;
		float Spacing = 3.0f;            // metres between the points of a trail
		float Width = 4.0f;
		float Fade = 1.0f;               // the path mask fades to 0 over this past the width
	};
	struct RouteResult
	{
		std::vector<Trail> Trails;
		std::vector<Ford> Fords;
		std::vector<Col> Cols;
		std::vector<float> Path;         // N*N mask: 1 on the tread, 0 past Width / 2 + Fade
		int Components = 0;              // groups of sites the trails join (1 = all connected, C1)
		uint32_t RouteN = 0;
	};
	RouteResult PlanRoutes(const TgField& height, const WaterField* water, const std::vector<SettlementSite>& sites, const RouteParams& params);

	// ------------------------------------------------------------------ 3. levelling
	struct GradeParams
	{
		float MaxGrade = 0.14f;
		float Width = 4.0f;
		float Talus = 0.5f;              // tan of the side slopes of cuts, fills and platforms (26.6 deg)
		float SmoothLength = 24.0f;      // metres: the ground along a trail is averaged over this before the grade is imposed
		float MaxCut = 2.5f, MaxFill = 2.0f;
		float CulvertArea = 20000.0f;    // m2 drained: a trail does not fill over a channel this big (the stream goes under it)
		float NoCutNearWater = 6.0f;     // metres: no cut this close to the water (the bank is not cut below the water)
		float FordDepth = 0.6f;          // a built ford's bed under the water
		float Fade = 1.0f;
		int Platforms = 1;
	};
	struct GradeResult
	{
		std::vector<float> Height;       // N*N
		std::vector<float> Path, Platform;   // N*N masks
		std::vector<SettlementSite> Sites;   // with PlatformLevel, Cut and Fill
		std::vector<Trail> Trails;       // with their levelled heights, Cut, Fill, WithinGrade, MaxGrade
		std::vector<Ford> Fords;
		float DrainRaise = 0.0f;         // the most the final pit fill raised a cell
		glm::vec2 DrainRaiseAt{ 0.0f };  // where
		size_t DrainRaised = 0;          // cells it raised by more than 5 cm
		double DrainVolume = 0.0;        // m3 it added
		std::vector<float> DrainFill;    // N*N: metres the final fill raised each cell
	};
	GradeResult GradePaths(const TgField& height, const WaterField* water, const std::vector<SettlementSite>& sites, const std::vector<Trail>& trails,
		const std::vector<Ford>& fords, const GradeParams& params);

	// The grade of a trail on a ground (bilinear), measured over windows of `window` metres: the share of its length at `maxGrade` or less and
	// the steepest window. What path_grade reports and the probe checks.
	void MeasureTrailGrade(const TgField& ground, const Trail& trail, float maxGrade, float window, float& within, float& steepest);
}
