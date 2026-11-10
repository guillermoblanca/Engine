#pragma once
#include "GB/World/Heightmap.h"
#include <vector>

namespace GB
{
	struct FlowAccumulationResult
	{
		uint32_t N = 0;
		std::vector<float> Accumulation;   // N*N, row major like Heightmap (index = j*N+i); >= 1 everywhere (a cell always drains itself)
	};

	// D8 flow accumulation: every cell drains into its single steepest downhill neighbour (8-connected; a tie
	// keeps whichever comes first in a fixed direction order, so this is deterministic); a cell with no downhill
	// neighbour (a local pit, or the lowest point of the map) keeps what reached it instead of passing it on --
	// there is no pit-filling pass. Accumulation is 1 at a ridge (just the cell's own rain) and grows toward the
	// valleys and the outlets, so thresholding it marks the drainage network water would actually carve on this
	// terrain, as built -- unlike the Dijkstra path WorldGen.cpp decides on a coarse grid before the hills are
	// even final (docs/GRAFO_TERRENO.md, "huecos de realismo" #3). Deterministic, CPU only.
	class FlowAccumulation
	{
	public:
		static FlowAccumulationResult Run(const Heightmap& heightmap);
		// The same over a flat n x n array of heights (row major, index = j*n+i): what a terrain graph field holds, with no Heightmap in between.
		static FlowAccumulationResult Run(const float* heights, uint32_t n);
		// Where each cell of that array drains to: the flat index (j*n+i) of its steepest downhill neighbour, or -1 for a cell with none. The
		// same rule Run accumulates along, for whoever wants to follow the water itself (the river_trace node).
		static std::vector<int> Directions(const float* heights, uint32_t n);
	};
}
