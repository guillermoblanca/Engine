#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/Nodes/DepressionFillNode.h"
#include "GB/World/Graph/Nodes/DrainageNodes.h"
#include "GB/World/Graph/Nodes/FluvialErosionNode.h"
#include "GB/World/Graph/Nodes/TalusNode.h"
#include "GB/World/Graph/Nodes/DepositNode.h"
#include "GB/World/Graph/Nodes/ErosionNode.h"
#include "GB/World/Graph/Nodes/FieldNodes.h"
#include "GB/World/Graph/Nodes/FlowAccumulationNode.h"
#include "GB/World/Graph/Nodes/HeightInputNode.h"
#include "GB/World/Graph/Nodes/LakeBasinNode.h"
#include "GB/World/Graph/Nodes/LakeSiteNode.h"
#include "GB/World/Graph/Nodes/MaskNodes.h"
#include "GB/World/Graph/Nodes/NoiseNode.h"
#include "GB/World/Graph/Nodes/OutputNode.h"
#include "GB/World/Graph/Nodes/RiverTraceNode.h"
#include "GB/World/Graph/Nodes/ScatterNode.h"
#include "GB/World/Graph/Nodes/SurfaceLayerNodes.h"
#include "GB/World/Graph/Nodes/SettleNodes.h"
#include "GB/World/Graph/Nodes/ThermalErosionNode.h"
#include "GB/World/Graph/Nodes/ValleyNodes.h"
#include "GB/World/Graph/Nodes/WaterCarveNodes.h"
#include <memory>

namespace GB
{
	void RegisterBuiltinTerrainNodes(TerrainNodeRegistry& registry)
	{
		registry.Register("output", [] { return std::make_unique<OutputNode>(); });
		registry.Register("heightfield_noise", [] { return std::make_unique<HeightfieldNoiseNode>(); });
		registry.Register("erosion_hydraulic", [] { return std::make_unique<HydraulicErosionNode>(); });
		registry.Register("erosion_thermal", [] { return std::make_unique<ThermalErosionNode>(); });
		registry.Register("biome_classify", [] { return std::make_unique<BiomeClassifyNode>(); });
		registry.Register("scatter", [] { return std::make_unique<ScatterNode>(); });
		registry.Register("flow_accumulation", [] { return std::make_unique<FlowAccumulationNode>(); });
		// the water of a world: where its lake is, where the rain goes, the river that drains to the lake
		registry.Register("height_input", [] { return std::make_unique<HeightInputNode>(); });
		registry.Register("depression_fill", [] { return std::make_unique<DepressionFillNode>(); });
		registry.Register("lake_basin", [] { return std::make_unique<LakeBasinNode>(); });
		registry.Register("lake_site", [] { return std::make_unique<LakeSiteNode>(); });
		registry.Register("river_trace", [] { return std::make_unique<RiverTraceNode>(); });
		// generators, reshaping and combining (Nodes/FieldNodes.h)
		registry.Register("constant", [] { return std::make_unique<ConstantNode>(); });
		registry.Register("gradient", [] { return std::make_unique<GradientNode>(); });
		registry.Register("hills", [] { return std::make_unique<HillsNode>(); });
		registry.Register("dunes", [] { return std::make_unique<DunesNode>(); });
		registry.Register("remap", [] { return std::make_unique<RemapNode>(); });
		registry.Register("terrace", [] { return std::make_unique<TerraceNode>(); });
		registry.Register("blur", [] { return std::make_unique<BlurNode>(); });
		registry.Register("warp", [] { return std::make_unique<WarpNode>(); });
		registry.Register("combine", [] { return std::make_unique<CombineNode>(); });
		registry.Register("blend", [] { return std::make_unique<BlendNode>(); });
		registry.Register("strata", [] { return std::make_unique<StrataNode>(); });
		registry.Register("moisture", [] { return std::make_unique<MoistureNode>(); });
		// valleys along a course (Nodes/ValleyNodes.h)
		registry.Register("path_curve", [] { return std::make_unique<PathCurveNode>(); });
		registry.Register("valley_carve", [] { return std::make_unique<ValleyCarveNode>(); });
		registry.Register("mask_distance_to_path", [] { return std::make_unique<PathDistanceMaskNode>(); });
		// carving the world's water (Nodes/WaterCarveNodes.h)
		registry.Register("lake_carve", [] { return std::make_unique<LakeCarveNode>(); });
		registry.Register("river_carve", [] { return std::make_unique<RiverCarveNode>(); });
		// generator 5, phase E1 (TERRENO_V5): one drainage, lakes filled to their overflow level (Nodes/DrainageNodes.h)
		registry.Register("drainage", [] { return std::make_unique<DrainageNode>(); });
		registry.Register("lake_fill", [] { return std::make_unique<LakeFillNode>(); });
		// generator 5, phase E2: stream-power incision (Nodes/FluvialErosionNode.h)
		registry.Register("fluvial_erosion", [] { return std::make_unique<FluvialErosionNode>(); });
		// generator 5, phase E4: talus with hardness, cliffs and scree (Nodes/TalusNode.h)
		registry.Register("talus", [] { return std::make_unique<TalusNode>(); });
		// generator 5, phase E3: the deposit, valleys filled part of the way and fans (Nodes/DepositNode.h)
		registry.Register("deposit", [] { return std::make_unique<DepositNode>(); });
		// masks and the snow cover (Nodes/MaskNodes.h)
		registry.Register("mask_height", [] { return std::make_unique<HeightMaskNode>(); });
		registry.Register("mask_slope", [] { return std::make_unique<SlopeMaskNode>(); });
		registry.Register("mask_curvature", [] { return std::make_unique<CurvatureMaskNode>(); });
		registry.Register("mask_water", [] { return std::make_unique<WaterMaskNode>(); });
		registry.Register("biome_mask", [] { return std::make_unique<BiomeMaskNode>(); });
		registry.Register("snow_accumulate", [] { return std::make_unique<SnowAccumulateNode>(); });
		registry.Register("biome_weights", [] { return std::make_unique<BiomeWeightsNode>(); });
		// the material layers and the trees of the terrain of TERRENO_V5 (Nodes/SurfaceLayerNodes.h)
		registry.Register("layer_cliff_scree", [] { return std::make_unique<CliffScreeLayerNode>(); });
		registry.Register("layer_wet", [] { return std::make_unique<WetLayerNode>(); });
		registry.Register("layer_normalize", [] { return std::make_unique<NormalizeLayersNode>(); });
		registry.Register("biome_water", [] { return std::make_unique<BiomeWaterNode>(); });
		registry.Register("tree_species", [] { return std::make_unique<TreeSpeciesNode>(); });
		// settlement sites, the trails between them and their levelling (Nodes/SettleNodes.h; GRAFO_TERRENO, "Asentamientos y sendas")
		registry.Register("settlement_sites", [] { return std::make_unique<SettlementSitesNode>(); });
		registry.Register("route_network", [] { return std::make_unique<RouteNetworkNode>(); });
		registry.Register("path_grade", [] { return std::make_unique<PathGradeNode>(); });
	}
}
