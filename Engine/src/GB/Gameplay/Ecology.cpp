#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <vector>

namespace GB
{
	Season SeasonClock::At(double seconds) const
	{
		if (!(SeasonSeconds > 0.0f)) return Season::Spring;   // (a season of no length would divide by zero)
		const double s = seconds < 0.0 ? 0.0 : seconds;
		return static_cast<Season>(static_cast<long long>(s / static_cast<double>(SeasonSeconds)) % 4);
	}

	float SeasonClock::Progress(double seconds) const
	{
		if (!(SeasonSeconds > 0.0f)) return 0.0f;
		const double s = seconds < 0.0 ? 0.0 : seconds;
		return static_cast<float>(std::fmod(s, static_cast<double>(SeasonSeconds)) / static_cast<double>(SeasonSeconds));
	}

	const char* SeasonClock::Name(Season s)
	{
		switch (s)
		{
		case Season::Spring: return "spring";
		case Season::Summer: return "summer";
		case Season::Autumn: return "autumn";
		default: return "winter";
		}
	}

	float SeasonClock::GrowthRate(Season s)
	{
		switch (s)
		{
		case Season::Spring: return 1.2f;
		case Season::Summer: return 1.0f;
		case Season::Autumn: return 0.6f;
		default: return 0.0f;
		}
	}

	float SeasonClock::HungerRate(Season s)
	{
		return s == Season::Winter ? 1.5f : s == Season::Autumn ? 1.1f : 1.0f;
	}

	float SeasonClock::SnowCover(Season s, float progress)
	{
		const float p = std::max(0.0f, std::min(1.0f, progress));
		switch (s)
		{
		case Season::Winter: return 0.95f * std::min(1.0f, p / 0.15f);
		case Season::Spring: return 0.95f * std::max(0.0f, 1.0f - p / 0.25f);
		case Season::Autumn: return p > 0.9f ? (p - 0.9f) / 0.1f * 0.3f : 0.0f;   // a first dusting just before winter
		default: return 0.0f;
		}
	}

	namespace
	{
		glm::vec3 FoliageBase(Season s)
		{
			switch (s)
			{
			case Season::Spring: return { 0.36f, 0.64f, 0.24f };
			case Season::Summer: return { 0.2f, 0.45f, 0.16f };
			case Season::Autumn: return { 0.78f, 0.42f, 0.1f };
			default: return { 0.7f, 0.74f, 0.78f };   // frosted
			}
		}
	}

	glm::vec3 SeasonClock::Foliage(Season s, float progress)
	{
		const float p = std::max(0.0f, std::min(1.0f, progress));
		const float t = std::max(0.0f, std::min(1.0f, (p - 0.7f) / 0.3f));   // the last 30% turns into the next season
		const float k = t * t * (3.0f - 2.0f * t);
		return FoliageBase(s) * (1.0f - k) + FoliageBase(static_cast<Season>((static_cast<int>(s) + 1) % 4)) * k;
	}

	const char* SeasonClock::Weather(Season s, float progress)
	{
		switch (s)
		{
		case Season::Spring: return progress < 0.2f ? "lightrain" : "clear";
		case Season::Summer: return progress > 0.45f && progress < 0.55f ? "storm" : "clear";
		case Season::Autumn: return progress > 0.65f ? "rain" : "clear";
		default: return progress < 0.9f ? "snow" : "clear";
		}
	}

