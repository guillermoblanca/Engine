#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/Nodes/DepressionFillNode.h"
#include "GB/World/Graph/Nodes/ErosionNode.h"
#include "GB/World/Graph/Nodes/FlowAccumulationNode.h"
#include "GB/World/Graph/Nodes/HeightInputNode.h"
#include "GB/World/Graph/Nodes/LakeBasinNode.h"
#include "GB/World/Graph/Nodes/LakeSiteNode.h"
#include "GB/World/Graph/Nodes/NoiseNode.h"
#include "GB/World/Graph/Nodes/OutputNode.h"
#include "GB/World/Graph/Nodes/RiverTraceNode.h"
#include "GB/World/Graph/Nodes/ScatterNode.h"
#include "GB/World/Graph/Nodes/ThermalErosionNode.h"
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
	}
}
