#include "GB/Water/WaterPresets.h"

namespace GB
{
	WaterGenParams DefaultWaterGen(bool alpine, bool organic)
	{
		WaterGenParams p;
		p.LakeRadius = alpine ? 26.0f : 22.0f;
		p.RiverWidth = alpine ? 5.0f : 6.5f;
		if (organic) p.LakeIrregularity = 0.2f;
		return p;
	}

	WaterLook DefaultWaterLook()
	{
		WaterLook look;
		look.Lake.WaveHeight = 0.05f;
		look.River.WaveHeight = 0.015f;
		look.River.Choppiness = 1.4f;
		look.River.Clarity = 1.6f;
		look.River.Foam = 0.4f;
		return look;
	}

	std::unique_ptr<WaterSurface> MakeLakeSurface(const WaterField& field, const WaterLook& look)
	{
		if (!field.HasLake()) return nullptr;
		const LakeBody& lake = field.Lake();
		// a lobed shore reaches out to (1 + irregularity) radii: the grid and the disc that bounds the water cover it (a circle: exactly as before)
		const float reach = lake.Radius * (1.0f + lake.Outline.Irregularity);
		auto surface = std::make_unique<WaterSurface>(reach * look.LakeSurfaceScale, look.LakeSubdivisions);
		surface->Position = { lake.Center.x, lake.Level, lake.Center.y };
		surface->Radius = reach * look.LakeMaskScale;
		surface->Params = look.Lake;
		return surface;
	}

	std::unique_ptr<WaterSurface> MakeRiverSurface(const WaterField& field, const WaterLook& look)
	{
		const RiverData* river = field.River();
		if (!river) return nullptr;
		auto surface = std::make_unique<WaterSurface>(*river);
		surface->Params = look.River;
		return surface;
	}
}
