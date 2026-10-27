#include "WorldInstance.h"
#include "GB/Log.h"
#include <algorithm>

namespace GB::Editor
{
	void WorldInstance::Clear(Scene& scene)
	{
		scene.Objects.clear();
		scene.Grass.clear();
		scene.Water.clear();
		scene.Ripples = nullptr;
		scene.Ground = nullptr;
		m_Lake.reset(); m_River.reset(); m_Grass.reset();
		m_RockVariants.clear(); m_TreeVariants.clear(); m_TreeFactory.reset();
		m_Terrain.reset();
		m_World.reset();
	}

	void WorldInstance::Build(const WorldDesc& desc, Scene& scene)
	{
		Clear(scene);
		m_World = std::make_unique<GeneratedWorld>(WorldGenerator::Generate(desc));
		m_Terrain = std::move(m_World->TerrainPtr);
		m_Terrain->AddToScene(scene);
		const Terrain* terrain = m_Terrain.get();
		const GeneratedWorld* world = m_World.get();

		if (desc.Type == WorldType::Temperate)
		{
			GrassParams gp;
			gp.Seed = desc.Seed;
			gp.Size = std::min(190.0f, desc.Size * 0.5f);
			gp.Density = 30.0f;
			gp.HeightFn = terrain->MakeGroundFunction();
			const Heightmap* hm = &terrain->GetHeightmap();
			gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
			gp.DensityFn = [world](float x, float z) { return world->GrassDensityAt(x, z); };
			m_Grass = std::make_unique<GrassField>(gp);
			scene.Grass.push_back(m_Grass.get());
		}

		m_TreeFactory = std::make_unique<TreeFactory>();
		for (const TreeParams& tp : world->TreeRecipes)
			m_TreeVariants.push_back(m_TreeFactory->Create(tp));
		for (const TreePlacement& pl : world->Trees)
			m_TreeVariants[static_cast<size_t>(pl.Variant)]->AddToScene(scene, pl.Position, pl.Yaw, pl.Scale, 340.0f);

		auto waterNear = [terrain](float x, float z)
		{
			float level = -1e9f;
			if (terrain->HasLake() && glm::distance(glm::vec2(x, z), terrain->GetLakeCenter()) < terrain->GetLakeRadius() * 1.5f) level = terrain->GetWaterLevel();
			if (terrain->GetRiver() && terrain->DistanceToRiver(x, z) < terrain->GetRiver()->HalfWidth * 3.0f) level = std::max(level, terrain->GetRiverSurfaceLevel(x, z));
			return level;
		};
		for (const RockParams& rp : world->RockRecipes)
			m_RockVariants.push_back(RockFactory::Create(rp));
		for (const RockPlacement& rk : world->Rocks)
			m_RockVariants[static_cast<size_t>(rk.Variant)]->AddToScene(scene, rk.Position, rk.Yaw, rk.Scale, 0.12f * rk.Scale, waterNear(rk.Position.x, rk.Position.z));

		if (terrain->HasLake())
		{
			m_Lake = std::make_unique<WaterSurface>(terrain->GetLakeRadius() * 2.6f, 160);
			const glm::vec2 c = terrain->GetLakeCenter();
			m_Lake->Position = { c.x, terrain->GetWaterLevel(), c.y };
			m_Lake->Radius = terrain->GetLakeRadius() * 1.2f;
			m_Lake->Params.WaveHeight = 0.05f;
			scene.Water.push_back(m_Lake.get());
		}
		if (terrain->GetRiver())
		{
			m_River = std::make_unique<WaterSurface>(*terrain->GetRiver());
			m_River->Params.WaveHeight = 0.015f;
			m_River->Params.Choppiness = 1.4f;
			m_River->Params.Clarity = 1.6f;
			m_River->Params.Foam = 0.4f;
			scene.Water.push_back(m_River.get());
		}
		scene.Env.ShadowRadius = 70.0f;
		GB_CLIENT_INFO("World built: {} trees, {} rocks, {} grass blades{}{}", world->Trees.size(), world->Rocks.size(), GrassBlades(),
			terrain->HasLake() ? ", lake" : "", terrain->GetRiver() ? ", river" : "");
	}
}
