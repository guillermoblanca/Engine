#include "GB/Renderer/TreesGpu.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	TreesGpu::TreesGpu()
	{
		const TreeImages& images = TreeFactory::Textures();
		for (size_t t = 0; t < static_cast<size_t>(TreeTexture::Count); ++t)
		{
			m_Textures[t] = std::make_unique<Texture2D>(images.Images[t], TextureEncoding::SRGB);
			if (images.Normals[t].IsValid())   // T128: the bark's relief and shine (data maps)
				m_NormalMaps[t] = std::make_unique<Texture2D>(images.Normals[t], TextureEncoding::Linear);
			if (images.Heights[t].IsValid())
				m_HeightMaps[t] = std::make_unique<Texture2D>(images.Heights[t], TextureEncoding::Linear);
			if (images.Roughness[t].IsValid())
				m_RoughnessMaps[t] = std::make_unique<Texture2D>(images.Roughness[t], TextureEncoding::Linear);
		}
	}

	TreesGpu::~TreesGpu() = default;

	std::unique_ptr<TreeGpu> TreesGpu::Upload(const Tree& source) const
	{
		const TreeParams& p = source.Params;
		const float H = source.Height;
		auto tree = std::make_unique<TreeGpu>();
		tree->Height = source.Height;
		tree->CrownRadius = source.CrownRadius;
		tree->BoundsHeight = source.BoundsHeight;
		tree->TriangleCount = source.TriangleCount;
		tree->LeafCardCount = source.LeafCardCount;

		tree->Trunk = std::make_unique<Mesh>(source.Trunk.Vertices, source.Trunk.Indices);
		Material trunk;
		trunk.Albedo = p.Species == TreeSpecies::Bare ? glm::vec3(0.8f, 0.75f, 0.7f) : glm::vec3(1.0f);
		trunk.AlbedoMap = GetTexture(TreeFactory::TrunkTexture(p));
		// T128: the bark's relief, cavity and shine, painted from the albedo's pattern (TreeImages); their slopes are drawn for the tile
		// below. The height map only darkens the ambient light in the furrows (CavityStrength; HeightScale 0: no displacement).
		trunk.NormalMap = GetNormalMap(TreeFactory::TrunkTexture(p));
		trunk.HeightMap = GetHeightMap(TreeFactory::TrunkTexture(p));
		trunk.MetallicRoughnessMap = GetRoughnessMap(TreeFactory::TrunkTexture(p));
		// T110: the bark's UVs are metres; the bark tile is 0.8 m (about what it was round a trunk before), the palm's 1 m
		trunk.UVScale = p.Species == TreeSpecies::Palm ? 1.0f : 1.25f;
		trunk.Roughness = 0.95f;
		trunk.HeightScale = 0.0f;
		trunk.CavityStrength = 0.8f;
		trunk.WindSway = p.Part != TreePart::Standing ? 0.0f : (p.Species == TreeSpecies::Palm ? 0.9f : 0.5f);   // (T113: a felled piece lies still)
		trunk.WindHeight = H;
		tree->TrunkMaterial = trunk;

		// T113: the cut faces in pale wood (a plain colour: the end-grain texture of the gallery is not one of the trees' yet), swaying with the
		// trunk they close; the earth of a planted or uprooted tree, still
		if (!source.Cuts.Empty())
		{
			tree->Cuts = std::make_unique<Mesh>(source.Cuts.Vertices, source.Cuts.Indices);
			Material cut;
			cut.Albedo = glm::vec3(0.62f, 0.49f, 0.33f);
			cut.Roughness = 0.85f;
			cut.HeightScale = 0.0f;
			cut.CavityStrength = 0.0f;
			cut.WindSway = trunk.WindSway;
			cut.WindHeight = H;
			tree->CutMaterial = cut;
		}
		if (!source.Soil.Empty())
		{
			tree->Soil = std::make_unique<Mesh>(source.Soil.Vertices, source.Soil.Indices);
			Material earth;
			earth.Albedo = glm::vec3(0.2f, 0.15f, 0.1f);
			earth.Roughness = 0.95f;
			earth.HeightScale = 0.0f;
			earth.CavityStrength = 0.0f;
			tree->SoilMaterial = earth;
		}

		if (!source.Foliage.Vertices.empty())
		{
			tree->Foliage = std::make_unique<Mesh>(source.Foliage.Vertices, source.Foliage.Indices);
			Material foliage;
			foliage.Albedo = p.LeafTint;
			foliage.AlbedoMap = GetTexture(TreeFactory::FoliageTexture(p));
			foliage.Roughness = 0.7f;
			foliage.Alpha = AlphaMode::Mask;
			foliage.AlphaCutoff = 0.4f;
			foliage.DoubleSided = true;
			foliage.FoliageNormals = true;
			foliage.HeightScale = 0.0f;
			foliage.CavityStrength = 0.0f;
			foliage.WindSway = p.Species == TreeSpecies::Conifer ? 0.45f : (p.Species == TreeSpecies::Palm ? 1.0f : 0.8f);
			foliage.WindHeight = H;
			foliage.WindFlutter = p.Species == TreeSpecies::Conifer ? 0.03f : 0.09f;
			// T128: the PBR foliage layer (GB_FOLIAGE; MATERIALES.md, "Follaje" and T107), with the values the material gallery dresses its
			// tree crowns with (tests/data/material_gallery.json, leaf_broad / leaf_small and leaf_needle, and material_gallery_test's addTree):
			// - the light through the leaves, warm (the red over the green: with the leaf's albedo a greener tint glows lime), a little less
			//   through the needles;
			// - colour per tree (a hash of its origin) and world patches;
			// - the alpha raised by mip, so the far cards keep their fill;
			// - no sphere blend (NormalBlend 0) and no occlusion at the foot: the crown normal is baked in the mesh (FoliageNormals). The core
			//   within a quarter of the tree's height of the trunk darkens (measured from the instance's origin, the foot of the tree, in world
			//   metres: a tree placed at scale s keeps its unscaled height here).
			// The palm is not in the gallery (not of era 1): it takes the broadleaf's values.
			const bool needles = p.Species == TreeSpecies::Conifer;
			foliage.Translucency = needles ? 0.8f : 1.0f;
			foliage.TransmissionTint = glm::vec3(1.45f, 1.35f, 0.45f);
			foliage.ColorVariation = needles ? 0.3f : 0.4f;
			foliage.MacroVariation = needles ? 0.2f : 0.25f;
			foliage.AlphaMipScale = needles ? 0.2f : 0.25f;
			foliage.BacklightSpread = needles ? 0.5f : 0.6f;			foliage.NormalBlend = 0.0f;
			foliage.BaseOcclusion = 0.0f;
			foliage.OcclusionHeight = 0.25f * H;
			foliage.InteriorOcclusion = 0.3f;
			tree->FoliageMaterial = foliage;
		}
		return tree;
	}

	void TreeGpu::AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale, float maxDistance) const
	{
		const glm::mat4 transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		const float top = std::max(Height, BoundsHeight);
		const glm::vec3 boundsCenter(0.0f, top * 0.5f, 0.0f);
		const float boundsRadius = 1.1f * glm::length(glm::vec2(top * 0.5f, CrownRadius + 0.5f));
		RenderObject t;
		t.BoundingCenter = boundsCenter; t.BoundingRadius = boundsRadius; t.MaxDistance = maxDistance;
		t.Instanced = true;   // (every tree of this variant: the same trunk mesh and material)
		t.MeshPtr = Trunk.get();
		t.Mat = TrunkMaterial;
		t.Transform = transform;
		scene.Objects.push_back(t);
		for (const auto& [mesh, mat] : { std::pair<const Mesh*, const Material*>{ Cuts.get(), &CutMaterial }, std::pair<const Mesh*, const Material*>{ Soil.get(), &SoilMaterial } })
		{
			if (!mesh) continue;   // (T113: only a tree with cut faces or earth adds these)
			RenderObject o;
			o.BoundingCenter = boundsCenter; o.BoundingRadius = boundsRadius; o.MaxDistance = maxDistance;
			o.Instanced = true;
			o.MeshPtr = mesh;
			o.Mat = *mat;
			o.Transform = transform;
			scene.Objects.push_back(o);
		}
		if (Foliage)
		{
			RenderObject f;
			f.BoundingCenter = boundsCenter; f.BoundingRadius = boundsRadius; f.MaxDistance = maxDistance;
			f.Instanced = true;
			f.MeshPtr = Foliage.get();
			f.Mat = FoliageMaterial;
			f.Transform = transform;
			scene.Objects.push_back(f);
		}
	}
}
