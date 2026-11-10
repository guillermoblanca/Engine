#pragma once
#include "GB/Game/Entity.h"
#include "GB/Renderer/GrassGpu.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/RocksGpu.h"
#include "GB/Renderer/TerrainGpu.h"
#include "GB/Renderer/TreesGpu.h"
#include "GB/World/Grass.h"
#include "GB/World/Rocks.h"
#include "GB/World/Terrain.h"
#include "GB/World/Trees.h"
#include "GB/World/Water.h"
#include "GB/World/WorldGen.h"
#include <functional>
#include <memory>
#include <vector>

namespace GB::Editor
{
	// A generated world turned into renderable things: terrain, grass, lake and river water, plus the tree and rock
	// recipes. The individual trees and rocks are not drawn from here: the editor makes an entity for each
	// (see SceneEditor::PopulateEnvironment), so they can be selected, moved and deleted, and draws them through AddProp.
	// It owns every GPU resource the scene refers to, so reset it only after the scene stopped using them.
	class WorldInstance
	{
	public:
		void Build(const WorldDesc& desc, Scene& scene);
		void Build(GeneratedWorld&& world, Scene& scene);
		void Clear(Scene& scene);
		bool IsBuilt() const { return m_World != nullptr; }
		// How many worlds this instance has built: what reads the world (a site chooser holding its terrain) is renewed when it changes, also when
		// the new world is the same seed again at the same address.
		uint64_t BuildCount() const { return m_BuildCount; }

		const GeneratedWorld& World() const { return *m_World; }
		Terrain& GetTerrain() { return *m_Terrain; }
		TerrainGpu& GetTerrainGpu() { return *m_TerrainGpu; }   // its mesh, textures and material (the macro map the trails tint)
		WaterSurface* Lake() { return m_Lake.get(); }
		WaterSurface* River() { return m_River.get(); }
		size_t TreeCount() const { return m_World ? m_World->Trees.size() : 0; }
		size_t RockCount() const { return m_World ? m_World->Rocks.size() : 0; }
		size_t GrassBlades() const { return m_Grass ? m_Grass->GetBladeCount() : 0; }
		// Draws one tree or rock entity (the EntityRenderer's prop builder). False when the variant does not exist.
		bool AddProp(const PropComponent& prop, const glm::mat4& world, Scene& scene) const;
		// Local bounding sphere of a prop, for picking.
		bool PropBounds(const PropComponent& prop, glm::vec3& center, float& radius) const;
		std::function<float(float, float)> GroundFunction() const { return m_Terrain ? m_Terrain->MakeGroundFunction(true) : nullptr; }
		// Keeps the grass field around the camera (a world can be much bigger than the field). Call once a frame before drawing.
		void FollowCamera(const glm::vec3& position);
		// Ground trodden by people, where the grass grows short and thin or not at all: a clearing (full `strength` within `inner` metres, none past
		// `outer`) or a path between two points `width` wide. Call RegrowGrass once they are all added.
		void AddClearing(const glm::vec2& at, float inner, float outer, float strength);
		void AddPath(const glm::vec2& from, const glm::vec2& to, float width, float strength);
		void RegrowGrass() { if (m_Grass) m_Grass->Regrow(); }
		float TroddenAt(float x, float z) const;   // 0 = untouched, 1 = bare
		// T5c (TrailsView.cpp): the clans' trails, 0..1 at a place: how much grass they take away (TroddenAt) and how much of the undergrowth's cost
		// they take away for the navigation (PlayerMode::SyncNavigation reads TrailEase). Unset: none.
		std::function<float(float, float)> TrailTrodden, TrailEase;
		bool RegrowGrassAreas(const std::vector<glm::vec4>& rects) { return m_Grass && m_Grass->RegrowAreas(rects); }

	private:
		float WaterLevelAt(float x, float z) const;

		std::unique_ptr<GeneratedWorld> m_World;
		std::unique_ptr<Terrain> m_Terrain;
		std::unique_ptr<TerrainGpu> m_TerrainGpu;   // after m_Terrain: destroyed before it
		std::unique_ptr<TreesGpu> m_TreesGpu;   // the bark and leaf textures every variant below points at: destroyed after them
		std::vector<std::unique_ptr<TreeGpu>> m_TreeVariants;
		std::vector<std::unique_ptr<TreeGpu>> m_TreeVariantsFar;   // the same trees with half the foliage (TreeParams::Detail 0), drawn past kTreeLodDistance
		static constexpr float kTreeLodDistance = 70.0f;
		glm::vec3 m_Viewer{ 0.0f };   // the camera, for the trees' level of detail (FollowCamera)
		std::vector<std::unique_ptr<RockGpu>> m_RockVariants;
		std::unique_ptr<GrassField> m_Grass;
		std::unique_ptr<GrassGpu> m_GrassGpu;   // after m_Grass: destroyed before it
		std::unique_ptr<WaterSurface> m_Lake, m_River;
		std::vector<std::unique_ptr<WaterSurface>> m_MoreLakes;   // WaterField::MoreLakes(), drawn like the lake
		uint64_t m_BuildCount = 0;
		struct Trodden { glm::vec2 A, B; float Inner, Outer, Strength; };   // A == B: a clearing; else a path (Inner: half its width)
		std::vector<Trodden> m_Trodden;
	};
}
