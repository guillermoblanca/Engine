#pragma once
#include "GB/Renderer/Material.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include "GB/Renderer/Texture.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace GB
{
	class Terrain;

	// The GPU side of a GB::Terrain (GB/World/Terrain.h, pure CPU): its mesh, the textures of its material and the material itself, built from what
	// Terrain::PrepareRenderData made (the main thread only uploads), and kept in step with the terrain's edits. The terrain does not know it
	// exists: the render reads the world, never the other way round (docs/ARQUITECTURA_REVISION.md, H5).
	//
	// Keeping in step: Terrain::RefreshMesh rewrites its CPU copies (vertices, normal map), records what changed and bumps a revision; Sync
	// compares the revision and uploads that part. Call Sync after editing the terrain and before drawing it (it is cheap when nothing changed).
	// The macro colour map is uploaded once and never again by Sync: a tool may tint it in place (T5c: trails).
	//
	// T115: a partition terrain (Terrain::SurfaceLayersArePartition: a terrain graph's world) is drawn by its 13 material layers instead of
	// the grain map over the macro colours: the layer weights go into splat arrays, each layer kind has a tiling material
	// (TerrainLayerTextures.h), and the Renderer's GB_TERRAIN_LAYERS program blends them by weight and height (render.md, "Terreno por capas
	// de material"). Every other terrain is drawn exactly as before.
	//
	// Needs an OpenGL context. Owned by whoever draws the terrain (the editor's WorldInstance, the Sandbox, the tests); it must not outlive the
	// Terrain, and the scene objects AddToScene made point at its mesh and textures.
	class TerrainGpu
	{
	public:
		// Prepares the terrain's render data if nobody did (on this thread), takes it and uploads it.
		explicit TerrainGpu(Terrain& terrain);
		TerrainGpu(const TerrainGpu&) = delete;
		TerrainGpu& operator=(const TerrainGpu&) = delete;

		// Uploads the terrain's edits since the last call (Terrain::TakeEdits).
		void Sync();
		// One object per tile of the mesh (culled by its bounds), or a single one for a terrain of one tile or less.
		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

		const Terrain& GetTerrain() const { return *m_Terrain; }
		const Mesh& GetMesh() const { return *m_Mesh; }
		const Material& GetMaterial() const { return m_Material; }
		Material& GetMaterial() { return m_Material; }
		Texture2D* MacroTexture() { return m_Macro.get(); }   // the macro colour map (null without surface maps): a tool may tint it in place (T5c: trails)
		const Texture2D* GetNormalMap() const { return m_NormalMap.get(); }   // high-resolution surface normals (null for none)
		// T115: whether the terrain is drawn by its material layers (a partition terrain with surface maps: Terrain::SurfaceLayersArePartition).
		// The splat weights are uploaded once: an edit of the heights does not move them (Sync keeps the mesh and normals only).
		bool HasLayerMaterials() const { return m_Material.TerrainLayers != nullptr; }
		// T132: the material as the terrain is drawn without its layers (the grain map over the macro colours, as before T115): what the
		// layers' cost is measured against (terrain_layers_test, GB_TERRAIN_BENCH). The same as GetMaterial() on a terrain without layers.
		const Material& GetPlainMaterial() const { return m_PlainMaterial; }

	private:
		void MakeLayerMaterials(const Terrain& terrain);   // T115: the splat arrays and the layers' maps, and m_LayerMaps pointing at them

		Terrain* m_Terrain = nullptr;
		uint64_t m_Revision = 0;   // the terrain's edit revision last uploaded
		std::unique_ptr<Mesh> m_Mesh;
		std::unique_ptr<Texture2D> m_Grain, m_Ripples, m_Macro, m_NormalMap;
		std::unique_ptr<TextureArray> m_Splat, m_LayerAlbedo, m_LayerSurface;   // T115 (partition terrains only)
		TerrainLayerMaps m_LayerMaps;
		Material m_Material;
		Material m_PlainMaterial;   // T132 (GetPlainMaterial)
	};
}
