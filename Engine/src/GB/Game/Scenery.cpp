#include "GB/Game/Scenery.h"
#include <algorithm>
#include <cstdlib>

namespace GB
{
	bool ParseSceneryName(const std::string& name, PropKind& kind, int& index)
	{
		std::string number;
		if (name.rfind("Tree ", 0) == 0) { kind = PropKind::Tree; number = name.substr(5); }
		else if (name.rfind("Rock ", 0) == 0) { kind = PropKind::Rock; number = name.substr(5); }
		else return false;
		if (number.empty() || number.size() > 9 || !std::all_of(number.begin(), number.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) return false;
		index = std::atoi(number.c_str());
		return index > 0;
	}

	SceneryHost EntityScenery(EntityWorld& world, std::function<float(const Entity&, float)> radius)
	{
		SceneryHost host;
		EntityWorld* w = &world;
		host.Around = [w, radius](const glm::vec2& lo, const glm::vec2& hi, std::vector<SceneryProp>& out)
		{
			out.clear();
			for (const auto& [id, e] : w->All())
			{
				if (!e.Prop || e.Gameplay || !e.Children.empty()) continue;
				SceneryProp p;
				if (!ParseSceneryName(e.Name, p.Kind, p.Index) || p.Kind != e.Prop->Kind) continue;
				const glm::mat4 m = w->WorldMatrix(id);
				const glm::vec3 at(m[3]);
				if (at.x < lo.x || at.x > hi.x || at.z < lo.y || at.z > hi.y) continue;
				p.Name = e.Name;
				p.Variant = e.Prop->Variant;
				p.Position = at;
				p.Yaw = e.Transform.Rotation.y;
				p.Scale = e.Transform.Scale.x;
				p.Radius = radius ? radius(e, glm::length(glm::vec3(m[0]))) : 0.0f;
				out.push_back(std::move(p));
			}
			std::sort(out.begin(), out.end(), [](const SceneryProp& a, const SceneryProp& b) { return a.Kind != b.Kind ? a.Kind < b.Kind : a.Index < b.Index; });
		};
		host.Remove = [w](const std::string& name)
		{
			PropKind kind;
			int index = 0;
			if (!ParseSceneryName(name, kind, index)) return false;
			const EntityId id = w->FindByName(name);
			const Entity* e = w->Find(id);
			if (!e || !e->Prop || e->Gameplay || !e->Children.empty()) return false;
			return w->Destroy(id);
		};
		return host;
	}
}
