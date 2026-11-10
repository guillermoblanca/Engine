#pragma once
#include <cstdint>
#include <vector>

namespace GB
{
	// The drainage of a height field, in one pass (TERRENO_V5 E1): where the water of every cell goes, in what order the cells can be walked so
	// that each one comes after the cell it drains to, how much area drains through each cell, and the level water stands at where it pools.
	// The one drainage the later phases build on: the stream-power erosion (E2) solves along Order and Receiver, the deposit (E3) and the river
	// network (E5) read Area, the lakes (lake_fill) read Level.
	//
	// The algorithm is Priority-Flood with a pit queue (Barnes, Lehman & Mulla, 2014, algorithms 2 and 3): the outlets (the border of the map
	// and the cells marked as outlets: the water already there, a lake, a river) are seeded; the lowest cell waiting is taken from a priority
	// queue keyed on the water level; each neighbour not yet reached gets the level max(its height, that level) and the taken cell as its parent.
	// A neighbour that does not rise above the level (a pit or a flat) goes to a FIFO queue that is emptied first, so a filled hollow is flooded
	// breadth first from its spill point and its cells point back toward it, not along the rows. Ties in the priority queue go to the lower
	// index. Every cell is taken once: O(N log N) for the slopes, O(N) for the hollows. Sequential, and the same result every time.
	//
	//   Level  : the exact fill, flat in every hollow at the height of its spill point (no epsilon): the surface of a lake filled to overflowing.
	//   Filled : the fill with an epsilon: along the parents it rises at least Epsilon per cell, so every cell that is not an outlet has a
	//            neighbour strictly lower. Inside a hollow it is Level plus a slope of Epsilon per cell toward the spill point; downstream of a
	//            large hollow it can stand that many epsilons above the ground (lake_fill builds its own ground, with the lakes left out).
	//   Order  : the cells in the order they were taken (the outlets first). Every cell comes after its parent and after its Receiver.
	//   Receiver: the neighbour the cell drains to, -1 for an outlet. With ReceiverMode 1 (the default) the steepest descent of Filled among
	//            the neighbours taken before it (diagonals at sqrt 2); with 0, the parent it was flooded from.
	//   Area   : m2 that drain through the cell, its own included, weighted by Precipitation when given (accumulated along Order, backwards).
	struct DrainageOptions
	{
		float Epsilon = 0.001f;                   // metres per cell
		bool BorderOutlets = true;                // the map's border is an outlet
		const uint8_t* Outlets = nullptr;         // N*N, non-zero = an outlet; null = only the border
		const float* Precipitation = nullptr;     // N*N, the rain of each cell relative to 1; null = 1 everywhere
		int ReceiverMode = 1;                     // 1 steepest descent among the earlier cells, 0 the flooding parent
	};

	struct DrainageResult
	{
		uint32_t N = 0;
		float Spacing = 1.0f;
		std::vector<float> Level, Filled, Area;   // N*N, row major (index = j*N + i)
		std::vector<int32_t> Receiver;            // N*N
		std::vector<uint32_t> Order;              // N*N cell indices
	};

	// `heights` is N*N, row major; `spacing` the metres between samples. N < 2 gives an empty result.
	DrainageResult ComputeDrainage(const float* heights, uint32_t n, float spacing, const DrainageOptions& options = {});

	// Area again (m2, weighted by `precipitation`, null = 1) for the Receiver and Order a result holds: what E2 calls after each step.
	void AccumulateDrainageArea(DrainageResult& drainage, const float* precipitation);
}
