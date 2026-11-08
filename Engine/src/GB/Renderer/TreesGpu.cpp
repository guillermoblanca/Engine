#include "GB/Renderer/TreesGpu.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	TreesGpu::TreesGpu()
	{
		const TreeImages& images = TreeFactory::Textures();
		for (size_t t = 0; t < static_cast<size_t>(TreeTexture::Count); ++t)
			m_Textures[t] = std::make_unique<Texture2D>(images.Images[t], TextureEncoding::SRGB);
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
		trunk.Roughness = 0.95f;
		trunk.HeightScale = 0.0f;
		trunk.CavityStrength = 0.0f;
		trunk.WindSway = p.Species == TreeSpecies::Palm ? 0.9f : 0.5f;
		trunk.WindHeight = H;
		tree->TrunkMaterial = trunk;

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
