#pragma once
#include "GB/World/Graph/ITerrainNode.h"

namespace GB
{
	// Valleys along a course: a hand-laid curve (path_curve), a U-shaped trough with a flat floor dug along it (valley_carve), and the distance
	// to any path as a mask (mask_distance_to_path: a riparian strip, a road verge). The course of valley_carve can be any path, a river_trace
	// too; the floor follows the ground along it, smoothed and always running downhill from its first point to its last.

	// "path_curve": a smooth curve through up to six control points (P0..P5, Points of them), bent by a meander of Amplitude metres every
	// Wavelength metres, sampled every Spacing metres. Its values are the distance along it.
	class PathCurveNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "valley_carve": the ground within FloorWidth of the course is set to the floor (the bed, Incision metres below the smoothed ground along
	// the course); past it the walls rise as a power curve (WallGamma > 1: a U, < 1: a V) over WallWidth metres to the ground they cut into,
	// and blend into it over Blend metres. Outputs the carved height, the floor (1 on it, 0 past the walls) and the distance to the course.
	class ValleyCarveNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};

	// "mask_distance_to_path": 1 within Inner metres of any of the paths, fading to 0 at Outer.
	class PathDistanceMaskNode final : public ITerrainNode
	{
	public:
		TgNodeSchema Describe() const override;
		bool Run(const TgNode& node, const TgContext& ctx, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const override;
	};
}
