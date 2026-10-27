#pragma once
#include "GB/Game/Entity.h"
#include "GB/Renderer/Renderer.h"
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
		void Clear(Scene& scene);
		bool IsBuilt() const { return m_World != nullptr; }

		const GeneratedWorld& World() const { return *m_World; }
		Terrain& GetTerrain() { return *m_Terrain; }
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

	private:
		float WaterLevelAt(float x, float z) const;

		std::unique_ptr<GeneratedWorld> m_World;
		std::unique_ptr<Terrain> m_Terrain;
		std::unique_ptr<TreeFactory> m_TreeFactory;
		std::vector<std::unique_ptr<Tree>> m_TreeVariants;
		std::vector<std::unique_ptr<Rock>> m_RockVariants;
		std::unique_ptr<GrassField> m_Grass;
		std::unique_ptr<WaterSurface> m_Lake, m_River;
	};
}
