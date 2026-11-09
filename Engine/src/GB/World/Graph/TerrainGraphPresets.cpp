#include "GB/World/Graph/TerrainGraphPresets.h"

namespace GB
{
	namespace
	{
		struct Builder
		{
			TerrainGraph& G;
			float X = 40.0f, Y = 60.0f;

			void Node(const char* id, const char* type, std::map<std::string, double> params = {}, const char* source = "", float x = -1.0f, float y = -1.0f)
			{
				TgNode n;
				n.Id = id; n.Type = type; n.Label = id; n.Source = source; n.Params = std::move(params);
				n.X = x >= 0.0f ? x : X; n.Y = y >= 0.0f ? y : Y;
				G.Nodes.push_back(std::move(n));
			}
			// from.port -> to.port
			void Edge(const char* from, const char* fromPort, const char* to, const char* port) { G.Edges.push_back({ from, to, port, fromPort }); }
			void Output(const char* id, const char* source, const char* from, const char* fromPort, float x, float y)
			{
				Node(id, "output", {}, source, x, y);
				Edge(from, fromPort, id, "in");
			}
		};

		void AlpinePeaks(TerrainGraph& g)
		{
			g.Id = "alpine_peaks";
			g.Name = "Alpine peaks";
			Builder b{ g };
			// the relief: sharp ridged crests, bent by a domain warp, gathered into a massif by a radial falloff, over low billowy foothills
			b.Node("ridges", "heightfield_noise", { { "Mode", 1 }, { "Frequency", 0.0042 }, { "Octaves", 8 }, { "Amplitude", 1 }, { "Lacunarity", 2.1 },
				{ "Gain", 0.56 }, { "WarpAmount", 45 }, { "WarpFrequency", 0.0028 } }, "", 40, 40);
			b.Node("massif", "gradient", { { "Mode", 1 }, { "CenterX", 0 }, { "CenterZ", 40 }, { "Radius", 240 } }, "", 40, 200);
			b.Node("range", "combine", { { "Mode", 2 } }, "", 290, 100);
			b.Edge("ridges", "height", "range", "a");
			b.Edge("massif", "mask", "range", "b");
			b.Node("foothills", "heightfield_noise", { { "Mode", 2 }, { "Frequency", 0.012 }, { "Octaves", 4 }, { "Amplitude", 0.12 }, { "Seed", 3 } }, "", 290, 260);
			b.Node("relief", "combine", { { "Mode", 0 } }, "", 540, 160);
			b.Edge("range", "out", "relief", "a");
			b.Edge("foothills", "height", "relief", "b");
			// to metres, the summits sharpened by the curve
			b.Node("metres", "remap", { { "AutoRange", 1 }, { "OutMin", 0 }, { "OutMax", 125 }, { "Gamma", 1.15 } }, "", 790, 160);
			b.Edge("relief", "out", "metres", "in");
			// crags: small sharp ribs, only on what is already steep (the faces break into buttresses and gullies, the snowfields stay smooth)
			b.Node("crags", "heightfield_noise", { { "Mode", 1 }, { "Frequency", 0.03 }, { "Octaves", 4 }, { "Amplitude", 9 }, { "Gain", 0.5 },
				{ "WarpAmount", 6 }, { "WarpFrequency", 0.02 }, { "Seed", 7 } }, "", 790, 320);
			b.Node("steep", "mask_slope", { { "MinDegrees", 20 }, { "Falloff", 15 } }, "", 790, 480);
			b.Edge("metres", "out", "steep", "height");
			b.Node("crag_mask", "combine", { { "Mode", 2 } }, "", 1040, 400);
			b.Edge("crags", "height", "crag_mask", "a");
			b.Edge("steep", "mask", "crag_mask", "b");
			b.Node("cragged", "combine", { { "Mode", 0 } }, "", 1040, 160);
			b.Edge("metres", "out", "cragged", "a");
			b.Edge("crag_mask", "out", "cragged", "b");
			// rock bedding on the faces, then rain (gullies, couloirs) and rockfall (scree at the foot, cliffs kept: a high angle of repose)
			b.Node("strata", "strata", { { "Spacing", 5 }, { "Strength", 0.55 }, { "Sharpness", 0.8 }, { "Waver", 4 }, { "MinSlopeDegrees", 28 }, { "FullSlopeDegrees", 45 } }, "", 1290, 320);
			b.Edge("cragged", "out", "strata", "height");
			b.Node("rain", "erosion_hydraulic", { { "DropletDensity", 0.63 }, { "Lifetime", 70 }, { "Capacity", 10 }, { "ErodeSpeed", 0.4 }, { "DepositSpeed", 0.15 },
				{ "Strength", 1.6 }, { "Inertia", 0.08 }, { "BrushRadius", 2 } }, "", 1290, 160);
			b.Edge("strata", "height", "rain", "height");
			b.Node("rockfall", "erosion_thermal", { { "Iterations", 12 }, { "TalusAngleDegrees", 52 }, { "Strength", 0.3 } }, "", 1540, 160);
			b.Edge("rain", "height", "rockfall", "height");
			b.Output("out_height", "height", "rockfall", "height", 1800, 40);
			// the material: snow where it lies (off the faces, in the hollows and couloirs), rock on everything steep under it
			b.Node("drainage", "flow_accumulation", {}, "", 1800, 200);
			b.Edge("rockfall", "height", "drainage", "height");
			b.Node("snow", "snow_accumulate", { { "FullSlopeDegrees", 27 }, { "BareSlopeDegrees", 40 }, { "HollowBoost", 0.25 }, { "RidgeLoss", 0.3 },
				{ "HollowRadius", 4 }, { "FlowBoost", 0.75 }, { "Breakup", 0.3 }, { "BreakupScale", 6 } }, "", 2050, 200);
			b.Edge("rockfall", "height", "snow", "height");
			b.Edge("drainage", "flow", "snow", "flow");
			b.Output("out_snow", "splat_snow", "snow", "snow", 2300, 200);
			b.Node("faces", "mask_slope", { { "MinDegrees", 24 }, { "Falloff", 10 } }, "", 2050, 360);
			b.Edge("rockfall", "height", "faces", "height");
			b.Output("out_rock", "splat_rock", "faces", "mask", 2300, 360);
			// biomes (snow high up, rock on the faces) and a few boulders; no trees on this massif
			b.Node("biomes", "biome_classify", { { "SnowRelative", 0.3 }, { "RockSlopeDegrees", 38 }, { "WaterLevel", -1000 } }, "", 2050, 520);
			b.Edge("rockfall", "height", "biomes", "height");
			b.Output("out_biome", "biome", "biomes", "biome", 2300, 520);
			b.Node("boulders", "scatter", { { "Count", 60 }, { "Variants", 6 }, { "MinSpacing", 6 }, { "MaxSlopeDegrees", 40 } }, "", 2050, 680);
			b.Edge("rockfall", "height", "boulders", "height");
			b.Output("out_rocks", "rocks", "boulders", "placements", 2300, 680);
			b.Node("no_trees", "scatter", { { "Count", 0 } }, "", 2050, 840);
			b.Edge("rockfall", "height", "no_trees", "height");
			b.Output("out_trees", "trees", "no_trees", "placements", 2300, 840);
		}

