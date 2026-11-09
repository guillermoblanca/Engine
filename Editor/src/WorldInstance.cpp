#include "WorldInstance.h"
#include "GB/Log.h"
#include "GB/World/Water/WaterPresets.h"
#include "GB/World/TerrainNoise.h"
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
		m_Trodden.clear();
		scene.Objects.clear();
		scene.Grass.clear();
		scene.Water.clear();
		scene.Ripples = nullptr;
		scene.Ground = nullptr;
		m_Lake.reset(); m_River.reset(); m_GrassGpu.reset(); m_Grass.reset();
		m_RockVariants.clear(); m_TreeVariants.clear(); m_TreeVariantsFar.clear(); m_TreesGpu.reset();
		m_TerrainGpu.reset();
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
			const bool far = variant < m_TreeVariantsFar.size() && glm::distance(t.Position, m_Viewer) > kTreeLodDistance;
			(far ? m_TreeVariantsFar : m_TreeVariants)[variant]->AddToScene(scene, t.Position, yaw, scale, 340.0f);
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
			const TreeGpu& tree = *m_TreeVariants[variant];
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

	void WorldInstance::AddClearing(const glm::vec2& at, float inner, float outer, float strength) { m_Trodden.push_back({ at, at, inner, std::max(outer, inner + 0.1f), strength }); }
	void WorldInstance::AddPath(const glm::vec2& from, const glm::vec2& to, float width, float strength) { m_Trodden.push_back({ from, to, 0.5f * width, 0.5f * width + 1.0f, strength }); }

	float WorldInstance::TroddenAt(float x, float z) const
	{
		float trodden = TrailTrodden ? std::clamp(TrailTrodden(x, z), 0.0f, 1.0f) : 0.0f;   // T5c: the clans' trails
		const glm::vec2 p(x, z);
		for (const Trodden& t : m_Trodden)
		{
			const glm::vec2 ab = t.B - t.A;
			const float along = glm::dot(ab, ab) > 1e-6f ? glm::clamp(glm::dot(p - t.A, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
			const float d = glm::distance(p, t.A + along * ab);
			if (d < t.Outer) trodden = std::max(trodden, t.Strength * (1.0f - glm::smoothstep(t.Inner, t.Outer, d)));
		}
		return trodden;
	}

	void WorldInstance::FollowCamera(const glm::vec3& position)
	{
		m_Viewer = position;
		if (m_Grass) m_Grass->Recenter({ position.x, position.z });
	}

	void WorldInstance::Build(GeneratedWorld&& generated, Scene& scene)
	{
		Clear(scene);
		m_World = std::make_unique<GeneratedWorld>(std::move(generated));
		++m_BuildCount;
		m_Terrain = std::move(m_World->TerrainPtr);
		m_TerrainGpu = std::make_unique<TerrainGpu>(*m_Terrain);   // uploads what the generation prepared (or prepares it here first)
		m_TerrainGpu->AddToScene(scene);
		const Terrain* terrain = m_Terrain.get();
		const GeneratedWorld* world = m_World.get();
		const WorldDesc& desc = world->Desc;

		if (desc.Type == WorldType::Temperate)
		{
			GrassParams gp;
			gp.Seed = desc.Seed;
			// the field follows the camera (FollowCamera): a square a little wider than what is drawn, farther in an open world
			gp.DrawDistance = desc.Extent() > 1000.0f ? 60.0f : gp.DrawDistance;
			gp.Size = std::min(std::max(190.0f, 2.0f * (gp.DrawDistance + gp.ChunkSize)), std::max(desc.Extent() * 0.5f, 2.0f * (gp.DrawDistance + gp.ChunkSize)));
			gp.Density = 30.0f;
			if (desc.Extent() > 1000.0f)   // an open world: a thick sward around the camera, a deeper green
			{
				gp.Density = 55.0f; gp.Width = 0.055f; gp.Size = 2.0f * (gp.DrawDistance + gp.ChunkSize);
				gp.TipColor = { 0.29f, 0.46f, 0.11f }; gp.DryColor = { 0.52f, 0.47f, 0.22f };
			}
			gp.HeightFn = terrain->MakeGroundFunction();
			const Heightmap* hm = &terrain->GetHeightmap();
			gp.NormalFn = [hm](float x, float z) { return hm->GetNormal(x, z); };
			gp.DensityFn = [this, world](float x, float z) { return world->GrassDensityAt(x, z) * (1.0f - TroddenAt(x, z)); };
			// In an open world, its look by place: drier and warmer (toward olive and straw) where the ground is dry, in patches of tens of
			// metres; tall clumps and short turf in smaller patches, taller where it is wet. (A small world keeps the plain look of before.)
			const uint32_t seed = desc.Seed;
			if (desc.Extent() > 1000.0f) gp.TintFn = [world, seed](float x, float z)
			{
				const float wet = world->MoistureAt(x, z);
				const float patch = TerrainNoise::Fbm(x / 26.0f, z / 26.0f, 3, seed * 31u + 501u);
				const float dry = glm::clamp((0.5f - wet) * 1.4f + (patch - 0.5f) * 1.1f, 0.0f, 1.0f);
				const glm::vec3 tint = glm::mix(glm::vec3(0.85f, 0.97f, 0.83f), glm::vec3(1.02f, 0.98f, 0.76f), glm::clamp(patch * 1.3f - 0.15f, 0.0f, 1.0f));
				return glm::vec4(tint, dry * 0.4f);
			};
			if (desc.Extent() > 1000.0f) gp.HeightScaleFn = [this, world, seed](float x, float z)
			{
				const float clump = TerrainNoise::Fbm(x / 7.0f, z / 7.0f, 3, seed * 31u + 503u);
				return (0.55f + 0.6f * glm::smoothstep(0.35f, 0.75f, clump)) * (0.85f + 0.3f * world->MoistureAt(x, z)) * (1.0f - 0.6f * TroddenAt(x, z));
			};
			m_Grass = std::make_unique<GrassField>(gp);
			m_GrassGpu = std::make_unique<GrassGpu>(*m_Grass);   // uploads when first drawn, and again after every Recenter or Regrow
			scene.Grass.push_back(m_GrassGpu.get());
		}

		m_TreesGpu = std::make_unique<TreesGpu>();
		for (const TreeParams& tp : world->TreeRecipes)
		{
			m_TreeVariants.push_back(m_TreesGpu->Create(tp));
			TreeParams farParams = tp;   // the level of detail of the far trees: the same silhouette, half the leaf cards
			farParams.Detail = 0;
			m_TreeVariantsFar.push_back(m_TreesGpu->Create(farParams));
			GB_CLIENT_INFO("tree variant {}: {} triangles, {} far", m_TreeVariants.size() - 1, m_TreeVariants.back()->TriangleCount, m_TreeVariantsFar.back()->TriangleCount);
		}
		for (const RockParams& rp : world->RockRecipes)
			m_RockVariants.push_back(RocksGpu::Create(rp));

		// The water is drawn from the terrain's own WaterField: the surfaces and how they look come from GB/World/Water.
		m_Lake = MakeLakeSurface(terrain->Water());
		if (m_Lake) scene.Water.push_back(m_Lake.get());
		m_River = MakeRiverSurface(terrain->Water());
		if (m_River) scene.Water.push_back(m_River.get());
		scene.Env.ShadowRadius = 70.0f;
		GB_CLIENT_INFO("World built: {} trees, {} rocks, {} grass blades{}{}", world->Trees.size(), world->Rocks.size(), GrassBlades(),
			terrain->HasLake() ? ", lake" : "", terrain->GetRiver() ? ", river" : "");
	}
}
