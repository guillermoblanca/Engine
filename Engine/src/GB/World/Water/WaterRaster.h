#pragma once
#include "GB/World/Water/WaterField.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GB
{
	// The water of a WaterField over a square grid of samples, cell by cell: which samples are lake, which are river and at what level the
	// water stands there. One definition of "wet" for whoever needs it as a raster: the drainage node takes the wet samples as outlets
	// (TERRENO_V5 E1) and the terrain probe counts them as water, so a cell the one calls water the other never calls a pit.
	//
	//   lake  : a sample a lake covers (LakeBody::Covers: inside its mask, or inside the outline of its radius), the main lake first;
	//   river : otherwise, a sample within 1.5 half-widths of the river axis (the ribbon WaterSurface draws, widthScale 1.5), measured to the
	//           first strictly nearest segment among those whose box, grown by 3.2 of the widest half-widths, holds the sample.
	struct WaterRaster
	{
		enum Kind : uint8_t { Dry = 0, Lake = 1, River = 2 };
		uint32_t N = 0;
		std::vector<uint8_t> Kinds;       // N*N, row major (index = j*N + i)
		std::vector<float> Level;         // the water level of a wet sample; kNoWaterLevel on dry ones
		// The river's nearest point of each sample within that reach (1e9 / -1e9 elsewhere): distance, distance in half-widths, level.
		std::vector<float> RiverDistance, RiverRelative, RiverLevel;

		bool Wet(size_t index) const { return Kinds[index] != Dry; }
	};

	// `min` is the world x,z of sample (0, 0); sample (i, j) is at min + (i, j) * spacing.
	WaterRaster RasterizeWater(const WaterField& water, uint32_t n, const glm::vec2& min, float spacing);
}