		void HighlandValley(TerrainGraph& g)
		{
			g.Id = "highland_valley";
			g.Name = "Highland valley";
			Builder b{ g };
			// laid out for a map of about 3 km, seen from the south-west: a grassy hill in front, a valley that runs from the range in the
			// north-east down to a lake, forest on its sides, meadow on its floor
			b.Node("rolling", "heightfield_noise", { { "Frequency", 0.0018 }, { "Octaves", 4 }, { "Gain", 0.4 }, { "Amplitude", 45 } }, "", 40, 40);
			b.Node("crests", "heightfield_noise", { { "Mode", 1 }, { "Frequency", 0.0012 }, { "Octaves", 9 }, { "Amplitude", 1 }, { "Gain", 0.52 },
				{ "WarpAmount", 160 }, { "WarpFrequency", 0.0008 }, { "Seed", 4 } }, "", 40, 200);
			b.Node("north_east", "gradient", { { "Mode", 0 }, { "AngleDegrees", 50 } }, "", 40, 360);
			b.Node("range_mask", "remap", { { "InMin", 0.5 }, { "InMax", 0.97 }, { "Gamma", 1.2 } }, "", 290, 360);
			b.Edge("north_east", "mask", "range_mask", "in");
			b.Node("range", "combine", { { "Mode", 2 } }, "", 540, 280);
			b.Edge("crests", "height", "range", "a");
			b.Edge("range_mask", "out", "range", "b");
			b.Node("range_m", "remap", { { "InMin", 0 }, { "InMax", 1 }, { "OutMin", 0 }, { "OutMax", 380 }, { "Clamp", 0 } }, "", 790, 280);
			b.Edge("range", "out", "range_m", "in");
			b.Node("hill", "gradient", { { "Mode", 1 }, { "CenterX", -1000 }, { "CenterZ", -1050 }, { "Radius", 850 } }, "", 40, 520);
			b.Node("hill_m", "remap", { { "OutMax", 150 }, { "Gamma", 0.75 } }, "", 290, 520);
			b.Edge("hill", "mask", "hill_m", "in");
			b.Node("bowl", "gradient", { { "Mode", 1 }, { "CenterX", -250 }, { "CenterZ", 50 }, { "Radius", 900 } }, "", 40, 680);
			b.Node("sum1", "combine", { { "Mode", 0 } }, "", 1040, 200);
			b.Edge("rolling", "height", "sum1", "a");
			b.Edge("range_m", "out", "sum1", "b");
			b.Node("sum2", "combine", { { "Mode", 0 } }, "", 1290, 280);
			b.Edge("sum1", "out", "sum2", "a");
			b.Edge("hill_m", "out", "sum2", "b");
			b.Node("ground", "combine", { { "Mode", 1 }, { "WeightB", 30 } }, "", 1540, 360);
			b.Edge("sum2", "out", "ground", "a");
			b.Edge("bowl", "mask", "ground", "b");
			// the valley: the way the water would run off the range into the basin (the same analysis as the river's), dug along it as a U with
			// a flat floor that runs downhill; both its sides are higher ground by construction
			b.Node("basin", "lake_site", { { "Margin", 18 } }, "", 1540, 520);
			b.Edge("ground", "out", "basin", "height");
			b.Node("basin_fill", "depression_fill", {}, "", 1540, 680);
			b.Edge("ground", "out", "basin_fill", "height");
			b.Edge("basin", "outlets", "basin_fill", "outlets");
			b.Node("basin_flow", "flow_accumulation", {}, "", 1540, 840);
			b.Edge("basin_fill", "height", "basin_flow", "height");
			b.Node("course", "river_trace", { { "Spacing", 10 }, { "MinStartDistance", 0.35 }, { "Smooth", 8 } }, "", 1790, 680);
			b.Edge("basin_fill", "height", "course", "height");
			b.Edge("basin_flow", "flow", "course", "flow");
			b.Edge("basin", "outlets", "course", "outlets");
			b.Node("valley", "valley_carve", { { "FloorWidth", 110 }, { "WidthVariation", 0.35 }, { "WallWidth", 320 }, { "WallGamma", 1.5 }, { "Incision", 10 },
				{ "BedSmoothing", 400 }, { "FloorTilt", 0.01 }, { "MaxFill", 4 } }, "", 1790, 360);
			b.Edge("ground", "out", "valley", "height");
			b.Edge("course", "river", "valley", "path");
			// the lake at the foot of the floor, and the river that runs down the floor into it
			b.Node("site", "lake_site", { { "Margin", 18 } }, "", 2040, 520);
			b.Edge("valley", "height", "site", "height");
			b.Node("lake", "lake_carve", { { "Radius", 170 }, { "Depth", 7 }, { "LevelAbove", 1.5 }, { "BankSlope", 0.2 }, { "Irregularity", 0.3 } }, "", 2290, 360);
			b.Edge("valley", "height", "lake", "height");
			b.Edge("site", "site", "lake", "site");
			b.Node("fill", "depression_fill", {}, "", 2290, 680);
			b.Edge("lake", "height", "fill", "height");
			b.Edge("site", "outlets", "fill", "outlets");
			b.Node("flow", "flow_accumulation", {}, "", 2540, 680);
			b.Edge("fill", "height", "flow", "height");
			b.Node("trace", "river_trace", { { "Spacing", 8 }, { "MinStartDistance", 0.3 } }, "", 2790, 680);
			b.Edge("fill", "height", "trace", "height");
			b.Edge("flow", "flow", "trace", "flow");
			b.Edge("site", "outlets", "trace", "outlets");
			b.Node("river", "river_carve", { { "Width", 9 }, { "Depth", 1.6 }, { "MouthLength", 30 } }, "", 2540, 360);
			b.Edge("lake", "height", "river", "height");
			b.Edge("trace", "river", "river", "river");
			b.Edge("lake", "water", "river", "water");
			b.Output("out_water", "water", "river", "water", 3040, 40);
			// rain and rockfall everywhere but in the water
			b.Node("dry", "mask_water", { { "Reach", 3 }, { "Falloff", 8 } }, "", 2790, 520);
			b.Edge("river", "height", "dry", "height");
			b.Edge("river", "water", "dry", "water");
			b.Node("rain", "erosion_hydraulic", { { "DropletDensity", 0.3 }, { "Lifetime", 70 }, { "Capacity", 8 }, { "Strength", 1.0 }, { "Seed", 1 } }, "", 3040, 360);
			b.Edge("river", "height", "rain", "height");
			b.Edge("dry", "mask", "rain", "mask");
			b.Node("rockfall", "erosion_thermal", { { "Iterations", 10 }, { "TalusAngleDegrees", 42 }, { "Strength", 0.3 } }, "", 3290, 360);
			b.Edge("rain", "height", "rockfall", "height");
			b.Edge("dry", "mask", "rockfall", "mask");
			// the droplets leave pits and mounds a few metres across: on ground that was gentle before the rain (a meadow, the valley's floor)
			// they are smoothed away, the faces and their gullies keep them; never at the water, whose banks the lake and the river were carved to
			b.Node("smoothed", "blur", { { "Radius", 4 }, { "Passes", 3 } }, "", 3290, 200);
			b.Edge("rockfall", "height", "smoothed", "in");
			b.Node("gentle", "mask_slope", { { "MinDegrees", 0 }, { "MaxDegrees", 20 }, { "Falloff", 8 } }, "", 3290, 520);
			b.Edge("river", "height", "gentle", "height");   // (gentle before the rain: the pits it digs are steep themselves)
			b.Node("gentle_dry", "combine", { { "Mode", 2 } }, "", 3540, 520);
			b.Edge("gentle", "mask", "gentle_dry", "a");
			b.Edge("dry", "mask", "gentle_dry", "b");
			b.Node("settled", "blend", {}, "", 3540, 360);
			b.Edge("rockfall", "height", "settled", "a");
			b.Edge("smoothed", "out", "settled", "b");
			b.Edge("gentle_dry", "out", "settled", "mask");
			b.Output("out_height", "height", "settled", "out", 3790, 200);
			// moisture: wetter down the drainage, drier up high
			b.Node("drainage", "flow_accumulation", {}, "", 3540, 520);
			b.Edge("settled", "out", "drainage", "height");
			b.Node("wet", "moisture", { { "Scale", 260 }, { "FlowWetness", 0.35 }, { "AltitudeDryness", 0.3 }, { "FlowBlur", 8 } }, "", 3790, 520);
			b.Edge("settled", "out", "wet", "height");
			b.Edge("drainage", "flow", "wet", "flow");
			b.Output("out_moisture", "moisture", "wet", "moisture", 4040, 520);
			// where forest grows: on the valley's sides and the hills (not too steep, below the tree line) where it is moist enough, not on the
			// open floor of the valley; and a strip of riverside wood along the river
			b.Node("wet_enough", "remap", { { "InMin", 0.3 }, { "InMax", 0.55 } }, "", 3790, 680);
			b.Edge("wet", "moisture", "wet_enough", "in");
			b.Node("sides", "mask_slope", { { "MinDegrees", 0 }, { "MaxDegrees", 34 }, { "Falloff", 5 } }, "", 3790, 840);
			b.Edge("settled", "out", "sides", "height");
			b.Node("below_treeline", "mask_height", { { "Relative", 1 }, { "Min", -1 }, { "Max", 0.48 }, { "Falloff", 0.08 } }, "", 3790, 1000);
			b.Edge("settled", "out", "below_treeline", "height");
			// (the floor's edge frayed by a noise, so the wood does not line it like a hedge)
			b.Node("edge_noise", "heightfield_noise", { { "Frequency", 0.009 }, { "Octaves", 3 }, { "Amplitude", 1 }, { "Seed", 9 } }, "", 3540, 1320);
			b.Node("edge_jitter", "remap", { { "AutoRange", 1 }, { "OutMin", -0.6 }, { "OutMax", 0.6 } }, "", 3790, 1320);
			b.Edge("edge_noise", "height", "edge_jitter", "in");
			b.Node("floor_frayed", "combine", { { "Mode", 0 } }, "", 3790, 1240);
			b.Edge("valley", "floor", "floor_frayed", "a");
			b.Edge("edge_jitter", "out", "floor_frayed", "b");
			b.Node("open_floor", "remap", { { "OutMin", 1 }, { "OutMax", 0 } }, "", 3790, 1160);
			b.Edge("floor_frayed", "out", "open_floor", "in");
			b.Node("suit_a", "combine", { { "Mode", 2 } }, "", 4040, 760);
			b.Edge("wet_enough", "out", "suit_a", "a");
			b.Edge("sides", "mask", "suit_a", "b");
			b.Node("suit_b", "combine", { { "Mode", 2 } }, "", 4290, 840);
			b.Edge("suit_a", "out", "suit_b", "a");
			b.Edge("below_treeline", "mask", "suit_b", "b");
			b.Node("suit_c", "combine", { { "Mode", 2 } }, "", 4540, 920);
			b.Edge("suit_b", "out", "suit_c", "a");
			b.Edge("open_floor", "out", "suit_c", "b");
			// the hill in front is grazed open: pasture on its crown, the wood starting down its flanks
			b.Node("pasture", "remap", { { "InMin", 0.12 }, { "InMax", 0.42 }, { "OutMin", 1 }, { "OutMax", 0 } }, "", 4540, 760);
			b.Edge("hill", "mask", "pasture", "in");
			b.Node("suit_d", "combine", { { "Mode", 2 } }, "", 4790, 840);
			b.Edge("suit_c", "out", "suit_d", "a");
			b.Edge("pasture", "out", "suit_d", "b");
			b.Node("suit_m", "remap", { { "InMin", 0 }, { "InMax", 0.7 } }, "", 4790, 920);
			b.Edge("suit_d", "out", "suit_m", "in");
			b.Node("riverside", "mask_distance_to_path", { { "Inner", 24 }, { "Outer", 70 } }, "", 4540, 1080);
			b.Edge("settled", "out", "riverside", "height");
			b.Edge("trace", "river", "riverside", "path");
			b.Node("riverside_t", "combine", { { "Mode", 2 }, { "WeightB", 0.8 } }, "", 4790, 1080);
			b.Edge("below_treeline", "mask", "riverside_t", "a");
			b.Edge("riverside", "mask", "riverside_t", "b");
			b.Node("suit", "combine", { { "Mode", 4 } }, "", 5040, 1000);
			b.Edge("suit_m", "out", "suit", "a");
			b.Edge("riverside_t", "out", "suit", "b");
			b.Node("forest_ok", "blur", { { "Radius", 3 }, { "Passes", 2 } }, "", 5290, 1000);
			b.Edge("suit", "out", "forest_ok", "in");
			// biomes: rock on the faces, snow on the summits only, forest where it may grow
			b.Node("biomes", "biome_classify", { { "RockSlopeDegrees", 42 }, { "SnowRelative", 0.82 }, { "ShoreBand", 0.8 } }, "", 5540, 680);
			b.Edge("settled", "out", "biomes", "height");
			b.Edge("wet", "moisture", "biomes", "moisture");
			b.Edge("river", "water", "biomes", "water");
			b.Edge("forest_ok", "out", "biomes", "forest");
			b.Output("out_biome", "biome", "biomes", "biome", 5790, 680);
			// trees: the forest, gathered in groves with glades between them, and a few lone trees on the meadow
			b.Node("tree_chance", "biome_weights", { { "Forest", 1 }, { "Meadow", 0.012 }, { "Rock", 0.04 } }, "", 5790, 840);
			b.Edge("biomes", "biome", "tree_chance", "biome");
			b.Node("tree_soft", "blur", { { "Radius", 2 }, { "Passes", 2 } }, "", 6040, 840);
			b.Edge("tree_chance", "weight", "tree_soft", "in");
			b.Node("trees", "scatter", { { "Count", 40000 }, { "Variants", 7 }, { "MinSpacing", 5 }, { "MaxSlopeDegrees", 36 }, { "Probabilistic", 1 }, { "StrongHash", 1 },
				{ "Clumping", 0.6 }, { "ClumpScale", 45 }, { "Seed", 3 } }, "", 6290, 840);
			b.Edge("settled", "out", "trees", "height");
			b.Edge("tree_soft", "out", "trees", "density");
			b.Output("out_trees", "trees", "trees", "placements", 6540, 840);
			b.Node("rock_chance", "biome_weights", { { "Rock", 0.5 }, { "Shore", 0.3 }, { "Snow", 0.2 }, { "Meadow", 0.03 } }, "", 5790, 1000);
			b.Edge("biomes", "biome", "rock_chance", "biome");
			b.Node("stones", "scatter", { { "Count", 900 }, { "Variants", 6 }, { "MinSpacing", 8 }, { "MaxSlopeDegrees", 50 }, { "Probabilistic", 1 }, { "StrongHash", 1 }, { "Seed", 2 } }, "", 6040, 1000);
			b.Edge("settled", "out", "stones", "height");
			b.Edge("rock_chance", "weight", "stones", "density");
			b.Output("out_rocks", "rocks", "stones", "placements", 6290, 1000);
			// the ground's material: dry grass where it is dry, the canopy over the forest, rock on the faces, snow on top
			b.Node("dryness", "remap", { { "InMin", 0.5 }, { "InMax", 0.28 } }, "", 5790, 1160);
			b.Edge("wet", "moisture", "dryness", "in");
			b.Output("out_dry", "splat_dry", "dryness", "out", 6040, 1160);
			// lush meadow where it is not dry: the same green as the blades around the camera, so the sward does not end in a darker ground
			b.Node("meadow", "biome_mask", { { "Biome", 2 }, { "Blur", 2 } }, "", 5540, 1240);
			b.Edge("biomes", "biome", "meadow", "biome");
			b.Node("not_dry", "remap", { { "OutMin", 0.85 }, { "OutMax", 0 } }, "", 5540, 1400);
			b.Edge("dryness", "out", "not_dry", "in");
			b.Node("lush", "combine", { { "Mode", 2 } }, "", 5790, 1240);
			b.Edge("meadow", "mask", "lush", "a");
			b.Edge("not_dry", "out", "lush", "b");
			b.Output("out_grass", "splat_grass", "lush", "out", 6040, 1240);
			b.Node("canopy", "biome_mask", { { "Biome", 3 }, { "Blur", 2 } }, "", 5790, 1320);
			b.Edge("biomes", "biome", "canopy", "biome");
			b.Output("out_canopy", "splat_forest", "canopy", "mask", 6040, 1320);
			b.Node("faces", "mask_slope", { { "MinDegrees", 43 }, { "Falloff", 7 } }, "", 5790, 1480);
			b.Edge("settled", "out", "faces", "height");
			b.Output("out_rock", "splat_rock", "faces", "mask", 6040, 1480);
			b.Node("snow", "snow_accumulate", { { "MinRelativeHeight", 0.8 }, { "FullSlopeDegrees", 30 }, { "BareSlopeDegrees", 44 }, { "FlowBoost", 0.5 }, { "Breakup", 0.3 }, { "BreakupScale", 20 } }, "", 5790, 1640);
			b.Edge("settled", "out", "snow", "height");
			b.Edge("drainage", "flow", "snow", "flow");
			b.Output("out_snow", "splat_snow", "snow", "snow", 6040, 1640);
		}

