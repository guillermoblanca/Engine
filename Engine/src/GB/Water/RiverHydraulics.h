#pragma once

namespace GB
{
	// The physics a river's size and speed follow, as small pure functions. A river is described by its relative discharge q (0..1: the
	// drainage area that reaches a point over the area that reaches the mouth), and everything below follows from it. The numbers are the
	// textbook ones, scaled to the width and depth the generator wants at the mouth:
	//
	//  * Hydraulic geometry (Leopold & Maddock, 1953): at a cross-section, width, depth and mean velocity are power laws of the discharge Q,
	//        W = a Q^0.5,   d = c Q^0.4,   v = k Q^0.1      (the exponents sum to 1, as continuity Q = W d v demands).
	//    Discharge itself grows with the drainage area, so with q = Q / Q_mouth a river that is W wide at its mouth is W sqrt(q) wide upstream.
	//  * Manning's equation (1891) for the speed of the water in an open channel:
	//        v = (1 / n) R^(2/3) S^(1/2),     R = A / P  (flow area over wetted perimeter),     S = slope of the water surface,
	//    n being the roughness of the bed (0.03 for a clean natural stream, 0.05 with cobbles and weeds).
	//
	// A river never shrinks to nothing nor runs dry or at a gallop in a game: the widths, depths and speeds are bounded below and above.
	struct HydraulicRules
	{
		float MinWidthFraction = 0.30f;   // the headwaters are at least this fraction of the mouth's width
		float MinDepthFraction = 0.45f;   // ... and of its depth
		float WidthExponent = 0.5f;       // Leopold & Maddock
		float DepthExponent = 0.4f;
		float Roughness = 0.04f;          // Manning's n
		float MinSlope = 0.0006f;         // a flat reach still moves: the slope never counts as less than this
		float MinSpeed = 0.6f;            // m/s
		float MaxSpeed = 4.5f;
	};

	// Width of the river at relative discharge q, as a fraction of the width at the mouth (1 at q = 1).
	float HydraulicWidthFactor(float q, const HydraulicRules& rules = {});
	// Depth of the river at relative discharge q, as a fraction of the depth at the mouth.
	float HydraulicDepthFactor(float q, const HydraulicRules& rules = {});
	// Hydraulic radius of a rectangular channel: flow area over wetted perimeter, R = W d / (W + 2 d).
	float HydraulicRadius(float width, float depth);
	// Manning's mean velocity for the channel above on a surface slope S (m/m), bounded to the rules' speeds.
	float ManningSpeed(float width, float depth, float slope, const HydraulicRules& rules = {});
}
