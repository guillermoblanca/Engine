#pragma once
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
	// A generated world turned into renderable things: terrain, grass, trees, rocks, lake and river water.
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
		std::function<float(float, float)> GroundFunction() const { return m_Terrain ? m_Terrain->MakeGroundFunction(true) : nullptr; }

	private:
		std::unique_ptr<GeneratedWorld> m_World;
		std::unique_ptr<Terrain> m_Terrain;
		std::unique_ptr<TreeFactory> m_TreeFactory;
		std::vector<std::unique_ptr<Tree>> m_TreeVariants;
		std::vector<std::unique_ptr<Rock>> m_RockVariants;
		std::unique_ptr<GrassField> m_Grass;
		std::unique_ptr<WaterSurface> m_Lake, m_River;
	};
}
