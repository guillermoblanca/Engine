#pragma once

namespace GB
{
	class TerrainNodeRegistry;

	// Registers every node type GB ships with (output, heightfield_noise, erosion_hydraulic, biome_classify...)
	// into `registry`. This is the one file allowed to know about every concrete node (a small composition
	// root, the same role GB::ContentPacks plays for content sections): GB is a single static library
	// (Engine/CMakeLists.txt), and a node's .cpp otherwise exports no symbol any other translation unit
	// references by name, so a self-registering static in it could be dropped by the linker before it ever
	// runs. There is no self-registration at all: a node added outside the engine (Editor, Sandbox, a mod or a
	// plugin loaded at run time) is registered explicitly into the registry its owner passes around, e.g. from
	// an exported `GB_RegisterTerrainNodes(TerrainNodeRegistry&)` the loader calls.
	void RegisterBuiltinTerrainNodes(TerrainNodeRegistry& registry);
}
