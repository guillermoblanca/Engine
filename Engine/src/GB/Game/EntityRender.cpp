#include "GB/Game/EntityRender.h"
#include "GB/Log.h"

namespace GB
{
	const Mesh* EntityRenderer::PrimitiveMesh(MeshShape shape, float size)
	{
		const auto key = std::make_pair(static_cast<int>(shape), size);
		auto it = m_Primitives.find(key);
		if (it == m_Primitives.end())
		{
			Mesh mesh = shape == MeshShape::Sphere ? Mesh::CreateSphere(size * 0.5f)
				: shape == MeshShape::Plane ? Mesh::CreatePlane(size)
				: Mesh::CreateCube(size);
			it = m_Primitives.emplace(key, std::make_unique<Mesh>(std::move(mesh))).first;
		}
		return it->second.get();
	}

	ModelInstance* EntityRenderer::InstanceFor(EntityId id, const std::string& path)
	{
		auto inst = m_Instances.find(id);
		if (inst != m_Instances.end() && inst->second.first == path)
			return inst->second.second.get();

		auto model = m_Models.find(path);
		if (model == m_Models.end())
		{
			const std::string full = m_AssetDir.empty() ? path : m_AssetDir + "/" + path;
			auto loaded = Model::Load(full);
			if (!loaded) GB_CORE_WARN("Entity model '{}' could not be loaded", full);
			model = m_Models.emplace(path, std::move(loaded)).first;
		}
		if (!model->second) return nullptr;
		auto& slot = m_Instances[id];
		slot.first = path;
		slot.second = std::make_unique<ModelInstance>(*model->second);
		return slot.second.get();
	}

	size_t EntityRenderer::Build(const EntityWorld& world, Scene& scene, EntityId highlight)
	{
		size_t drawn = 0;
		for (const auto& [id, e] : world.All())
		{
			if (!e.Mesh && !e.Light) continue;
			if (!world.IsActive(id)) continue;
			const glm::mat4 m = world.WorldMatrix(id);
			const size_t firstObject = scene.Objects.size();
			if (e.Mesh)
			{
				const MeshComponent& mc = *e.Mesh;
				if (!mc.Model.empty())
				{
					if (ModelInstance* inst = InstanceFor(id, mc.Model))
					{
						inst->AddToScene(scene, m);
						++drawn;
					}
				}
				else
				{
					RenderObject obj;
					obj.MeshPtr = PrimitiveMesh(mc.Shape, mc.Size);
					obj.Mat.Albedo = mc.Albedo;
					obj.Mat.Metallic = mc.Metallic;
					obj.Mat.Roughness = mc.Roughness;
					obj.Mat.Emissive = mc.Emissive;
					obj.Transform = m;
					scene.Objects.push_back(std::move(obj));
					++drawn;
				}
			}
			if (id == highlight)
				for (size_t i = firstObject; i < scene.Objects.size(); ++i)
					scene.Objects[i].Mat.Emissive += glm::vec3(0.35f, 0.18f, 0.02f);
			if (e.Light)
			{
				PointLight l;
				l.Position = glm::vec3(m[3]);
				l.Color = e.Light->Color;
				l.Intensity = e.Light->Intensity;
				l.Radius = e.Light->Radius;
				scene.PointLights.push_back(l);
				if (!e.Mesh) ++drawn;
			}
		}
		return drawn;
	}

	void EntityRenderer::Update(float deltaTime)
	{
		for (auto& [id, slot] : m_Instances)
			slot.second->Update(deltaTime);
	}
}
