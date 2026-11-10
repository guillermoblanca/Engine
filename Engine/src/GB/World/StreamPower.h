#pragma once
#include "GB/World/Drainage.h"
#include <cstdint>
#include <vector>

namespace GB
{
	// Fluvial incision by the stream-power law, solved implicitly (TERRENO_V5 E2; Braun & Willett, 2013), with uplift and hillslope diffusion.
	// The ground changes as
	//     dh/dt = U - K * sqrt(A) * S + Kd * laplacian(h)
	// with U the uplift (m/yr), K the erodibility (1/yr; lower in hard rock), A the drainage area (m2, weighted by the rain), S the slope to the
	// cell the water goes to, and Kd the diffusivity (m2/yr). The exponents are fixed at m = 0.5 and n = 1: with n = 1 the implicit step is linear
	// in each cell, and m = 0.5 is a square root, exact in IEEE (no pow: the same result on every platform). Metric units throughout, so K, U, Kd
	// and the time step mean the same at any cell size.
	//
	// One iteration: the drainage of the ground (GB/World/Drainage.h: receivers, order, the level of every hollow; the area by multiple flow
	// unless MultipleFlow is off, AccumulateMultipleFlowArea below), then the incision, one
	// cell at a time along Order -- each cell after its receiver, whose new height is already known --
	//     h'[c] = (h[c] + U dt + F * base) / (1 + F),   F = K dt sqrt(A) / L,   L the distance to the receiver (1 or sqrt 2 cells)
	// and last the diffusion (explicit, a 4-neighbour Laplacian, in as many sub-steps as keep Kd dt' / dx2 <= 0.2, each from a copy: the result
	// does not depend on the order the cells are visited in; non-linear near a critical slope when CriticalSlope is set). The incision depends
	// on the order and is sequential; everything else is per cell.
	//
	// Base level: the map's border and the outlet cells are not uplifted nor incised; a border cell only goes down, to its inner neighbour's
	// height when that one ends lower (so the edge never stands as a wall over land that drained elsewhere). A cell in a hollow (its drainage Level above its
	// ground: a lake) is not incised, it only rises with the uplift; the base its donors erode toward is the lake's surface, the new height of
	// the spill point it drains to, not the lake's floor. A cell no higher than that base after the uplift is not incised either (no slope
	// toward the receiver), so the incision never digs a hollow.
	struct StreamPowerOptions
	{
		float Erodibility = 1e-5f;                // K, 1/yr
		float Uplift = 0.0f;                      // U, m/yr
		const float* UpliftMap = nullptr;         // N*N, multiplies Uplift (null = 1 everywhere)
		const float* Hardness = nullptr;          // N*N, 0 soft .. 1 hard (null = 0)
		float HardErodibility = 0.25f;            // K is multiplied by mix(1, this, hardness)
		const float* Precipitation = nullptr;     // N*N, rain relative to 1: weights the area (null = 1)
		bool MultipleFlow = true;                 // the area by multiple flow (AccumulateMultipleFlowArea); false: along the receivers only
		float Diffusion = 0.0f;                   // Kd, m2/yr
		// The hillslope diffusion made non-linear near a critical slope (Roering, Kirchner and Dietrich, 1999; TERRENO_V5 E2b): the flux across
		// each edge between two cells is Kd S / (1 - (S/Sc)^2), S the slope along that edge, so ground near Sc creeps much faster than ground
		// at a gentle slope. A steep wall is worn back toward Sc while a shallow hollow (where the channels start) keeps the low Kd. From
		// S/Sc = CriticalSlopeCap on, the flux follows its tangent there (it keeps growing, linearly): the flux's derivative, what bounds an
		// explicit step, is then at most (1 + cap^2) / (1 - cap^2)^2 times Kd (12.6 with 0.8), and the sub-steps are sized by it. 0 = linear
		// diffusion (what it was).
		float CriticalSlope = 0.0f;               // Sc, the tangent of the critical angle; 0 = off
		float CriticalSlopeCap = 0.8f;            // S/Sc from which the flux follows its tangent (<= 0.95)
		// The channel head (TERRENO_V5 E2b): the fluvial incision only acts where the drainage area passes ChannelArea (m2, so the same at every
		// stage of the cascade); below it the ground is hillslope, shaped by the uplift and the diffusion alone. Without it every cell incises
		// toward its receiver and the walls between gullies are incision fronts nothing wears back. The incision is weighted by a ramp linear in
		// sqrt(A) from sqrt(ChannelArea / ChannelSpread) to sqrt(ChannelArea * ChannelSpread): sqrt(A) is already the law's factor and exact in
		// IEEE (no log: the same on every platform), and over a span of 2 either side it stays within a few percent of a ramp in log A, with no
		// step where the channels start. ChannelSpread <= 1: a hard step. 0 = off (every cell incises, what it was).
		float ChannelArea = 0.0f;                 // A_c, m2; 0 = off
		float ChannelSpread = 2.0f;               // the ramp spans A_c / this .. A_c * this
		float TimeStep = 1000.0f;                 // years per iteration
		int Iterations = 10;
		int DrainageEvery = 1;                    // recompute the drainage every this many iterations (the receivers in between are the last ones)
		bool BorderOutlets = true;                // the map's border is base level
		const uint8_t* Outlets = nullptr;         // N*N, non-zero = base level too (water carved before)
	};

