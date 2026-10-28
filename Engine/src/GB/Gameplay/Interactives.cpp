#include "GB/Gameplay/Interactives.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	namespace
	{
		using Json = nlohmann::json;

		AbilityDef Use(const InteractiveAbility& u, const std::string& kindTag)
		{
			AbilityDef a;
			a.Name = u.Name;
			a.Tag = "Ability." + u.Name;
			a.NeedsTarget = true;
			a.MoveToTarget = true;
			a.BlockedTags = { "State.Busy", "State.Carrying" };
			a.ActiveTags = { "State.Busy" };
			if (!u.Montage.empty()) a.Montages = { u.Montage };
			a.Duration = u.Duration;
			a.RequiredTags = u.Requires;
			a.TargetRequiredTags = { kindTag };
			for (const std::string& t : u.TargetRequires) a.TargetRequiredTags.push_back(t);
			a.TargetBlockedTags = u.TargetBlocked;
			for (const auto& [name, v] : u.TargetMin) a.TargetMinAttributes.push_back({ name, v });
			a.Events.push_back({ u.Notify, 0.5f, u.Event, {}, {} });
			a.CooldownSeconds = u.Cooldown;
			a.Note = u.Note;
			return a;
		}

		InteractiveAbility Ability(const char* name, const char* montage, const char* notify, const char* event, const char* note)
		{
			InteractiveAbility a;
			a.Name = name;
			a.Montage = montage;
			a.Notify = notify;
			a.Event = event;
			a.Note = note;
			return a;
		}

		// The events each behaviour listens to: an ability of an interactive must send one of them, or it would do nothing.
		bool KnownEvent(const std::string& kind, const std::string& event)
		{
			if (kind == "campfire") return event == "Event.Fuel.Add";
			if (kind == "well") return event == "Event.Water.Draw";
			if (kind == "door") return event == "Event.Door.Open" || event == "Event.Door.Close";
			return false;
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

		double Round(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.35f is written as 0.35

		Json Vec(const glm::vec3& v) { return Json::array({ Round(v.x), Round(v.y), Round(v.z) }); }

		// ---- reading: each helper returns why the value is wrong ("" = fine)
		std::string ReadVec(const Json& o, const char* key, glm::vec3& out)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) return std::string(key) + " must be [x, y, z]";
			out = { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
			return {};
		}

		std::string ReadTags(const Json& o, const char* key, std::vector<std::string>& out)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_array()) return std::string(key) + " must be a list of tags";
			out.clear();
			for (const Json& t : *it)
			{
				if (!t.is_string() || !GameplayTag::IsValidTagName(t.get<std::string>())) return std::string(key) + ": bad tag " + t.dump();
				out.push_back(t.get<std::string>());
			}
			return {};
		}

		template <typename T>
		std::string ReadNumber(const Json& o, const char* key, T& out, double least)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_number() || it->get<double>() < least) return std::string(key) + " must be a number of " + (least > 0.0 ? "more than 0" : "0 or more");
			out = it->get<T>();
			return {};
		}

		std::string ReadString(const Json& o, const char* key, std::string& out)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_string()) return std::string(key) + " must be text";
			out = it->get<std::string>();
			return {};
		}

		std::string ReadAbility(const Json& o, const std::string& kind, InteractiveAbility& a)
		{
			if (!o.is_object()) return "an ability must be an object";
			std::string e;
			if (!(e = ReadString(o, "name", a.Name)).empty() || !(e = ReadString(o, "montage", a.Montage)).empty() || !(e = ReadString(o, "notify", a.Notify)).empty()
				|| !(e = ReadString(o, "event", a.Event)).empty() || !(e = ReadString(o, "note", a.Note)).empty()) return e;
			if (a.Name.empty()) return "an ability needs a name";
			if (!(e = ReadNumber(o, "duration", a.Duration, 0.0)).empty() || !(e = ReadNumber(o, "cooldown", a.Cooldown, 0.0)).empty()
				|| !(e = ReadTags(o, "requires", a.Requires)).empty() || !(e = ReadTags(o, "targetRequires", a.TargetRequires)).empty()
				|| !(e = ReadTags(o, "targetBlocked", a.TargetBlocked)).empty()) return a.Name + ": " + e;
			if (const auto m = o.find("targetMin"); m != o.end())
			{
				if (!m->is_object()) return a.Name + ": targetMin must be an object of attribute -> number";
				for (const auto& [name, v] : m->items())
				{
					if (!v.is_number()) return a.Name + ": targetMin of " + name + " must be a number";
					a.TargetMin[name] = v.get<float>();
				}
			}
			if (!KnownEvent(kind, a.Event)) return a.Name + ": a " + kind + " does not listen to the event '" + a.Event + "'";
			return {};
		}

		std::string ReadDef(const std::string& id, const Json& o, InteractiveDef& d)
		{
			if (!o.is_object()) return "expected an object";
			d.Id = id;
			std::string e;
			if (!(e = ReadString(o, "kind", d.Kind)).empty() || !(e = ReadString(o, "name", d.Name)).empty()) return e;
			const std::string kindTag = InteractiveKindTag(d.Kind);
			if (kindTag.empty()) return "unknown kind '" + d.Kind + "' (campfire, well or door)";
			if (d.Name.empty()) return "it needs a name (the entity's)";
			if (!(e = ReadVec(o, "at", d.At)).empty() || !(e = ReadVec(o, "stand", d.Stand)).empty() || !(e = ReadTags(o, "tags", d.Tags)).empty()) return e;
			if (std::find(d.Tags.begin(), d.Tags.end(), kindTag) == d.Tags.end()) return "its tags must hold " + kindTag;
			if (const auto at = o.find("attributes"); at != o.end())
			{
				if (!at->is_object()) return "attributes must be an object of name -> { value, min, max }";
				for (const auto& [name, v] : at->items())
				{
					InteractiveAttribute a;
					a.Name = name;
					if (!v.is_object() || !v.contains("value") || !v.contains("max") || !v["value"].is_number() || !v["max"].is_number() || (v.contains("min") && !v["min"].is_number()))
						return "attribute " + name + ": expected { \"value\": v, \"min\": a, \"max\": b }";
					a.Value = v["value"].get<float>();
					a.Min = v.value("min", 0.0f);
					a.Max = v["max"].get<float>();
					if (a.Min > a.Max || a.Value < a.Min || a.Value > a.Max) return "attribute " + name + ": the value must lie between min and max";
					d.Attributes.push_back(a);
				}
			}
			const char* needs = d.Kind == "campfire" ? "Fuel" : d.Kind == "well" ? "Water" : nullptr;
			if (needs && std::none_of(d.Attributes.begin(), d.Attributes.end(), [&](const InteractiveAttribute& a) { return a.Name == needs; }))
				return std::string("a ") + d.Kind + " needs the attribute " + needs;
			if (!(e = ReadNumber(o, "burnSeconds", d.BurnSeconds, 0.01)).empty() || !(e = ReadNumber(o, "fuelPerWood", d.FuelPerWood, 1.0)).empty()
				|| !(e = ReadNumber(o, "refillSeconds", d.RefillSeconds, 0.01)).empty() || !(e = ReadNumber(o, "refillAmount", d.RefillAmount, 0.0)).empty()
				|| !(e = ReadNumber(o, "waterPerDraw", d.WaterPerDraw, 1.0)).empty() || !(e = ReadNumber(o, "swingDegrees", d.SwingDegrees, -360.0)).empty()
				|| !(e = ReadNumber(o, "swingSpeed", d.SwingSpeed, 0.01)).empty()) return e;
			if (const auto ab = o.find("abilities"); ab != o.end())
			{
				if (!ab->is_array()) return "abilities must be a list";
				for (const Json& j : *ab)
				{
					InteractiveAbility a;
					if (!(e = ReadAbility(j, d.Kind, a)).empty()) return "ability " + e;
					d.Abilities.push_back(a);
				}
			}
			return {};
		}
	}

	std::string InteractiveKindTag(const std::string& kind)
	{
		if (kind == "campfire") return "Interactive.Campfire";
		if (kind == "well") return "Interactive.Well";
		if (kind == "door") return "Interactive.Door";
		return {};
	}

	std::map<std::string, InteractiveDef> DefaultInteractives()
	{
		// campfire: a ring of stones and a log; flame and light appear when it burns
		InteractiveDef fire;
		fire.Id = "fire_pit";
		fire.Kind = "campfire";
		fire.Name = "Fire Pit";
		fire.Stand = { 0.0f, 0.0f, -1.6f };
		fire.Tags = { "Interactive.Campfire", "State.Unlit" };
		fire.Attributes = { { "Fuel", 0.0f, 0.0f, 10.0f } };
		InteractiveAbility stoke = Ability("Stoke", "plant", "stoke", "Event.Fuel.Add", "puts a piece of wood or a branch in a campfire (needs one in the bag)");
		stoke.Requires = { "Item.Resource.Wood" };
		fire.Abilities = { stoke };

		// well: a ring of stones with a roof; it holds water that comes back
		InteractiveDef well;
		well.Id = "well";
		well.Kind = "well";
		well.Name = "Well";
		well.At = { 6.0f, 0.0f, 0.0f };
		well.Stand = { 0.0f, 0.0f, -1.7f };
		well.Tags = { "Interactive.Well" };
		well.Attributes = { { "Water", 6.0f, 0.0f, 6.0f } };
		InteractiveAbility draw = Ability("DrawWater", "pick_fruit", "draw", "Event.Water.Draw", "fills the bag with water from a well");
		draw.TargetMin = { { "Water", 1.0f } };
		well.Abilities = { draw };

		// door in a fence: the hinge turns the panel
		InteractiveDef gate;
		gate.Id = "gate";
		gate.Kind = "door";
		gate.Name = "Gate";
		gate.At = { -6.0f, 0.0f, 0.0f };
		gate.Stand = { 0.0f, 0.0f, -1.3f };
		gate.Tags = { "Interactive.Door", "State.Closed" };
		InteractiveAbility open = Ability("OpenDoor", "pick_fruit", "open", "Event.Door.Open", "opens a door");
		open.TargetBlocked = { "State.Open" };
		InteractiveAbility close = Ability("CloseDoor", "pick_fruit", "close", "Event.Door.Close", "closes a door");
		close.TargetRequires = { "State.Open" };
		gate.Abilities = { open, close };
		return { { fire.Id, fire }, { well.Id, well }, { gate.Id, gate } };
	}

	std::string LoadInteractives(const std::string& json, std::map<std::string, InteractiveDef>& defs)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("interactives");
		if (section == j.end()) return {};
		if (!section->is_object()) return "interactives: expected an object of id -> interactive";
		std::map<std::string, InteractiveDef> read;
		for (const auto& [id, o] : section->items())
		{
			InteractiveDef d;
			if (id.empty()) return "interactives: an empty id";
			if (const std::string e = ReadDef(id, o, d); !e.empty()) return "interactive '" + id + "': " + e;
			read[id] = std::move(d);
		}
		for (auto& [id, d] : read) defs[id] = std::move(d);
		return {};
	}

	std::string LoadInteractivesFile(const std::string& path, std::map<std::string, InteractiveDef>& defs)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadInteractives(ss.str(), defs);
		return error.empty() ? error : path + ": " + error;
	}

	std::string InteractivesToJson(const std::map<std::string, InteractiveDef>& defs)
	{
		const InteractiveAbility plainAbility;
		Json out = Json::object();
		for (const auto& [id, d] : defs)
		{
			Json o = Json::object();
			o["kind"] = d.Kind;
			o["name"] = d.Name;
			o["at"] = Vec(d.At);
			o["stand"] = Vec(d.Stand);
			o["tags"] = d.Tags;
			Json at = Json::object();
			for (const InteractiveAttribute& a : d.Attributes) at[a.Name] = { { "value", Round(a.Value) }, { "min", Round(a.Min) }, { "max", Round(a.Max) } };
			if (!at.empty()) o["attributes"] = at;
			// the timers and amounts of its kind only
			if (d.Kind == "campfire") { o["burnSeconds"] = Round(d.BurnSeconds); o["fuelPerWood"] = d.FuelPerWood; }
			if (d.Kind == "well") { o["refillSeconds"] = Round(d.RefillSeconds); o["refillAmount"] = d.RefillAmount; o["waterPerDraw"] = d.WaterPerDraw; }
			if (d.Kind == "door") { o["swingDegrees"] = Round(d.SwingDegrees); o["swingSpeed"] = Round(d.SwingSpeed); }
			Json abilities = Json::array();
			for (const InteractiveAbility& a : d.Abilities)
			{
				Json ab = { { "name", a.Name }, { "event", a.Event }, { "notify", a.Notify } };
				if (!a.Montage.empty()) ab["montage"] = a.Montage;
				if (a.Duration != plainAbility.Duration) ab["duration"] = Round(a.Duration);
				if (a.Cooldown != plainAbility.Cooldown) ab["cooldown"] = Round(a.Cooldown);
				if (!a.Requires.empty()) ab["requires"] = a.Requires;
				if (!a.TargetRequires.empty()) ab["targetRequires"] = a.TargetRequires;
				if (!a.TargetBlocked.empty()) ab["targetBlocked"] = a.TargetBlocked;
				if (!a.TargetMin.empty()) { Json m = Json::object(); for (const auto& [n, v] : a.TargetMin) m[n] = Round(v); ab["targetMin"] = m; }
				if (!a.Note.empty()) ab["note"] = a.Note;
				abilities.push_back(ab);
			}
			o["abilities"] = abilities;
			out[id] = o;
		}
		return out.dump(2);
	}

	void RegisterInteractiveContent(GameplayLibrary& lib, const std::map<std::string, InteractiveDef>& defs)
	{
		for (const auto& [id, d] : defs)
			for (const InteractiveAbility& a : d.Abilities) lib.AddAbility(Use(a, InteractiveKindTag(d.Kind)));
	}

	EntityId BuildInteractive(EntityWorld& w, const InteractiveDef& def, const std::string& name, EntityId parent, const glm::vec3& pos)
	{
		const EntityId id = w.Create(name, parent);
		{
			Entity& e = *w.Find(id);
			e.Transform.Position = pos;
			Stand(e, def.Stand, 0.0f);
			e.Gameplay.emplace();
			e.Gameplay->Tags = def.Tags;
			for (const InteractiveAttribute& a : def.Attributes) e.Gameplay->Attributes.push_back({ a.Name, a.Value, a.Min, a.Max });
		}
		const std::string n = w.Find(id)->Name;
		if (def.Kind == "campfire")
		{
			for (int i = 0; i < 6; ++i)
			{
				const float a = static_cast<float>(i) * 1.0472f;
				Part(w, id, n + " Stone " + std::to_string(i + 1), MeshShape::Sphere, { std::cos(a) * 0.45f, 0.1f, std::sin(a) * 0.45f }, glm::vec3(0.22f), { 0.4f, 0.4f, 0.42f });
			}
			Part(w, id, n + " Log", MeshShape::Cube, { 0.0f, 0.12f, 0.0f }, { 0.7f, 0.12f, 0.12f }, { 0.3f, 0.2f, 0.1f });
			const EntityId flame = Part(w, id, n + " Flame", MeshShape::Sphere, { 0.0f, 0.4f, 0.0f }, { 0.3f, 0.5f, 0.3f }, { 1.0f, 0.55f, 0.1f });
			w.Find(flame)->Mesh->Emissive = { 1.6f, 0.7f, 0.1f };
			w.Find(flame)->Active = false;
			const EntityId light = w.Create(n + " Light", id);
			w.Find(light)->Transform.Position = { 0.0f, 0.8f, 0.0f };
			w.Find(light)->Light.emplace();
			w.Find(light)->Light->Color = { 1.0f, 0.6f, 0.25f };
			w.Find(light)->Light->Intensity = 12.0f;
			w.Find(light)->Light->Radius = 9.0f;
			w.Find(light)->Active = false;
		}
		else if (def.Kind == "well")
		{
			Part(w, id, n + " Wall", MeshShape::Cube, { 0.0f, 0.4f, 0.0f }, { 1.3f, 0.8f, 1.3f }, { 0.45f, 0.43f, 0.4f });
			Part(w, id, n + " Water", MeshShape::Cube, { 0.0f, 0.82f, 0.0f }, { 0.9f, 0.04f, 0.9f }, { 0.15f, 0.35f, 0.6f });
			Part(w, id, n + " Post A", MeshShape::Cube, { -0.55f, 1.4f, 0.0f }, { 0.1f, 1.2f, 0.1f }, { 0.35f, 0.24f, 0.13f });
			Part(w, id, n + " Post B", MeshShape::Cube, { 0.55f, 1.4f, 0.0f }, { 0.1f, 1.2f, 0.1f }, { 0.35f, 0.24f, 0.13f });
			Part(w, id, n + " Roof", MeshShape::Cube, { 0.0f, 2.05f, 0.0f }, { 1.5f, 0.1f, 1.0f }, { 0.5f, 0.2f, 0.15f });
		}
		else if (def.Kind == "door")
		{
			const glm::vec3 wood(0.4f, 0.28f, 0.15f);
			Part(w, id, n + " Post L", MeshShape::Cube, { -0.75f, 0.6f, 0.0f }, { 0.15f, 1.2f, 0.15f }, wood);
			Part(w, id, n + " Post R", MeshShape::Cube, { 0.75f, 0.6f, 0.0f }, { 0.15f, 1.2f, 0.15f }, wood);
			Part(w, id, n + " Fence L", MeshShape::Cube, { -2.2f, 0.5f, 0.0f }, { 2.2f, 0.9f, 0.08f }, wood);
			Part(w, id, n + " Fence R", MeshShape::Cube, { 2.2f, 0.5f, 0.0f }, { 2.2f, 0.9f, 0.08f }, wood);
			const EntityId hinge = w.Create(n + " Hinge", id);
			w.Find(hinge)->Transform.Position = { -0.7f, 0.0f, 0.0f };
			Part(w, hinge, n + " Panel", MeshShape::Cube, { 0.7f, 0.55f, 0.0f }, { 1.4f, 0.9f, 0.08f }, { 0.55f, 0.38f, 0.2f });
		}
		return id;
	}

	InteractiveLayout BuildInteractives(EntityWorld& w, const glm::vec3& origin, const std::map<std::string, InteractiveDef>& defs)
	{
		if (const EntityId old = w.FindByName("Interactives"); old != NullEntity) w.Destroy(old);
		InteractiveLayout out;
		const EntityId root = w.Create("Interactives");
		w.Find(root)->Transform.Position = origin;
		out.Root = w.Find(root)->Name;
		auto rank = [](const std::string& kind) { return kind == "campfire" ? 0 : kind == "well" ? 1 : kind == "door" ? 2 : 3; };
		std::vector<const InteractiveDef*> order;
		for (const auto& [id, d] : defs) order.push_back(&d);
		std::stable_sort(order.begin(), order.end(), [&](const InteractiveDef* l, const InteractiveDef* r) { return rank(l->Kind) < rank(r->Kind); });
		for (const InteractiveDef* d : order)
		{
			const std::string name = w.Find(BuildInteractive(w, *d, d->Name, root, d->At))->Name;
			std::string& slot = d->Kind == "campfire" ? out.Campfire : d->Kind == "well" ? out.Well : out.Door;
			if (slot.empty()) slot = name;
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

	const InteractiveDef* InteractiveSystem::DefOf(const std::string& name, const AbilitySystemComponent& a) const
	{
		for (const auto& [id, d] : Defs) if (d.Name == name && a.HasTag(InteractiveKindTag(d.Kind))) return &d;
		for (const auto& [id, d] : Defs) if (a.HasTag(InteractiveKindTag(d.Kind))) return &d;
		return nullptr;
	}

	void InteractiveSystem::Bind(GameplayWorld& gameplay, Take take, Give give)
	{
		m_Gameplay = &gameplay;
		m_Take = std::move(take);
		m_Give = std::move(give);
		m_Burn.clear();
		m_Bound.clear();
		for (const std::string& name : gameplay.Names()) Listen(name, gameplay.Find(name));
	}

	// The behaviours listen to the events their abilities send. The def is looked up at the moment of the event, so a `content load` that changes
	// an amount counts at once.
	void InteractiveSystem::Listen(const std::string& name, AbilitySystemComponent* a)
	{
		if (!a) return;
		const bool campfire = a->HasTag("Interactive.Campfire"), well = a->HasTag("Interactive.Well"), door = a->HasTag("Interactive.Door");
		if (!campfire && !well && !door) return;
		m_Bound[name] = a;
		if (campfire)
			a->AddListener("Event.Fuel", [this, a, name](const GameplayEventData& e)
			{
				if (e.Tag != "Event.Fuel.Add" || !e.Instigator || !m_Take) return;
				const InteractiveDef* def = DefOf(name, *a);
				const int fuel = TakeFuel(e.Instigator->Name(), def ? def->FuelPerWood : InteractiveDef{}.FuelPerWood);
				if (fuel > 0) a->Attributes().AddBase("Fuel", static_cast<float>(fuel));
			});
		if (well)
			a->AddListener("Event.Water", [this, a, name](const GameplayEventData& e)
			{
				if (e.Tag != "Event.Water.Draw" || !e.Instigator || !m_Give) return;
				const InteractiveDef* def = DefOf(name, *a);
				const int have = static_cast<int>(std::lround(a->Attributes().Current("Water")));
				const int give = std::min(have, def ? def->WaterPerDraw : InteractiveDef{}.WaterPerDraw);
				if (give <= 0) return;
				const int got = m_Give(e.Instigator->Name(), "water", give);
				a->Attributes().AddBase("Water", -static_cast<float>(got));
			});
		if (door)
			a->AddListener("Event.Door", [a](const GameplayEventData& e)
			{
				if (e.Tag == "Event.Door.Open") SetState(*a, { "State.Open", "State.Closed" }, "State.Open");
				else if (e.Tag == "Event.Door.Close") SetState(*a, { "State.Open", "State.Closed" }, "State.Closed");
			});
	}

	int InteractiveSystem::FuelOf(const std::string& item, int fallback) const
	{
		const ItemDef* def = Items ? Items->Find(item) : nullptr;
		return def && def->Fuel > 0 ? def->Fuel : fallback;
	}

	int InteractiveSystem::TakeFuel(const std::string& actor, int fallback) const
	{
		if (!Items) return m_Take(actor, "wood", 1) > 0 ? fallback : 0;
		// Anything tagged Item.Resource.Wood burns (a branch included); the piece with the most heat goes in first, then the lowest id.
		std::vector<std::pair<int, std::string>> burnable;
		for (const std::string& id : Items->Ids())
			for (const std::string& tag : Items->Find(id)->Tags)
				if (tag == "Item.Resource.Wood" || tag.rfind("Item.Resource.Wood.", 0) == 0) { burnable.push_back({ FuelOf(id, fallback), id }); break; }
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
		const InteractiveDef plain;
		for (const std::string& name : m_Gameplay->Names())
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			if (const auto bound = m_Bound.find(name); bound == m_Bound.end() || bound->second != a) Listen(name, a);   // one made since Bind
			const InteractiveDef* found = DefOf(name, *a);
			const InteractiveDef& def = found ? *found : plain;
			if (a->HasTag("Interactive.Campfire"))
			{
				AttributeSet& at = a->Attributes();
				float& t = timer(name);
				// an instance may burn at its own pace (the tribe's fire: one heat every FireSeconds / FireHeat)
				const float burn = at.Has("BurnSeconds") && at.Current("BurnSeconds") > 0.0f ? at.Current("BurnSeconds") : def.BurnSeconds;
				if (at.Current("Fuel") >= 1.0f)
				{
					t += dt;
					if (t >= burn) { t -= burn; at.AddBase("Fuel", -1.0f); }
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
				if (at.Current("Water") < at.Max("Water"))
				{
					t += dt;
					if (t >= def.RefillSeconds) { t -= def.RefillSeconds; at.AddBase("Water", static_cast<float>(def.RefillAmount)); }
				}
				else t = 0.0f;
			}
			else if (a->HasTag("Interactive.Door") && entities)
			{
				if (const EntityId hinge = entities->FindByName(name + " Hinge"); hinge != NullEntity)
				{
					Entity* h = entities->Find(hinge);
					const float want = a->HasTag("State.Open") ? def.SwingDegrees : 0.0f;
					if (std::fabs(h->Transform.Rotation.y - want) > 0.01f)
					{
						const float step = def.SwingSpeed * dt;   // a quarter turn in under half a second
						h->Transform.Rotation.y += std::max(-step, std::min(step, want - h->Transform.Rotation.y));
					}
				}
			}
		}
	}

	std::string InteractiveSystem::ToJson() const
	{
		Json timers = Json::array();
		for (const auto& [name, t] : m_Burn) timers.push_back(Json::array({ name, t }));
		return Json{ { "timers", timers } }.dump();
	}

	bool InteractiveSystem::FromJson(const std::string& text)
	{
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
