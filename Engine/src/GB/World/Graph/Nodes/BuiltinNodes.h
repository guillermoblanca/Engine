#pragma once

namespace GB
{
	class TerrainNodeRegistry;

	// Registers every node type GB ships with (heightfield_noise, erosion_hydraulic, biome_classify...) into
	// `registry`. This is the one file allowed to know about every concrete node (a small composition root,
	// the same role GB::ContentPacks plays for content sections): GB is a single static library
	// (Engine/CMakeLists.txt), and a node's .cpp otherwise exports no symbol any other translation unit
	// references by name, so a self-registering static in it could be dropped by the linker before it ever
	// runs. A node added outside the engine (Editor, Sandbox, a mod) is part of its own executable instead and
	// can self-register safely with TerrainNodeAutoRegister (GB/World/Graph/ITerrainNode.h).
	void RegisterBuiltinTerrainNodes(TerrainNodeRegistry& registry);
}
