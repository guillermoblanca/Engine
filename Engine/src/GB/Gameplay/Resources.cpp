#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		const char* KindTag(const ResourceNodeDef& d)
		{
			if (d.Item == "wood") return "Resource.Wood";
			if (d.Item == "stone") return "Resource.Stone";
			return "Resource.Plant";
		}

		AbilityDef GatherAbility(const char* name, const char* montage, const char* notify, const char* kind, const char* tool)
		{
			AbilityDef a;
			a.Name = name;
			a.Tag = std::string("Ability.") + name;
			a.NeedsTarget = true;
			a.MoveToTarget = true;
			a.BlockedTags = { "State.Busy", "State.Carrying" };
			a.ActiveTags = { "State.Busy" };
			if (tool[0]) a.RequiredTags = { tool };
			a.TargetRequiredTags = { "Resource.Node", kind };
			a.TargetBlockedTags = { "State.Depleted" };
			a.TargetMinAttributes = { { "Amount", 1.0f } };
			a.Montages = { montage };
			a.Duration = 2.0f;
			a.Events.push_back({ notify, 0.6f, "Event.Resource.Gather", {}, {} });
			a.CooldownSeconds = 0.3f;
			return a;
		}

		EntityId Part(EntityWorld& w, EntityId parent, const std::string& name, MeshShape shape, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& color)
		{
			const EntityId id = w.Create(name, parent);
			Entity* e = w.Find(id);
			e->Transform.Position = pos;
			e->Transform.Scale = scale;
			e->Mesh.emplace();
			e->Mesh->Shape = shape;
			e->Mesh->Size = 1.0f;
			e->Mesh->Albedo = color;
			e->Mesh->Roughness = 0.9f;
			return id;
		}
	}

	void RegisterResourceContent(GameplayLibrary& lib, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		for (const auto& [id, def] : nodes)
		{
			if (def.RegenSeconds <= 0.0f) continue;
			GameplayEffectDef e;
			e.Name = "Regrow." + id;
			e.Policy = EffectPolicy::Infinite;
			e.Period = def.RegenSeconds;
			e.Modifiers.push_back({ "Amount", ModOp::Add, static_cast<float>(std::max(1, def.Regen)) });
			lib.AddEffect(e);
		}
		// Stand-in montages until the packs have chopping, mining and gathering clips (docs/ANIMACIONES_PENDIENTES.md).
		AbilityDef chop = GatherAbility("Chop", "dig_plant", "chop", "Resource.Wood", "Item.Tool.Axe");
		chop.Note = "chops a tree (needs an axe)";
		lib.AddAbility(chop);
		AbilityDef mine = GatherAbility("Mine", "dig_plant", "mine", "Resource.Stone", "Item.Tool.Pickaxe");
		mine.Note = "mines a rock (needs a pickaxe)";
		lib.AddAbility(mine);
		AbilityDef pick = GatherAbility("Gather", "pick_fruit", "gather", "Resource.Plant", "");
		pick.Note = "gathers berries or fibre by hand";
		lib.AddAbility(pick);
	}

	ResourceLayout BuildResources(EntityWorld& w, const glm::vec3& origin, const std::map<std::string, ResourceNodeDef>& nodes, int trees, int rocks, int bushes, int grass)
	{
		if (const EntityId old = w.FindByName("Resources"); old != NullEntity) w.Destroy(old);
		ResourceLayout out;
		const EntityId root = w.Create("Resources");
		w.Find(root)->Transform.Position = origin;
		out.Root = w.Find(root)->Name;

		const struct { const char* id; int count; float z; } rows[] = { { "tree", trees, 0.0f }, { "rock", rocks, 5.0f }, { "berry_bush", bushes, 9.0f }, { "fiber_grass", grass, 12.0f } };
		for (const auto& row : rows)
		{
			auto it = nodes.find(row.id);
			if (it == nodes.end()) continue;
			const ResourceNodeDef& def = it->second;
			for (int i = 0; i < row.count; ++i)
			{
				std::string label = row.id;   // entity names use spaces ("berry bush 1") so a task list can write them with underscores
				std::replace(label.begin(), label.end(), '_', ' ');
				const std::string name = label + " " + std::to_string(i + 1);
				const EntityId id = w.Create(name, root);
				Entity& e = *w.Find(id);
				e.Transform.Position = { (static_cast<float>(i) - (row.count - 1) * 0.5f) * 4.0f, 0.0f, row.z };
				InteractionPoint p;
				p.Name = "work";
				p.Position = { 0.0f, 0.0f, -1.6f };
				e.Interact.emplace();
				e.Interact->Points.push_back(p);
				e.Gameplay.emplace();
				e.Gameplay->Tags = { "Resource.Node", std::string("Resource.Node.") + row.id, KindTag(def) };
				e.Gameplay->Attributes = { { "Amount", static_cast<float>(def.Capacity), 0.0f, static_cast<float>(def.Capacity) } };
				if (def.RegenSeconds > 0.0f) e.Gameplay->Effects = { "Regrow." + std::string(row.id) };
				const std::string kind = row.id;
				if (kind == "tree")
				{
					Part(w, id, name + " Trunk", MeshShape::Cube, { 0.0f, 1.2f, 0.0f }, { 0.35f, 2.4f, 0.35f }, { 0.36f, 0.24f, 0.13f });
					Part(w, id, name + " Crown", MeshShape::Sphere, { 0.0f, 3.0f, 0.0f }, { 2.4f, 2.0f, 2.4f }, { 0.2f, 0.45f, 0.16f });
				}
				else if (kind == "rock")
					Part(w, id, name + " Rock", MeshShape::Sphere, { 0.0f, 0.5f, 0.0f }, { 1.4f, 1.0f, 1.2f }, { 0.45f, 0.45f, 0.47f });
				else if (kind == "berry_bush")
				{
					Part(w, id, name + " Bush", MeshShape::Sphere, { 0.0f, 0.5f, 0.0f }, { 1.2f, 0.9f, 1.2f }, { 0.18f, 0.4f, 0.14f });
					Part(w, id, name + " Berries", MeshShape::Sphere, { 0.3f, 0.7f, 0.5f }, glm::vec3(0.2f), { 0.7f, 0.1f, 0.25f });
				}
				else
					Part(w, id, name + " Tuft", MeshShape::Cube, { 0.0f, 0.3f, 0.0f }, { 0.5f, 0.6f, 0.5f }, { 0.55f, 0.6f, 0.25f });
				out.Nodes.push_back(e.Name);
			}
		}
		return out;
	}

	void ResourceSystem::Bind(GameplayWorld& gameplay, const std::map<std::string, ResourceNodeDef>& nodes, Yield onYield)
	{
		m_Defs = nodes;
		m_Yield = std::move(onYield);
		for (const std::string& name : gameplay.Names())
		{
			AbilitySystemComponent* node = gameplay.Find(name);
			if (!node->HasTag("Resource.Node")) continue;
			node->AddListener("Event.Resource.Gather", [this, node](const GameplayEventData& e)
			{
				const ResourceNodeDef* def = nullptr;
				for (const auto& [id, d] : m_Defs)
					if (node->HasTag("Resource.Node." + id)) { def = &d; break; }
				if (!def) return;
				const int have = Amount(*node);
				const int take = std::min(have, std::max(1, def->PerGather));
				if (take <= 0) return;
				node->Attributes().SetBase("Amount", static_cast<float>(have - take));
				if (m_Yield && e.Instigator) m_Yield(e.Instigator->Name(), def->Item, take);
			});
		}
	}

	int ResourceSystem::Amount(const AbilitySystemComponent& node)
	{
		return static_cast<int>(std::lround(node.Attributes().Current("Amount")));
	}

	void ResourceSystem::SyncVisuals(GameplayWorld& gameplay, EntityWorld& entities) const
	{
		for (const std::string& name : gameplay.Names())
		{
			AbilitySystemComponent* node = gameplay.Find(name);
			if (!node->HasTag("Resource.Node")) continue;
			const bool empty = Amount(*node) <= 0;
			if (empty && !node->HasTag("State.Depleted")) node->Tags().Add("State.Depleted");
			if (!empty && node->HasTag("State.Depleted")) node->Tags().RemoveAll("State.Depleted");
			const EntityId id = entities.FindByName(name);
			if (id == NullEntity) continue;
			// A depleted node keeps its trunk but loses what is gathered from it (crown, berries, tuft, rock).
			for (const EntityId c : entities.Find(id)->Children)
				if (Entity* part = entities.Find(c))
				{
					const bool gatherable = part->Name.find("Crown") != std::string::npos || part->Name.find("Berries") != std::string::npos
						|| part->Name.find("Tuft") != std::string::npos || part->Name.find(" Rock") != std::string::npos;
					const bool want = !(empty && gatherable);
					if (part->Active != want) part->Active = want;
				}
		}
	}

	void ResourceSystem::SyncTools(AbilitySystemComponent& who, const Inventory& bag, const ItemLibrary& items)
	{
		for (const std::string& id : items.Ids())
		{
			const ItemDef* def = items.Find(id);
			for (const std::string& tag : def->Tags)
			{
				if (tag.rfind("Item.", 0) != 0) continue;   // tools and materials: what a bag holds is what an ability may ask for
				const bool has = bag.Count(id) > 0;
				if (has && !who.Tags().HasExact(tag)) who.Tags().Add(tag);
				if (!has && who.Tags().HasExact(tag)) who.Tags().RemoveAll(tag);
			}
		}
	}
}
