#pragma once
#include "GB/World/Graph/TgField.h"
#include <glm/glm.hpp>

namespace GB
{
	// Where the lake of a height field goes: the lowest point of the ground, box-blurred on a coarse grid (`coarse` cells per side, a 3x3 mean at
	// each) and leaving `margin` coarse cells clear of the border. The one answer to "where is the basin", shared by the nodes that need it
	// (lake_site marks it, lake_basin digs it), so they cannot disagree.
	struct LakeSiteResult
	{
		glm::vec2 Position{ 0.0f };
		float Height = 0.0f;   // the smoothed height there
	};
	LakeSiteResult FindLakeSite(const TgField& field, int coarse = 65, int margin = 12);
}