	struct StreamPowerStats
	{
		int Iterations = 0, Drainages = 0, DiffusionSteps = 0;
		double DrainageMs = 0.0, SolveMs = 0.0, DiffusionMs = 0.0;
		double UpliftVolume = 0.0;     // m3 added by the uplift
		double IncisionVolume = 0.0;   // m3 the incision took away (sum of (h + U dt - h') * cell over the incised cells)
		double DiffusionVolume = 0.0;  // m3 the diffusion changed in total (it only moves ground: what leaves is what crosses the border)
		double BorderVolume = 0.0;     // m3 the border went down following the ground inside it
	};

	// Erodes `heights` (N*N, row major, `spacing` metres between samples) in place. `drainage`, when given, receives the drainage of the last
	// iteration (computed before its incision); `stats` the time and volumes.
	void StreamPowerErode(std::vector<float>& heights, uint32_t n, float spacing, const StreamPowerOptions& options,
		StreamPowerStats* stats = nullptr, DrainageResult* drainage = nullptr);

	// The cascade (TERRENO_V5 E2): the incision first on a coarse grid of CoarseN samples over the same extent, then on grids twice as fine up to
	// N, each starting from the coarser result interpolated plus the detail the input has at that resolution and the coarse one lacks:
	//     h_s = up(eroded_{s-1}) + (input_s - up(input_{s-1}))
	// The coarse stage carves the valleys; the finer ones only add what a coarser cell could not hold. When (N-1) is a multiple of (CoarseN-1)
	// (1025 and 513 over 257) the coarse input is the input decimated, sample for sample: the graph evaluated at CoarseN (the editor's preview)
	// runs exactly that coarse stage, so the preview predicts the world. Otherwise (641 over 257) it is resampled bilinearly, which is close but
	// not the same. N <= CoarseN: one stage at N with CoarseIterations. Every stage uses options' time step and fields (resampled);
	// options.Iterations is ignored.
	struct StreamPowerCascade
	{
		uint32_t CoarseN = 257;
		int CoarseIterations = 60;
		int MidIterations = 12;      // each stage between the coarse and the last
		int FineIterations = 4;      // the last stage, at N
		int FineDrainageEvery = 1;   // DrainageEvery of the finer stages (the coarse one recomputes it every iteration)
	};

	struct StreamPowerCascadeResult
	{
		std::vector<float> Heights;       // N*N
		std::vector<float> Coarse;        // the coarse stage's result, CoarseN*CoarseN (N*N when there was a single stage)
		uint32_t CoarseN = 0;
		std::vector<uint32_t> Stages;     // the resolution of each stage
		StreamPowerStats Stats;           // all stages together
		DrainageResult Drainage;          // the last iteration's drainage, at N
	};

	StreamPowerCascadeResult StreamPowerErodeCascade(const float* heights, uint32_t n, float spacing, const StreamPowerOptions& options,
		const StreamPowerCascade& cascade);

	// The drainage area (m2, weighted by `precipitation`) by multiple flow: each cell sends its area to every neighbour lower than it in Filled
	// that comes before it in Order, in proportion to the drop times the contour length (Quinn and others, 1991, with exponent 1: no pow), and
	// to its Receiver when there is none (a flat). The incision still goes toward the single Receiver: only A changes. Along single receivers
	// (D8) every smooth slope gathers its water into straight lines eight directions apart, and the incision digs them (a cone becomes a star);
	// spread over the lower neighbours, water only gathers where the ground converges, so the channels start where the relief makes them.
	void AccumulateMultipleFlowArea(DrainageResult& drainage, const float* precipitation);

	// Bilinear resampling of an N*N grid onto M*M samples over the same extent (corner samples coincide). Exact sample for sample when (N-1) is a
	// multiple of (M-1) or the other way round at the coinciding samples.
	std::vector<float> ResampleGrid(const float* values, uint32_t n, uint32_t m);
}
