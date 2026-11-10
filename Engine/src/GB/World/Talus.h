#pragma once
#include <cstdint>
#include <vector>

namespace GB
{
	// Talus with hardness (TERRENO_V5 E4): cliffs where the rock is hard, scree at their foot, and nothing touched below the angle of repose.
	// The incision of E2 leaves walls far steeper than real ground; a diffusion (E2b) wears them back but smooths every scale, and the gullies
	// go with the walls. This one only cuts what stands above the angle of repose of its rock, and leaves the rest as it was, crests and
	// channel floors included.
	//
	// 1. The angle of each cell: tan(theta) = mix(tan(SoftAngle), tan(HardAngle), hardness) -- hard rock stands as a cliff, soft rock as a
	//    slope.
	// 2. The cut (exact, not a relaxation): the highest ground h' <= h on which no cell stands more than tan(theta of that cell) * L above any
	//    neighbour at the distance L. It is a shortest path from the low ground up, solved with Dijkstra (ties to the lower index) over 16
	//    neighbours -- the 8 around and the 8 a knight's move away, so a slope facing any direction is held within 3 % of its angle (8 alone
	//    let a slope at 22.5 degrees stand 8 % steeper). The fixed point is unique: the visiting order does not change it, and it is the same on
	//    every run. A cell that goes down stays strictly above the neighbour that bounds it, so the cut makes no hollow, and a cell at or under
	//    its angle is never touched.
	// 3. The fall: what the cut takes goes down the steepest descent of h' (D8), cell after cell from the highest, until it reaches a cell
	//    where the ground eases under the scree's angle (the foot), or is carried away: by a channel (drainage area over ChannelArea, m2, the
	//    same at every resolution), a lake, the border of the map or the edge of a flat.
	// 4. The scree: at each foot, a cone of the scree's angle around an apex above the foot, as high as its volume needs (a half cone against
	//    the wall it fell from); the cones of a cliff's foot merge into one apron leaning on it. It never covers a channel, a lake or the
	//    border, and it is trimmed back to the scree's angle wherever it would stand steeper over them (what a river carries away from the
	//    toe). One factor over every apex is adjusted (a few passes) so the scree laid down matches the volume that reached the feet; the rest
	//    of the misfit, like what the channels, lakes and border took, is counted as carried away.
	// Lakes -- the cells of a hollow (the Priority-Flood level of the input above its ground) and the cells marked fixed -- do not move; for
	// the cut and the trim they stand at their water level, so the shore is held at its angle over the water, not over the lake's floor.
	// CPU only, sequential, no threads: the same bytes every run.
	struct TalusOptions
	{
		float SoftAngle = 36.0f;          // degrees, the angle soft rock stands at (hardness 0)
		float HardAngle = 70.0f;          // degrees, at hardness 1
		float ScreeAngle = 33.0f;         // degrees, the angle the fallen material rests at
		const float* Hardness = nullptr;  // N*N, 0 soft .. 1 hard (null = 0)
		float ChannelArea = 2000.0f;      // m2: a cell draining more than this is a channel and carries what reaches it away; 0 = no channels
		bool Deposit = true;              // false: what the cut takes is all carried away (no scree)
		bool BorderOutlets = true;        // the map's border carries away what reaches it
		const uint8_t* Fixed = nullptr;   // N*N, non-zero = never moves (water carved before), like a lake
	};

	struct TalusStats
	{
		size_t CutCells = 0, ScreeCells = 0, LakeCells = 0, Feet = 0, ChannelCells = 0;
		double CutVolume = 0.0;           // m3 the cut took
		double FootVolume = 0.0;          // m3 that reached a foot
		double ChannelVolume = 0.0, LakeVolume = 0.0, BorderVolume = 0.0;   // m3 carried away before a foot
		double ScreeVolume = 0.0;         // m3 laid down as scree
		double ApexFactor = 1.0;          // the factor over the cones' heights the fit ended with
		int FitPasses = 0;
		double FloodMs = 0.0, CutMs = 0.0, FallMs = 0.0, ScreeMs = 0.0;
		// CutVolume = ScreeVolume + ChannelVolume + LakeVolume + BorderVolume + (FootVolume - ScreeVolume)
	};

	struct TalusResult
	{
		std::vector<float> Heights;   // N*N
		std::vector<float> Scree;     // N*N, metres of scree laid down
		std::vector<float> Cut;       // N*N, metres the cut took
		std::vector<uint8_t> Lake;    // N*N, 1 = a lake cell (did not move)
		TalusStats Stats;
	};

	TalusResult TalusErode(const float* heights, uint32_t n, float spacing, const TalusOptions& options);

	// The hardness a talus node makes when none is given: a noise of patches of hard rock (`cover` of the map, the quantile of the noise, so
	// the fraction is exact) and, with strataSpacing > 0, horizontal beds `strataSpacing` metres apart of which `strataHard` of each period is
	// hard, waving `strataWarp` metres up and down with the same noise. 0..1, with a soft edge of `edge` (in noise units / of a period).
	struct TalusHardnessParams
	{
		float Cover = 0.1f;          // 0..1, fraction of the map the patches cover
		float NoiseScale = 250.0f;   // metres, the size of the patches
		uint32_t Seed = 0;
		float Edge = 0.08f;          // the soft edge of the patches (fraction of the noise's range) and of the beds (fraction of a period)
		float StrataSpacing = 0.0f;  // metres between beds; 0 = no beds
		float StrataHard = 0.35f;    // fraction of each period that is hard
		float StrataWarp = 0.0f;     // metres the beds wave
		float StrataCover = 1.0f;    // the beds only where the noise is above its (1 - this) quantile (1 = everywhere)
		float Steep = 0.0f;          // 0..1: how much the patches and the districts of beds follow the steep ground of the input rather than
		                             // the noise (the hard rock is what stood when the rivers cut: the steepest of the input, smoothed
		                             // over a few metres). The cover fractions stay exact.
	};

	// `originX`, `originZ`: the world position of sample (0, 0); `spacing` the metres between samples.
	std::vector<float> TalusHardness(const float* heights, uint32_t n, float spacing, float originX, float originZ, const TalusHardnessParams& params);
}
