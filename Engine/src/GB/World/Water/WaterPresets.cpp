#include "GB/World/Water/WaterPresets.h"

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
		return MakeLakeSurface(field.Lake(), look);
	}

	std::vector<std::unique_ptr<WaterSurface>> MakeMoreLakeSurfaces(const WaterField& field, const WaterLook& look)
	{
		std::vector<std::unique_ptr<WaterSurface>> surfaces;
		for (const LakeBody& lake : field.MoreLakes())
			if (auto surface = MakeLakeSurface(lake, look)) surfaces.push_back(std::move(surface));
		return surfaces;
	}

	std::unique_ptr<WaterSurface> MakeLakeSurface(const LakeBody& lake, const WaterLook& look)
	{
		if (!lake.Present) return nullptr;
		if (!lake.Mask.Empty())
		{
			// a lake filled to its overflow level (lake_fill): its Radius already holds every cell of it, so the disc is that circle and no
			// more (the renderer masks by the disc alone: past the shore, ground below the level inside it would show water; TERRENO_V5 E1)
			auto surface = std::make_unique<WaterSurface>(lake.Radius * 2.1f, look.LakeSubdivisions);
			surface->Position = { lake.Center.x, lake.Level, lake.Center.y };
			surface->Radius = lake.Radius;
			surface->Params = look.Lake;
			return surface;
		}
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
