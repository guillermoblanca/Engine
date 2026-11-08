#include "GB/Game/EntityRender.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <unordered_set>

namespace GB
{
	const Mesh* EntityRenderer::PrimitiveMesh(MeshShape shape, float size)
	{
		const auto key = std::make_pair(static_cast<int>(shape), size);
		auto it = m_Primitives.find(key);
		if (it == m_Primitives.end())
		{
			Mesh mesh = shape == MeshShape::Sphere ? Mesh::CreateSphere(size * 0.5f, 16, 8)   // (a scene's primitives are many and small: 256 triangles, not 4096)
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

	void EntityRenderer::RefreshLists(const EntityWorld& world)
	{
		m_ListedCount = world.Count();
		m_ListedAge = 0;
		m_LookIds.clear(); m_DrawIds.clear(); m_PropIds.clear(); m_PropCells.clear();
		for (const auto& [id, e] : world.All())
		{
			if (e.Gameplay || !e.Tags.empty()) m_LookIds.push_back(id);
			if (e.Mesh || e.Light)
			{
				EntityId look = NullEntity;
				for (EntityId p = e.Parent; p != NullEntity && look == NullEntity; )
				{
					const Entity* parent = world.Find(p);
					if (!parent) break;
					if (parent->Gameplay || !parent->Tags.empty()) look = p;
					p = parent->Parent;
				}
				m_DrawIds.push_back({ id, look });
			}
			else if (e.Prop)
			{
				m_PropIds.push_back(id);
				glm::vec3 at = e.Transform.Position;   // (its group sits at its own place: one level is enough for the scenery)
				if (const Entity* group = world.Find(e.Parent)) at += group->Transform.Position;
				m_PropCells[{ static_cast<int>(std::floor(at.x / kPropCell)), static_cast<int>(std::floor(at.z / kPropCell)) }].push_back(id);
			}
		}
	}

	size_t EntityRenderer::Build(const EntityWorld& world, Scene& scene, EntityId highlight)
	{
		size_t drawn = 0;
		if (world.Count() != m_ListedCount || ++m_ListedAge > 120) RefreshLists(world);
		// the entities drawn by their data look (and so their descendants are not drawn again)
		std::unordered_set<EntityId> claimed;
		if (m_EntityVisual)
			for (const EntityId id : m_LookIds)
			{
				const Entity* found = world.Find(id);
				if (!found) continue;
				const Entity& e = *found;
				if (!e.Gameplay && e.Tags.empty()) continue;   // (looks hang off gameplay tags and entity tags only)
				const Entity* group = m_CullDistance > 0.0f ? world.Find(e.Parent) : nullptr;
				if (m_CullDistance > 0.0f && e.Gameplay && glm::distance(e.Transform.Position + (group ? group->Transform.Position : glm::vec3(0.0f)), m_CullOrigin) > m_CullDistance + 30.0f)
				{
					// far resource nodes and creatures are not drawn (their looks cull themselves well before this); buildings are seen from afar;
					// a group (tags only, no gameplay: the Trees of the scenery) is never culled here, its children are judged one by one
					const auto building = [](const std::vector<std::string>& tags) { return std::find(tags.begin(), tags.end(), "Building") != tags.end(); };
					if (!building(e.Tags) && !(e.Gameplay && building(e.Gameplay->Tags))) { claimed.insert(id); continue; }   // (claimed: its fallback parts are not drawn either)
				}
				if (!world.IsActive(id)) continue;
				const size_t firstObject = scene.Objects.size();
				if (!m_EntityVisual(world, id, world.WorldMatrix(id), scene)) continue;
				claimed.insert(id);
				++drawn;
				if (id == highlight)
					for (size_t i = firstObject; i < scene.Objects.size(); ++i)
						{ scene.Objects[i].Mat.Emissive += glm::vec3(0.35f, 0.18f, 0.02f); scene.Objects[i].Instanced = false; }   // (its own material now)
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
		// the meshes and lights, then the props of the cells within reach (all of them without a cull distance)
		std::vector<EntityId> walk;
		walk.reserve(m_DrawIds.size() + 4096);
		for (const auto& [id, look] : m_DrawIds)
			if (look == NullEntity || !claimed.count(look)) walk.push_back(id);   // (a part of a node or a building drawn by its look: skipped at once)
		if (m_CullDistance > 0.0f)
		{
			const float reach = m_CullDistance + 30.0f;
			const int x0 = static_cast<int>(std::floor((m_CullOrigin.x - reach) / kPropCell)), x1 = static_cast<int>(std::floor((m_CullOrigin.x + reach) / kPropCell));
			const int z0 = static_cast<int>(std::floor((m_CullOrigin.z - reach) / kPropCell)), z1 = static_cast<int>(std::floor((m_CullOrigin.z + reach) / kPropCell));
			for (int cz = z0; cz <= z1; ++cz)
				for (int cx = x0; cx <= x1; ++cx)
					if (const auto cell = m_PropCells.find({ cx, cz }); cell != m_PropCells.end()) walk.insert(walk.end(), cell->second.begin(), cell->second.end());
		}
		else walk.insert(walk.end(), m_PropIds.begin(), m_PropIds.end());
		for (const EntityId id : walk)
		{
			const Entity* found = world.Find(id);
			if (!found) continue;
			const Entity& e = *found;
			if (!e.Mesh && !e.Light && !e.Prop) continue;
			if (e.Prop && !e.Mesh && !e.Light && m_CullDistance > 0.0f)   // far props go before anything is computed for them (their group sits at its own place)
			{
				glm::vec3 at = e.Transform.Position;
				if (const Entity* group = world.Find(e.Parent)) at += group->Transform.Position;
				if (glm::distance(at, m_CullOrigin) > m_CullDistance + 30.0f) continue;
			}
			if (!world.IsActive(id)) continue;
			if (!claimed.empty() && (claimed.count(id) || underClaimed(e))) continue;
			const glm::mat4 m = world.WorldMatrix(id);
			if (e.Prop && !e.Mesh && !e.Light && m_CullDistance > 0.0f && glm::distance(glm::vec3(m[3]), m_CullOrigin) > m_CullDistance) continue;
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
					obj.BoundingRadius = 0.87f * mc.Size;   // (bounded, so the renderer culls it by the view, by the shadows' box and past 200 m)
					obj.MaxDistance = 200.0f;
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
					{ scene.Objects[i].Mat.Emissive += glm::vec3(0.35f, 0.18f, 0.02f); scene.Objects[i].Instanced = false; }   // (its own material now)
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