	namespace
	{
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
			e->Mesh->Roughness = 0.95f;
			return id;
		}
	}

	std::map<std::string, SpeciesDef> DefaultSpecies()
	{
		SpeciesDef rabbit;   // the defaults of SpeciesDef and EcologyConfig are the rabbit
		rabbit.Id = "rabbit";

		// a small hunter: few of them, faster than a rabbit, long-lived on one catch, slow to breed
		SpeciesDef fox;
		fox.Id = "fox";
		fox.Name = "Fox";
		fox.Plural = "foxes";
		fox.Tag = "Animal.Fox";
		fox.Body = "quadruped";
		fox.Color = { 0.78f, 0.38f, 0.12f };
		fox.Size = 1.0f;
		fox.Eats = { "Animal.Rabbit" };
		fox.Sight = 18.0f;
		fox.BreedCost = 30.0f;
		fox.NewbornFood = 70.0f;
		fox.SpawnFood = 0.8f;
		fox.LegSeconds = 6.0f;
		fox.Life.MaxAnimals = 2;
		fox.Life.Speed = 2.4f;
		fox.Life.WanderRadius = 18.0f;
		fox.Life.HungerPerSecond = 0.12f;
		fox.Life.HungryBelow = 50.0f;
		fox.Life.EatReach = 0.8f;
		fox.Life.EatSeconds = 4.0f;
		fox.Life.FoodPerPlant = 55.0f;
		fox.Life.BreedAbove = 90.0f;
		fox.Life.AdultSeconds = 200.0f;
		fox.Life.BreedCooldown = 300.0f;
		fox.Life.StarveSeconds = 120.0f;
		return { { rabbit.Id, rabbit }, { fox.Id, fox } };
	}

	namespace
	{
		using Json = nlohmann::json;

		double Round3(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

		std::string Number(const Json& o, const char* key, float& out, float least, bool strict)
		{
			const auto it = o.find(key);
			if (it == o.end()) return {};
			if (!it->is_number() || (strict ? it->get<float>() <= least : it->get<float>() < least))
				return std::string(key) + " must be a number " + (strict ? "above " : "of at least ") + std::to_string(least);
			out = it->get<float>();
			return {};
		}

		std::string ReadSpecies(const std::string& id, const Json& o, SpeciesDef& d)
		{
			if (!o.is_object()) return "expected an object";
			d.Id = id;
			for (const auto& [key, field] : { std::pair<const char*, std::string*>{ "name", &d.Name }, { "plural", &d.Plural }, { "tag", &d.Tag }, { "body", &d.Body } })
				if (const auto it = o.find(key); it != o.end())
				{
					if (!it->is_string()) return std::string(key) + " must be text";
					*field = it->get<std::string>();
				}
			if (d.Name.empty() || d.Plural.empty()) return "it needs a name and a plural";
			if (!GameplayTag::IsValidTagName(d.Tag) || d.Tag.rfind("Animal.", 0) != 0) return "its tag must be Animal.<something>, not '" + d.Tag + "'";
			if (d.Body != "rabbit" && d.Body != "quadruped") return "body must be rabbit or quadruped";
			if (const auto it = o.find("color"); it != o.end())
			{
				if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) return "color must be [r, g, b]";
				d.Color = { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
			}
			if (const auto it = o.find("eats"); it != o.end())
			{
				if (!it->is_array() || it->empty()) return "eats must be a list of tags";
				d.Eats.clear();
				for (const Json& t : *it)
				{
					if (!t.is_string() || !GameplayTag::IsValidTagName(t.get<std::string>())) return "eats: bad tag " + t.dump();
					d.Eats.push_back(t.get<std::string>());
				}
			}
			const bool hunts = d.Hunts();
			for (const std::string& t : d.Eats)
			{
				if ((t.rfind("Animal.", 0) == 0) != hunts) return "it eats plants or animals, not both";
				if (GameplayTag(d.Tag).Matches(t) || GameplayTag(t).Matches(d.Tag)) return "it would eat its own kind";
			}
			float maxAnimals = static_cast<float>(d.Life.MaxAnimals);
			std::string e;
			if (!(e = Number(o, "size", d.Size, 0.0f, true)).empty() || !(e = Number(o, "sight", d.Sight, 0.0f, true)).empty() || !(e = Number(o, "breedCost", d.BreedCost, 0.0f, false)).empty()
				|| !(e = Number(o, "newbornFood", d.NewbornFood, 0.0f, true)).empty() || !(e = Number(o, "spawnFood", d.SpawnFood, 0.0f, true)).empty() || !(e = Number(o, "legSeconds", d.LegSeconds, 0.0f, true)).empty()
				|| !(e = Number(o, "maxAnimals", maxAnimals, 0.0f, false)).empty() || !(e = Number(o, "speed", d.Life.Speed, 0.0f, true)).empty() || !(e = Number(o, "wanderRadius", d.Life.WanderRadius, 0.0f, false)).empty()
				|| !(e = Number(o, "foodMax", d.Life.FoodMax, 0.0f, true)).empty() || !(e = Number(o, "hungerPerSecond", d.Life.HungerPerSecond, 0.0f, false)).empty() || !(e = Number(o, "hungryBelow", d.Life.HungryBelow, 0.0f, false)).empty()
				|| !(e = Number(o, "eatReach", d.Life.EatReach, 0.0f, true)).empty() || !(e = Number(o, "eatSeconds", d.Life.EatSeconds, 0.0f, false)).empty() || !(e = Number(o, "foodPerMeal", d.Life.FoodPerPlant, 0.0f, true)).empty()
				|| !(e = Number(o, "breedAbove", d.Life.BreedAbove, 0.0f, false)).empty() || !(e = Number(o, "adultSeconds", d.Life.AdultSeconds, 0.0f, false)).empty() || !(e = Number(o, "breedCooldown", d.Life.BreedCooldown, 0.0f, false)).empty()
				|| !(e = Number(o, "starveSeconds", d.Life.StarveSeconds, 0.0f, false)).empty()) return e;
			d.Life.MaxAnimals = static_cast<int>(maxAnimals);
			if (d.SpawnFood > 1.0f || d.NewbornFood > d.Life.FoodMax || d.Life.HungryBelow > d.Life.FoodMax) return "spawnFood is a part of foodMax (0..1), and newbornFood and hungryBelow cannot pass foodMax";
			return {};
		}
	}

	std::string LoadSpecies(const std::string& json, std::map<std::string, SpeciesDef>& species)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("species");
		if (section == j.end()) return {};
		if (!section->is_object()) return "species: expected an object of id -> species";
		std::map<std::string, SpeciesDef> read;
		for (const auto& [id, o] : section->items())
		{
			if (id.empty()) return "species: an empty id";
			SpeciesDef d;
			if (const std::string e = ReadSpecies(id, o, d); !e.empty()) return "species '" + id + "': " + e;
			read[id] = std::move(d);
		}
		std::map<std::string, SpeciesDef> all = species;
		for (auto& [id, d] : read) all[id] = d;
		std::map<std::string, std::string> byTag, byName;   // two species may not share a tag or a name (the system tells them apart by both)
		for (const auto& [id, d] : all)
		{
			if (!byTag.emplace(d.Tag, id).second) return "species '" + id + "' and '" + byTag[d.Tag] + "' have the same tag " + d.Tag;
			if (!byName.emplace(d.Name, id).second) return "species '" + id + "' and '" + byName[d.Name] + "' have the same name " + d.Name;
		}
		species = std::move(all);
		return {};
	}

	std::string LoadSpeciesFile(const std::string& path, std::map<std::string, SpeciesDef>& species)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadSpecies(ss.str(), species);
		return error.empty() ? error : path + ": " + error;
	}

	std::string SpeciesToJson(const std::map<std::string, SpeciesDef>& species)
	{
		Json out = Json::object();
		for (const auto& [id, d] : species)
		{
			const EcologyConfig& l = d.Life;
			out[id] = { { "name", d.Name }, { "plural", d.Plural }, { "tag", d.Tag }, { "body", d.Body }, { "color", { Round3(d.Color.x), Round3(d.Color.y), Round3(d.Color.z) } },
				{ "size", Round3(d.Size) }, { "eats", d.Eats }, { "sight", Round3(d.Sight) }, { "breedCost", Round3(d.BreedCost) }, { "newbornFood", Round3(d.NewbornFood) },
				{ "spawnFood", Round3(d.SpawnFood) }, { "legSeconds", Round3(d.LegSeconds) }, { "maxAnimals", l.MaxAnimals }, { "speed", Round3(l.Speed) },
				{ "wanderRadius", Round3(l.WanderRadius) }, { "foodMax", Round3(l.FoodMax) }, { "hungerPerSecond", Round3(l.HungerPerSecond) }, { "hungryBelow", Round3(l.HungryBelow) },
				{ "eatReach", Round3(l.EatReach) }, { "eatSeconds", Round3(l.EatSeconds) }, { "foodPerMeal", Round3(l.FoodPerPlant) }, { "breedAbove", Round3(l.BreedAbove) },
				{ "adultSeconds", Round3(l.AdultSeconds) }, { "breedCooldown", Round3(l.BreedCooldown) }, { "starveSeconds", Round3(l.StarveSeconds) } };
		}
		return out.dump(2);
	}

	const SpeciesDef* EcologySystem::SpeciesOf(const AbilitySystemComponent& a) const
	{
		for (const auto& [id, s] : Species) if (a.Tags().HasExact(s.Tag)) return &s;
		return nullptr;
	}

	void EcologySystem::Bind(EntityWorld& entities, GameplayWorld& gameplay, Ground ground)
	{
		m_Entities = &entities;
		m_Gameplay = &gameplay;
		m_Ground = std::move(ground);
		m_Walks.clear();
		m_Serials.clear();
		for (const std::string& name : gameplay.Names())
		{
			const AbilitySystemComponent* a = gameplay.Find(name);
			const SpeciesDef* s = a ? SpeciesOf(*a) : nullptr;
			if (!s) continue;
			const std::string prefix = s->Name + " ";
			if (name.rfind(prefix, 0) == 0) m_Serials[s->Id] = std::max(m_Serials[s->Id], std::atoi(name.c_str() + prefix.size()));
			Walk& w = m_Walks[name];
			if (const EntityId id = entities.FindByName(name); id != NullEntity)
			{
				const glm::vec3 p = entities.Find(id)->Transform.Position;
				w.Home = { p.x, p.z };
				w.Target = w.Home;
			}
		}
	}

	EntityId EcologySystem::Make(const SpeciesDef& s, const glm::vec3& at, float food, float age)
	{
		EntityId root = m_Entities->FindByName("Wildlife");
		if (root == NullEntity) root = m_Entities->Create("Wildlife");
		const std::string name = s.Name + " " + std::to_string(++m_Serials[s.Id]);
		const EntityId id = m_Entities->Create(name, root);
		Entity& e = *m_Entities->Find(id);
		e.Transform.Position = { at.x, GroundAt(at.x, at.z), at.z };
		e.Gameplay.emplace();
		e.Gameplay->Tags = { s.Tag };
		e.Gameplay->Attributes = { { "Food", food, 0.0f, s.Life.FoodMax }, { "Age", age, 0.0f, 1e9f }, { "Cooldown", 0.0f, 0.0f, 1e6f }, { "Starving", 0.0f, 0.0f, 1e6f } };
		const glm::vec3 fur = s.Color;
		const float k = s.Size;
		if (s.Body == "quadruped")
		{
			Part(*m_Entities, id, name + " Body", MeshShape::Sphere, glm::vec3(0.0f, 0.38f, 0.0f) * k, glm::vec3(0.3f, 0.28f, 0.7f) * k, fur);
			Part(*m_Entities, id, name + " Head", MeshShape::Sphere, glm::vec3(0.0f, 0.52f, 0.4f) * k, glm::vec3(0.22f, 0.2f, 0.24f) * k, fur);
			Part(*m_Entities, id, name + " Snout", MeshShape::Sphere, glm::vec3(0.0f, 0.48f, 0.55f) * k, glm::vec3(0.09f, 0.08f, 0.16f) * k, fur * 0.8f);
			Part(*m_Entities, id, name + " Ear L", MeshShape::Cube, glm::vec3(-0.07f, 0.66f, 0.38f) * k, glm::vec3(0.05f, 0.12f, 0.04f) * k, fur * 0.6f);
			Part(*m_Entities, id, name + " Ear R", MeshShape::Cube, glm::vec3(0.07f, 0.66f, 0.38f) * k, glm::vec3(0.05f, 0.12f, 0.04f) * k, fur * 0.6f);
			for (int leg = 0; leg < 4; ++leg)
				Part(*m_Entities, id, name + " Leg " + std::to_string(leg + 1), MeshShape::Cube, glm::vec3(leg % 2 ? 0.1f : -0.1f, 0.13f, leg < 2 ? 0.22f : -0.22f) * k, glm::vec3(0.06f, 0.26f, 0.06f) * k, fur * 0.5f);
			Part(*m_Entities, id, name + " Tail", MeshShape::Sphere, glm::vec3(0.0f, 0.4f, -0.45f) * k, glm::vec3(0.12f, 0.12f, 0.32f) * k, { 0.95f, 0.95f, 0.92f });
		}
		else
		{
			Part(*m_Entities, id, name + " Body", MeshShape::Sphere, glm::vec3(0.0f, 0.22f, 0.0f) * k, glm::vec3(0.3f, 0.26f, 0.45f) * k, fur);
			Part(*m_Entities, id, name + " Head", MeshShape::Sphere, glm::vec3(0.0f, 0.34f, 0.24f) * k, glm::vec3(0.2f, 0.18f, 0.2f) * k, fur);
			Part(*m_Entities, id, name + " Ear L", MeshShape::Cube, glm::vec3(-0.05f, 0.52f, 0.22f) * k, glm::vec3(0.04f, 0.2f, 0.06f) * k, fur);
			Part(*m_Entities, id, name + " Ear R", MeshShape::Cube, glm::vec3(0.05f, 0.52f, 0.22f) * k, glm::vec3(0.04f, 0.2f, 0.06f) * k, fur);
			Part(*m_Entities, id, name + " Tail", MeshShape::Sphere, glm::vec3(0.0f, 0.24f, -0.26f) * k, glm::vec3(0.1f) * k, { 0.95f, 0.95f, 0.92f });
		}

		AbilitySystemComponent* a = m_Gameplay->Create(name);
		a->Tags().Add(s.Tag);
		a->Attributes().Define("Food", food, 0.0f, s.Life.FoodMax);
		a->Attributes().Define("Age", age, 0.0f, 1e9f);
		a->Attributes().Define("Cooldown", 0.0f, 0.0f, 1e6f);
		a->Attributes().Define("Starving", 0.0f, 0.0f, 1e6f);
		Walk& w = m_Walks[name];
		w.Home = { at.x, at.z };
		w.Target = w.Home;
		return id;
	}

	int EcologySystem::Spawn(const glm::vec3& at, int count, const std::string& species)
	{
		if (!m_Entities || !m_Gameplay) return 0;
		const auto it = Species.find(species);
		if (it == Species.end()) return 0;
		const SpeciesDef& s = it->second;
		int made = 0;
		const int present = Count(species);
		for (int i = 0; i < count && present + made < s.Life.MaxAnimals; ++i, ++made)
		{
			const float angle = static_cast<float>(i) * 2.399963f;   // the golden angle: they do not pile up
			const float r = 0.6f * std::sqrt(static_cast<float>(i) + 1.0f);
			Make(s, { at.x + std::cos(angle) * r, 0.0f, at.z + std::sin(angle) * r }, s.Life.FoodMax * s.SpawnFood, s.Life.AdultSeconds);
		}
		return made;
	}

	int EcologySystem::Count() const
	{
		int n = 0;
		if (!m_Gameplay) return 0;
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name); a && SpeciesOf(*a)) ++n;
		return n;
	}

	int EcologySystem::Count(const std::string& species) const
	{
		const auto it = Species.find(species);
		if (!m_Gameplay || it == Species.end()) return 0;
		int n = 0;
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name); a && a->Tags().HasExact(it->second.Tag)) ++n;
		return n;
	}

	int EcologySystem::Born() const { int n = 0; for (const auto& [id, c] : m_Counts) n += c.Born; return n; }
	int EcologySystem::Starved() const { int n = 0; for (const auto& [id, c] : m_Counts) n += c.Starved; return n; }
	int EcologySystem::Eaten() const { int n = 0; for (const auto& [id, c] : m_Counts) n += c.Eaten; return n; }
	int EcologySystem::Killed() const { int n = 0; for (const auto& [id, c] : m_Counts) n += c.Killed; return n; }

	float EcologySystem::Hash01(const std::string& name, int n, int salt) const
	{
		uint32_t h = 2166136261u;
		for (const char c : name) h = (h ^ static_cast<uint32_t>(static_cast<unsigned char>(c))) * 16777619u;
		h = (h ^ static_cast<uint32_t>(n)) * 16777619u;
		h = (h ^ static_cast<uint32_t>(salt)) * 2654435761u;
		h ^= h >> 15;
		h *= 2246822519u;
		h ^= h >> 13;
		return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
	}

	// An animal caught by a hunter: it goes from the scene and the systems.
	void EcologySystem::Kill(const std::string& name)
	{
		if (const EntityId id = m_Entities->FindByName(name); id != NullEntity) m_Entities->Destroy(id);
		if (const AbilitySystemComponent* a = m_Gameplay->Find(name))
			if (const SpeciesDef* s = SpeciesOf(*a)) ++m_Counts[s->Id].Killed;
		m_Gameplay->Remove(name);
		m_Walks.erase(name);
	}

	void EcologySystem::Update(double clock, float dt)
	{
		if (!m_Entities || !m_Gameplay) return;
		const SeasonClock seasons;
		const Season season = seasons.At(clock);
		const float breeding = SeasonClock::GrowthRate(season);

		std::vector<std::string> names;
		std::map<std::string, int> population;   // species -> how many there are
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name))
				if (const SpeciesDef* s = SpeciesOf(*a)) { names.push_back(name); ++population[s->Id]; }

		// The plants each grazer may eat and where they stand, listed once per update the first time one looks for food (the search by name is
		// linear in the entities of the scene, and plants do not move). Whether a plant still has something is asked again at the moment of choosing.
		struct Plant { std::string Name; glm::vec2 At; };
		std::map<std::string, std::vector<Plant>> plants;   // species -> the plants of its diet
		auto listPlants = [&](const SpeciesDef& s) -> const std::vector<Plant>&
		{
			if (const auto it = plants.find(s.Id); it != plants.end()) return it->second;
			std::vector<Plant>& list = plants[s.Id];
			for (const std::string& n : m_Gameplay->Names())
			{
				const AbilitySystemComponent* plant = m_Gameplay->Find(n);
				if (!plant || !std::any_of(s.Eats.begin(), s.Eats.end(), [&](const std::string& t) { return plant->HasTag(t); })) continue;
				const EntityId pid = m_Entities->FindByName(n);
				if (pid == NullEntity) continue;
				const glm::vec4 pp = m_Entities->WorldMatrix(pid)[3];
				list.push_back({ n, { pp.x, pp.z } });
			}
			return list;
		};
		// Where an animal stands now (prey moves: asked every time).
		auto whereIs = [&](const std::string& n, glm::vec2& out)
		{
			const auto it = m_Walks.find(n);
			const Entity* e = it != m_Walks.end() && it->second.Id != NullEntity ? m_Entities->Find(it->second.Id) : nullptr;
			if (!e || e->Name != n) { const EntityId id = m_Entities->FindByName(n); e = id == NullEntity ? nullptr : m_Entities->Find(id); }
			if (!e) return false;
			out = { e->Transform.Position.x, e->Transform.Position.z };
			return true;
		};

		for (const std::string& name : names)
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			if (!a) continue;   // caught earlier in this update
			const SpeciesDef& s = *SpeciesOf(*a);
			const EcologyConfig& life = s.Life;
			Walk& w = m_Walks[name];
			// The id is kept between frames (the search by name is linear in the entities of the scene); the name is checked in case the scene was replaced.
			const Entity* known = w.Id != NullEntity ? m_Entities->Find(w.Id) : nullptr;
			const EntityId id = known && known->Name == name ? w.Id : m_Entities->FindByName(name);
			if (id == NullEntity) continue;
			w.Id = id;
			Entity& e = *m_Entities->Find(id);
			AttributeSet& at = a->Attributes();
			if (!w.Has) { w.Has = true; w.Home = { e.Transform.Position.x, e.Transform.Position.z }; w.Target = w.Home; }

			at.AddBase("Food", -life.HungerPerSecond * SeasonClock::HungerRate(season) * dt);
			at.AddBase("Age", dt);
			at.AddBase("Cooldown", -dt * breeding);
			if (at.Current("Food") <= 0.0f) at.AddBase("Starving", dt); else at.SetBase("Starving", 0.0f);
			if (at.Current("Starving") > life.StarveSeconds)
			{
				m_Entities->Destroy(id);
				m_Gameplay->Remove(name);
				m_Walks.erase(name);
				++m_Counts[s.Id].Starved;
				--population[s.Id];
				continue;
			}

			glm::vec2 pos(e.Transform.Position.x, e.Transform.Position.z);
			const bool hunts = s.Hunts();

			// something to eat when hungry: the nearest plant with something left, or the nearest prey
			if (w.Plant.empty() && at.Current("Food") < life.HungryBelow)
			{
				float best = s.Sight;
				if (hunts)
				{
					for (const std::string& n : names)
					{
						const AbilitySystemComponent* prey = n == name ? nullptr : m_Gameplay->Find(n);
						glm::vec2 p;
						if (!prey || !std::any_of(s.Eats.begin(), s.Eats.end(), [&](const std::string& t) { return prey->HasTag(t); }) || !whereIs(n, p)) continue;
						const float d = glm::distance(pos, p);
						if (d < best) { best = d; w.Plant = n; w.Target = p; }
					}
				}
				else
					for (const Plant& p : listPlants(s))
					{
						const AbilitySystemComponent* plant = m_Gameplay->Find(p.Name);
						if (!plant || ResourceSystem::Amount(*plant) <= 0) continue;
						const float d = glm::distance(pos, p.At);
						if (d < best) { best = d; w.Plant = p.Name; w.Target = p.At; }
					}
			}
			if (hunts && !w.Plant.empty())   // the prey runs on: follow it, or give up when it is gone (caught by another, starved) or out of sight
			{
				glm::vec2 p;
				if (!m_Gameplay->Find(w.Plant) || !whereIs(w.Plant, p) || glm::distance(pos, p) > s.Sight * 1.5f) { w.Plant.clear(); w.Eating = 0.0f; w.Target = pos; }
				else w.Target = p;
			}

			const glm::vec2 to = w.Target - pos;
			const float distance = glm::length(to);
			const bool atPlant = !w.Plant.empty() && distance <= life.EatReach;
			if (atPlant && hunts)
			{
				w.Eating += dt;   // the struggle: the prey is held for EatSeconds, then eaten
				if (w.Eating >= life.EatSeconds)
				{
					w.Eating = 0.0f;
					if (const AbilitySystemComponent* prey = m_Gameplay->Find(w.Plant))
						if (const SpeciesDef* ps = SpeciesOf(*prey)) --population[ps->Id];
					Kill(w.Plant);
					at.AddBase("Food", life.FoodPerPlant);
					++m_Counts[s.Id].Eaten;
					w.Plant.clear();
					w.Target = pos;
				}
			}
			else if (atPlant)
			{
				AbilitySystemComponent* plant = m_Gameplay->Find(w.Plant);
				if (!plant || ResourceSystem::Amount(*plant) <= 0) { w.Plant.clear(); w.Eating = 0.0f; }
				else
				{
					w.Eating += dt;
					if (w.Eating >= life.EatSeconds)
					{
						w.Eating = 0.0f;
						plant->Attributes().SetBase("Amount", static_cast<float>(ResourceSystem::Amount(*plant) - 1));
						at.AddBase("Food", life.FoodPerPlant);
						++m_Counts[s.Id].Eaten;
						if (at.Current("Food") >= life.BreedAbove + 10.0f) w.Plant.clear();   // it eats until it could breed
					}
				}
			}
			else if (distance > 0.2f)
			{
				const float step = std::min(distance, life.Speed * dt);
				pos += to / distance * step;
				e.Transform.Rotation.y = glm::degrees(std::atan2(to.x, to.y));
			}
			else if (w.Plant.empty())
			{
				// a new leg of the wander
				const int leg = static_cast<int>(at.Current("Age") / s.LegSeconds);
				const float angle = Hash01(name, leg, 1) * 6.2831853f;
				const float r = std::sqrt(Hash01(name, leg, 2)) * life.WanderRadius;
				w.Target = w.Home + glm::vec2(std::cos(angle), std::sin(angle)) * r;
			}
			e.Transform.Position = { pos.x, GroundAt(pos.x, pos.y), pos.y };

			// breeding
			if (population[s.Id] < life.MaxAnimals && season != Season::Winter && at.Current("Food") >= life.BreedAbove
				&& at.Current("Age") >= life.AdultSeconds && at.Current("Cooldown") <= 0.0f)
			{
				at.SetBase("Cooldown", life.BreedCooldown);
				at.AddBase("Food", -s.BreedCost);
				Make(s, { pos.x, 0.0f, pos.y }, s.NewbornFood, 0.0f);
				++population[s.Id];
				++m_Counts[s.Id].Born;
			}
		}
	}

	std::string SeasonLook::ToJson() const
	{
		using Json = nlohmann::json;
		return Json{ { "enabled", Enabled }, { "weather", Weather }, { "snow", Snow }, { "leaf", Json::array({ Leaf.x, Leaf.y, Leaf.z }) } }.dump();
	}

	bool SeasonLook::FromJson(const std::string& text)
	{
		using Json = nlohmann::json;
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		try
		{
			SeasonLook look;
			look.Enabled = j.at("enabled").get<bool>();
			look.Weather = j.at("weather").get<std::string>();
			look.Snow = j.at("snow").get<float>();
			look.Leaf = { j.at("leaf").at(0).get<float>(), j.at("leaf").at(1).get<float>(), j.at("leaf").at(2).get<float>() };
			*this = look;
		}
		catch (const Json::exception&)
		{
			return false;
		}
		return true;
	}

	std::string EcologySystem::ToJson() const
	{
		using Json = nlohmann::json;
		Json walks = Json::array();
		for (const auto& [name, w] : m_Walks)
		{
			if (!w.Has) continue;
			walks.push_back(Json{ { "name", name }, { "home", Json::array({ w.Home.x, w.Home.y }) }, { "target", Json::array({ w.Target.x, w.Target.y }) }, { "plant", w.Plant }, { "eating", w.Eating } });
		}
		// the totals as before (a save of rabbits alone reads the same), and each species' own counters when there is more than the rabbits
		Json j{ { "born", Born() }, { "starved", Starved() }, { "eaten", Eaten() }, { "walks", walks } };
		const bool others = std::any_of(m_Counts.begin(), m_Counts.end(), [](const auto& c) { return c.first != "rabbit"; }) || Killed() > 0;
		if (others)
		{
			Json counts = Json::object();
			for (const auto& [id, c] : m_Counts) counts[id] = { { "born", c.Born }, { "starved", c.Starved }, { "eaten", c.Eaten }, { "killed", c.Killed } };
			j["species"] = counts;
		}
		return j.dump();
	}

	bool EcologySystem::FromJson(const std::string& text)
	{
		using Json = nlohmann::json;
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		try
		{
			std::map<std::string, Walk> walks;
			for (const Json& o : j.at("walks"))
			{
				Walk w;
				w.Has = true;
				w.Home = { o.at("home").at(0).get<float>(), o.at("home").at(1).get<float>() };
				w.Target = { o.at("target").at(0).get<float>(), o.at("target").at(1).get<float>() };
				w.Plant = o.at("plant").get<std::string>();
				w.Eating = o.at("eating").get<float>();
				walks[o.at("name").get<std::string>()] = w;
			}
			std::map<std::string, Counts> counts;
			if (const auto it = j.find("species"); it != j.end())
				for (const auto& [id, c] : it->items()) counts[id] = { c.at("born").get<int>(), c.at("starved").get<int>(), c.at("eaten").get<int>(), c.at("killed").get<int>() };
			else
			{
				// a save from before the species: every animal was a rabbit
				Counts& c = counts["rabbit"];
				c.Born = j.at("born").get<int>();
				c.Starved = j.at("starved").get<int>();
				c.Eaten = j.at("eaten").get<int>();
				if (c.Born == 0 && c.Starved == 0 && c.Eaten == 0) counts.clear();
			}
			m_Counts = std::move(counts);
			for (auto& [name, w] : walks)
				if (const auto it = m_Walks.find(name); it != m_Walks.end()) it->second = w;   // (an animal the scene does not have is dropped)
		}
		catch (const Json::exception&)
		{
			return false;
		}
		return true;
	}

	std::string EcologySystem::Describe() const
	{
		// the grazers first (the rabbits: the line it always gave), then the hunters; a species with nobody and no history is left out, but there is always a line
		std::vector<const SpeciesDef*> order;
		for (const auto& [id, s] : Species) if (!s.Hunts()) order.push_back(&s);
		for (const auto& [id, s] : Species) if (s.Hunts()) order.push_back(&s);
		std::string out;
		for (const SpeciesDef* s : order)
		{
			const auto it = m_Counts.find(s->Id);
			const Counts c = it == m_Counts.end() ? Counts{} : it->second;
			const int n = Count(s->Id);
			if (!out.empty() && n == 0 && c.Born == 0 && c.Starved == 0 && c.Eaten == 0 && c.Killed == 0) continue;
			out += (out.empty() ? "" : "; ") + std::to_string(n) + " " + s->Plural + " (born " + std::to_string(c.Born) + ", starved " + std::to_string(c.Starved)
				+ (s->Hunts() ? ", caught " : ", plants eaten ") + std::to_string(c.Eaten) + (c.Killed ? ", caught by hunters " + std::to_string(c.Killed) : "") + ")";
		}
		return out;
	}
}
