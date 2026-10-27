#include "GB/Gameplay/Garden.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		const char* const kSoilTags[] = { "State.Soil.Grass", "State.Soil.Dug", "State.Soil.Planted.Sown", "State.Soil.Planted.Sprout", "State.Soil.Planted.Grown" };
		const char* const kTreeTags[] = { "State.Tree.Empty", "State.Tree.Sapling", "State.Tree.Grown" };

		void SetSoil(AbilitySystemComponent& plot, const char* state)
		{
			for (const char* t : kSoilTags) plot.Tags().RemoveAll(t);
			plot.Tags().Add(state);
		}

		void SetTree(AbilitySystemComponent& tree, const char* state)
		{
			for (const char* t : kTreeTags) tree.Tags().RemoveAll(t);
			tree.Tags().Add(state);
		}

		GameplayEffectDef Instant(const char* name, const char* attribute, float amount)
		{
			GameplayEffectDef e;
			e.Name = name;
			e.Policy = EffectPolicy::Instant;
			e.Modifiers.push_back({ attribute, ModOp::Add, amount });
			return e;
		}

		GameplayEffectDef Regrow(const char* name, const char* attribute, float seconds, float amount = 1.0f)
		{
			GameplayEffectDef e;
			e.Name = name;
			e.Policy = EffectPolicy::Infinite;
			e.Period = seconds;
			e.Modifiers.push_back({ attribute, ModOp::Add, amount });
			return e;
		}

		GameplayEffectDef Carrying(const char* name, const char* tag)
		{
			GameplayEffectDef e;
			e.Name = name;
			e.Policy = EffectPolicy::Infinite;
			e.GrantedTags = { tag };
			return e;
		}

		GameplayEffectDef Cost(const char* name, std::vector<std::pair<const char*, float>> spend)
		{
			GameplayEffectDef e;
			e.Name = name;
			e.Policy = EffectPolicy::Instant;
			for (const auto& [attr, amount] : spend) e.Modifiers.push_back({ attr, ModOp::Add, -amount });
			return e;
		}

		// The ability every "work on a target" action shares: walk to its point, play a montage, one event at the notify.
		AbilityDef Work(const char* name, const char* montage, const char* notify, float notifyTime, const char* eventTag, float duration)
		{
			AbilityDef a;
			a.Name = name;
			a.Tag = std::string("Ability.") + name;
			a.NeedsTarget = true;
			a.MoveToTarget = true;
			a.BlockedTags = { "State.Busy", "State.Carrying" };
			a.ActiveTags = { "State.Busy" };
			if (montage[0]) a.Montages = { montage };
			a.Duration = duration;
			a.Events.push_back({ notify, notifyTime, eventTag, {}, {} });
			a.CooldownSeconds = 0.5f;
			return a;
		}
	}

	void RegisterGardenContent(GameplayLibrary& lib)
	{
		// effects
		GameplayEffectDef regen;
		regen.Name = "EnergyRegen";
		regen.Policy = EffectPolicy::Infinite;
		regen.Period = 1.0f;
		regen.Modifiers.push_back({ "Energy", ModOp::Add, 1.0f });
		lib.AddEffect(regen);
		lib.AddEffect(Regrow("FruitRegrow", "Fruit", 8.0f));
		lib.AddEffect(Regrow("MilkRegrow", "Milk", 10.0f));
		lib.AddEffect(Instant("Soak", "Moisture", 1.0f));
		lib.AddEffect(Instant("Gain.Fruit", "Fruit", 1.0f));
		lib.AddEffect(Instant("Lose.Fruit", "Fruit", -1.0f));
		lib.AddEffect(Instant("Gain.Crop", "Crops", 1.0f));
		lib.AddEffect(Instant("Gain.Milk", "Milk", 1.0f));
		lib.AddEffect(Instant("Lose.Milk", "Milk", -1.0f));
		lib.AddEffect(Instant("Gain.Compost", "Compost", 1.0f));
		lib.AddEffect(Carrying("Carrying.Box", "State.Carrying.Box"));
		lib.AddEffect(Carrying("Carrying.Wheelbarrow", "State.Carrying.Wheelbarrow"));
		lib.AddEffect(Cost("Cost.Dig", { { "Energy", 5.0f } }));
		lib.AddEffect(Cost("Cost.Sow", { { "Energy", 2.0f }, { "Seeds", 1.0f } }));
		lib.AddEffect(Cost("Cost.Water", { { "Energy", 1.0f }, { "Water", 1.0f } }));
		lib.AddEffect(Cost("Cost.PlantTree", { { "Energy", 8.0f }, { "Saplings", 1.0f } }));
		lib.AddEffect(Cost("Cost.Pick", { { "Energy", 2.0f } }));
		lib.AddEffect(Cost("Cost.Pull", { { "Energy", 4.0f } }));
		lib.AddEffect(Cost("Cost.Milk", { { "Energy", 3.0f } }));
		lib.AddEffect(Cost("Cost.Dump", { { "Crops", 1.0f } }));

		// abilities. The montage names are those of MixamoMontages(); where the pack has no clip for the action, the closest one is used (see
		// docs/ANIMACIONES_PENDIENTES.md).
		AbilityDef dig = Work("Dig", "dig_plant", "dig", 0.6f, "Event.Soil.Dig", 2.5f);
		dig.TargetRequiredTags = { "State.Soil.Grass" };
		dig.Cost = "Cost.Dig";
		dig.EquipProp = "shovel";
		dig.Note = "digs a plot of grass";
		lib.AddAbility(dig);

		AbilityDef sow = Work("Sow", "plant", "sow", 0.5f, "Event.Soil.Sow", 2.0f);
		sow.TargetRequiredTags = { "State.Soil.Dug" };
		sow.Cost = "Cost.Sow";
		sow.EquipProp = "seeds";
		sow.Note = "sows seeds in a dug plot";
		lib.AddAbility(sow);

		AbilityDef water = Work("Water", "water", "pour", 0.5f, "Event.Plant.Water", 2.5f);
		water.TargetRequiredAnyTags = { "State.Soil.Planted", "State.Tree.Sapling" };
		water.Events[0].TargetEffects = { "Soak" };
		water.Cost = "Cost.Water";
		water.EquipProp = "can";
		water.Note = "waters a sown plot or a sapling";
		lib.AddAbility(water);

		AbilityDef tree = Work("PlantTree", "plant_tree", "plant", 0.6f, "Event.Tree.Plant", 3.0f);
		tree.TargetRequiredTags = { "State.Tree.Empty" };
		tree.Cost = "Cost.PlantTree";
		tree.EquipProp = "shovel";
		tree.WaitEvent = "Event.Tree.Rooted";
		tree.Note = "plants a sapling in the tree spot";
		lib.AddAbility(tree);

		AbilityDef pick = Work("PickFruit", "pick_fruit", "pick", 0.5f, "Event.Fruit.Pick", 2.0f);
		pick.Montages = { "pick_fruit", "pick_fruit_2", "pick_fruit_3" };
		pick.TargetRequiredTags = { "State.Tree.Grown" };
		pick.TargetMinAttributes = { { "Fruit", 1.0f } };
		pick.Events[0].SelfEffects = { "Gain.Fruit" };
		pick.Events[0].TargetEffects = { "Lose.Fruit" };
		pick.Cost = "Cost.Pick";
		pick.Note = "picks a fruit (three animations in turn)";
		lib.AddAbility(pick);

		AbilityDef pull = Work("Pull", "pull_plant", "pull", 0.6f, "Event.Soil.Harvest", 2.5f);
		pull.Montages = { "pull_plant", "pull_plant_2" };
		pull.TargetRequiredTags = { "State.Soil.Planted.Grown" };
		pull.Events[0].SelfEffects = { "Gain.Crop" };
		pull.Cost = "Cost.Pull";
		pull.Note = "pulls a grown plant out of its plot";
		lib.AddAbility(pull);

		AbilityDef milk = Work("Milk", "milk_cow", "milk", 0.5f, "Event.Cow.Milk", 3.0f);
		milk.TargetRequiredTags = { "State.Cow" };
		milk.TargetMinAttributes = { { "Milk", 1.0f } };
		milk.Events[0].SelfEffects = { "Gain.Milk" };
		milk.Events[0].TargetEffects = { "Lose.Milk" };
		milk.Cost = "Cost.Milk";
		milk.Note = "milks the cow";
		lib.AddAbility(milk);

		AbilityDef dump = Work("DumpWheelbarrow", "wheelbarrow_dump", "dump", 0.6f, "Event.Compost.Dump", 3.0f);
		dump.BlockedTags = { "State.Busy" };
		dump.RequiredTags = { "State.Carrying.Wheelbarrow" };
		dump.TargetRequiredTags = { "Garden.Compost" };
		dump.Events[0].TargetEffects = { "Gain.Compost" };
		dump.Cost = "Cost.Dump";
		dump.Note = "tips a crop out of the wheelbarrow onto the compost";
		lib.AddAbility(dump);

		// carrying: no animation of their own (the locomotion set changes); the state is an endless effect that grants the tag
		auto carry = [&](const char* name, const char* prop, const char* effect)
		{
			AbilityDef a;
			a.Name = name;
			a.Tag = std::string("Ability.") + name;
			a.BlockedTags = { "State.Busy", "State.Carrying" };
			a.ActivationEffects = { effect };
			a.EquipProp = prop;
			a.KeepProp = true;
			lib.AddAbility(a);
		};
		auto release = [&](const char* name, const char* prop, const char* effect, const char* required)
		{
			AbilityDef a;
			a.Name = name;
			a.Tag = std::string("Ability.") + name;
			a.RequiredTags = { required };
			a.BlockedTags = { "State.Busy" };
			a.RemoveEffects = { effect };
			a.UnequipProp = prop;
			lib.AddAbility(a);
		};
		carry("CarryBox", "box", "Carrying.Box");
		release("DropBox", "box", "Carrying.Box", "State.Carrying.Box");
		carry("PushWheelbarrow", "wheelbarrow", "Carrying.Wheelbarrow");
		release("ReleaseWheelbarrow", "wheelbarrow", "Carrying.Wheelbarrow", "State.Carrying.Wheelbarrow");
	}

	// ---- the scene

	namespace
	{
		EntityId Part(EntityWorld& w, EntityId parent, const std::string& name, MeshShape shape, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& color,
			float roughness = 0.85f)
		{
			const EntityId id = w.Create(name, parent);
			Entity* e = w.Find(id);
			e->Transform.Position = pos;
			e->Transform.Scale = scale;
			e->Mesh.emplace();
			e->Mesh->Shape = shape;
			e->Mesh->Size = 1.0f;
			e->Mesh->Albedo = color;
			e->Mesh->Roughness = roughness;
			return id;
		}

		void Stand(Entity& e, const char* name, const glm::vec3& at, float yawDegrees)
		{
			if (!e.Interact) e.Interact.emplace();
			InteractionPoint p;
			p.Name = name;
			p.Position = at;
			p.Yaw = yawDegrees;
			e.Interact->Points.push_back(p);
		}

		void Static(Entity& e, const glm::vec3& half)
		{
			e.Body.emplace();
			e.Body->Type = BodyType::Static;
			e.Body->Shape = ColliderShape::Box;
			e.Body->HalfExtents = half;
		}

		GameplayComponent Actor()
		{
			GameplayComponent g;
			g.Attributes = { { "Energy", 100.0f, 0.0f, 100.0f }, { "Water", 10.0f, 0.0f, 10.0f }, { "Seeds", 6.0f, 0.0f, 99.0f }, { "Saplings", 2.0f, 0.0f, 9.0f },
				{ "Fruit", 0.0f, 0.0f, 99.0f }, { "Crops", 0.0f, 0.0f, 99.0f }, { "Milk", 0.0f, 0.0f, 99.0f } };
			g.Effects = { "EnergyRegen" };
			for (const char* a : { "Dig", "Sow", "Water", "PlantTree", "PickFruit", "Pull", "Milk", "CarryBox", "DropBox", "PushWheelbarrow", "ReleaseWheelbarrow", "DumpWheelbarrow" })
				g.Abilities.push_back(a);
			return g;
		}

		void TreeParts(EntityWorld& w, EntityId tree, const std::string& name)
		{
			const glm::vec3 bark(0.36f, 0.24f, 0.13f), leaf(0.2f, 0.45f, 0.16f);
			Part(w, tree, name + " Sapling", MeshShape::Cube, { 0.0f, 0.35f, 0.0f }, { 0.06f, 0.7f, 0.06f }, bark);
			Part(w, tree, name + " Trunk", MeshShape::Cube, { 0.0f, 1.2f, 0.0f }, { 0.35f, 2.4f, 0.35f }, bark);
			Part(w, tree, name + " Crown", MeshShape::Sphere, { 0.0f, 3.0f, 0.0f }, { 2.6f, 2.2f, 2.6f }, leaf);
			const glm::vec3 spots[3] = { { 0.9f, 2.7f, 0.5f }, { -0.7f, 3.0f, 0.9f }, { 0.2f, 2.4f, -1.0f } };
			for (int i = 0; i < 3; ++i)
			{
				const EntityId f = Part(w, tree, name + " Fruit " + std::to_string(i + 1), MeshShape::Sphere, spots[i], glm::vec3(0.22f), { 0.85f, 0.15f, 0.1f }, 0.4f);
				w.Find(f)->Mesh->Emissive = { 0.05f, 0.0f, 0.0f };
			}
		}
	}

	GardenLayout BuildGarden(EntityWorld& w, const glm::vec3& origin)
	{
		if (const EntityId old = w.FindByName("Garden"); old != NullEntity) w.Destroy(old);
		for (const char* n : { "Gardener", "Farmhand" })
			if (const EntityId old = w.FindByName(n); old != NullEntity) w.Destroy(old);
		GardenLayout out;
		const EntityId root = w.Create("Garden");
		w.Find(root)->Transform.Position = origin;
		out.Root = w.Find(root)->Name;
		out.Center = origin;

		// the plots: 3 x 2, a gap between rows to stand in
		const glm::vec3 grass(0.26f, 0.5f, 0.2f);
		int n = 0;
		for (int r = 0; r < 2; ++r)
			for (int c = 0; c < 3; ++c)
			{
				const std::string name = "Plot " + std::to_string(++n);
				const EntityId id = w.Create(name, root);
				Entity& e = *w.Find(id);
				e.Transform.Position = { (c - 1) * 2.4f, 0.0f, r * 3.2f };
				Part(w, id, name + " Soil", MeshShape::Cube, { 0.0f, 0.06f, 0.0f }, { 1.8f, 0.12f, 1.8f }, grass);
				Stand(e, "work", { 0.0f, 0.0f, -1.5f }, 0.0f);
				e.Tags = { "plot" };
				e.Gameplay.emplace();
				e.Gameplay->Tags = { "Garden.Plot", "State.Soil.Grass" };
				e.Gameplay->Attributes = { { "Moisture", 0.0f, 0.0f, 1.0f }, { "Growth", 0.0f, 0.0f, 2.0f } };
				Part(w, id, name + " Sprout", MeshShape::Sphere, { 0.0f, 0.2f, 0.0f }, { 0.25f, 0.2f, 0.25f }, { 0.4f, 0.7f, 0.2f });
				Part(w, id, name + " Stem", MeshShape::Cube, { 0.0f, 0.4f, 0.0f }, { 0.07f, 0.6f, 0.07f }, { 0.25f, 0.5f, 0.15f });
				Part(w, id, name + " Leaves", MeshShape::Sphere, { 0.0f, 0.75f, 0.0f }, { 0.9f, 0.5f, 0.9f }, { 0.2f, 0.55f, 0.15f });
				out.Plots.push_back(e.Name);
			}

		// the tree spot (empty until a sapling is planted) and a tree with fruit
		const glm::vec3 mound(0.3f, 0.2f, 0.1f);
		const EntityId spot = w.Create("Tree Spot", root);
		{
			Entity& e = *w.Find(spot);
			e.Transform.Position = { -5.0f, 0.0f, 1.6f };
			Part(w, spot, "Tree Spot Mound", MeshShape::Cube, { 0.0f, 0.04f, 0.0f }, { 1.4f, 0.08f, 1.4f }, mound);
			Stand(e, "work", { 0.0f, 0.0f, -1.6f }, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Garden.Tree", "State.Tree.Empty" };
			e.Gameplay->Attributes = { { "Moisture", 0.0f, 0.0f, 1.0f }, { "Growth", 0.0f, 0.0f, 1.0f }, { "Fruit", 0.0f, 0.0f, 3.0f } };
			TreeParts(w, spot, "Tree Spot");
			out.TreeSpot = e.Name;
		}
		const EntityId fruitTree = w.Create("Fruit Tree", root);
		{
			Entity& e = *w.Find(fruitTree);
			e.Transform.Position = { 5.5f, 0.0f, 1.6f };
			Part(w, fruitTree, "Fruit Tree Mound", MeshShape::Cube, { 0.0f, 0.04f, 0.0f }, { 1.4f, 0.08f, 1.4f }, mound);
			Stand(e, "work", { 0.0f, 0.0f, -1.8f }, 0.0f);
			Static(e, { 0.4f, 1.0f, 0.4f });
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Garden.Tree", "State.Tree.Grown" };
			e.Gameplay->Attributes = { { "Moisture", 0.0f, 0.0f, 1.0f }, { "Growth", 1.0f, 0.0f, 1.0f }, { "Fruit", 2.0f, 0.0f, 3.0f } };
			e.Gameplay->Effects = { "FruitRegrow" };
			TreeParts(w, fruitTree, "Fruit Tree");
			out.FruitTree = e.Name;
		}

		// a cow
		const EntityId cow = w.Create("Cow", root);
		{
			Entity& e = *w.Find(cow);
			e.Transform.Position = { -3.5f, 0.0f, 7.0f };
			Stand(e, "milk", { -1.5f, 0.0f, 0.0f }, 90.0f);   // beside it (its body runs along Z), looking at its flank
			Static(e, { 0.5f, 0.6f, 0.9f });
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Garden.Cow", "State.Cow" };
			e.Gameplay->Attributes = { { "Milk", 2.0f, 0.0f, 3.0f } };
			e.Gameplay->Effects = { "MilkRegrow" };
			const glm::vec3 white(0.92f, 0.9f, 0.86f), black(0.12f, 0.1f, 0.1f);
			Part(w, cow, "Cow Body", MeshShape::Cube, { 0.0f, 0.95f, 0.0f }, { 0.7f, 0.65f, 1.5f }, white);
			Part(w, cow, "Cow Patch", MeshShape::Cube, { 0.0f, 1.0f, 0.2f }, { 0.72f, 0.5f, 0.5f }, black);
			Part(w, cow, "Cow Head", MeshShape::Cube, { 0.0f, 1.25f, 0.95f }, { 0.4f, 0.4f, 0.45f }, white);
			for (int i = 0; i < 4; ++i)
				Part(w, cow, "Cow Leg " + std::to_string(i + 1), MeshShape::Cube, { (i % 2 ? 0.25f : -0.25f), 0.3f, (i < 2 ? 0.55f : -0.55f) }, { 0.14f, 0.6f, 0.14f }, white);
			out.Cow = e.Name;
		}

		// the compost heap and the wheelbarrow
		const EntityId compost = w.Create("Compost", root);
		{
			Entity& e = *w.Find(compost);
			e.Transform.Position = { 4.5f, 0.0f, 7.0f };
			Part(w, compost, "Compost Heap", MeshShape::Sphere, { 0.0f, 0.2f, 0.0f }, { 1.6f, 0.7f, 1.6f }, { 0.22f, 0.15f, 0.08f }, 1.0f);
			Stand(e, "work", { 0.0f, 0.0f, -1.7f }, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Garden.Compost" };
			e.Gameplay->Attributes = { { "Compost", 0.0f, 0.0f, 99.0f } };
			out.Compost = e.Name;
		}
		const EntityId barrow = w.Create("Wheelbarrow", root);
		w.Find(barrow)->Transform.Position = { 2.0f, 0.0f, -4.0f };
		w.Find(barrow)->Transform.Rotation = { 0.0f, 20.0f, 0.0f };
		Part(w, barrow, "Wheelbarrow Tray", MeshShape::Cube, { 0.0f, 0.55f, 0.0f }, { 0.7f, 0.3f, 0.9f }, { 0.15f, 0.4f, 0.2f });
		Part(w, barrow, "Wheelbarrow Wheel", MeshShape::Sphere, { 0.0f, 0.25f, 0.6f }, { 0.12f, 0.5f, 0.5f }, { 0.08f, 0.08f, 0.08f });
		Part(w, barrow, "Wheelbarrow Handle", MeshShape::Cube, { 0.3f, 0.6f, -0.9f }, { 0.05f, 0.05f, 0.9f }, { 0.5f, 0.35f, 0.2f });
		Part(w, barrow, "Wheelbarrow Handle 2", MeshShape::Cube, { -0.3f, 0.6f, -0.9f }, { 0.05f, 0.05f, 0.9f }, { 0.5f, 0.35f, 0.2f });
		Static(*w.Find(barrow), { 0.4f, 0.4f, 0.8f });

		// boxes
		for (int i = 0; i < 3; ++i)
		{
			const EntityId b = Part(w, root, "Box " + std::to_string(i + 1), MeshShape::Cube, { -5.0f + i * 0.9f, 0.2f, -4.0f - (i == 1 ? 0.3f : 0.0f) }, { 0.45f, 0.4f, 0.4f }, { 0.62f, 0.44f, 0.26f });
			Entity& e = *w.Find(b);
			e.Tags = { "box" };
			Static(e, { 0.23f, 0.2f, 0.2f });
			out.Boxes.push_back(e.Name);
		}

		// tools lying around: seed tray, watering can, shovel
		const EntityId tray = w.Create("Seed Tray", root);
		w.Find(tray)->Transform.Position = { 0.6f, 0.0f, -3.8f };
		Part(w, tray, "Seed Tray Base", MeshShape::Cube, { 0.0f, 0.05f, 0.0f }, { 0.5f, 0.1f, 0.3f }, { 0.4f, 0.3f, 0.2f });
		for (int i = 0; i < 4; ++i)
			Part(w, tray, "Seed Tray Seed " + std::to_string(i + 1), MeshShape::Sphere, { -0.18f + i * 0.12f, 0.12f, 0.0f }, { 0.06f, 0.05f, 0.08f }, { 0.75f, 0.65f, 0.3f });
		const EntityId can = w.Create("Watering Can", root);
		w.Find(can)->Transform.Position = { 1.2f, 0.0f, -3.6f };
		Part(w, can, "Watering Can Body", MeshShape::Cube, { 0.0f, 0.18f, 0.0f }, { 0.25f, 0.3f, 0.25f }, { 0.2f, 0.4f, 0.65f }, 0.4f);
		Part(w, can, "Watering Can Spout", MeshShape::Cube, { 0.0f, 0.3f, 0.25f }, { 0.05f, 0.05f, 0.3f }, { 0.2f, 0.4f, 0.65f }, 0.4f);
		const EntityId shovel = w.Create("Shovel", root);
		w.Find(shovel)->Transform.Position = { -0.5f, 0.0f, -3.7f };
		Part(w, shovel, "Shovel Handle", MeshShape::Cube, { 0.0f, 0.04f, 0.0f }, { 0.05f, 0.05f, 0.9f }, { 0.5f, 0.35f, 0.2f });
		Part(w, shovel, "Shovel Blade", MeshShape::Cube, { 0.0f, 0.04f, 0.55f }, { 0.18f, 0.03f, 0.25f }, { 0.55f, 0.57f, 0.6f }, 0.4f);

		// the two characters: abilities and numbers only (the player's model and an NPC's are the host's business)
		for (const char* who : { "Gardener", "Farmhand" })
		{
			const EntityId id = w.Create(who, root);
			w.Find(id)->Gameplay = Actor();
			(std::string(who) == "Gardener" ? out.Player : out.Npc) = w.Find(id)->Name;
		}
		return out;
	}

	// ---- the rules

	std::string GardenSystem::SoilState(const AbilitySystemComponent& p)
	{
		if (!p.HasTag("Garden.Plot")) return "";
		if (p.HasTag("State.Soil.Planted.Grown")) return "grown";
		if (p.HasTag("State.Soil.Planted.Sprout")) return "sprout";
		if (p.HasTag("State.Soil.Planted.Sown")) return "sown";
		if (p.HasTag("State.Soil.Dug")) return "dug";
		return "grass";
	}

	std::string GardenSystem::TreeState(const AbilitySystemComponent& t)
	{
		if (!t.HasTag("Garden.Tree")) return "";
		if (t.HasTag("State.Tree.Grown")) return "grown";
		if (t.HasTag("State.Tree.Sapling")) return "sapling";
		return "empty";
	}

	void GardenSystem::Bind(GameplayWorld& gameplay, EntityWorld* entities)
	{
		m_Gameplay = &gameplay;
		m_Entities = entities;
		for (const std::string& name : gameplay.Names())
		{
			AbilitySystemComponent* a = gameplay.Find(name);
			if (a->HasTag("Garden.Plot"))
			{
				a->AddListener("Event.Soil", [a](const GameplayEventData& e)
				{
					if (e.Tag == "Event.Soil.Dig") { SetSoil(*a, "State.Soil.Dug"); a->Attributes().SetBase("Growth", 0.0f); a->Attributes().SetBase("Moisture", 0.0f); }
					else if (e.Tag == "Event.Soil.Sow") { SetSoil(*a, "State.Soil.Planted.Sown"); a->Attributes().SetBase("Growth", 0.0f); }
					else if (e.Tag == "Event.Soil.Harvest") { SetSoil(*a, "State.Soil.Dug"); a->Attributes().SetBase("Growth", 0.0f); a->Attributes().SetBase("Moisture", 0.0f); }
				});
			}
			if (a->HasTag("Garden.Tree"))
			{
				a->AddListener("Event.Tree", [a](const GameplayEventData& e)
				{
					if (e.Tag != "Event.Tree.Plant") return;
					SetTree(*a, "State.Tree.Sapling");
					a->Attributes().SetBase("Growth", 0.0f);
					if (e.Instigator) e.Instigator->SendEvent({ "Event.Tree.Rooted", a, e.Instigator });
				});
			}
		}
	}

	void GardenSystem::Update(float dt)
	{
		if (!m_Gameplay) return;
		for (const std::string& name : m_Gameplay->Names())
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			const bool plot = a->HasTag("Garden.Plot"), tree = a->HasTag("Garden.Tree");
			if (!plot && !tree) continue;
			AttributeSet& at = a->Attributes();
			const bool wet = at.Current("Moisture") > 0.0f;
			if (wet && !a->HasTag("State.Wet")) a->Tags().Add("State.Wet");
			if (!wet && a->HasTag("State.Wet")) a->Tags().RemoveAll("State.Wet");
			if (!wet) continue;
			at.AddBase("Moisture", -dt / Config.MoistureSeconds);
			if (plot && a->HasTag("State.Soil.Planted"))
			{
				at.AddBase("Growth", dt / Config.StageSeconds);
				if (at.Current("Growth") >= 2.0f - 1e-5f) SetSoil(*a, "State.Soil.Planted.Grown");
				else if (at.Current("Growth") >= 1.0f - 1e-5f && a->HasTag("State.Soil.Planted.Sown")) SetSoil(*a, "State.Soil.Planted.Sprout");
			}
			else if (tree && a->HasTag("State.Tree.Sapling"))
			{
				at.AddBase("Growth", dt / Config.TreeSeconds);
				if (at.Current("Growth") >= 1.0f - 1e-5f)
				{
					SetTree(*a, "State.Tree.Grown");
					at.SetBase("Fruit", 1.0f);
					a->ApplyEffect("FruitRegrow");
				}
			}
		}
		SyncVisuals();
	}

	void GardenSystem::SyncVisuals()
	{
		if (!m_Gameplay || !m_Entities) return;
		auto show = [this](const std::string& name, bool on)
		{
			if (Entity* e = m_Entities->Find(m_Entities->FindByName(name)); e && e->Active != on) e->Active = on;
		};
		for (const std::string& name : m_Gameplay->Names())
		{
			const AbilitySystemComponent* a = m_Gameplay->Find(name);
			const std::string soil = SoilState(*a), tree = TreeState(*a);
			if (!soil.empty())
			{
				show(name + " Sprout", soil == "sprout");
				show(name + " Stem", soil == "grown");
				show(name + " Leaves", soil == "grown");
				Entity* e = m_Entities->Find(m_Entities->FindByName(name + " Soil"));
				if (e && e->Mesh)
				{
					const bool wet = a->HasTag("State.Wet");
					glm::vec3 c = soil == "grass" ? glm::vec3(0.26f, 0.5f, 0.2f) : wet ? glm::vec3(0.16f, 0.1f, 0.05f) : glm::vec3(0.3f, 0.19f, 0.1f);
					if (e->Mesh->Albedo != c) e->Mesh->Albedo = c;
				}
			}
			if (!tree.empty())
			{
				show(name + " Sapling", tree == "sapling");
				show(name + " Trunk", tree == "grown");
				show(name + " Crown", tree == "grown");
				const float fruit = a->Attribute("Fruit");
				for (int i = 1; i <= 3; ++i) show(name + " Fruit " + std::to_string(i), tree == "grown" && fruit >= static_cast<float>(i) - 1e-4f);
			}
		}
	}
}
