#include "GB/Renderer/ModelGpu.h"
#include "GB/Log.h"
#include "GB/Scene/Image.h"
#include <map>
#include <string>

namespace GB
{
	namespace
	{
		// Decodes and uploads one image of a model. A file image is uploaded once for every model that uses it (the pieces of a modular kit
		// share a dozen textures between them): the texture lives while any model that uses it does.
		std::shared_ptr<Texture2D> UploadImage(const ModelImage& source)
		{
			static std::map<std::string, std::weak_ptr<Texture2D>> shared;
			if (!source.SharedKey.empty())
				if (std::shared_ptr<Texture2D> there = shared[source.SharedKey].lock())
					return there;
			// glTF UV (0,0) is the top-left of the image, which is row 0 of the uploaded data (no flip).
			const Image image = !source.File.empty() ? Image::Load(source.File, false)
				: Image::LoadFromMemory(source.Encoded.data(), source.Encoded.size(), false);
			if (!image.IsValid())
			{
				GB_CORE_WARN("model: could not decode an image; the material will render without it");
				return nullptr;
			}
			auto texture = std::make_shared<Texture2D>(image, source.SRGB ? TextureEncoding::SRGB : TextureEncoding::Linear);
			if (!source.SharedKey.empty())
				shared[source.SharedKey] = texture;
			return texture;
		}

		Material ToMaterial(const ModelMaterial& src, const std::vector<std::shared_ptr<Texture2D>>& textures)
		{
			auto map = [&](int image) -> const Texture2D*
			{
				return image >= 0 && static_cast<size_t>(image) < textures.size() ? textures[static_cast<size_t>(image)].get() : nullptr;
			};
			Material m;
			m.Albedo = src.Albedo;
			m.Opacity = src.Opacity;
			m.Metallic = src.Metallic;
			m.Roughness = src.Roughness;
			m.Emissive = src.Emissive;
			m.NormalStrength = src.NormalStrength;
			m.OcclusionStrength = src.OcclusionStrength;
			m.HeightScale = src.HeightScale;
			m.CavityStrength = src.CavityStrength;
			m.Alpha = src.Alpha;
			m.AlphaCutoff = src.AlphaCutoff;
			m.DoubleSided = src.DoubleSided;
			m.AlbedoMap = map(src.AlbedoMap);
			m.MetallicRoughnessMap = map(src.MetallicRoughnessMap);
			m.NormalMap = map(src.NormalMap);
			m.OcclusionMap = map(src.OcclusionMap);
			m.EmissiveMap = map(src.EmissiveMap);
			return m;
		}
	}

	const ModelGpu& ModelGpu::Of(const Model& model)
	{
		if (const ModelGpu* gpu = dynamic_cast<const ModelGpu*>(model.GetRenderData()))
			return *gpu;
		auto made = std::make_unique<ModelGpu>(model);
		const ModelGpu& result = *made;
		model.SetRenderData(std::move(made));
		return result;
	}

	ModelGpu::ModelGpu(const Model& model)
	{
		m_Textures.reserve(model.GetImages().size());
		for (const ModelImage& image : model.GetImages())
			m_Textures.push_back(UploadImage(image));
		for (const ModelMaterial& material : model.GetMaterials())
			m_Materials.push_back(ToMaterial(material, m_Textures));
		m_Default = ToMaterial(model.GetMaterial(-1), m_Textures);

		m_Meshes.resize(model.GetMeshCount());
		for (size_t m = 0; m < m_Meshes.size(); ++m)
			for (const Model::Primitive& prim : model.GetPrimitives(static_cast<int>(m)))
				m_Meshes[m].push_back(std::make_unique<Mesh>(prim.Geometry.Vertices, prim.Geometry.Indices, prim.Skin.empty() ? nullptr : &prim.Skin));
	}

	const Material& ModelGpu::GetMaterial(int materialIndex) const
	{
		if (materialIndex >= 0 && static_cast<size_t>(materialIndex) < m_Materials.size())
			return m_Materials[static_cast<size_t>(materialIndex)];
		return m_Default;
	}

	void AddToScene(const ModelInstance& instance, Scene& scene, const glm::mat4& root, const std::vector<glm::vec3>* tints)
	{
		const Model& model = instance.GetModel();
		const ModelGpu& gpu = ModelGpu::Of(model);
		const auto& nodes = model.GetNodes();
		for (int n : model.GetNodeOrder())
		{
			if (nodes[n].MeshIndex < 0)
				continue;
			const bool skinned = nodes[n].SkinIndex >= 0;
			const std::vector<Model::Primitive>& prims = model.GetPrimitives(nodes[n].MeshIndex);
			for (size_t p = 0; p < prims.size(); ++p)
			{
				const Model::Primitive& prim = prims[p];
				const Mesh& mesh = gpu.GetMesh(nodes[n].MeshIndex, p);
				RenderObject obj;
				obj.MeshPtr = &mesh;
				obj.Mat = gpu.GetMaterial(prim.MaterialIndex);
				if (tints && prim.MaterialIndex >= 0 && prim.MaterialIndex < static_cast<int>(tints->size())) obj.Mat.Albedo *= (*tints)[static_cast<size_t>(prim.MaterialIndex)];
				if (skinned && mesh.HasSkin())
				{
					// glTF: the transform of a skinned mesh's node is ignored; the joints place the vertices.
					obj.Transform = root;
					obj.JointMatrices = &instance.GetJointMatrices(n);
				}
				else
					obj.Transform = root * instance.GetNodeWorld(n);
				scene.Objects.push_back(std::move(obj));
			}
		}
	}

	void AddToScene(const Model& model, Scene& scene, const glm::mat4& root)
	{
		ModelInstance rest(model);   // rest pose, no playback
		const size_t first = scene.Objects.size();
		AddToScene(rest, scene, root);
		// The palettes die with `rest`: skinned meshes are drawn unskinned (bind pose) in static scenes.
		for (size_t i = first; i < scene.Objects.size(); ++i)
			scene.Objects[i].JointMatrices = nullptr;
	}
}
