#pragma once
#include <cstdint>
#include <vector>

namespace GB
{
	// Deposit (TERRENO_V5 E3): what the rivers leave behind -- the floor of the valleys raised by their fill, and fans where a channel leaves
	// the mountain. It runs after the incision (E2, GB/World/StreamPower.h) and the talus (E4, GB/World/Talus.h).
	//
	// Why a fill and not an easing of the slopes: E2 cuts its gullies as deep as the stream power goes and leaves nothing in them, and the
	// talus holds their walls at the angle of repose, so half of the sierra stands at 30-40 degrees (a real badland, about a fifth). Measured
	// on the valley (TERRENO_V5 sec. 17), 94 % of that ground lies on hillslopes steeper than 30 degrees on average from their crest to the
	// channel they drain to: with the crest and the channel where they are, no reshaping takes it under 30 degrees. Either the crests go down
	// (a cut: the optional easing below) or the floors come up (the fill). The fill keeps the crests, the plan of every channel and the
	// network, and is what a valley that keeps some of its sediment looks like.
	//
	// 1. The easing (optional, off by default): a second talus (TalusErode) whose angle eases from the angle of repose (short slopes, crests,
	//    channels, hard rock) to EaseAngle on long slopes (the area draining through a cell per metre of contour, from EaseStart to EaseFull
	//    metres). What it cuts is carried away.
	// 2. The fill: the closing of the ground by a flat octagon of FillRadius metres (the level each valley narrower than about twice that
	//    would have if it were filled to its sides; never above the crests around it), and every cell raised Fill of the way up to it,
	//    less on hard rock. A V-shaped gully keeps its shape, shallower by that fraction over its lowest R * tan(wall) metres.
	// 3. The fans: a channel draining FanArea m2 or more that leaves a reach steeper than FanApexAngle (over FanReach metres upstream) for one
	//    gentler than FanAngle (over FanReach downstream) is the apex of a fan: a cone of FanAngle over the ground around it, as high as
	//    FanDepth metres of the basin above it need (a few passes fit one factor over every apex), merged with the cones next to it.
	// Hollows (the Priority-Flood level of the input above its ground: the lakes E1 makes later), the cells beside them and the cells marked
	// Fixed (water carved before) do not move, so no lake changes its level or shore. Last, along the drainage of the input every cell stays
	// strictly over the cell it drains to (a receiver is lowered under its donor, never under its ground), so nothing is dammed and no hollow
	// is made. CPU only, sequential, no pow, exp or log: the same bytes every run.
	struct DepositOptions
	{
		const float* Hardness = nullptr;  // N*N, 0 soft .. 1 hard (null = 0): hard rock is not filled nor eased
		const uint8_t* Fixed = nullptr;   // N*N, non-zero = never moves (water carved before)
		// the fill
		float Fill = 0.3f;                // 0..0.95, the fraction of the way up to the closing; 0 = no fill (0.3: 30-40 deg 49 -> 30 %, 0.5 -> 24 % at more gully depth)
		float FillRadius = 50.0f;         // metres
		// the fans
		float FanArea = 20000.0f;         // m2 a channel drains to make a fan; 0 = no fans
		float FanAngle = 5.0f;            // degrees, the slope of a fan
		float FanApexAngle = 8.0f;        // degrees, the slope of the channel above the apex at least
		float FanReach = 60.0f;           // metres, the reach the slopes above and below a cell are measured over
		float FanDepth = 1.0f;            // metres of the basin above the apex the fan holds (its volume = FanDepth * basin area)
		// the easing (a cut)
		float EaseAngle = 0.0f;           // degrees the longest slopes ease to; 0 = no easing
		float ReposeAngle = 36.0f;        // degrees, the angle of repose of the talus before (its SoftAngle)
		float HardAngle = 55.0f;          // degrees, its HardAngle
		float EaseStart = 15.0f;          // metres of slope (m2 per m of contour) the easing begins at
		float EaseFull = 60.0f;           // metres where it is full
		float ChannelArea = 2000.0f;      // m2: a channel is not eased
	};

	struct DepositStats
	{
		double EaseVolume = 0.0;          // m3 the easing cut (carried away)
		double FillVolume = 0.0;          // m3 the fill laid down
		double FanVolume = 0.0;           // m3 the fans laid down
		double FanTarget = 0.0;           // m3 the apexes asked for
		double DrainVolume = 0.0;         // m3 taken off again to keep the drainage
		size_t Apexes = 0, FanCells = 0, FillCells = 0, HeldCells = 0;
		double ApexFactor = 1.0;
		int FitPasses = 0;
		double DrainageMs = 0.0, EaseMs = 0.0, FillMs = 0.0, FanMs = 0.0, KeepMs = 0.0;
	};

	struct DepositResult
	{
		std::vector<float> Heights;   // N*N
		std::vector<float> Alluvium;  // N*N, metres laid down (fill and fans)
		std::vector<float> Fan;       // N*N, metres of fan
		std::vector<float> Eased;     // N*N, metres the easing cut
		DepositStats Stats;
	};

	DepositResult DepositSediment(const float* heights, uint32_t n, float spacing, const DepositOptions& options);
}