		void TemperateValley(TerrainGraph& g)
		{
			g.Id = "temperate_valley";
			g.Name = "Temperate valley";
			Builder b{ g };
			b.Node("hills", "hills", { { "Height", 16 }, { "Scale", 140 }, { "Flatten", 0.25 } }, "", 40, 160);
			// where the water goes (the same analysis as GB/World/Water/WaterPlanner.h), then carve it
			b.Node("site", "lake_site", {}, "", 290, 40);
			b.Edge("hills", "height", "site", "height");
			b.Node("lake", "lake_carve", { { "Radius", 22 }, { "Depth", 3 }, { "LevelAbove", 1 }, { "Irregularity", 0.12 } }, "", 540, 40);
			b.Edge("hills", "height", "lake", "height");
			b.Edge("site", "site", "lake", "site");
			b.Node("fill", "depression_fill", {}, "", 790, 200);
			b.Edge("lake", "height", "fill", "height");
			b.Edge("site", "outlets", "fill", "outlets");
			b.Node("flow", "flow_accumulation", {}, "", 1040, 200);
			b.Edge("fill", "height", "flow", "height");
			b.Node("trace", "river_trace", { { "Spacing", 6 } }, "", 1290, 200);
			b.Edge("fill", "height", "trace", "height");
			b.Edge("flow", "flow", "trace", "flow");
			b.Edge("site", "outlets", "trace", "outlets");
			b.Node("river", "river_carve", { { "Width", 5 } }, "", 1540, 40);
			b.Edge("lake", "height", "river", "height");
			b.Edge("trace", "river", "river", "river");
			b.Edge("lake", "water", "river", "water");
			b.Output("out_water", "water", "river", "water", 1800, 0);
			// rain everywhere but in the water and its banks
			b.Node("dry", "mask_water", { { "Reach", 1 }, { "Falloff", 4 } }, "", 1540, 360);
			b.Edge("river", "height", "dry", "height");
			b.Edge("river", "water", "dry", "water");
			b.Node("rain", "erosion_hydraulic", { { "DropletDensity", 0.17 } }, "", 1800, 160);
			b.Edge("river", "height", "rain", "height");
			b.Edge("dry", "mask", "rain", "mask");
			b.Output("out_height", "height", "rain", "height", 2050, 40);
			// moisture and biomes, then what grows and lies where
			b.Node("wet", "moisture", { { "Scale", 60 } }, "", 2050, 200);
			b.Edge("rain", "height", "wet", "height");
			b.Edge("flow", "flow", "wet", "flow");
			b.Output("out_moisture", "moisture", "wet", "moisture", 2300, 200);
			b.Node("biomes", "biome_classify", { { "RockSlopeDegrees", 36 }, { "ForestMoisture", 0.45 }, { "ShoreBand", 0.7 } }, "", 2050, 360);
			b.Edge("rain", "height", "biomes", "height");
			b.Edge("wet", "moisture", "biomes", "moisture");
			b.Edge("river", "water", "biomes", "water");
			b.Output("out_biome", "biome", "biomes", "biome", 2300, 360);
			b.Node("forest", "biome_mask", { { "Biome", 3 }, { "Blur", 1 } }, "", 2300, 520);
			b.Edge("biomes", "biome", "forest", "biome");
			b.Node("trees", "scatter", { { "Count", 420 }, { "Variants", 7 }, { "MinSpacing", 4.2 }, { "MaxSlopeDegrees", 38 }, { "DensityThreshold", 0.6 } }, "", 2550, 520);
			b.Edge("rain", "height", "trees", "height");
			b.Edge("forest", "mask", "trees", "density");
			b.Output("out_trees", "trees", "trees", "placements", 2800, 520);
			b.Node("stones", "scatter", { { "Count", 70 }, { "Variants", 6 }, { "MinSpacing", 5 }, { "MaxSlopeDegrees", 55 }, { "Seed", 2 } }, "", 2550, 680);
			b.Edge("rain", "height", "stones", "height");
			b.Output("out_rocks", "rocks", "stones", "placements", 2800, 680);
			b.Node("faces", "mask_slope", { { "MinDegrees", 30 }, { "Falloff", 8 } }, "", 2050, 840);
			b.Edge("rain", "height", "faces", "height");
			b.Output("out_rock", "splat_rock", "faces", "mask", 2300, 840);
		}
	}

