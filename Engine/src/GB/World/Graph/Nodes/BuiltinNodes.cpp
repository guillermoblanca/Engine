#include "GB/World/Graph/Nodes/BuiltinNodes.h"
#include "GB/World/Graph/ITerrainNode.h"
#include "GB/World/Graph/Nodes/BiomeClassifyNode.h"
#include "GB/World/Graph/Nodes/ErosionNode.h"
#include "GB/World/Graph/Nodes/NoiseNode.h"
#include "GB/World/Graph/Nodes/ThermalErosionNode.h"
#include <memory>

namespace GB
{
	void RegisterBuiltinTerrainNodes(TerrainNodeRegistry& registry)
	{
		registry.Register("heightfield_noise", [] { return std::make_unique<HeightfieldNoiseNode>(); });
		registry.Register("erosion_hydraulic", [] { return std::make_unique<HydraulicErosionNode>(); });
		registry.Register("erosion_thermal", [] { return std::make_unique<ThermalErosionNode>(); });
		registry.Register("biome_classify", [] { return std::make_unique<BiomeClassifyNode>(); });
	}
}
