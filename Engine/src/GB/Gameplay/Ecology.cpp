#include "GB/Gameplay/Ecology.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
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

	void EcologySystem::Bind(EntityWorld& entities, GameplayWorld& gameplay, Ground ground)
	{
		m_Entities = &entities;
		m_Gameplay = &gameplay;
		m_Ground = std::move(ground);
		m_Walks.clear();
		m_Serial = 0;
		for (const std::string& name : gameplay.Names())
		{
			const AbilitySystemComponent* a = gameplay.Find(name);
			if (!a || !a->HasTag("Animal.Rabbit")) continue;
			if (name.rfind("Rabbit ", 0) == 0) m_Serial = std::max(m_Serial, std::atoi(name.c_str() + 7));
			Walk& w = m_Walks[name];
			if (const EntityId id = entities.FindByName(name); id != NullEntity)
			{
				const glm::vec3 p = entities.Find(id)->Transform.Position;
				w.Home = { p.x, p.z };
				w.Target = w.Home;
			}
		}
	}

	EntityId EcologySystem::Make(const glm::vec3& at, float food, float age)
	{
		EntityId root = m_Entities->FindByName("Wildlife");
		if (root == NullEntity) root = m_Entities->Create("Wildlife");
		const std::string name = "Rabbit " + std::to_string(++m_Serial);
		const EntityId id = m_Entities->Create(name, root);
		Entity& e = *m_Entities->Find(id);
		e.Transform.Position = { at.x, GroundAt(at.x, at.z), at.z };
		e.Gameplay.emplace();
		e.Gameplay->Tags = { "Animal.Rabbit" };
		e.Gameplay->Attributes = { { "Food", food, 0.0f, Config.FoodMax }, { "Age", age, 0.0f, 1e9f }, { "Cooldown", 0.0f, 0.0f, 1e6f }, { "Starving", 0.0f, 0.0f, 1e6f } };
		const glm::vec3 fur(0.62f, 0.52f, 0.4f);
		Part(*m_Entities, id, name + " Body", MeshShape::Sphere, { 0.0f, 0.22f, 0.0f }, { 0.3f, 0.26f, 0.45f }, fur);
		Part(*m_Entities, id, name + " Head", MeshShape::Sphere, { 0.0f, 0.34f, 0.24f }, { 0.2f, 0.18f, 0.2f }, fur);
		Part(*m_Entities, id, name + " Ear L", MeshShape::Cube, { -0.05f, 0.52f, 0.22f }, { 0.04f, 0.2f, 0.06f }, fur);
		Part(*m_Entities, id, name + " Ear R", MeshShape::Cube, { 0.05f, 0.52f, 0.22f }, { 0.04f, 0.2f, 0.06f }, fur);
		Part(*m_Entities, id, name + " Tail", MeshShape::Sphere, { 0.0f, 0.24f, -0.26f }, glm::vec3(0.1f), { 0.95f, 0.95f, 0.92f });

		AbilitySystemComponent* a = m_Gameplay->Create(name);
		a->Tags().Add("Animal.Rabbit");
		a->Attributes().Define("Food", food, 0.0f, Config.FoodMax);
		a->Attributes().Define("Age", age, 0.0f, 1e9f);
		a->Attributes().Define("Cooldown", 0.0f, 0.0f, 1e6f);
		a->Attributes().Define("Starving", 0.0f, 0.0f, 1e6f);
		Walk& w = m_Walks[name];
		w.Home = { at.x, at.z };
		w.Target = w.Home;
		return id;
	}

	int EcologySystem::Spawn(const glm::vec3& at, int count)
	{
		if (!m_Entities || !m_Gameplay) return 0;
		int made = 0;
		const int present = Count();
		for (int i = 0; i < count && present + made < Config.MaxAnimals; ++i, ++made)
		{
			const float angle = static_cast<float>(i) * 2.399963f;   // the golden angle: they do not pile up
			const float r = 0.6f * std::sqrt(static_cast<float>(i) + 1.0f);
			Make({ at.x + std::cos(angle) * r, 0.0f, at.z + std::sin(angle) * r }, Config.FoodMax * 0.7f, Config.AdultSeconds);
		}
		return made;
	}

	int EcologySystem::Count() const
	{
		int n = 0;
		if (!m_Gameplay) return 0;
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name); a && a->HasTag("Animal.Rabbit")) ++n;
		return n;
	}

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

	void EcologySystem::Update(double clock, float dt)
	{
		if (!m_Entities || !m_Gameplay) return;
		const SeasonClock seasons;
		const Season season = seasons.At(clock);
		const float hunger = Config.HungerPerSecond * SeasonClock::HungerRate(season);
		const float breeding = SeasonClock::GrowthRate(season);

		std::vector<std::string> names;
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name); a && a->HasTag("Animal.Rabbit")) names.push_back(name);
		int population = static_cast<int>(names.size());

		// The plants and where they stand, listed once per update the first time a rabbit looks for food (the search by name is linear in the
		// entities of the scene, and plants do not move). Whether a plant still has something is asked again at the moment of choosing.
		struct Plant { std::string Name; glm::vec2 At; };
		std::vector<Plant> plants;
		bool plantsListed = false;
		auto listPlants = [&]()
		{
			plantsListed = true;
			for (const std::string& n : m_Gameplay->Names())
			{
				const AbilitySystemComponent* plant = m_Gameplay->Find(n);
				if (!plant || !plant->HasTag("Resource.Plant")) continue;
				const EntityId pid = m_Entities->FindByName(n);
				if (pid == NullEntity) continue;
				const glm::vec4 pp = m_Entities->WorldMatrix(pid)[3];
				plants.push_back({ n, { pp.x, pp.z } });
			}
		};

		for (const std::string& name : names)
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			Walk& w = m_Walks[name];
			// The id is kept between frames (the search by name is linear in the entities of the scene); the name is checked in case the scene was replaced.
			const Entity* known = w.Id != NullEntity ? m_Entities->Find(w.Id) : nullptr;
			const EntityId id = known && known->Name == name ? w.Id : m_Entities->FindByName(name);
			if (!a || id == NullEntity) continue;
			w.Id = id;
			Entity& e = *m_Entities->Find(id);
			AttributeSet& at = a->Attributes();
			if (!w.Has) { w.Has = true; w.Home = { e.Transform.Position.x, e.Transform.Position.z }; w.Target = w.Home; }

			at.AddBase("Food", -hunger * dt);
			at.AddBase("Age", dt);
			at.AddBase("Cooldown", -dt * breeding);
			if (at.Current("Food") <= 0.0f) at.AddBase("Starving", dt); else at.SetBase("Starving", 0.0f);
			if (at.Current("Starving") > Config.StarveSeconds)
			{
				m_Entities->Destroy(id);
				m_Gameplay->Remove(name);
				m_Walks.erase(name);
				++m_Starved;
				--population;
				continue;
			}

			glm::vec2 pos(e.Transform.Position.x, e.Transform.Position.z);

			// a plant to eat when hungry
			if (w.Plant.empty() && at.Current("Food") < Config.HungryBelow)
			{
				if (!plantsListed) listPlants();
				float best = 20.0f;
				for (const Plant& p : plants)
				{
					const AbilitySystemComponent* plant = m_Gameplay->Find(p.Name);
					if (!plant || ResourceSystem::Amount(*plant) <= 0) continue;
					const float d = glm::distance(pos, p.At);
					if (d < best) { best = d; w.Plant = p.Name; w.Target = p.At; }
				}
			}

			const glm::vec2 to = w.Target - pos;
			const float distance = glm::length(to);
			const bool atPlant = !w.Plant.empty() && distance <= Config.EatReach;
			if (atPlant)
			{
				AbilitySystemComponent* plant = m_Gameplay->Find(w.Plant);
				if (!plant || ResourceSystem::Amount(*plant) <= 0) { w.Plant.clear(); w.Eating = 0.0f; }
				else
				{
					w.Eating += dt;
					if (w.Eating >= Config.EatSeconds)
					{
						w.Eating = 0.0f;
						plant->Attributes().SetBase("Amount", static_cast<float>(ResourceSystem::Amount(*plant) - 1));
						at.AddBase("Food", Config.FoodPerPlant);
						++m_Eaten;
						if (at.Current("Food") >= Config.BreedAbove + 10.0f) w.Plant.clear();   // it eats until it could breed
					}
				}
			}
			else if (distance > 0.2f)
			{
				const float step = std::min(distance, Config.Speed * dt);
				pos += to / distance * step;
				e.Transform.Rotation.y = glm::degrees(std::atan2(to.x, to.y));
			}
			else if (w.Plant.empty())
			{
				// a new leg of the wander
				const int leg = static_cast<int>(at.Current("Age") / 5.0f);
				const float angle = Hash01(name, leg, 1) * 6.2831853f;
				const float r = std::sqrt(Hash01(name, leg, 2)) * Config.WanderRadius;
				w.Target = w.Home + glm::vec2(std::cos(angle), std::sin(angle)) * r;
			}
			e.Transform.Position = { pos.x, GroundAt(pos.x, pos.y), pos.y };

			// breeding
			if (population < Config.MaxAnimals && season != Season::Winter && at.Current("Food") >= Config.BreedAbove
				&& at.Current("Age") >= Config.AdultSeconds && at.Current("Cooldown") <= 0.0f)
			{
				at.SetBase("Cooldown", Config.BreedCooldown);
				at.AddBase("Food", -15.0f);
				Make({ pos.x, 0.0f, pos.y }, 60.0f, 0.0f);
				++population;
				++m_Born;
			}
		}
	}

	std::string EcologySystem::Describe() const
	{
		return std::to_string(Count()) + " rabbits (born " + std::to_string(m_Born) + ", starved " + std::to_string(m_Starved) + ", plants eaten " + std::to_string(m_Eaten) + ")";
	}
}
