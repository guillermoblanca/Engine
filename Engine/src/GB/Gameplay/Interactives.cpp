#include "GB/Gameplay/Interactives.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	namespace
	{
		AbilityDef Use(const char* name, const char* montage, const char* notify, const char* eventTag, const char* targetTag)
		{
			AbilityDef a;
			a.Name = name;
			a.Tag = std::string("Ability.") + name;
			a.NeedsTarget = true;
			a.MoveToTarget = true;
			a.BlockedTags = { "State.Busy", "State.Carrying" };
			a.ActiveTags = { "State.Busy" };
			if (montage[0]) a.Montages = { montage };
			a.Duration = 1.5f;
			a.TargetRequiredTags = { targetTag };
			a.Events.push_back({ notify, 0.5f, eventTag, {}, {} });
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

		void Stand(Entity& e, const glm::vec3& at, float yaw)
		{
			if (!e.Interact) e.Interact.emplace();
			InteractionPoint p;
			p.Name = "work";
			p.Position = at;
			p.Yaw = yaw;
			e.Interact->Points.push_back(p);
		}

		void SetActive(EntityWorld& w, const std::string& name, bool on)
		{
			if (const EntityId id = w.FindByName(name); id != NullEntity)
				if (Entity* e = w.Find(id); e && e->Active != on) e->Active = on;
		}

		void SetState(AbilitySystemComponent& a, std::initializer_list<const char*> all, const char* now)
		{
			for (const char* t : all) if (std::string(t) != now && a.Tags().HasExact(t)) a.Tags().RemoveAll(t);
			if (!a.Tags().HasExact(now)) a.Tags().Add(now);
		}
	}

	void RegisterInteractiveContent(GameplayLibrary& lib)
	{
		AbilityDef stoke = Use("Stoke", "plant", "stoke", "Event.Fuel.Add", "Interactive.Campfire");
		stoke.RequiredTags = { "Item.Resource.Wood" };
		stoke.Note = "puts a piece of wood or a branch in a campfire (needs one in the bag)";
		lib.AddAbility(stoke);
		AbilityDef draw = Use("DrawWater", "pick_fruit", "draw", "Event.Water.Draw", "Interactive.Well");
		draw.TargetMinAttributes = { { "Water", 1.0f } };
		draw.Note = "fills the bag with water from a well";
		lib.AddAbility(draw);
		AbilityDef open = Use("OpenDoor", "pick_fruit", "open", "Event.Door.Open", "Interactive.Door");
		open.TargetBlockedTags = { "State.Open" };
		open.Note = "opens a door";
		lib.AddAbility(open);
		AbilityDef close = Use("CloseDoor", "pick_fruit", "close", "Event.Door.Close", "Interactive.Door");
		close.TargetRequiredTags = { "Interactive.Door", "State.Open" };
		close.Note = "closes a door";
		lib.AddAbility(close);
	}

	InteractiveLayout BuildInteractives(EntityWorld& w, const glm::vec3& origin)
	{
		if (const EntityId old = w.FindByName("Interactives"); old != NullEntity) w.Destroy(old);
		InteractiveLayout out;
		const EntityId root = w.Create("Interactives");
		w.Find(root)->Transform.Position = origin;
		out.Root = w.Find(root)->Name;

		// campfire: a ring of stones and a log; flame and light appear when it burns
		out.Campfire = "Fire Pit";
		{
			const EntityId id = w.Create(out.Campfire, root);
			Entity& e = *w.Find(id);
			e.Transform.Position = { 0.0f, 0.0f, 0.0f };
			Stand(e, { 0.0f, 0.0f, -1.6f }, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Interactive.Campfire", "State.Unlit" };
			e.Gameplay->Attributes = { { "Fuel", 0.0f, 0.0f, 10.0f } };
			for (int i = 0; i < 6; ++i)
			{
				const float a = static_cast<float>(i) * 1.0472f;
				Part(w, id, out.Campfire + " Stone " + std::to_string(i + 1), MeshShape::Sphere, { std::cos(a) * 0.45f, 0.1f, std::sin(a) * 0.45f }, glm::vec3(0.22f), { 0.4f, 0.4f, 0.42f });
			}
			Part(w, id, out.Campfire + " Log", MeshShape::Cube, { 0.0f, 0.12f, 0.0f }, { 0.7f, 0.12f, 0.12f }, { 0.3f, 0.2f, 0.1f });
			const EntityId flame = Part(w, id, out.Campfire + " Flame", MeshShape::Sphere, { 0.0f, 0.4f, 0.0f }, { 0.3f, 0.5f, 0.3f }, { 1.0f, 0.55f, 0.1f });
			w.Find(flame)->Mesh->Emissive = { 1.6f, 0.7f, 0.1f };
			w.Find(flame)->Active = false;
			const EntityId light = w.Create(out.Campfire + " Light", id);
			w.Find(light)->Transform.Position = { 0.0f, 0.8f, 0.0f };
			w.Find(light)->Light.emplace();
			w.Find(light)->Light->Color = { 1.0f, 0.6f, 0.25f };
			w.Find(light)->Light->Intensity = 12.0f;
			w.Find(light)->Light->Radius = 9.0f;
			w.Find(light)->Active = false;
		}

		// well: a ring of stones with a roof; it holds water that comes back
		out.Well = "Well";
		{
			const EntityId id = w.Create(out.Well, root);
			Entity& e = *w.Find(id);
			e.Transform.Position = { 6.0f, 0.0f, 0.0f };
			Stand(e, { 0.0f, 0.0f, -1.7f }, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Interactive.Well" };
			e.Gameplay->Attributes = { { "Water", 6.0f, 0.0f, 6.0f } };
			Part(w, id, out.Well + " Wall", MeshShape::Cube, { 0.0f, 0.4f, 0.0f }, { 1.3f, 0.8f, 1.3f }, { 0.45f, 0.43f, 0.4f });
			Part(w, id, out.Well + " Water", MeshShape::Cube, { 0.0f, 0.82f, 0.0f }, { 0.9f, 0.04f, 0.9f }, { 0.15f, 0.35f, 0.6f });
			Part(w, id, out.Well + " Post A", MeshShape::Cube, { -0.55f, 1.4f, 0.0f }, { 0.1f, 1.2f, 0.1f }, { 0.35f, 0.24f, 0.13f });
			Part(w, id, out.Well + " Post B", MeshShape::Cube, { 0.55f, 1.4f, 0.0f }, { 0.1f, 1.2f, 0.1f }, { 0.35f, 0.24f, 0.13f });
			Part(w, id, out.Well + " Roof", MeshShape::Cube, { 0.0f, 2.05f, 0.0f }, { 1.5f, 0.1f, 1.0f }, { 0.5f, 0.2f, 0.15f });
		}

		// door in a fence: the hinge turns the panel
		out.Door = "Gate";
		{
			const EntityId id = w.Create(out.Door, root);
			Entity& e = *w.Find(id);
			e.Transform.Position = { -6.0f, 0.0f, 0.0f };
			Stand(e, { 0.0f, 0.0f, -1.3f }, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Interactive.Door", "State.Closed" };
			const glm::vec3 wood(0.4f, 0.28f, 0.15f);
			Part(w, id, out.Door + " Post L", MeshShape::Cube, { -0.75f, 0.6f, 0.0f }, { 0.15f, 1.2f, 0.15f }, wood);
			Part(w, id, out.Door + " Post R", MeshShape::Cube, { 0.75f, 0.6f, 0.0f }, { 0.15f, 1.2f, 0.15f }, wood);
			Part(w, id, out.Door + " Fence L", MeshShape::Cube, { -2.2f, 0.5f, 0.0f }, { 2.2f, 0.9f, 0.08f }, wood);
			Part(w, id, out.Door + " Fence R", MeshShape::Cube, { 2.2f, 0.5f, 0.0f }, { 2.2f, 0.9f, 0.08f }, wood);
			const EntityId hinge = w.Create(out.Door + " Hinge", id);
			w.Find(hinge)->Transform.Position = { -0.7f, 0.0f, 0.0f };
			Part(w, hinge, out.Door + " Panel", MeshShape::Cube, { 0.7f, 0.55f, 0.0f }, { 1.4f, 0.9f, 0.08f }, { 0.55f, 0.38f, 0.2f });
		}
		return out;
	}

	void DoorObstacles(const EntityWorld& world, const GameplayWorld& gameplay, std::vector<CircleObstacle>& out)
	{
		for (const std::string& name : gameplay.Names())
		{
			const AbilitySystemComponent* a = gameplay.Find(name);
			if (!a || !a->HasTag("Interactive.Door")) continue;
			const EntityId id = world.FindByName(name);
			if (id == NullEntity || !world.IsActive(id)) continue;
			const glm::mat4 m = world.WorldMatrix(id);
			const float scale = glm::length(glm::vec3(m[0]));
			auto at = [&](float x, float radius) { const glm::vec4 p = m * glm::vec4(x, 0.0f, 0.0f, 1.0f); out.push_back({ { p.x, p.z }, radius * scale }); };
			for (float x = 1.2f; x <= 3.2f; x += 0.5f) { at(-x, 0.15f); at(x, 0.15f); }   // the fence either side of the opening (about 1.1 to 3.3 m from the middle)
			if (InteractiveSystem::DoorState(*a) == "closed")
				for (float x : { -0.6f, -0.2f, 0.2f, 0.6f }) at(x, 0.25f);                  // the leaf
		}
	}

	void InteractiveSystem::Bind(GameplayWorld& gameplay, Take take, Give give)
	{
		m_Gameplay = &gameplay;
		m_Take = std::move(take);
		m_Give = std::move(give);
		m_Burn.clear();
		for (const std::string& name : gameplay.Names())
		{
			AbilitySystemComponent* a = gameplay.Find(name);
			if (a->HasTag("Interactive.Campfire"))
				a->AddListener("Event.Fuel", [this, a](const GameplayEventData& e)
				{
					if (e.Tag != "Event.Fuel.Add" || !e.Instigator || !m_Take) return;
					const int fuel = TakeFuel(e.Instigator->Name());
					if (fuel > 0) a->Attributes().AddBase("Fuel", static_cast<float>(fuel));
				});
			if (a->HasTag("Interactive.Well"))
				a->AddListener("Event.Water", [this, a](const GameplayEventData& e)
				{
					if (e.Tag != "Event.Water.Draw" || !e.Instigator || !m_Give) return;
					const int have = static_cast<int>(std::lround(a->Attributes().Current("Water")));
					const int give = std::min(have, Settings.WaterPerDraw);
					if (give <= 0) return;
					const int got = m_Give(e.Instigator->Name(), "water", give);
					a->Attributes().AddBase("Water", -static_cast<float>(got));
				});
			if (a->HasTag("Interactive.Door"))
				a->AddListener("Event.Door", [a](const GameplayEventData& e)
				{
					if (e.Tag == "Event.Door.Open") SetState(*a, { "State.Open", "State.Closed" }, "State.Open");
					else if (e.Tag == "Event.Door.Close") SetState(*a, { "State.Open", "State.Closed" }, "State.Closed");
				});
		}
	}

	int InteractiveSystem::FuelOf(const std::string& item) const
	{
		const ItemDef* def = Items ? Items->Find(item) : nullptr;
		return def && def->Fuel > 0 ? def->Fuel : Settings.FuelPerWood;
	}

	int InteractiveSystem::TakeFuel(const std::string& actor) const
	{
		if (!Items) return m_Take(actor, "wood", 1) > 0 ? Settings.FuelPerWood : 0;
		// Anything tagged Item.Resource.Wood burns (a branch included); the piece with the most heat goes in first, then the lowest id.
		std::vector<std::pair<int, std::string>> burnable;
		for (const std::string& id : Items->Ids())
			for (const std::string& tag : Items->Find(id)->Tags)
				if (tag == "Item.Resource.Wood" || tag.rfind("Item.Resource.Wood.", 0) == 0) { burnable.push_back({ FuelOf(id), id }); break; }
		std::sort(burnable.begin(), burnable.end(), [](const auto& l, const auto& r) { return l.first != r.first ? l.first > r.first : l.second < r.second; });
		for (const auto& [fuel, id] : burnable)
			if (m_Take(actor, id, 1) > 0) return fuel;
		return 0;
	}

	void InteractiveSystem::Update(float dt, EntityWorld* entities)
	{
		if (!m_Gameplay) return;
		auto timer = [this](const std::string& key) -> float&
		{
			for (auto& t : m_Burn) if (t.first == key) return t.second;
			m_Burn.push_back({ key, 0.0f });
			return m_Burn.back().second;
		};
		for (const std::string& name : m_Gameplay->Names())
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			if (a->HasTag("Interactive.Campfire"))
			{
				AttributeSet& at = a->Attributes();
				float& t = timer(name);
				if (at.Current("Fuel") >= 1.0f)
				{
					t += dt;
					if (t >= Settings.BurnSeconds) { t -= Settings.BurnSeconds; at.AddBase("Fuel", -1.0f); }
				}
				else t = 0.0f;
				const bool burning = at.Current("Fuel") >= 1.0f;
				const std::string now = CampfireState(*a);
				if (burning) SetState(*a, { "State.Unlit", "State.Lit", "State.Out" }, "State.Lit");
				else if (now == "lit") SetState(*a, { "State.Unlit", "State.Lit", "State.Out" }, "State.Out");
				if (entities) { SetActive(*entities, name + " Flame", burning); SetActive(*entities, name + " Light", burning); }
			}
			else if (a->HasTag("Interactive.Well"))
			{
				AttributeSet& at = a->Attributes();
				float& t = timer(name);
				if (at.Current("Water") < 6.0f)
				{
					t += dt;
					if (t >= 20.0f) { t -= 20.0f; at.AddBase("Water", 1.0f); }
				}
				else t = 0.0f;
			}
			else if (a->HasTag("Interactive.Door") && entities)
			{
				if (const EntityId hinge = entities->FindByName(name + " Hinge"); hinge != NullEntity)
				{
					Entity* h = entities->Find(hinge);
					const float want = a->HasTag("State.Open") ? 100.0f : 0.0f;
					if (std::fabs(h->Transform.Rotation.y - want) > 0.01f)
					{
						const float step = 240.0f * dt;   // a quarter turn in under half a second
						h->Transform.Rotation.y += std::max(-step, std::min(step, want - h->Transform.Rotation.y));
					}
				}
			}
		}
	}

	std::string InteractiveSystem::ToJson() const
	{
		using Json = nlohmann::json;
		Json timers = Json::array();
		for (const auto& [name, t] : m_Burn) timers.push_back(Json::array({ name, t }));
		return Json{ { "timers", timers } }.dump();
	}

	bool InteractiveSystem::FromJson(const std::string& text)
	{
		using Json = nlohmann::json;
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		std::vector<std::pair<std::string, float>> timers;
		try
		{
			for (const Json& t : j.at("timers")) timers.push_back({ t.at(0).get<std::string>(), t.at(1).get<float>() });
		}
		catch (const Json::exception&)
		{
			return false;
		}
		m_Burn = std::move(timers);
		return true;
	}

	std::string InteractiveSystem::CampfireState(const AbilitySystemComponent& a)
	{
		if (!a.HasTag("Interactive.Campfire")) return {};
		return a.HasTag("State.Lit") ? "lit" : a.HasTag("State.Out") ? "out" : "unlit";
	}

	std::string InteractiveSystem::DoorState(const AbilitySystemComponent& a)
	{
		if (!a.HasTag("Interactive.Door")) return {};
		return a.HasTag("State.Open") ? "open" : "closed";
	}
}
