#pragma once
#include <cstdint>

namespace GB
{
	struct RenderStats
	{
		uint32_t ObjectsDrawn = 0;
		uint32_t ObjectsCulled = 0;
	};
}
