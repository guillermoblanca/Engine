#include "GB/Visual/BuildingVisuals.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		void SetAttribute(GameplayComponent& g, const std::string& name, float value, float max)
		{
			for (GameplayAttribute& a : g.Attributes)
				if (a.Name == name) { a.Value = value; a.Max = max; return; }
			g.Attributes.push_back({ name, value, 0.0f, max });
		}

		std::string BuildingTag(const Entity& e)
		{
			for (const std::string& t : e.Tags)
				if (t.rfind("Building.", 0) == 0) return t.substr(9);
			return "";
		}
	}

	namespace BuildingVisuals
	{
		EntityId Build(EntityWorld& world, const std::string& id, int phase, float progress, const std::string& name, EntityId parent, const glm::vec3& position, float yawDegrees)
		{
			const std::string wanted = name.empty() ? id + " site" : name;
			EntityId e = world.FindByName(wanted);
			if (e == NullEntity)
			{
				e = world.Create(wanted, parent);
				Entity& ent = *world.Find(e);
				ent.Transform.Position = position;
				ent.Transform.Rotation.y = yawDegrees;
				ent.Tags = { "Building", "Building." + id };
				ent.Gameplay.emplace();
				ent.Gameplay->Tags = { "Building", "Building." + id };
			}
			else
			{
				Entity& ent = *world.Find(e);
				ent.Tags.erase(std::remove_if(ent.Tags.begin(), ent.Tags.end(), [](const std::string& t) { return t.rfind("Building.", 0) == 0; }), ent.Tags.end());
				if (std::find(ent.Tags.begin(), ent.Tags.end(), "Building") == ent.Tags.end()) ent.Tags.push_back("Building");
				ent.Tags.push_back("Building." + id);
				if (!ent.Gameplay) ent.Gameplay.emplace();
			}
			Set(world, e, phase, progress);
			return e;
		}

		bool Set(EntityWorld& world, EntityId building, int phase, float progress)
		{
			Entity* e = world.Find(building);
			if (!e || BuildingTag(*e).empty()) return false;
			if (!e->Gameplay) e->Gameplay.emplace();
			SetAttribute(*e->Gameplay, "Phase", static_cast<float>(std::max(0, phase)), 1000.0f);
			SetAttribute(*e->Gameplay, "Progress", std::clamp(progress, 0.0f, 1.0f), 1.0f);
			return true;
		}

		bool Read(const EntityWorld& world, EntityId building, std::string& id, int& phase, float& progress)
		{
			const Entity* e = world.Find(building);
			if (!e) return false;
			id = BuildingTag(*e);
			if (id.empty()) return false;
			phase = 0;
			progress = 0.0f;
			if (e->Gameplay)
				for (const GameplayAttribute& a : e->Gameplay->Attributes)
				{
					if (a.Name == "Phase") phase = static_cast<int>(std::floor(a.Value));
					if (a.Name == "Progress") progress = a.Value;
				}
			return true;
		}

		int PhaseCount(const VisualLibrary& library, const std::string& id)
		{
			const auto it = library.Buildings.find(id);
			return it == library.Buildings.end() ? 0 : static_cast<int>(it->second.Phases.size());
		}

		std::string PhaseName(const VisualLibrary& library, const std::string& id, int phase)
		{
			const auto it = library.Buildings.find(id);
			if (it == library.Buildings.end() || phase < 0 || phase >= static_cast<int>(it->second.Phases.size())) return "";
			return it->second.Phases[static_cast<size_t>(phase)].Name;
		}

		float Overall(const VisualLibrary& library, const std::string& id, int phase, float progress)
		{
			const int n = PhaseCount(library, id);
			if (n <= 0) return 0.0f;
			if (phase >= n) return 1.0f;
			return std::clamp((static_cast<float>(std::max(0, phase)) + std::clamp(progress, 0.0f, 1.0f)) / static_cast<float>(n), 0.0f, 1.0f);
		}

		void FromOverall(const VisualLibrary& library, const std::string& id, float overall, int& phase, float& progress)
		{
			const int n = PhaseCount(library, id);
			overall = std::clamp(overall, 0.0f, 1.0f);
			if (n <= 0 || overall >= 1.0f) { phase = std::max(n, 0); progress = 1.0f; return; }
			const float x = overall * static_cast<float>(n);
			phase = static_cast<int>(std::floor(x));
			progress = x - static_cast<float>(phase);
		}
	}
}
