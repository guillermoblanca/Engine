#include "WorldInstance.h"
#include "GB/Log.h"
#include "GB/Water/WaterPresets.h"
#include <algorithm>

namespace GB::Editor
{
	namespace
	{
		// How far from the water a rock still looks wet, in lake radii and river half-widths (the band the shader darkens).
		constexpr float kWetLakeReach = 1.5f, kWetRiverReach = 3.0f;
	}

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
		return m_Terrain->Water().LevelNear(x, z, kWetLakeReach, kWetRiverReach);
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
		Build(WorldGenerator::GenerateData(desc), scene);
	}

	void WorldInstance::Build(GeneratedWorld&& generated, Scene& scene)
	{
		Clear(scene);
		WorldGenerator::BuildGpu(generated);
		m_World = std::make_unique<GeneratedWorld>(std::move(generated));
		m_Terrain = std::move(m_World->TerrainPtr);
		m_Terrain->AddToScene(scene);
		const Terrain* terrain = m_Terrain.get();
		const GeneratedWorld* world = m_World.get();
		const WorldDesc& desc = world->Desc;

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

		// The water is drawn from the terrain's own WaterField: the surfaces and how they look come from GB/Water.
		m_Lake = MakeLakeSurface(terrain->Water());
		if (m_Lake) scene.Water.push_back(m_Lake.get());
		m_River = MakeRiverSurface(terrain->Water());
		if (m_River) scene.Water.push_back(m_River.get());
		scene.Env.ShadowRadius = 70.0f;
		GB_CLIENT_INFO("World built: {} trees, {} rocks, {} grass blades{}{}", world->Trees.size(), world->Rocks.size(), GrassBlades(),
			terrain->HasLake() ? ", lake" : "", terrain->GetRiver() ? ", river" : "");
	}
}
