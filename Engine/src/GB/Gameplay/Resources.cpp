#include "GB/Gameplay/Resources.h"
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace GB
{
	std::string ResourceKindTag(const ResourceNodeDef& d)
	{
		if (!d.Kind.empty()) return "Resource." + d.Kind;
		if (d.Item == "wood") return "Resource.Wood";
		if (d.Item == "stone") return "Resource.Stone";
		return "Resource.Plant";
	}

	namespace
	{
		std::string KindTag(const ResourceNodeDef& d) { return ResourceKindTag(d); }

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
		// (Older scenes carry these regrowth effects; new nodes regrow through ResourceSystem::Update, which a season can slow down.)
		// Montages (checked frame by frame in the editor): a tool swing for the tree and the rock (the "tool" prop in the hand), the arm at the
		// height of a bush for berries and a bend down for the tufts. They are stand-ins until the artist makes chopping, mining and gathering
		// clips of their own (docs/ANIMACIONES_PENDIENTES.md).
		AbilityDef chop = GatherAbility("Chop", "sword_slash_1", "chop", "Resource.Wood", "Item.Tool.Axe");
		chop.Montages = { "sword_slash_1", "sword_slash_3" };   // two swings in turn
		chop.EquipProp = "tool";
		chop.Note = "chops a tree (needs an axe)";
		lib.AddAbility(chop);
		AbilityDef mine = GatherAbility("Mine", "sword_attack_4", "mine", "Resource.Stone", "Item.Tool.Pickaxe");
		mine.EquipProp = "tool";
		mine.Note = "mines a rock (needs a pickaxe)";
		lib.AddAbility(mine);
		AbilityDef pick = GatherAbility("Gather", "pick_fruit_2", "gather", "Resource.Plant", "");
		pick.TargetBlockedTags = { "State.Depleted", "Resource.Node.fiber_grass" };
		pick.Note = "picks berries by hand";
		lib.AddAbility(pick);
		// Loose things lying on the ground (E1): clay from a bank, reeds from a bed. By hand, bending down like for a tuft of grass.
		AbilityDef collect = GatherAbility("Collect", "pull_plant", "collect", "Resource.Loose", "");
		collect.Note = "collects clay or reeds by hand";
		lib.AddAbility(collect);
		AbilityDef pull = GatherAbility("PullFiber", "pull_plant", "pull", "Resource.Node.fiber_grass", "");
		pull.Note = "pulls fibre from a tuft of grass";
		lib.AddAbility(pull);
	}

	ResourceCounts ScaledResources(int workers)
	{
		const int w = std::max(1, workers);
		ResourceCounts c;
		c.Trees = 2 + 2 * w;
		c.Rocks = 3 + 2 * w;
		c.Bushes = 2 + w;
		c.Grass = 2 + w;
		return c;
	}

	namespace
	{
		constexpr int kPerRow = 8;   // a long row wraps into more lines

		// The primitives each model is drawn with. Names matter: a node that runs empty hides the parts called Crown, Berries, Tuft, Rock, Mound, Reeds, Flint
		// or Ore (what is gathered from it) and keeps the rest (ResourceSystem::SyncVisuals).
		void BuildModel(EntityWorld& w, EntityId id, const std::string& name, const std::string& model)
		{
			auto yaw = [&](EntityId part, float degrees) { w.Find(part)->Transform.Rotation.y = degrees; };
			if (model == "tree")
			{
				Part(w, id, name + " Trunk", MeshShape::Cube, { 0.0f, 1.2f, 0.0f }, { 0.35f, 2.4f, 0.35f }, { 0.36f, 0.24f, 0.13f });
				Part(w, id, name + " Crown", MeshShape::Sphere, { 0.0f, 3.0f, 0.0f }, { 2.4f, 2.0f, 2.4f }, { 0.2f, 0.45f, 0.16f });
			}
			else if (model == "young_tree")   // a slim sapling: a thin pale trunk and a small bright crown
			{
				Part(w, id, name + " Trunk", MeshShape::Cube, { 0.0f, 0.7f, 0.0f }, { 0.12f, 1.4f, 0.12f }, { 0.45f, 0.33f, 0.19f });
				Part(w, id, name + " Crown", MeshShape::Sphere, { 0.0f, 1.7f, 0.0f }, { 0.95f, 0.85f, 0.95f }, { 0.38f, 0.64f, 0.22f });
			}
			else if (model == "old_tree")   // a big tree: a thick dark trunk and a wide dark crown high up
			{
				Part(w, id, name + " Trunk", MeshShape::Cube, { 0.0f, 2.6f, 0.0f }, { 0.85f, 5.2f, 0.85f }, { 0.28f, 0.19f, 0.1f });
				Part(w, id, name + " Crown", MeshShape::Sphere, { 0.0f, 6.4f, 0.0f }, { 4.4f, 3.4f, 4.4f }, { 0.11f, 0.32f, 0.12f });
			}
			else if (model == "rock")
				Part(w, id, name + " Rock", MeshShape::Sphere, { 0.0f, 0.5f, 0.0f }, { 1.4f, 1.0f, 1.2f }, { 0.45f, 0.45f, 0.47f });
			else if (model == "berry_bush")
			{
				Part(w, id, name + " Bush", MeshShape::Sphere, { 0.0f, 0.5f, 0.0f }, { 1.2f, 0.9f, 1.2f }, { 0.18f, 0.4f, 0.14f });
				Part(w, id, name + " Berries", MeshShape::Sphere, { 0.3f, 0.7f, 0.5f }, glm::vec3(0.2f), { 0.7f, 0.1f, 0.25f });
			}
			else if (model == "fiber_grass")
				Part(w, id, name + " Tuft", MeshShape::Cube, { 0.0f, 0.3f, 0.0f }, { 0.5f, 0.6f, 0.5f }, { 0.55f, 0.6f, 0.25f });
			else if (model == "clay_bank")   // two low brown mounds
			{
				Part(w, id, name + " Mound 1", MeshShape::Sphere, { 0.0f, 0.2f, 0.0f }, { 2.0f, 0.7f, 1.6f }, { 0.55f, 0.35f, 0.21f });
				Part(w, id, name + " Mound 2", MeshShape::Sphere, { 0.9f, 0.14f, 0.7f }, { 1.0f, 0.45f, 0.9f }, { 0.62f, 0.4f, 0.25f });
			}
			else if (model == "reed_bed")   // a bunch of thin, tall, green stalks
			{
				const float spots[][3] = { { 0.0f, 0.0f, 2.1f }, { 0.25f, 0.2f, 2.5f }, { -0.3f, 0.15f, 1.9f }, { 0.1f, -0.3f, 2.3f }, { -0.2f, -0.25f, 2.0f }, { 0.4f, -0.1f, 1.7f } };
				int n = 0;
				for (const auto& s : spots)
					Part(w, id, name + " Reeds " + std::to_string(++n), MeshShape::Cube, { s[0], s[2] * 0.5f, s[1] }, { 0.07f, s[2], 0.07f }, { 0.36f, 0.6f, 0.24f });
			}
			else if (model == "fallen_branch")   // two dead branches on the ground (the Mound parts disappear when they are picked up)
			{
				yaw(Part(w, id, name + " Mound 1", MeshShape::Cube, { 0.0f, 0.06f, 0.0f }, { 1.3f, 0.1f, 0.12f }, { 0.33f, 0.22f, 0.12f }), 15.0f);
				yaw(Part(w, id, name + " Mound 2", MeshShape::Cube, { 0.2f, 0.12f, 0.1f }, { 0.8f, 0.09f, 0.1f }, { 0.4f, 0.28f, 0.16f }), -50.0f);
			}
			else if (model == "mushroom_patch")   // three pale stalks with brown caps
			{
				const float spots[][3] = { { 0.0f, 0.0f, 0.3f }, { 0.25f, 0.15f, 0.22f }, { -0.2f, 0.2f, 0.26f } };
				int n = 0;
				for (const auto& s : spots)
				{
					++n;
					Part(w, id, name + " Tuft " + std::to_string(n), MeshShape::Cube, { s[0], s[2] * 0.5f, s[1] }, { 0.06f, s[2], 0.06f }, { 0.88f, 0.84f, 0.74f });
					Part(w, id, name + " Tuft cap " + std::to_string(n), MeshShape::Sphere, { s[0], s[2], s[1] }, { 0.22f, 0.12f, 0.22f }, { 0.52f, 0.3f, 0.16f });
				}
			}
			else if (model == "wild_roots")   // a low rosette of leaves
				Part(w, id, name + " Tuft", MeshShape::Sphere, { 0.0f, 0.12f, 0.0f }, { 0.7f, 0.25f, 0.7f }, { 0.3f, 0.5f, 0.2f });
			else if (model == "hazelnut_bush")   // a round bush with pale nuts
			{
				Part(w, id, name + " Bush", MeshShape::Sphere, { 0.0f, 0.6f, 0.0f }, { 1.4f, 1.1f, 1.4f }, { 0.22f, 0.42f, 0.15f });
				Part(w, id, name + " Berries", MeshShape::Sphere, { 0.4f, 0.8f, 0.5f }, glm::vec3(0.16f), { 0.62f, 0.45f, 0.22f });
			}
			else if (model == "bird_nest")   // a ring of twigs with pale eggs
			{
				Part(w, id, name + " Bush", MeshShape::Sphere, { 0.0f, 0.1f, 0.0f }, { 0.6f, 0.2f, 0.6f }, { 0.36f, 0.26f, 0.14f });
				Part(w, id, name + " Berries", MeshShape::Sphere, { 0.0f, 0.22f, 0.0f }, { 0.22f, 0.16f, 0.22f }, { 0.9f, 0.88f, 0.8f });
			}
			else if (model.rfind("flowers_", 0) == 0)   // a small bunch of flowers: a green tuft and five bright heads on thin stems (scenery)
			{
				const std::string colour = model.substr(8);
				const glm::vec3 head = colour == "red" ? glm::vec3(0.85f, 0.12f, 0.15f) : colour == "yellow" ? glm::vec3(0.95f, 0.8f, 0.1f)
					: colour == "blue" ? glm::vec3(0.25f, 0.35f, 0.9f) : colour == "purple" ? glm::vec3(0.6f, 0.25f, 0.75f) : glm::vec3(0.95f, 0.95f, 0.9f);
				Part(w, id, name + " Leaves", MeshShape::Sphere, { 0.0f, 0.08f, 0.0f }, { 0.45f, 0.16f, 0.45f }, { 0.2f, 0.42f, 0.14f });
				const float spots[][3] = { { 0.0f, 0.0f, 0.34f }, { 0.16f, 0.1f, 0.28f }, { -0.14f, 0.12f, 0.3f }, { 0.08f, -0.15f, 0.26f }, { -0.1f, -0.1f, 0.32f } };
				int n = 0;
				for (const auto& s : spots)
					Part(w, id, name + " Bloom " + std::to_string(++n), MeshShape::Sphere, { s[0], s[2], s[1] }, glm::vec3(0.1f), head);
			}
			else if (model == "flint_outcrop")   // sharp dark grey slabs
			{
				yaw(Part(w, id, name + " Flint 1", MeshShape::Cube, { 0.0f, 0.4f, 0.0f }, { 0.8f, 0.8f, 0.5f }, { 0.2f, 0.2f, 0.23f }), 25.0f);
				yaw(Part(w, id, name + " Flint 2", MeshShape::Cube, { 0.5f, 0.3f, 0.35f }, { 0.5f, 0.6f, 0.4f }, { 0.16f, 0.16f, 0.19f }), -40.0f);
				yaw(Part(w, id, name + " Flint 3", MeshShape::Cube, { -0.45f, 0.25f, 0.3f }, { 0.4f, 0.5f, 0.35f }, { 0.24f, 0.24f, 0.27f }), 70.0f);
			}
			else if (model == "copper_vein")   // a grey rock streaked with greenish ore
			{
				Part(w, id, name + " Base", MeshShape::Sphere, { 0.0f, 0.5f, 0.0f }, { 1.5f, 1.1f, 1.3f }, { 0.4f, 0.4f, 0.42f });
				Part(w, id, name + " Ore 1", MeshShape::Sphere, { 0.45f, 0.75f, 0.5f }, { 0.4f, 0.3f, 0.3f }, { 0.25f, 0.6f, 0.5f });
				Part(w, id, name + " Ore 2", MeshShape::Sphere, { -0.4f, 0.9f, 0.35f }, { 0.35f, 0.3f, 0.3f }, { 0.3f, 0.55f, 0.42f });
				Part(w, id, name + " Ore 3", MeshShape::Sphere, { 0.1f, 0.5f, 0.75f }, { 0.3f, 0.25f, 0.2f }, { 0.55f, 0.45f, 0.25f });
			}
			else   // a model this build does not know: a plain grey blob, so a node from a newer content file is still there to work
				Part(w, id, name + " Rock", MeshShape::Sphere, { 0.0f, 0.4f, 0.0f }, { 1.0f, 0.8f, 1.0f }, { 0.5f, 0.5f, 0.5f });
		}

		EntityId MakeNode(EntityWorld& w, EntityId root, const ResourceNodeDef& def, int index, const glm::vec3& pos, ResourceLayout& out, const std::string& prefix = "",
			bool primitives = true)
		{
			std::string label = prefix + def.Id;   // entity names use spaces ("berry bush 1") so a task list can write them with underscores
			std::replace(label.begin(), label.end(), '_', ' ');
			const std::string name = label + " " + std::to_string(index);
			const EntityId id = w.Create(name, root);
			Entity& e = *w.Find(id);
			e.Transform.Position = pos;
			if (!def.Decor)   // scenery (flowers) has nothing to gather: no interaction point, no abilities, no tags
			{
				InteractionPoint p;
				p.Name = "work";
				p.Position = { 0.0f, 0.0f, -1.6f };
				e.Interact.emplace();
				e.Interact->Points.push_back(p);
				e.Gameplay.emplace();
				e.Gameplay->Tags = { "Resource.Node", std::string("Resource.Node.") + def.Id, KindTag(def) };
				e.Gameplay->Attributes = { { "Amount", static_cast<float>(def.Capacity), 0.0f, static_cast<float>(def.Capacity) } };
				if (def.RegenSeconds > 0.0f) e.Gameplay->Attributes.push_back({ "Regrow", 0.0f, 0.0f, 1e6f });   // the regrowth timer: ResourceSystem::Update
			}
			if (primitives) BuildModel(w, id, name, def.Model.empty() ? def.Id : def.Model);
			out.Nodes.push_back(e.Name);
			return id;
		}

		EntityId MakeRoot(EntityWorld& w, const glm::vec3& origin, ResourceLayout& out, const std::string& rootName = "Resources")
		{
			if (const EntityId old = w.FindByName(rootName); old != NullEntity) w.Destroy(old);
			const EntityId root = w.Create(rootName);
			w.Find(root)->Transform.Position = origin;
			out.Root = w.Find(root)->Name;
			return root;
		}
	}

	ResourceLayout BuildResources(EntityWorld& w, const glm::vec3& origin, const std::map<std::string, ResourceNodeDef>& nodes, int trees, int rocks, int bushes, int grass)
	{
		ResourceLayout out;
		const EntityId root = MakeRoot(w, origin, out);
		const struct { const char* id; int count; float z; } rows[] = { { "tree", trees, 0.0f }, { "rock", rocks, 5.0f }, { "berry_bush", bushes, 9.0f }, { "fiber_grass", grass, 12.0f } };
		for (const auto& row : rows)
		{
			auto it = nodes.find(row.id);
			if (it == nodes.end()) continue;
			for (int i = 0; i < row.count; ++i)
			{
				const int line = i / kPerRow, inLine = i % kPerRow, lineCount = std::min(kPerRow, row.count - line * kPerRow);
				MakeNode(w, root, it->second, i + 1, { (static_cast<float>(inLine) - (lineCount - 1) * 0.5f) * 4.0f, 0.0f, row.z + 16.0f * static_cast<float>(line) }, out);
			}
		}
		return out;
	}

	std::vector<std::pair<std::string, int>> PatchCounts(const std::map<std::string, ResourceNodeDef>& nodes, int workers)
	{
		std::vector<std::pair<std::string, int>> counts;
		for (const auto& [id, def] : nodes)
			if (const int n = def.Patch.CountFor(workers); n > 0) counts.push_back({ id, n });
		return counts;
	}

	ResourceLayout BuildResourcePatch(EntityWorld& w, const glm::vec3& origin, const std::map<std::string, ResourceNodeDef>& nodes, int workers)
	{
		ResourceLayout out;
		const EntityId root = MakeRoot(w, origin, out);
		const std::vector<std::pair<std::string, int>> counts = PatchCounts(nodes, workers);
		std::map<std::string, std::vector<glm::vec3>> placed;   // where each type went, for the types that stand beside another
		float cursor = 0.0f;                                    // the next free depth (z) of the patch
		for (int pass = 0; pass < 2; ++pass)
			for (const auto& [id, n] : counts)
			{
				const ResourceNodeDef& def = nodes.at(id);
				const ResourcePatchDef& patch = def.Patch;
				const bool beside = !patch.Near.empty() && nodes.count(patch.Near) && patch.Near != id;
				if (beside != (pass == 1)) continue;
				std::vector<glm::vec3> spots;
				if (beside)
				{
					// Sibling nodes (a big tree and its young shoots): each stands beside a node of the other type, on a growing ring if there are more of them.
					const std::vector<glm::vec3> anchors = placed[patch.Near];
					if (anchors.empty()) continue;
					for (int i = 0; i < n; ++i)
					{
						const int turn = i / static_cast<int>(anchors.size());
						const float angle = 0.9f + 2.4f * static_cast<float>(turn), radius = 2.4f + 1.0f * static_cast<float>(turn);
						spots.push_back(anchors[static_cast<size_t>(i) % anchors.size()] + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius);
					}
				}
				else
				{
					const float sp = patch.Spacing;
					float depth = sp;
					if (patch.Shape == "ring")
					{
						const float r = std::max(sp, sp * static_cast<float>(n) / 6.2831853f);
						for (int i = 0; i < n; ++i)
						{
							const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
							spots.push_back({ std::cos(a) * r, 0.0f, cursor + r + std::sin(a) * r });
						}
						depth = 2.0f * r + sp;
					}
					else if (patch.Shape == "cluster")
					{
						// A golden-angle scatter: even, no two on top of each other, the same every time.
						const float reach = sp * 0.8f * std::sqrt(static_cast<float>(n));
						for (int i = 0; i < n; ++i)
						{
							const float a = 2.3999632f * static_cast<float>(i), r = sp * 0.8f * std::sqrt(static_cast<float>(i) + 0.5f);
							spots.push_back({ std::cos(a) * r, 0.0f, cursor + reach + sp + std::sin(a) * r });
						}
						depth = 2.0f * (reach + sp);
					}
					else
					{
						for (int i = 0; i < n; ++i)
						{
							const int line = i / kPerRow, inLine = i % kPerRow, lineCount = std::min(kPerRow, n - line * kPerRow);
							spots.push_back({ (static_cast<float>(inLine) - (lineCount - 1) * 0.5f) * sp, 0.0f, cursor + sp * static_cast<float>(line) });
						}
						depth = sp * static_cast<float>((n + kPerRow - 1) / kPerRow);
					}
					cursor += depth + 3.0f;
				}
				for (int i = 0; i < n; ++i) MakeNode(w, root, def, i + 1, spots[static_cast<size_t>(i)], out);
				placed[id] = spots;
			}
		return out;
	}

	ResourceLayout BuildWorldResources(EntityWorld& w, const std::vector<ResourceSpot>& spots, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		ResourceLayout out;
		const EntityId root = MakeRoot(w, glm::vec3(0.0f), out, "World resources");
		w.Find(root)->Tags.push_back("world");   // generated from the seed, like the groups of trees and rocks: part of the save's baseline, gone with the world
		std::map<std::string, int> next;
		for (const ResourceSpot& s : spots)
		{
			auto it = nodes.find(s.Node);
			if (it == nodes.end()) continue;
			const ResourceNodeDef& def = it->second;
			// a node with a model of the world (a procedural tree or rock) is drawn with it; without one (no world recipes, an old plan) with primitives
			const bool prop = !def.Spawn.Prop.empty() && s.Variant >= 0;
			const EntityId id = MakeNode(w, root, def, ++next[s.Node], s.Position, out, "wild ", !prop);
			w.Find(id)->Transform.Rotation.y = s.Yaw;
			if (!prop) continue;
			const std::string name = w.Find(id)->Name;
			const float k = s.Scale;
			auto addProp = [&](const std::string& partName, PropKind kind, int variant, const glm::vec3& at, float scale)
			{
				const EntityId part = w.Create(partName, id);
				Entity* p = w.Find(part);
				p->Transform.Position = at;
				p->Transform.Scale = glm::vec3(scale);
				p->Prop = PropComponent{ kind, variant };
			};
			if (def.Spawn.Prop == "tree")
			{
				// what is left when it is felled: a stump (the crown, the model, is hidden while the node is empty)
				Part(w, id, name + " Trunk", MeshShape::Cube, { 0.0f, 0.25f * k, 0.0f }, { 0.28f * k, 0.5f * k, 0.28f * k }, { 0.3f, 0.2f, 0.11f });
				addProp(name + " Crown", PropKind::Tree, s.Variant, glm::vec3(0.0f), k);
			}
			else
			{
				addProp(name + " Rock", PropKind::Rock, s.Variant, glm::vec3(0.0f), k);
				for (int i = 1; i <= s.Stack; ++i)   // the rocks piled on top: smaller each, a little to one side so they sit on the ones below
				{
					const float a = 2.4f * static_cast<float>(i), f = std::pow(0.62f, static_cast<float>(i));
					addProp(name + " Rock " + std::to_string(i + 1), PropKind::Rock, s.Variant, glm::vec3(std::cos(a) * 0.35f * k, 0.55f * k * static_cast<float>(i), std::sin(a) * 0.35f * k), k * f);
				}
			}
		}
		return out;
	}

	void SnapToGround(EntityWorld& w, const ResourceLayout& layout, const std::function<float(float, float)>& height)
	{
		const Entity* root = w.Find(w.FindByName(layout.Root));
		if (!root || !height) return;
		const glm::vec3 origin = root->Transform.Position;
		for (const std::string& name : layout.Nodes)
		{
			Entity* e = w.Find(w.FindByName(name));
			if (!e) continue;
			const float x = origin.x + e->Transform.Position.x, z = origin.z + e->Transform.Position.z;
			// the lowest of the centre and four points around it: on a slope the base sinks a little instead of hanging in the air
			float h = height(x, z);
			for (const glm::vec2 d : { glm::vec2(0.8f, 0.0f), glm::vec2(-0.8f, 0.0f), glm::vec2(0.0f, 0.8f), glm::vec2(0.0f, -0.8f) }) h = std::min(h, height(x + d.x, z + d.y));
			e->Transform.Position.y = h - origin.y;
		}
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
				// The tool in the gatherer's hands: its family's power is the attribute SyncTools keeps on whoever carries it.
				int bonus = 0;
				if (e.Instigator && !def->RequiresTag.empty())
					bonus = std::max(0, static_cast<int>(std::lround(e.Instigator->Attributes().Current(PowerAttribute(def->RequiresTag)))));
				const int take = std::min(have, std::max(1, def->PerGather) + bonus);
				if (take <= 0) return;
				node->Attributes().SetBase("Amount", static_cast<float>(have - take));
				if (m_Yield && e.Instigator) m_Yield(e.Instigator->Name(), def->Item, take);
			});
		}
	}

	void ResourceSystem::Update(GameplayWorld& gameplay, float dt, float rate) const
	{
		if (rate <= 0.0f) return;
		for (const std::string& name : gameplay.Names())
		{
			AbilitySystemComponent* node = gameplay.Find(name);
			if (!node || !node->HasTag("Resource.Node") || !node->Attributes().Has("Regrow")) continue;
			const ResourceNodeDef* def = nullptr;
			for (const auto& [id, d] : m_Defs)
				if (node->HasTag("Resource.Node." + id)) { def = &d; break; }
			if (!def || def->RegenSeconds <= 0.0f) continue;
			AttributeSet& at = node->Attributes();
			const int amount = Amount(*node);
			if (amount >= def->Capacity) { if (at.Current("Regrow") != 0.0f) at.SetBase("Regrow", 0.0f); continue; }
			at.AddBase("Regrow", dt * rate);
			if (at.Current("Regrow") >= def->RegenSeconds)
			{
				at.AddBase("Regrow", -def->RegenSeconds);
				at.SetBase("Amount", static_cast<float>(std::min(def->Capacity, amount + std::max(1, def->Regen))));
			}
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
						|| part->Name.find("Tuft") != std::string::npos || part->Name.find(" Rock") != std::string::npos || part->Name.find(" Mound") != std::string::npos
							|| part->Name.find(" Reeds") != std::string::npos || part->Name.find(" Flint") != std::string::npos || part->Name.find(" Ore") != std::string::npos;
					const bool want = !(empty && gatherable);
					if (part->Active != want) part->Active = want;
				}
		}
	}

	std::string ResourceSystem::PowerAttribute(const std::string& toolTag) { return "Power:" + ToolFamily(toolTag); }
	std::string ResourceSystem::SpeedAttribute(const std::string& toolTag) { return "Speed:" + ToolFamily(toolTag); }

	void ResourceSystem::SyncTools(AbilitySystemComponent& who, const Inventory& bag, const ItemLibrary& items)
	{
		// Power and speed of the best tool of each family held ("Item.Tool.Axe" finds a stone axe and a flint axe alike; the flint one wins).
		// Attributes only appear for a family that was ever held, and go back to the defaults (0 and 1) when the last tool leaves.
		std::vector<std::string> families;
		for (const std::string& tag : items.ToolTags())   // (H16: kept by the library; walking every item here cost microseconds per member and frame)
			if (std::find(families.begin(), families.end(), ToolFamily(tag)) == families.end()) families.push_back(ToolFamily(tag));
		for (const std::string& family : families)
		{
			const ItemDef* best = bag.BestTool(items, family);
			const float power = best ? static_cast<float>(best->Power) : 0.0f, speed = best ? best->Speed : 1.0f;
			AttributeSet& at = who.Attributes();
			if (!at.Has(PowerAttribute(family)))
			{
				if (!best) continue;
				at.Define(PowerAttribute(family), 0.0f, 0.0f, 1000.0f);
				at.Define(SpeedAttribute(family), 1.0f, 0.1f, 10.0f);
			}
			if (at.Base(PowerAttribute(family)) != power) at.SetBase(PowerAttribute(family), power);
			if (at.Base(SpeedAttribute(family)) != speed) at.SetBase(SpeedAttribute(family), speed);
		}

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
