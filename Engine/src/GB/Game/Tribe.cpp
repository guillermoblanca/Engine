#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		std::string Underscored(std::string s)
		{
			std::replace(s.begin(), s.end(), ' ', '_');
			return s;
		}

		const char* AbilityFor(const std::string& kind)
		{
			return kind == "tree" ? "Chop" : kind == "rock" ? "Mine" : "Gather";
		}

		bool IsTool(const ItemLibrary& items, const std::string& id)
		{
			const ItemDef* d = items.Find(id);
			return d && d->Category == "tool";
		}

		int Carried(const ItemLibrary& items, const Inventory& bag)
		{
			int n = 0;
			for (const ItemSlot& s : bag.Stacks()) if (!IsTool(items, s.Id)) n += s.Count;
			return n;
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

	void Tribe::Bind(EntityWorld& entities, GameplayWorld& gameplay, const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		m_Defs = nodes;
		m_Entities = &entities;
		m_Gameplay = &gameplay;
		m_Items = &items;
		m_Recipes = &recipes;
	}

	void Tribe::AddMember(const std::string& name, const std::string& role, ITribeBody* body)
	{
		TribeMember m;
		m.Name = name;
		m.Role = role;
		m.Body = body;
		m_Members.push_back(m);
	}

	void Tribe::Clear()
	{
		m_Members.clear();
		m_Claims.clear();
		m_Log.clear();
		m_Store = Inventory(40, 0.0f);
		m_Time = 0.0;
		m_Think = 0.0f;
		m_Fire = false;
	}

	void Tribe::Say(const TribeMember& m, const std::string& text)
	{
		char t[32];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(t + m.Name + " (" + m.Role + ") " + text);
	}

	void Tribe::Update(float dt)
	{
		if (!m_Entities || !m_Gameplay || !m_Items || !m_Recipes) return;
		m_Time += static_cast<double>(dt);
		m_Think -= dt;
		if (m_Think > 0.0f) return;
		m_Think = Config.ThinkSeconds;
		for (TribeMember& m : m_Members)
		{
			if (!m.Body || m.Body->Busy()) continue;
			if (!m.Job.empty()) Finish(m);
			if (m.Job.empty()) Think(m);
		}
	}

	std::string Tribe::NearestNode(const TribeMember& m, const std::string& kind) const
	{
		std::string best;
		float bestDistance = 1e30f;
		const glm::vec2 from = m.Body->Position();
		for (const std::string& name : m_Gameplay->Names())
		{
			const AbilitySystemComponent* node = m_Gameplay->Find(name);
			if (!node || !node->HasTag("Resource.Node." + kind) || ResourceSystem::Amount(*node) <= 0) continue;
			if (auto claim = m_Claims.find(name); claim != m_Claims.end() && claim->second != m.Name) continue;
			const EntityId id = m_Entities->FindByName(name);
			if (id == NullEntity) continue;
			const glm::vec4 p = m_Entities->WorldMatrix(id)[3];
			const float d = glm::distance(from, glm::vec2(p.x, p.z));
			if (d < bestDistance) { bestDistance = d; best = name; }   // ties keep the first by name (Names() is ordered)
		}
		return best;
	}

	bool Tribe::StartGather(TribeMember& m, const std::string& kind)
	{
		const auto kindDef = m_Defs.find(kind);
		if (kindDef != m_Defs.end() && m_Store.Count(kindDef->second.Item) >= Config.StockCap) return false;   // enough of that for now
		const std::string node = NearestNode(m, kind);
		if (node.empty()) return false;
		std::string text, error;
		// As many swings as the node has pieces left (a swing at an empty node would fail the whole trip).
		const auto def = m_Defs.find(kind);
		const int per = def == m_Defs.end() ? 1 : std::max(1, def->second.PerGather);
		const int swings = std::max(1, std::min(Config.Swings, (ResourceSystem::Amount(*m_Gameplay->Find(node)) + per - 1) / per));
		for (int i = 0; i < swings; ++i)
			text += std::string(i ? " | " : "") + "ability " + AbilityFor(kind) + " " + Underscored(node) + " | wait 1";
		if (!m.Body->Do(text, error)) { Say(m, "cannot go to " + node + ": " + error); return false; }
		m_Claims[node] = m.Name;
		m.Job = "gather";
		m.Detail = node;
		Say(m, "goes to " + node + " (" + AbilityFor(kind) + ")");
		return true;
	}

	bool Tribe::StartCraftTrip(TribeMember& m, const std::string& tool)
	{
		std::string error;
		char go[64];
		std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(Config.Camp.x), static_cast<double>(Config.Camp.y));
		if (!m.Body->Do(go, error)) return false;
		m.Job = "craft";
		m.Detail = tool;
		Say(m, "goes to the camp to make a " + tool);
		return true;
	}

	// Every worker has the tool of its trade: the campfire must not eat the stone a pickaxe still needs.
	bool Tribe::ToolsReady() const
	{
		for (const TribeMember& w : m_Members)
		{
			const char* tag = w.Role == "woodcutter" ? "Item.Tool.Axe" : w.Role == "miner" ? "Item.Tool.Pickaxe" : nullptr;
			if (tag && w.Body->Bag().CountTagged(*m_Items, tag) == 0) return false;
		}
		return true;
	}

	void Tribe::Think(TribeMember& m)
	{
		Inventory& bag = m.Body->Bag();
		std::string tool, toolTag;
		if (m.Role == "woodcutter") { tool = "stone_axe"; toolTag = "Item.Tool.Axe"; }
		else if (m.Role == "miner") { tool = "stone_pickaxe"; toolTag = "Item.Tool.Pickaxe"; }

		// 1. the tool of the trade
		if (!tool.empty() && bag.CountTagged(*m_Items, toolTag) == 0)
		{
			if (m_Recipes->WhyNot(*m_Items, m_Store, tool, "workbench").empty() && StartCraftTrip(m, tool)) return;
			if (StartGather(m, "fiber_grass") || StartGather(m, "berry_bush")) return;   // help with what the tools lack
		}

		// 2. carry what is gathered to the store
		const int carried = Carried(*m_Items, bag);
		std::string error;
		auto deposit = [&]()
		{
			char go[64];
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(Config.Camp.x), static_cast<double>(Config.Camp.y));
			if (!m.Body->Do(go, error)) return false;
			m.Job = "deposit";
			Say(m, "carries " + std::to_string(carried) + " things to the store");
			return true;
		};
		if (carried >= Config.DepositAt && deposit()) return;

		// 3. the campfire, once the store can pay for it
		if (m.Role == "woodcutter" && !m_Fire && !tool.empty() && bag.CountTagged(*m_Items, toolTag) > 0 && ToolsReady()
			&& m_Store.Count("wood") >= 4 && m_Store.Count("stone") >= 3)
		{
			char go[64];
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(Config.Camp.x), static_cast<double>(Config.Camp.y));
			if (m.Body->Do(go, error)) { m.Job = "fire"; Say(m, "goes to light the campfire"); return; }
		}

		// 4. the work of the role
		bool started = false;
		if (m.Role == "woodcutter") started = StartGather(m, "tree");
		else if (m.Role == "miner") started = StartGather(m, "rock");
		else started = (m_Store.Count("fiber") < 12 && StartGather(m, "fiber_grass")) || StartGather(m, "berry_bush") || StartGather(m, "fiber_grass");
		if (started) return;
		if (m.Role != "forager" && (StartGather(m, "fiber_grass") || StartGather(m, "berry_bush"))) return;   // nothing of its own left: it helps

		if (carried > 0 && deposit()) return;
		if (m.Detail != "nothing to do") Say(m, "has nothing to do and rests");
		m.Job = "rest";
		m.Detail = "nothing to do";
	}

	void Tribe::Finish(TribeMember& m)
	{
		Inventory& bag = m.Body->Bag();
		const std::string job = m.Job;
		m.Job.clear();
		if (m.Body->Failed())
		{
			++m.Failures;
			Say(m, "could not finish " + job + (m.Detail.empty() ? "" : " " + m.Detail));
		}
		if (job == "gather")
		{
			m_Claims.erase(m.Detail);
		}
		else if (job == "deposit")
		{
			int moved = 0;
			for (const ItemSlot& s : std::vector<ItemSlot>(bag.Stacks()))
				if (!IsTool(*m_Items, s.Id)) moved += bag.Transfer(*m_Items, m_Store, s.Id, s.Count);
			m.Deposited += moved;
			Say(m, "leaves " + std::to_string(moved) + " in the store");
		}
		else if (job == "craft")
		{
			if (m_Recipes->Craft(*m_Items, m_Store, m.Detail, "workbench"))
			{
				m_Store.Transfer(*m_Items, bag, m.Detail, 1);
				++m.Made;
				Say(m, "makes a " + m.Detail + " and takes it");
			}
			else Say(m, "cannot make the " + m.Detail + ": " + m_Recipes->WhyNot(*m_Items, m_Store, m.Detail, "workbench"));
		}
		else if (job == "fire")
		{
			m_Store.Transfer(*m_Items, bag, "wood", 4);
			m_Store.Transfer(*m_Items, bag, "stone", 3);
			if (m_Recipes->Craft(*m_Items, bag, "campfire", ""))
			{
				bag.Remove("campfire", 1);
				BuildFire();
				++m.Made;
				Say(m, "lights the campfire");
			}
			else
			{
				// give back what was taken
				bag.Transfer(*m_Items, m_Store, "wood", 4);
				bag.Transfer(*m_Items, m_Store, "stone", 3);
				Say(m, "cannot build the campfire: " + m_Recipes->WhyNot(*m_Items, bag, "campfire", ""));
			}
		}
		if (job != "rest") m.Detail.clear();
	}

	void Tribe::BuildFire()
	{
		m_Fire = true;
		if (!m_Entities) return;
		if (const EntityId old = m_Entities->FindByName("Campfire"); old != NullEntity) m_Entities->Destroy(old);
		const EntityId fire = m_Entities->Create("Campfire");
		m_Entities->Find(fire)->Transform.Position = { Config.Camp.x + 1.5f, Config.CampY, Config.Camp.y };
		for (int i = 0; i < 6; ++i)
		{
			const float a = static_cast<float>(i) * 1.0472f;
			Part(*m_Entities, fire, "Campfire Stone " + std::to_string(i + 1), MeshShape::Sphere, { std::cos(a) * 0.45f, 0.1f, std::sin(a) * 0.45f }, glm::vec3(0.22f), { 0.4f, 0.4f, 0.42f });
		}
		Part(*m_Entities, fire, "Campfire Log", MeshShape::Cube, { 0.0f, 0.12f, 0.0f }, { 0.7f, 0.12f, 0.12f }, { 0.3f, 0.2f, 0.1f });
		const EntityId flame = Part(*m_Entities, fire, "Campfire Flame", MeshShape::Sphere, { 0.0f, 0.4f, 0.0f }, { 0.3f, 0.5f, 0.3f }, { 1.0f, 0.55f, 0.1f });
		m_Entities->Find(flame)->Mesh->Emissive = { 1.6f, 0.7f, 0.1f };
		Entity& light = *m_Entities->Find(m_Entities->Create("Campfire Light", fire));
		light.Transform.Position = { 0.0f, 0.8f, 0.0f };
		light.Light.emplace();
		light.Light->Color = { 1.0f, 0.6f, 0.25f };
		light.Light->Intensity = 12.0f;
		light.Light->Radius = 9.0f;
	}

	std::string Tribe::Report() const
	{
		std::string s = "store:";
		for (const std::string& id : m_Items->Ids())
			if (const int n = m_Store.Count(id)) s += " " + id + " x" + std::to_string(n);
		if (m_Store.Stacks().empty()) s += " empty";
		s += m_Fire ? "; the campfire is lit\n" : "; no campfire yet\n";
		for (const TribeMember& m : m_Members)
		{
			s += m.Name + " (" + m.Role + "): " + (m.Job.empty() ? "idle" : m.Job + (m.Detail.empty() ? "" : " " + m.Detail))
				+ ", carried " + std::to_string(m.Deposited) + " to the store, made " + std::to_string(m.Made);
			if (m.Failures) s += ", " + std::to_string(m.Failures) + " failed trips";
			s += "\n";
		}
		return s;
	}
}
