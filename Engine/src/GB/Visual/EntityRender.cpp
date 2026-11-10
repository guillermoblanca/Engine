#include "GB/Visual/EntityRender.h"
#include "GB/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

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
		m_ListedStructure = world.Structure();
		m_ListedAge = 0;
		m_Looks.clear(); m_Draws.clear(); m_Props.clear(); m_PropCells.clear();
		for (const auto& [id, e] : world.All())
		{
			const Entity* parentOf = world.Find(e.Parent);
			if (e.Gameplay || !e.Tags.empty()) m_Looks.push_back({ id, &e, parentOf });
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
				m_Draws.push_back({ id, &e, parentOf, look });
			}
			else if (e.Prop)
			{
				m_Props.push_back({ id, &e, parentOf });
				glm::vec3 at = e.Transform.Position;   // (its group sits at its own place: one level is enough for the scenery)
				if (parentOf) at += parentOf->Transform.Position;
				m_PropCells[{ static_cast<int>(std::floor(at.x / kPropCell)), static_cast<int>(std::floor(at.z / kPropCell)) }].push_back({ id, &e, parentOf });
			}
		}
	}

	size_t EntityRenderer::Build(const EntityWorld& world, Scene& scene, EntityId highlight)
	{
		using Clock = std::chrono::steady_clock;
		const auto start = Clock::now();
		m_Stats = BuildStats();
		size_t drawn = 0;
		if (world.Structure() != m_ListedStructure || ++m_ListedAge > 120) RefreshLists(world);
		// the look entities claimed this Build: drawn by their look, or culled far with all they hold (their descendants are not drawn again)
		if (++m_BuildNumber == 0) { std::fill(m_ClaimedAt.begin(), m_ClaimedAt.end(), 0u); m_BuildNumber = 1; }
		if (m_ClaimedAt.size() < world.NextId()) m_ClaimedAt.resize(world.NextId(), 0u);
		bool anyClaimed = false;
		const auto claim = [&](EntityId id)
		{
			if (id >= m_ClaimedAt.size()) m_ClaimedAt.resize(static_cast<size_t>(id) + 1, 0u);
			m_ClaimedAt[id] = m_BuildNumber;
			anyClaimed = true;
		};
		const auto claimed = [&](EntityId id) { return id < m_ClaimedAt.size() && m_ClaimedAt[id] == m_BuildNumber; };
		if (m_EntityVisual)
			for (const Listed& listed : m_Looks)
			{
				const EntityId id = listed.Id;
				const Entity& e = *listed.E;
				if (!e.Gameplay && e.Tags.empty()) continue;   // (looks hang off gameplay tags and entity tags only)
				const Entity* group = m_CullDistance > 0.0f ? listed.Parent : nullptr;
				if (m_CullDistance > 0.0f && e.Gameplay && glm::distance(e.Transform.Position + (group ? group->Transform.Position : glm::vec3(0.0f)), m_CullOrigin) > m_CullDistance + 30.0f)
				{
					// far resource nodes and creatures are not drawn (their looks cull themselves well before this); buildings are seen from afar;
					// a group (tags only, no gameplay: the Trees of the scenery) is never culled here, its children are judged one by one
					const auto building = [](const std::vector<std::string>& tags) { return std::find(tags.begin(), tags.end(), "Building") != tags.end(); };
					if (!building(e.Tags) && !(e.Gameplay && building(e.Gameplay->Tags))) { claim(id); continue; }   // (claimed: its fallback parts are not drawn either)
				}
				// EntityWorld::IsActive and WorldMatrix in one walk up (the same products in the same order)
				m_Chain.clear();
				bool active = true;
				for (const Entity* a = &e; a; a = a == &e ? listed.Parent : a->Parent == NullEntity ? nullptr : world.Find(a->Parent))
				{
					m_Chain.push_back(a);
					active = active && a->Active;
				}
				if (!active) continue;
				glm::mat4 m(1.0f);
				for (const Entity* a : m_Chain) m = a->Transform.ToMatrix() * m;
				++m_Stats.Looks;
				const size_t firstObject = scene.Objects.size();
				if (!m_EntityVisual(world, id, m, scene)) continue;
				++m_Stats.LooksDrawn;
				claim(id);
				++drawn;
				if (id == highlight)
					for (size_t i = firstObject; i < scene.Objects.size(); ++i)
						{ scene.Objects[i].Mat.Emissive += glm::vec3(0.35f, 0.18f, 0.02f); scene.Objects[i].Instanced = false; }   // (its own material now)
			}
		const auto looksDone = Clock::now();
		m_Stats.LooksMs = std::chrono::duration<float, std::milli>(looksDone - start).count();
		// the meshes and lights, then the props of the cells within reach (all of them without a cull distance)
		m_Walk.clear();
		for (const Listed& listed : m_Draws)
			if (listed.Look == NullEntity || !claimed(listed.Look)) m_Walk.push_back(&listed);   // (a part of a node or a building drawn by its look: skipped at once)
		if (m_CullDistance > 0.0f)
		{
			const float reach = m_CullDistance + 30.0f;
			const int x0 = static_cast<int>(std::floor((m_CullOrigin.x - reach) / kPropCell)), x1 = static_cast<int>(std::floor((m_CullOrigin.x + reach) / kPropCell));
			const int z0 = static_cast<int>(std::floor((m_CullOrigin.z - reach) / kPropCell)), z1 = static_cast<int>(std::floor((m_CullOrigin.z + reach) / kPropCell));
			for (int cz = z0; cz <= z1; ++cz)
				for (int cx = x0; cx <= x1; ++cx)
					if (const auto cell = m_PropCells.find({ cx, cz }); cell != m_PropCells.end())
						for (const Listed& listed : cell->second) m_Walk.push_back(&listed);
		}
		else
			for (const Listed& listed : m_Props) m_Walk.push_back(&listed);
		m_Stats.Walked = static_cast<uint32_t>(m_Walk.size());
		const Entity* groupOf = nullptr;
		glm::mat4 groupMatrix(1.0f);
		for (const Listed* listed : m_Walk)
		{
			const EntityId id = listed->Id;
			const Entity& e = *listed->E;
			if (!e.Mesh && !e.Light && !e.Prop) continue;
			const Entity* parent = listed->Parent;
			if (e.Prop && !e.Mesh && !e.Light && m_CullDistance > 0.0f)   // far props go before anything is computed for them (their group sits at its own place)
			{
				glm::vec3 at = e.Transform.Position;
				if (parent) at += parent->Transform.Position;
				if (glm::distance(at, m_CullOrigin) > m_CullDistance + 30.0f) continue;
			}
			// one walk up the ancestors for what EntityWorld::IsActive, the claimed looks above it and EntityWorld::WorldMatrix would each
			// walk again: active only if every one is, skipped under a claimed look, and the world matrix with the same products in the same order
			m_Chain.clear();
			m_Chain.push_back(&e);
			bool active = e.Active, underClaimed = false;
			for (const Entity* a = parent; a; a = a->Parent == NullEntity ? nullptr : world.Find(a->Parent))   // (no entity has the null id)
			{
				m_Chain.push_back(a);
				active = active && a->Active;
				underClaimed = underClaimed || claimed(a->Id);
			}
			if (!active) continue;
			if (anyClaimed && (claimed(id) || underClaimed)) continue;
			glm::mat4 m(1.0f);
			for (size_t k = 0; k < m_Chain.size(); ++k)
			{
				const Entity* a = m_Chain[k];
				if (k == 1)   // the parent: the props of a group share it, so its local matrix is made once a Build (the same value: ToMatrix is pure)
				{
					if (a != groupOf) { groupOf = a; groupMatrix = a->Transform.ToMatrix(); }
					m = groupMatrix * m;
				}
				else m = a->Transform.ToMatrix() * m;
			}
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
						AddToScene(*inst, scene, m);
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
		m_Stats.WalkMs = std::chrono::duration<float, std::milli>(Clock::now() - looksDone).count();
		return drawn;
	}

	void EntityRenderer::Update(float deltaTime)
	{
		for (auto& [id, slot] : m_Instances)
			slot.second->Update(deltaTime);
	}
}