	TerrainGraph MakeWorldGraph(const WorldGraphOptions& o)
	{
		using Ground = WorldGraphOptions::Ground;
		TerrainGraph g;
		Builder b{ g };
		const bool mountains = o.Kind == Ground::Mountains;
		if (o.Kind == Ground::Dunes)
		{
			g.Id = "desert";
			g.Name = "Desert (generator 4)";
			b.Node("dunes", "dunes", {}, "", 40, 160);
			b.Output("out_height", "height", "dunes", "height", 290, 40);
			b.Node("sand", "constant", { { "Value", 6 } }, "", 40, 320);   // GB::Biome::Dunes everywhere
			b.Output("out_biome", "biome", "sand", "value", 290, 320);
			b.Node("palm_chance", "constant", { { "Value", 0.04 } }, "", 290, 480);
			b.Node("palms", "scatter", { { "Count", o.TreeCount }, { "Variants", 3 }, { "MinSpacing", 18 }, { "MaxSlopeDegrees", 38 }, { "Probabilistic", 1 }, { "Seed", 1 } }, "", 540, 480);
			b.Edge("dunes", "height", "palms", "height");
			b.Edge("palm_chance", "value", "palms", "density");
			b.Output("out_trees", "trees", "palms", "placements", 790, 480);
			b.Node("stone_chance", "constant", { { "Value", 0.05 } }, "", 290, 640);
			b.Node("stones", "scatter", { { "Count", o.RockCount }, { "Variants", 6 }, { "MinSpacing", 5 }, { "MaxSlopeDegrees", 55 }, { "Probabilistic", 1 }, { "Seed", 2 } }, "", 540, 640);
			b.Edge("dunes", "height", "stones", "height");
			b.Edge("stone_chance", "value", "stones", "density");
			b.Output("out_rocks", "rocks", "stones", "placements", 790, 640);
			return g;
		}

		g.Id = mountains ? "alpine" : "temperate";
		g.Name = mountains ? "Alpine (generator 4)" : "Temperate (generator 4)";
		b.Node("ground", "hills", { { "Height", mountains ? 58 : 16 }, { "Scale", mountains ? 190 : 140 }, { "Flatten", mountains ? 0.05 : 0.25 } }, "", 40, 200);
		// the water: where the lake goes and where the rain runs, analysed on the ground with the lake dug (as GB/World/Water/WaterPlanner.h does), then carved
		const char* ground = "ground";
		const char* groundPort = "height";
		const char* water = nullptr;
		const char* flow = nullptr;
		if (o.Lake)
		{
			b.Node("site", "lake_site", {}, "", 290, 40);
			b.Edge("ground", "height", "site", "height");
			b.Node("lake", "lake_carve", { { "Radius", o.LakeRadius }, { "Depth", o.LakeDepth }, { "LevelAbove", o.LakeLevelAbove }, { "Irregularity", o.LakeIrregularity } }, "", 540, 40);
			b.Edge("ground", "height", "lake", "height");
			b.Edge("site", "site", "lake", "site");
			ground = "lake"; water = "lake";
		}
		if (o.River)
		{
			b.Node("fill", "depression_fill", {}, "", 790, 200);
			b.Edge(ground, groundPort, "fill", "height");
			if (o.Lake) b.Edge("site", "outlets", "fill", "outlets");
			b.Node("drainage", "flow_accumulation", {}, "", 1040, 200);
			b.Edge("fill", "height", "drainage", "height");
			b.Node("course", "river_trace", { { "MinStartDistance", 0.3 }, { "Spacing", 6 } }, "", 1290, 200);
			b.Edge("fill", "height", "course", "height");
			b.Edge("drainage", "flow", "course", "flow");
			if (o.Lake) b.Edge("site", "outlets", "course", "outlets");
			b.Node("river", "river_carve", { { "Width", o.RiverWidth } }, "", 1540, 40);
			b.Edge(ground, groundPort, "river", "height");
			b.Edge("course", "river", "river", "river");
			if (water) b.Edge(water, "water", "river", "water");
			ground = "river"; water = "river"; flow = "drainage";
		}
		if (water) b.Output("out_water", "water", water, "water", 1800, 0);
		// rain everywhere but in the water and along its channel (out to about six half-widths of the river)
		if (o.Erosion)
		{
			b.Node("rain", "erosion_hydraulic", { { "DropletDensity", 1.0 / 6.0 }, { "Seed", 1 } }, "", 1800, 160);
			b.Edge(ground, groundPort, "rain", "height");
			if (water)
			{
				b.Node("dry", "mask_water", { { "Reach", 2 }, { "Falloff", 3 } }, "", 1540, 360);
				b.Edge(ground, groundPort, "dry", "height");
				b.Edge(water, "water", "dry", "water");
				b.Edge("dry", "mask", "rain", "mask");
			}
			ground = "rain";
		}
		b.Output("out_height", "height", ground, groundPort, 2050, 40);
		// moisture, biomes
		b.Node("wet", "moisture", { { "Scale", 60 }, { "FlowWetness", flow ? 0.2 : 0.0 }, { "AltitudeDryness", 0 } }, "", 2050, 200);
		b.Edge(ground, groundPort, "wet", "height");
		if (flow) b.Edge(flow, "flow", "wet", "flow");
		b.Output("out_moisture", "moisture", "wet", "moisture", 2300, 200);
		b.Node("biomes", "biome_classify", { { "RockSlopeDegrees", mountains ? 26 : 33 }, { "ForestMoisture", o.ForestMoisture }, { "ShoreBand", 0.7 },
			{ "SnowRelative", mountains ? 0.68 : 0 }, { "WaterLevel", -1e6 } }, "", 2050, 360);
		b.Edge(ground, groundPort, "biomes", "height");
		b.Edge("wet", "moisture", "biomes", "moisture");
		if (water) b.Edge(water, "water", "biomes", "water");
		b.Output("out_biome", "biome", "biomes", "biome", 2300, 360);
		// trees: forest, a lone tree here and there in the meadows, a few on rock; none within two widths of the river
		b.Node("tree_chance", "biome_weights", { { "Forest", 1 }, { "Meadow", 0.06 }, { "Rock", 0.12 } }, "", 2300, 520);
		b.Edge("biomes", "biome", "tree_chance", "biome");
		const char* treeDensity = "tree_chance";
		const char* treeDensityPort = "weight";
		if (o.River)
		{
			b.Node("banks", "mask_water", { { "Reach", 1.2 }, { "Falloff", 0.05 } }, "", 2300, 680);
			b.Edge(ground, groundPort, "banks", "height");
			b.Edge(water, "water", "banks", "water");
			b.Node("tree_density", "combine", { { "Mode", 2 } }, "", 2550, 600);
			b.Edge("tree_chance", "weight", "tree_density", "a");
			b.Edge("banks", "mask", "tree_density", "b");
			treeDensity = "tree_density"; treeDensityPort = "out";
		}
		b.Node("trees", "scatter", { { "Count", o.TreeCount }, { "Variants", 7 }, { "MinSpacing", 4.2 }, { "MaxSlopeDegrees", 38 }, { "Probabilistic", 1 }, { "Seed", 1 } }, "", 2800, 520);
		b.Edge(ground, groundPort, "trees", "height");
		b.Edge(treeDensity, treeDensityPort, "trees", "density");
		b.Output("out_trees", "trees", "trees", "placements", 3050, 520);
		// rocks: mostly on rock, by the water and in the snow
		b.Node("rock_chance", "biome_weights", { { "Shore", 0.35 }, { "Meadow", 0.05 }, { "Forest", 0.05 }, { "Rock", 0.5 }, { "Snow", 0.25 }, { "Dunes", 0.05 } }, "", 2300, 840);
		b.Edge("biomes", "biome", "rock_chance", "biome");
		b.Node("stones", "scatter", { { "Count", o.RockCount }, { "Variants", 6 }, { "MinSpacing", 5 }, { "MaxSlopeDegrees", 55 }, { "Probabilistic", 1 }, { "Seed", 2 } }, "", 2800, 840);
		b.Edge(ground, groundPort, "stones", "height");
		b.Edge("rock_chance", "weight", "stones", "density");
		b.Output("out_rocks", "rocks", "stones", "placements", 3050, 840);
		// on mountains, bare rock shows on the faces through the snow
		if (mountains)
		{
			b.Node("faces", "mask_slope", { { "MinDegrees", 30 }, { "Falloff", 10 } }, "", 2050, 1000);
			b.Edge(ground, groundPort, "faces", "height");
			b.Output("out_rock", "splat_rock", "faces", "mask", 2300, 1000);
		}
		return g;
	}

	std::vector<std::string> TerrainGraphPresetNames() { return { "temperate", "alpine", "desert", "alpine_peaks", "temperate_valley", "highland_valley" }; }

	bool MakeTerrainGraphPreset(const std::string& name, TerrainGraph& out)
	{
		out = TerrainGraph();
		WorldGraphOptions options;
		if (name == "alpine_peaks") AlpinePeaks(out);
		else if (name == "temperate_valley") TemperateValley(out);
		else if (name == "highland_valley") HighlandValley(out);
		else if (name == "temperate") out = MakeWorldGraph(options);
		else if (name == "alpine") { options.Kind = WorldGraphOptions::Ground::Mountains; options.LakeRadius = 18.0f; out = MakeWorldGraph(options); }
		else if (name == "desert") { options.Kind = WorldGraphOptions::Ground::Dunes; out = MakeWorldGraph(options); }
		else return false;
		return true;
	}
}
