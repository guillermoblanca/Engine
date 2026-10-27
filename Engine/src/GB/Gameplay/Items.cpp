#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	const ItemDef* ItemLibrary::Find(const std::string& id) const
	{
		auto it = m_Items.find(id);
		return it == m_Items.end() ? nullptr : &it->second;
	}

	std::vector<std::string> ItemLibrary::Ids() const
	{
		std::vector<std::string> out;
		for (const auto& kv : m_Items) out.push_back(kv.first);
		return out;
	}

	int Inventory::Add(const ItemLibrary& lib, const std::string& id, int count)
	{
		const ItemDef* def = lib.Find(id);
		if (!def || count <= 0) return 0;
		const int maxStack = std::max(1, def->MaxStack);

		// The weight limit caps how many units can come in at all.
		if (m_MaxWeight > 0.0f && def->Weight > 0.0f)
		{
			const float room = m_MaxWeight - Weight(lib);
			const int byWeight = room <= 0.0f ? 0 : static_cast<int>(room / def->Weight + 1e-4f);
			count = std::min(count, byWeight);
		}

		int added = 0;
		for (ItemSlot& s : m_Stacks)
		{
			if (count <= 0) break;
			if (s.Id != id || s.Count >= maxStack) continue;
			const int n = std::min(count, maxStack - s.Count);
			s.Count += n; count -= n; added += n;
		}
		while (count > 0 && UsedSlots() < m_Slots)
		{
			const int n = std::min(count, maxStack);
			m_Stacks.push_back({ id, n });
			count -= n; added += n;
		}
		return added;
	}

	int Inventory::Remove(const std::string& id, int count)
	{
		int taken = 0;
		for (int i = static_cast<int>(m_Stacks.size()) - 1; i >= 0 && count > 0; --i)
		{
			ItemSlot& s = m_Stacks[i];
			if (s.Id != id) continue;
			const int n = std::min(count, s.Count);
			s.Count -= n; count -= n; taken += n;
			if (s.Count == 0) m_Stacks.erase(m_Stacks.begin() + i);
		}
		return taken;
	}

	int Inventory::Count(const std::string& id) const
	{
		int n = 0;
		for (const ItemSlot& s : m_Stacks) if (s.Id == id) n += s.Count;
		return n;
	}

	int Inventory::CountTagged(const ItemLibrary& lib, const std::string& tag) const
	{
		int n = 0;
		for (const ItemSlot& s : m_Stacks)
		{
			const ItemDef* def = lib.Find(s.Id);
			if (!def) continue;
			for (const std::string& t : def->Tags)
				if (GameplayTag::TagMatches(tag, t)) { n += s.Count; break; }
		}
		return n;
	}

	float Inventory::Weight(const ItemLibrary& lib) const
	{
		float w = 0.0f;
		for (const ItemSlot& s : m_Stacks)
			if (const ItemDef* def = lib.Find(s.Id)) w += def->Weight * static_cast<float>(s.Count);
		return w;
	}

	int Inventory::Transfer(const ItemLibrary& lib, Inventory& to, const std::string& id, int count)
	{
		count = std::min(count, Count(id));
		const int moved = to.Add(lib, id, count);
		Remove(id, moved);
		return moved;
	}

	std::string Inventory::ToJson() const
	{
		Json j = Json::object();
		j["slots"] = m_Slots;
		j["maxWeight"] = m_MaxWeight;
		Json items = Json::array();
		for (const ItemSlot& s : m_Stacks) items.push_back(Json::array({ s.Id, s.Count }));
		j["items"] = items;
		return j.dump();
	}

	bool Inventory::FromJson(const std::string& text)
	{
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("items") || !j["items"].is_array()) return false;
		std::vector<ItemSlot> stacks;
		for (const Json& e : j["items"])
		{
			if (!e.is_array() || e.size() != 2 || !e[0].is_string() || !e[1].is_number_integer()) return false;
			stacks.push_back({ e[0].get<std::string>(), e[1].get<int>() });
		}
		m_Slots = j.value("slots", m_Slots);
		m_MaxWeight = j.value("maxWeight", m_MaxWeight);
		m_Stacks = std::move(stacks);
		return true;
	}

	int ResourceNode::Gather()
	{
		if (m_Amount <= 0) return 0;
		const int n = std::min(m_Amount, std::max(1, m_Def.PerGather));
		m_Amount -= n;
		return n;
	}

	void ResourceNode::Update(float dt)
	{
		if (m_Def.RegenSeconds <= 0.0f || m_Amount >= m_Def.Capacity) { m_Timer = 0.0f; return; }
		m_Timer += dt;
		while (m_Timer >= m_Def.RegenSeconds && m_Amount < m_Def.Capacity)
		{
			m_Timer -= m_Def.RegenSeconds;
			m_Amount = std::min(m_Def.Capacity, m_Amount + std::max(1, m_Def.Regen));
		}
	}

	void ResourceNode::SetAmount(int amount)
	{
		m_Amount = std::max(0, std::min(amount, m_Def.Capacity));
	}

	const Recipe* RecipeBook::Find(const std::string& id) const
	{
		auto it = m_Recipes.find(id);
		return it == m_Recipes.end() ? nullptr : &it->second;
	}

	std::vector<std::string> RecipeBook::Ids() const
	{
		std::vector<std::string> out;
		for (const auto& kv : m_Recipes) out.push_back(kv.first);
		return out;
	}

	std::string RecipeBook::WhyNot(const ItemLibrary& lib, const Inventory& inv, const std::string& id, const std::string& station) const
	{
		const Recipe* r = Find(id);
		if (!r) return "unknown recipe '" + id + "'";
		if (!r->Station.empty() && r->Station != station) return "needs the station '" + r->Station + "'";
		if (!r->ToolTag.empty() && inv.CountTagged(lib, r->ToolTag) == 0) return "needs a tool (" + r->ToolTag + ")";
		for (const auto& in : r->Inputs)
			if (inv.Count(in.first) < in.second)
				return "missing " + std::to_string(in.second - inv.Count(in.first)) + " " + in.first;

		// Room for the product, checked on a copy that has already given up the inputs.
		Inventory probe = inv;
		for (const auto& in : r->Inputs) probe.Remove(in.first, in.second);
		if (probe.Add(lib, r->Output, r->OutputCount) < r->OutputCount) return "no room for " + r->Output;
		return {};
	}

	bool RecipeBook::Craft(const ItemLibrary& lib, Inventory& inv, const std::string& id, const std::string& station) const
	{
		if (!WhyNot(lib, inv, id, station).empty()) return false;
		const Recipe* r = Find(id);
		for (const auto& in : r->Inputs) inv.Remove(in.first, in.second);
		inv.Add(lib, r->Output, r->OutputCount);
		return true;
	}

	void RegisterEraOneItems(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes)
	{
		items.Add({ "wood",      "Wood",        "resource", 2.0f, 10, { "Item.Resource.Wood" },  "", "" });
		items.Add({ "stone",     "Stone",       "resource", 3.0f, 10, { "Item.Resource.Stone" }, "", "" });
		items.Add({ "fiber",     "Plant fiber", "resource", 0.2f, 20, { "Item.Resource.Fiber" }, "", "" });
		items.Add({ "berry",     "Berry",       "food",     0.1f, 20, { "Item.Food" },           "Eat", "" });
		items.Add({ "stone_axe",     "Stone axe",     "tool", 2.5f, 1, { "Item.Tool.Axe" },     "Chop", "RightHand" });
		items.Add({ "stone_pickaxe", "Stone pickaxe", "tool", 3.0f, 1, { "Item.Tool.Pickaxe" }, "Mine", "RightHand" });
		items.Add({ "water",     "Water",       "food",     1.0f,  5, { "Item.Food.Water" },     "Drink", "" });
		items.Add({ "campfire",  "Campfire kit", "structure", 6.0f, 1, { "Item.Structure.Fire" }, "Place", "" });

		recipes.Add({ "stone_axe",     "workbench", "",                  { { "wood", 2 }, { "stone", 2 }, { "fiber", 3 } }, "stone_axe",     1, "Craft" });
		recipes.Add({ "stone_pickaxe", "workbench", "",                  { { "wood", 2 }, { "stone", 3 }, { "fiber", 3 } }, "stone_pickaxe", 1, "Craft" });
		recipes.Add({ "campfire",      "",          "Item.Tool.Axe",     { { "wood", 4 }, { "stone", 3 } },                 "campfire",      1, "Craft" });

		nodes["tree"]       = { "tree",       "wood",  5, 1, 30.0f, 1, "Item.Tool.Axe",     "Chop" };
		nodes["rock"]       = { "rock",       "stone", 4, 1,  0.0f, 1, "Item.Tool.Pickaxe", "Mine" };
		nodes["berry_bush"] = { "berry_bush", "berry", 6, 2, 20.0f, 2, "",                  "Gather" };
		nodes["fiber_grass"]= { "fiber_grass","fiber", 8, 2, 15.0f, 2, "",                  "Gather" };
	}
}
