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

	float WorldInstance::WaterLevelAt(float x, float z) const
	{
		const Terrain* terrain = m_Terrain.get();
		float level = -1e9f;
		if (terrain->HasLake() && glm::distance(glm::vec2(x, z), terrain->GetLakeCenter()) < terrain->GetLakeRadius() * 1.5f) level = terrain->GetWaterLevel();
		if (terrain->GetRiver() && terrain->DistanceToRiver(x, z) < terrain->GetRiver()->HalfWidth * 3.0f) level = std::max(level, terrain->GetRiverSurfaceLevel(x, z));
		return level;
	}

	bool WorldInstance::AddProp(const PropComponent& prop, const glm::mat4& world, Scene& scene) const
	{
		if (!m_World) return false;
		TransformComponent t;
		t.FromMatrix(world);
		const float yaw = glm::radians(t.Rotation.y);
		const float scale = (t.Scale.x + t.Scale.y + t.Scale.z) / 3.0f;   // the factories place uniformly scaled copies
		const size_t variant = static_cast<size_t>(std::max(prop.Variant, 0));
		if (prop.Kind == PropKind::Tree)
		{
			if (variant >= m_TreeVariants.size()) return false;
			m_TreeVariants[variant]->AddToScene(scene, t.Position, yaw, scale, 340.0f);
			return true;
		}
		if (variant >= m_RockVariants.size()) return false;
		m_RockVariants[variant]->AddToScene(scene, t.Position, yaw, scale, 0.12f * scale, WaterLevelAt(t.Position.x, t.Position.z));
		return true;
	}

	bool WorldInstance::PropBounds(const PropComponent& prop, glm::vec3& center, float& radius) const
	{
		const size_t variant = static_cast<size_t>(std::max(prop.Variant, 0));
		if (prop.Kind == PropKind::Tree)
		{
			if (variant >= m_TreeVariants.size()) return false;
			const Tree& tree = *m_TreeVariants[variant];
			center = { 0.0f, tree.Height * 0.5f, 0.0f };
			radius = std::max(tree.Height * 0.5f, tree.CrownRadius);
			return true;
		}
		if (variant >= m_RockVariants.size()) return false;
		center = { 0.0f, m_RockVariants[variant]->Height * 0.5f, 0.0f };
		radius = m_RockVariants[variant]->Radius;
		return true;
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
		for (const RockParams& rp : world->RockRecipes)
			m_RockVariants.push_back(RockFactory::Create(rp));

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
