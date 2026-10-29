#include "GB/Game/EntityRender.h"
#include "GB/Log.h"
#include <filesystem>
#include <set>

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
			const bool absolute = std::filesystem::path(path).is_absolute();
			const std::string full = m_AssetDir.empty() || absolute ? path : m_AssetDir + "/" + path;
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

	ModelInstance* EntityRenderer::FindInstance(EntityId id)
	{
		auto it = m_Instances.find(id);
		return it == m_Instances.end() ? nullptr : it->second.second.get();
	}

	bool EntityRenderer::ModelBounds(EntityId id, glm::vec3& center, float& radius)
	{
		auto inst = m_Instances.find(id);
		if (inst == m_Instances.end()) return false;
		auto model = m_Models.find(inst->second.first);
		if (model == m_Models.end() || !model->second) return false;
		center = model->second->GetBoundsCenter();
		radius = model->second->GetBoundsRadius();
		return true;
	}

	size_t EntityRenderer::Build(const EntityWorld& world, Scene& scene, EntityId highlight)
	{
		size_t drawn = 0;
		// the entities drawn by their data look (and so their descendants are not drawn again)
		std::set<EntityId> claimed;
		if (m_EntityVisual)
			for (const auto& [id, e] : world.All())
			{
				if (!e.Gameplay && e.Tags.empty()) continue;   // (looks hang off gameplay tags and entity tags only)
				if (!world.IsActive(id)) continue;
				const size_t firstObject = scene.Objects.size();
				if (!m_EntityVisual(world, id, world.WorldMatrix(id), scene)) continue;
				claimed.insert(id);
				++drawn;
				if (id == highlight)
					for (size_t i = firstObject; i < scene.Objects.size(); ++i)
						scene.Objects[i].Mat.Emissive += glm::vec3(0.35f, 0.18f, 0.02f);
			}
		auto underClaimed = [&](const Entity& e)
		{
			for (EntityId p = e.Parent; p != NullEntity; )
			{
				if (claimed.count(p)) return true;
				const Entity* parent = world.Find(p);
				p = parent ? parent->Parent : NullEntity;
			}
			return false;
		};
		for (const auto& [id, e] : world.All())
		{
			if (!e.Mesh && !e.Light && !e.Prop) continue;
			if (!world.IsActive(id)) continue;
			if (!claimed.empty() && (claimed.count(id) || underClaimed(e))) continue;
			const glm::mat4 m = world.WorldMatrix(id);
			const size_t firstObject = scene.Objects.size();
			if (e.Prop && m_PropBuilder && m_PropBuilder(*e.Prop, m, scene)) ++drawn;
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
