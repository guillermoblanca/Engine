#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/GameplayTag.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
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

	const std::vector<std::string>& ItemLibrary::ToolTags() const
	{
		if (!m_ToolTags)
		{
			auto tags = std::make_shared<std::vector<std::string>>();
			for (const auto& [id, def] : m_Items)
				for (const std::string& tag : def.Tags)
					if (tag.rfind("Item.Tool.", 0) == 0 && std::find(tags->begin(), tags->end(), tag) == tags->end()) tags->push_back(tag);
			m_ToolTags = std::move(tags);
		}
		return *m_ToolTags;
	}

	namespace
	{
		// Does the item carry `tag` or a descendant of it?
		bool Carries(const ItemDef& def, const std::string& tag)
		{
			for (const std::string& t : def.Tags)
				if (GameplayTag::TagMatches(tag, t)) return true;
			return false;
		}
	}

	std::string ToolFamily(const std::string& tag)
	{
		size_t end = 0;
		for (int dots = 0; end < tag.size(); ++end)
			if (tag[end] == '.' && ++dots == 3) break;
		return tag.substr(0, end);
	}

	const ItemDef* Inventory::BestContainer(const ItemLibrary& lib) const
	{
		const ItemDef* best = nullptr;
		for (const ItemSlot& s : m_Stacks)
		{
			const ItemDef* def = lib.Find(s.Id);
			if (!def || s.Count < 1 || !Carries(*def, "Item.Container")) continue;
			const bool better = !best || def->BagSlots > best->BagSlots || (def->BagSlots == best->BagSlots && (def->BagWeight > best->BagWeight
				|| (def->BagWeight == best->BagWeight && def->Id < best->Id)));
			if (better) best = def;
		}
		return best;
	}

	void Inventory::SyncCapacity(const ItemLibrary& lib)
	{
		const ItemDef* c = BestContainer(lib);
		SetBonus(c ? c->BagSlots : 0, c ? c->BagWeight : 0.0f);
	}

	const ItemDef* Inventory::BestTool(const ItemLibrary& lib, const std::string& tag) const
	{
		const ItemDef* best = nullptr;
		for (const ItemSlot& s : m_Stacks)
		{
			const ItemDef* def = lib.Find(s.Id);
			if (!def || s.Count < 1) continue;
			if (!Carries(*def, tag)) continue;
			const bool better = !best || def->Power > best->Power || (def->Power == best->Power && (def->Speed > best->Speed
				|| (def->Speed == best->Speed && def->Id < best->Id)));
			if (better) best = def;
		}
		return best;
	}

	int Inventory::Add(const ItemLibrary& lib, const std::string& id, int count)
	{
		return Add(lib, id, count, 0.0f);
	}

	int Inventory::Add(const ItemLibrary& lib, const std::string& id, int count, float age)
	{
		const ItemDef* def = lib.Find(id);
		age = std::max(0.0f, age);
		if (!def || count <= 0) return 0;
		const int maxStack = std::max(1, def->MaxStack);

		// The weight limit caps how many units can come in at all.
		if (MaxWeight() > 0.0f && def->Weight > 0.0f)
		{
			const float room = MaxWeight() - Weight(lib);
			const int byWeight = room <= 0.0f ? 0 : static_cast<int>(room / def->Weight + 1e-4f);
			count = std::min(count, byWeight);
		}

		int added = 0;
		for (ItemSlot& s : m_Stacks)
		{
			if (count <= 0) break;
			if (s.Id != id || s.Count >= maxStack) continue;
			const int n = std::min(count, maxStack - s.Count);
			if (s.Age != age) s.Age = (s.Age * static_cast<float>(s.Count) + age * static_cast<float>(n)) / static_cast<float>(s.Count + n);   // E6: the mean age of its units
			s.Count += n; count -= n; added += n;
		}
		while (count > 0 && UsedSlots() < Slots())
		{
			const int n = std::min(count, maxStack);
			m_Stacks.push_back({ id, n, age });
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

	int Inventory::RemoveOldest(const std::string& id, int count)
	{
		int taken = 0;
		while (count > 0)
		{
			int oldest = -1;
			for (int i = 0; i < static_cast<int>(m_Stacks.size()); ++i)
				if (m_Stacks[i].Id == id && (oldest < 0 || m_Stacks[i].Age > m_Stacks[oldest].Age)) oldest = i;   // ties: the first
			if (oldest < 0) break;
			ItemSlot& s = m_Stacks[oldest];
			const int n = std::min(count, s.Count);
			s.Count -= n; count -= n; taken += n;
			if (s.Count == 0) m_Stacks.erase(m_Stacks.begin() + oldest);
		}
		return taken;
	}

	float Inventory::OldestAge(const std::string& id) const
	{
		float age = 0.0f;
		for (const ItemSlot& s : m_Stacks) if (s.Id == id) age = std::max(age, s.Age);
		return age;
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
			if (def && Carries(*def, tag)) n += s.Count;
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
		// stack by stack from the last (as Remove takes them), each with its age (E6), until the other is full
		int moved = 0;
		for (int i = static_cast<int>(m_Stacks.size()) - 1; i >= 0 && count > 0; --i)
		{
			ItemSlot& s = m_Stacks[i];
			if (s.Id != id) continue;
			const int want = std::min(count, s.Count);
			const int n = to.Add(lib, id, want, s.Age);
			s.Count -= n; count -= n; moved += n;
			if (s.Count == 0) m_Stacks.erase(m_Stacks.begin() + i);
			if (n < want) break;
		}
		return moved;
	}

	std::string Inventory::ToJson() const
	{
		Json j = Json::object();
		j["slots"] = m_Slots;
		j["maxWeight"] = m_MaxWeight;
		Json items = Json::array();
		for (const ItemSlot& s : m_Stacks)
			items.push_back(s.Age != 0.0f ? Json::array({ s.Id, s.Count, std::round(static_cast<double>(s.Age) * 100.0) / 100.0 }) : Json::array({ s.Id, s.Count }));
		j["items"] = items;
		if (m_BonusSlots > 0) j["bonusSlots"] = m_BonusSlots;
		if (m_BonusWeight > 0.0f) j["bonusWeight"] = m_BonusWeight;
		return j.dump();
	}

	bool Inventory::FromJson(const std::string& text)
	{
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("items") || !j["items"].is_array()) return false;
		std::vector<ItemSlot> stacks;
		for (const Json& e : j["items"])
		{
			// (a stack of nothing or of a negative count would break every sum made over the stacks)
			if (!e.is_array() || (e.size() != 2 && e.size() != 3) || !e[0].is_string() || !e[1].is_number_integer() || e[1].get<long long>() < 1 || e[1].get<long long>() > 1000000) return false;
			// (E6: the age of the stack, when there is one; a text from before has none and the stack is fresh)
			if (e.size() == 3 && (!e[2].is_number() || !(e[2].get<double>() >= 0.0) || e[2].get<double>() > 1e9)) return false;
			stacks.push_back({ e[0].get<std::string>(), e[1].get<int>(), e.size() == 3 ? e[2].get<float>() : 0.0f });
		}
		int slots = m_Slots, bonusSlots = 0;
		float maxWeight = m_MaxWeight, bonusWeight = 0.0f;
		try
		{
			slots = j.value("slots", m_Slots);   // (throws when the value is not a number: a damaged file must not take the program down)
			maxWeight = j.value("maxWeight", m_MaxWeight);
			bonusSlots = j.value("bonusSlots", 0);
			bonusWeight = j.value("bonusWeight", 0.0f);
		}
		catch (const Json::exception&)
		{
			return false;
		}
		if (slots < 0 || !(maxWeight >= 0.0f) || bonusSlots < 0 || !(bonusWeight >= 0.0f)) return false;
		m_Slots = slots;
		m_MaxWeight = maxWeight;
		m_BonusSlots = bonusSlots;
		m_BonusWeight = bonusWeight;
		m_Stacks = std::move(stacks);
		return true;
	}

	namespace
	{
		std::string Num(float v, const char* format)
		{
			char b[48];
			std::snprintf(b, sizeof b, format, static_cast<double>(v));
			return b;
		}
	}

	std::string CapacityText(const ItemLibrary& lib, const Inventory& bag)
	{
		std::string out = std::to_string(bag.UsedSlots()) + " / " + std::to_string(bag.Slots()) + " slots";
		if (const ItemDef* c = bag.BestContainer(lib); c && bag.BonusSlots() > 0) out += " (" + std::to_string(bag.BaseSlots()) + " + " + std::to_string(bag.BonusSlots()) + " from " + c->Name + ")";
		out += ", " + Num(bag.Weight(lib), "%.1f");
		if (bag.MaxWeight() > 0.0f) out += " / " + Num(bag.MaxWeight(), "%.1f") + " kg";
		else out += " kg (no weight limit)";
		return out;
	}

	std::vector<std::string> ActiveTools(const ItemLibrary& lib, const Inventory& bag)
	{
		std::vector<std::string> families, out;
		for (const ItemSlot& s : bag.Stacks())
			if (const ItemDef* def = lib.Find(s.Id))
				for (const std::string& t : def->Tags)
					if (t.rfind("Item.Tool.", 0) == 0 && std::find(families.begin(), families.end(), ToolFamily(t)) == families.end()) families.push_back(ToolFamily(t));
		std::sort(families.begin(), families.end());
		for (const std::string& family : families)
			if (const ItemDef* best = bag.BestTool(lib, family))
				out.push_back(family + ": " + best->Name + " (power +" + std::to_string(best->Power) + ", speed x" + Num(best->Speed, "%.2f") + ")");
		return out;
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

	std::string RecipeBook::WhyNot(const ItemLibrary& lib, const Inventory& inv, const std::string& id, const std::string& station, const std::set<std::string>* known) const
	{
		if (known && Find(id) && !known->count(id)) return "does not know how to make '" + id + "'";
		return WhyNot(lib, inv, id, station);
	}

	bool RecipeBook::Craft(const ItemLibrary& lib, Inventory& inv, const std::string& id, const std::string& station, const std::set<std::string>* known) const
	{
		if (known && !known->count(id)) return false;
		return Craft(lib, inv, id, station);
	}

	std::vector<std::string> RecipeBook::Known(const std::set<std::string>* known) const
	{
		std::vector<std::string> out;
		for (const auto& kv : m_Recipes) if (!known || known->count(kv.first)) out.push_back(kv.first);
		return out;
	}

	bool RecipeBook::Craft(const ItemLibrary& lib, Inventory& inv, const std::string& id, const std::string& station) const
	{
		if (!WhyNot(lib, inv, id, station).empty()) return false;
		const Recipe* r = Find(id);
		for (const auto& in : r->Inputs) inv.Remove(in.first, in.second);
		inv.Add(lib, r->Output, r->OutputCount);
		return true;
	}

	namespace
	{
		double Round3(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.1f is written as 0.1
	}

	std::string LoadContent(const std::string& text, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes)
	{
		Json j = Json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		std::vector<ItemDef> newItems;
		std::vector<Recipe> newRecipes;
		std::vector<ResourceNodeDef> newNodes;
		auto str = [](const Json& o, const char* key, std::string& out) { if (auto it = o.find(key); it != o.end() && it->is_string()) out = it->get<std::string>(); };
		try
		{
			if (auto it = j.find("items"); it != j.end())
				for (const Json& o : *it)
				{
					ItemDef d;
					str(o, "id", d.Id);
					if (d.Id.empty()) return "an item has no id";
					d.Name = d.Id;
					str(o, "name", d.Name);
					str(o, "category", d.Category);
					d.Weight = o.value("weight", 1.0f);
					d.MaxStack = o.value("stack", 1);
					if (d.MaxStack < 1) return "item '" + d.Id + "': stack must be at least 1";
					if (d.Weight < 0.0f) return "item '" + d.Id + "': negative weight";
					if (auto t = o.find("tags"); t != o.end() && !t->is_array()) return "item '" + d.Id + "': tags must be a list";
					if (auto t = o.find("tags"); t != o.end()) for (const Json& x : *t) { if (!GameplayTag::IsValidTagName(x.get<std::string>())) return "item '" + d.Id + "': bad tag " + x.get<std::string>(); d.Tags.push_back(x.get<std::string>()); }
					str(o, "ability", d.Ability);
					str(o, "socket", d.Socket);
					d.Power = o.value("power", 0);
					d.Speed = o.value("speed", 1.0f);
					d.BagSlots = o.value("bagSlots", 0);
					d.BagWeight = o.value("bagWeight", 0.0f);
					d.Fuel = o.value("fuel", 0);
					if (d.Fuel < 0) return "item '" + d.Id + "': fuel cannot be negative";
					d.ShelfLife = o.value("shelfLife", 0.0f);
					str(o, "spoilsTo", d.SpoilsTo);
					if (!(d.ShelfLife >= 0.0f)) return "item '" + d.Id + "': shelfLife cannot be negative";
					if (d.SpoilsTo == d.Id) return "item '" + d.Id + "': it cannot spoil into itself";
					if (d.Power < 0 || d.BagSlots < 0 || d.BagWeight < 0.0f) return "item '" + d.Id + "': power, bagSlots and bagWeight cannot be negative";
					if (!(d.Speed >= 0.1f && d.Speed <= 10.0f)) return "item '" + d.Id + "': speed must be between 0.1 and 10";
					newItems.push_back(d);
				}
			if (auto it = j.find("recipes"); it != j.end())
				for (const Json& o : *it)
				{
					Recipe r;
					str(o, "id", r.Id);
					if (r.Id.empty()) return "a recipe has no id";
					str(o, "station", r.Station);
					str(o, "tool", r.ToolTag);
					str(o, "output", r.Output);
					str(o, "ability", r.Ability);
					r.OutputCount = o.value("count", 1);
					if (r.Output.empty() || r.OutputCount < 1) return "recipe '" + r.Id + "': it needs an output and a count of at least 1";
					if (auto in = o.find("inputs"); in != o.end())
						for (auto kv = in->begin(); kv != in->end(); ++kv)
						{
							if (kv.value().get<int>() < 1) return "recipe '" + r.Id + "': an input needs at least 1";
							r.Inputs[kv.key()] = kv.value().get<int>();
						}
					newRecipes.push_back(r);
				}
			if (auto it = j.find("nodes"); it != j.end())
				for (const Json& o : *it)
				{
					ResourceNodeDef d;
					str(o, "id", d.Id);
					str(o, "item", d.Item);
					if (d.Id.empty() || d.Item.empty()) return "a resource node needs an id and an item";
					d.Capacity = o.value("capacity", 5);
					d.PerGather = o.value("perGather", 1);
					d.RegenSeconds = o.value("regenSeconds", 0.0f);
					d.Regen = o.value("regen", 1);
					if (d.Capacity < 1 || d.PerGather < 1 || d.Regen < 1 || d.RegenSeconds < 0.0f) return "node '" + d.Id + "': capacity, perGather and regen must be at least 1";
					str(o, "requires", d.RequiresTag);
					str(o, "ability", d.Ability);
					str(o, "kind", d.Kind);
					str(o, "model", d.Model);
					d.Decor = o.value("decor", false);
					if (!d.Kind.empty() && !GameplayTag::IsValidTagName("Resource." + d.Kind)) return "node '" + d.Id + "': bad kind " + d.Kind;
					if (auto pt = o.find("patch"); pt != o.end())
					{
						if (!pt->is_object()) return "node '" + d.Id + "': patch must be an object";
						d.Patch.Count = pt->value("count", 0);
						d.Patch.PerWorker = pt->value("perWorker", 0);
						d.Patch.Spacing = pt->value("spacing", 4.0f);
						str(*pt, "shape", d.Patch.Shape);
						str(*pt, "near", d.Patch.Near);
						if (d.Patch.Count < 0 || d.Patch.PerWorker < 0) return "node '" + d.Id + "': the patch counts cannot be negative";
						if (!(d.Patch.Spacing >= 0.5f && d.Patch.Spacing <= 100.0f)) return "node '" + d.Id + "': the patch spacing must be between 0.5 and 100";
						if (d.Patch.Shape != "row" && d.Patch.Shape != "ring" && d.Patch.Shape != "cluster") return "node '" + d.Id + "': the patch shape must be row, ring or cluster";
					}
					if (auto sp = o.find("spawn"); sp != o.end())
					{
						if (!sp->is_object()) return "node '" + d.Id + "': spawn must be an object";
						static const char* const kBiomes[] = { "Meadow", "Forest", "Shore", "Rock", "Snow", "Dunes" };
						if (auto bs = sp->find("biomes"); bs != sp->end())
							for (const Json& b : *bs)
							{
								const std::string name = b.get<std::string>();
								if (std::find(std::begin(kBiomes), std::end(kBiomes), name) == std::end(kBiomes)) return "node '" + d.Id + "': unknown spawn biome '" + name + "'";
								d.Spawn.Biomes.push_back(name);
							}
						d.Spawn.PerHectare = sp->value("perHectare", 0.0f);
						d.Spawn.Spacing = sp->value("spacing", 6.0f);
						d.Spawn.MaxSlope = sp->value("maxSlope", 35.0f);
						d.Spawn.WaterWithin = sp->value("waterWithin", 0.0f);
						d.Spawn.Clump = sp->value("clump", 0.0f);
						d.Spawn.Max = sp->value("max", 0);
						str(*sp, "prop", d.Spawn.Prop);
						d.Spawn.ScaleMin = sp->value("scaleMin", 1.0f);
						d.Spawn.ScaleMax = sp->value("scaleMax", d.Spawn.ScaleMin);
						d.Spawn.Stack = sp->value("stack", 0);
						if (!d.Spawn.Prop.empty() && d.Spawn.Prop != "tree" && d.Spawn.Prop != "rock") return "node '" + d.Id + "': spawn prop must be tree or rock";
						if (!(d.Spawn.ScaleMin >= 0.1f && d.Spawn.ScaleMax >= d.Spawn.ScaleMin && d.Spawn.ScaleMax <= 6.0f) || d.Spawn.Stack < 0 || d.Spawn.Stack > 4) return "node '" + d.Id + "': spawn scaleMin/scaleMax (0.1..6) or stack (0..4) out of range";
						if (!(d.Spawn.PerHectare >= 0.0f && d.Spawn.PerHectare <= 500.0f)) return "node '" + d.Id + "': spawn perHectare must be between 0 and 500";
						if (!(d.Spawn.Spacing >= 1.0f && d.Spawn.Spacing <= 100.0f)) return "node '" + d.Id + "': the spawn spacing must be between 1 and 100";
						if (!(d.Spawn.MaxSlope >= 0.0f && d.Spawn.MaxSlope <= 90.0f)) return "node '" + d.Id + "': spawn maxSlope must be between 0 and 90";
						if (!(d.Spawn.WaterWithin >= 0.0f) || !(d.Spawn.Clump >= 0.0f && d.Spawn.Clump <= 0.99f) || d.Spawn.Max < 0) return "node '" + d.Id + "': spawn waterWithin, clump (0..0.99) or max out of range";
					}
					newNodes.push_back(d);
				}
		}
		catch (const std::exception& e)
		{
			return std::string("the content has a value of the wrong type (") + e.what() + ")";
		}
		// what refers to what must exist once everything is in
		ItemLibrary probe = items;
		for (const ItemDef& d : newItems) probe.Add(d);
		for (const Recipe& r : newRecipes)
		{
			if (!probe.Find(r.Output)) return "recipe '" + r.Id + "' makes an unknown item '" + r.Output + "'";
			for (const auto& in : r.Inputs) if (!probe.Find(in.first)) return "recipe '" + r.Id + "' needs an unknown item '" + in.first + "'";
		}
		for (const ResourceNodeDef& d : newNodes)
			if (!probe.Find(d.Item)) return "node '" + d.Id + "' gives an unknown item '" + d.Item + "'";
		for (const ItemDef& d : newItems)
			if (!d.SpoilsTo.empty() && !probe.Find(d.SpoilsTo)) return "item '" + d.Id + "' spoils into an unknown item '" + d.SpoilsTo + "'";
		for (const ItemDef& d : newItems) items.Add(d);
		for (const Recipe& r : newRecipes) recipes.Add(r);
		for (const ResourceNodeDef& d : newNodes) nodes[d.Id] = d;
		return {};
	}

	std::string LoadContentFile(const std::string& path, ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadContent(ss.str(), items, recipes, nodes);
		return error.empty() ? error : path + ": " + error;
	}

	std::string ContentToJson(const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		Json j = Json::object();
		Json ji = Json::array();
		for (const std::string& id : items.Ids())
		{
			const ItemDef& d = *items.Find(id);
			Json o = { { "id", d.Id }, { "name", d.Name }, { "category", d.Category }, { "weight", Round3(d.Weight) }, { "stack", d.MaxStack }, { "tags", d.Tags } };
			if (!d.Ability.empty()) o["ability"] = d.Ability;
			if (!d.Socket.empty()) o["socket"] = d.Socket;
			if (d.Power != 0) o["power"] = d.Power;
			if (d.Speed != 1.0f) o["speed"] = Round3(d.Speed);
			if (d.BagSlots != 0) o["bagSlots"] = d.BagSlots;
			if (d.BagWeight != 0.0f) o["bagWeight"] = Round3(d.BagWeight);
			if (d.Fuel != 0) o["fuel"] = d.Fuel;
			if (d.ShelfLife != 0.0f) o["shelfLife"] = Round3(d.ShelfLife);
			if (!d.SpoilsTo.empty()) o["spoilsTo"] = d.SpoilsTo;
			ji.push_back(o);
		}
		j["items"] = ji;
		Json jr = Json::array();
		for (const std::string& id : recipes.Ids())
		{
			const Recipe& r = *recipes.Find(id);
			Json inputs = Json::object();
			for (const auto& in : r.Inputs) inputs[in.first] = in.second;
			Json o = { { "id", r.Id }, { "inputs", inputs }, { "output", r.Output }, { "count", r.OutputCount } };
			if (!r.Station.empty()) o["station"] = r.Station;
			if (!r.ToolTag.empty()) o["tool"] = r.ToolTag;
			if (!r.Ability.empty()) o["ability"] = r.Ability;
			jr.push_back(o);
		}
		j["recipes"] = jr;
		Json jn = Json::array();
		for (const auto& [id, d] : nodes)
		{
			Json o = { { "id", d.Id }, { "item", d.Item }, { "capacity", d.Capacity }, { "perGather", d.PerGather }, { "regenSeconds", Round3(d.RegenSeconds) }, { "regen", d.Regen } };
			if (!d.RequiresTag.empty()) o["requires"] = d.RequiresTag;
			if (!d.Ability.empty()) o["ability"] = d.Ability;
			if (!d.Kind.empty()) o["kind"] = d.Kind;
			if (!d.Model.empty()) o["model"] = d.Model;
			if (d.Decor) o["decor"] = true;
			if (d.Patch.Count != 0 || d.Patch.PerWorker != 0)
			{
				Json pt = { { "count", d.Patch.Count }, { "perWorker", d.Patch.PerWorker }, { "spacing", Round3(d.Patch.Spacing) }, { "shape", d.Patch.Shape } };
				if (!d.Patch.Near.empty()) pt["near"] = d.Patch.Near;
				o["patch"] = pt;
			}
			if (!d.Spawn.Biomes.empty() || d.Spawn.PerHectare > 0.0f || !d.Spawn.Prop.empty())   // a half-filled spawn is written too, so nothing is lost
			{
				Json sp = { { "biomes", d.Spawn.Biomes }, { "perHectare", Round3(d.Spawn.PerHectare) }, { "spacing", Round3(d.Spawn.Spacing) }, { "maxSlope", Round3(d.Spawn.MaxSlope) } };
				if (d.Spawn.WaterWithin > 0.0f) sp["waterWithin"] = Round3(d.Spawn.WaterWithin);
				if (d.Spawn.Clump > 0.0f) sp["clump"] = Round3(d.Spawn.Clump);
				if (d.Spawn.Max > 0) sp["max"] = d.Spawn.Max;
				if (!d.Spawn.Prop.empty()) sp["prop"] = d.Spawn.Prop;
				if (d.Spawn.ScaleMin != 1.0f || d.Spawn.ScaleMax != 1.0f) { sp["scaleMin"] = Round3(d.Spawn.ScaleMin); sp["scaleMax"] = Round3(d.Spawn.ScaleMax); }
				if (d.Spawn.Stack > 0) sp["stack"] = d.Spawn.Stack;
				o["spawn"] = sp;
			}
			jn.push_back(o);
		}
		j["nodes"] = jn;
		return j.dump(2);
	}

	namespace
	{
		ItemDef Def(const char* id, const char* name, const char* category, float weight, int stack, std::vector<std::string> tags, const char* ability, const char* socket)
		{
			ItemDef d;
			d.Id = id; d.Name = name; d.Category = category; d.Weight = weight; d.MaxStack = stack; d.Tags = std::move(tags); d.Ability = ability; d.Socket = socket;
			return d;
		}

		ItemDef Fuel(ItemDef d, int fuel)
		{
			d.Fuel = fuel;
			return d;
		}

		ResourceNodeDef Node(const char* id, const char* item, int capacity, int perGather, float regenSeconds, int regen, const char* tool, const char* ability,
			const char* kind, int patchCount, int patchPerWorker, const char* shape = "row", float spacing = 4.0f, const char* beside = "")
		{
			ResourceNodeDef d;
			d.Id = id; d.Item = item; d.Capacity = capacity; d.PerGather = perGather; d.RegenSeconds = regenSeconds; d.Regen = regen; d.RequiresTag = tool; d.Ability = ability;
			d.Kind = kind;
			d.Patch.Count = patchCount; d.Patch.PerWorker = patchPerWorker; d.Patch.Shape = shape; d.Patch.Spacing = spacing; d.Patch.Near = beside;
			return d;
		}

		ItemDef Tool(ItemDef d, int power, float speed)
		{
			d.Power = power;
			d.Speed = speed;
			return d;
		}
	}

	void RegisterEraOneItems(ItemLibrary& items, RecipeBook& recipes, std::map<std::string, ResourceNodeDef>& nodes)
	{
		// Wood burns for 2 units of heat in a campfire, a branch for 1 (E1): it takes two branches for the heat of one piece of wood.
		items.Add(Fuel(Def("wood",      "Wood",        "resource", 2.0f, 10, { "Item.Resource.Wood" },  "", ""), 2));
		items.Add(Def("stone",     "Stone",       "resource", 3.0f, 10, { "Item.Resource.Stone" }, "", ""));
		items.Add(Def("fiber",     "Plant fiber", "resource", 0.2f, 20, { "Item.Resource.Fiber" }, "", ""));
		{
			// E6: a berry keeps a day and a half (1800 s): the stack that is not eaten goes off as a whole, which takes about as much surplus away as
			// the old sink did (one berry above 20 every 100 s) without touching what the clan eats in time (it eats the oldest first)
			ItemDef berry = Def("berry", "Berry", "food", 0.1f, 20, { "Item.Food" }, "Eat", "");
			berry.ShelfLife = 1800.0f;
			items.Add(berry);
			// H14: meat from the hunt (GB/Gameplay/Combat.h, the "hunts" of the combat rules). It keeps half as long as berries: a clan eats it
			// first (it eats what goes off soonest) and a surplus spoils in a day.
			ItemDef meat = Def("meat", "Meat", "food", 0.5f, 10, { "Item.Food.Meat" }, "Eat", "");
			meat.ShelfLife = 900.0f;
			items.Add(meat);
		}

		items.Add(Def("stone_axe",     "Stone axe",     "tool", 2.5f, 1, { "Item.Tool.Axe" },     "Chop", "RightHand"));
		items.Add(Def("stone_pickaxe", "Stone pickaxe", "tool", 3.0f, 1, { "Item.Tool.Pickaxe" }, "Mine", "RightHand"));
		items.Add(Def("water",     "Water",       "food",     1.0f,  5, { "Item.Food.Water" },     "Drink", ""));
		items.Add(Def("campfire",  "Campfire kit", "structure", 6.0f, 1, { "Item.Structure.Fire" }, "Place", ""));

		// Flint (E2): sharper than stone, so a flint tool gives one more unit per swing and swings a little faster. Branches, flint and reed
		// come from nodes that the extended content (era1_ampliado.json) defines; until a node places them, `actor bag <name> add` supplies them.
		items.Add(Fuel(Def("branch",  "Branch",  "resource", 0.6f, 20, { "Item.Resource.Wood.Branch" }, "", ""), 1));
		items.Add(Def("flint",   "Flint",   "resource", 1.0f, 10, { "Item.Resource.Stone.Flint" }, "", ""));
		items.Add(Def("reed",    "Reed",    "resource", 0.2f, 20, { "Item.Resource.Fiber.Reed" }, "", ""));
		items.Add(Def("cordage", "Cordage", "resource", 0.3f, 20, { "Item.Resource.Cordage" }, "", ""));
		// E1: clay from a bank, and copper (ore from a vein; the forge that smelts it arrives with E10, so its recipe cannot be made yet: WhyNot says so).
		items.Add(Def("clay",         "Clay",          "resource", 2.5f, 10, { "Item.Resource.Clay" },         "", ""));
		items.Add(Def("copper_ore",   "Copper ore",    "resource", 2.0f, 10, { "Item.Resource.Ore.Copper" },   "", ""));
		items.Add(Def("copper_ingot", "Copper ingot",  "resource", 1.5f, 10, { "Item.Resource.Metal.Copper" }, "", ""));
		items.Add(Tool(Def("flint_axe",     "Flint axe",     "tool", 2.2f, 1, { "Item.Tool.Axe.Flint" },     "Chop", "RightHand"), 1, 1.15f));
		items.Add(Tool(Def("flint_pickaxe", "Flint pickaxe", "tool", 2.6f, 1, { "Item.Tool.Pickaxe.Flint" }, "Mine", "RightHand"), 1, 1.15f));
		// A wicker basket in the bag adds six slots (the best container counts, they do not add up); the weight bonus is for bags that have a limit.
		ItemDef basket = Def("basket_wicker", "Wicker basket", "container", 1.5f, 1, { "Item.Container.Basket" }, "", "");
		basket.BagSlots = 6;
		basket.BagWeight = 8.0f;
		items.Add(basket);

		recipes.Add({ "stone_axe",     "workbench", "",                  { { "wood", 2 }, { "stone", 2 }, { "fiber", 3 } }, "stone_axe",     1, "Craft" });
		recipes.Add({ "stone_pickaxe", "workbench", "",                  { { "wood", 2 }, { "stone", 3 }, { "fiber", 3 } }, "stone_pickaxe", 1, "Craft" });
		recipes.Add({ "cordage",       "",          "",                  { { "fiber", 3 } },                                "cordage",       1, "Craft" });
		recipes.Add({ "flint_axe",     "workbench", "",                  { { "flint", 2 }, { "branch", 2 }, { "cordage", 1 } }, "flint_axe",     1, "Craft" });
		recipes.Add({ "flint_pickaxe", "workbench", "",                  { { "flint", 3 }, { "branch", 2 }, { "cordage", 1 } }, "flint_pickaxe", 1, "Craft" });
		recipes.Add({ "basket_wicker", "",          "",                  { { "reed", 5 }, { "branch", 2 } },                "basket_wicker", 1, "Craft" });
		recipes.Add({ "campfire",      "",          "Item.Tool.Axe",     { { "wood", 4 }, { "stone", 3 } },                 "campfire",      1, "Craft" });
		recipes.Add({ "copper_ingot",  "forge",     "",                  { { "copper_ore", 2 } },                           "copper_ingot",  1, "Craft" });

		// The Stone-Age progression (docs/PROGRESION.md): what the new technologies unlock and the new buildings are made of. Herbs grow wild; the hide
		// comes from the hunt (the byproduct of a rabbit, H14); the rest is made.
		items.Add(Def("herbs",    "Healing herbs", "resource", 0.1f, 20, { "Item.Resource.Herb" },     "", ""));
		items.Add(Def("hide",     "Hide",          "resource", 0.8f, 10, { "Item.Resource.Hide" },     "", ""));
		items.Add(Def("poultice", "Poultice",      "resource", 0.2f, 10, { "Item.Medicine.Poultice" }, "", ""));
		items.Add(Def("leather",  "Leather",       "resource", 0.6f, 10, { "Item.Resource.Leather" },  "", ""));
		items.Add(Def("daub",     "Daub",          "resource", 2.0f, 10, { "Item.Resource.Daub" },     "", ""));
		items.Add(Def("reed_mat", "Reed mat",      "resource", 1.0f, 10, { "Item.Resource.Mat" },      "", ""));
		items.Add(Fuel(Def("stake", "Stake",       "resource", 1.5f, 10, { "Item.Resource.Wood.Stake" }, "", ""), 1));
		items.Add(Def("fired_brick", "Fired brick", "resource", 2.5f, 10, { "Item.Resource.Brick.Fired" }, "", ""));
		items.Add(Def("flint_spear", "Flint spear", "weapon", 1.8f, 1, { "Item.Weapon.Spear" }, "", "RightHand"));
		{
			// a leather bag is a better container than the basket: three more slots
			ItemDef bag = Def("leather_bag", "Leather bag", "container", 1.0f, 1, { "Item.Container.Bag" }, "", "");
			bag.BagSlots = 9;
			bag.BagWeight = 12.0f;
			items.Add(bag);
			// cooked meat keeps a little longer than raw (900 s); smoked meat for days
			ItemDef cooked = Def("cooked_meat", "Cooked meat", "food", 0.4f, 10, { "Item.Food.Meat.Cooked" }, "Eat", "");
			cooked.ShelfLife = 2400.0f;
			items.Add(cooked);
			ItemDef smoked = Def("smoked_meat", "Smoked meat", "food", 0.3f, 20, { "Item.Food.Meat.Smoked" }, "Eat", "");
			smoked.ShelfLife = 9600.0f;
			items.Add(smoked);
		}
		recipes.Add({ "poultice",    "",             "",              { { "herbs", 2 }, { "fiber", 1 } },                    "poultice",    1, "Craft" });
		recipes.Add({ "leather",     "tannery",      "",              { { "hide", 2 } },                                     "leather",     1, "Craft" });
		recipes.Add({ "leather_bag", "workbench",    "",              { { "leather", 2 }, { "cordage", 1 } },                "leather_bag", 1, "Craft" });
		recipes.Add({ "cooked_meat", "hearth",       "",              { { "meat", 1 } },                                     "cooked_meat", 1, "Craft" });
		recipes.Add({ "smoked_meat", "smoking_rack", "",              { { "meat", 2 }, { "wood", 1 } },                      "smoked_meat", 2, "Craft" });
		recipes.Add({ "daub",        "",             "",              { { "clay", 2 }, { "fiber", 1 } },                     "daub",        2, "Craft" });
		recipes.Add({ "reed_mat",    "",             "",              { { "reed", 4 }, { "cordage", 1 } },                   "reed_mat",    1, "Craft" });
		recipes.Add({ "stakes",      "",             "Item.Tool.Axe", { { "wood", 1 } },                                     "stake",       2, "Craft" });
		recipes.Add({ "flint_spear", "workbench",    "",              { { "wood", 1 }, { "flint", 1 }, { "cordage", 1 } },   "flint_spear", 1, "Craft" });
		recipes.Add({ "fired_brick", "kiln",         "",              { { "clay", 2 }, { "wood", 1 } },                      "fired_brick", 2, "Craft" });

		// The patch fields say how many of each node a patch has (Count + PerWorker * workers; the first four match ScaledResources) and how they are laid out.
		// young_tree and old_tree are sibling nodes (E1): the young ones stand beside the old ones, branches for the hands and wood for the axe.
		nodes["tree"]          = Node("tree",          "wood",       5, 1,  30.0f, 1, "Item.Tool.Axe",     "Chop",      "Wood",         2, 2);
		nodes["rock"]          = Node("rock",          "stone",      4, 1,   0.0f, 1, "Item.Tool.Pickaxe", "Mine",      "Stone",        3, 2);
		nodes["berry_bush"]    = Node("berry_bush",    "berry",      6, 2,  20.0f, 2, "",                  "Gather",    "Plant",        2, 1);
		nodes["fiber_grass"]   = Node("fiber_grass",   "fiber",      8, 2,  15.0f, 2, "",                  "PullFiber", "Plant",        2, 1);
		nodes["young_tree"]    = Node("young_tree",    "branch",     3, 1,  40.0f, 1, "",                  "Gather",    "Plant.Branch", 1, 1, "row", 4.0f, "old_tree");
		nodes["old_tree"]      = Node("old_tree",      "wood",       8, 1, 120.0f, 1, "Item.Tool.Axe",     "Chop",      "Wood",         1, 1, "cluster", 7.0f);
		nodes["clay_bank"]     = Node("clay_bank",     "clay",       8, 2,  60.0f, 2, "",                  "Collect",   "Loose.Clay",   1, 0, "cluster", 3.0f);
		nodes["reed_bed"]      = Node("reed_bed",      "reed",       8, 2,  20.0f, 2, "",                  "Collect",   "Loose.Reed",   1, 1, "cluster", 3.0f);
		nodes["flint_outcrop"] = Node("flint_outcrop", "flint",      5, 1,   0.0f, 1, "Item.Tool.Pickaxe", "Mine",      "Stone.Flint",  1, 1, "cluster", 3.5f);
		nodes["copper_vein"]   = Node("copper_vein",   "copper_ore", 4, 1,   0.0f, 1, "Item.Tool.Pickaxe", "Mine",      "Stone.Copper", 1, 0, "row", 4.0f);
		// (herbs grow wild only: no patch of `resources build`, so the patch of a camp stays as it was)
		nodes["herb_patch"]    = Node("herb_patch",    "herbs",      4, 1,  60.0f, 1, "",                  "Gather",    "Plant.Herb",   0, 0);

		// Where each grows over a generated world (PlanWorldResources): wood in the forest, berries and fibre in the meadow, clay and reeds by the water,
		// stone, flint and copper on rocky ground (copper rare and in veins). PerHectare is the average over the matching ground; a map of 360 m gets a few hundred nodes.
		auto spawn = [&](const char* id, std::vector<std::string> biomes, float perHectare, float spacing, float maxSlope, float waterWithin = 0.0f, float clump = 0.0f, int most = 0)
		{
			ResourceSpawnDef& s = nodes[id].Spawn;
			s.Biomes = std::move(biomes); s.PerHectare = perHectare; s.Spacing = spacing; s.MaxSlope = maxSlope; s.WaterWithin = waterWithin; s.Clump = clump; s.Max = most;
		};
		spawn("tree",          { "Forest", "Meadow" },          6.0f, 8.0f, 30.0f, 0.0f, 0.3f);
		spawn("old_tree",      { "Forest" },                    8.0f, 9.0f, 28.0f, 0.0f, 0.2f);
		spawn("young_tree",    { "Forest", "Meadow" },          8.0f, 5.0f, 30.0f, 0.0f, 0.35f);
		spawn("berry_bush",    { "Meadow", "Forest" },          6.0f, 5.0f, 25.0f, 0.0f, 0.5f);
		spawn("fiber_grass",   { "Meadow", "Shore" },           8.0f, 5.0f, 25.0f, 0.0f, 0.3f);
		spawn("rock",          { "Rock", "Meadow", "Shore", "Dunes" }, 4.0f, 7.0f, 45.0f);
		spawn("flint_outcrop", { "Rock", "Shore", "Dunes" },    4.0f, 6.0f, 45.0f, 0.0f, 0.55f, 16);
		spawn("copper_vein",   { "Rock", "Snow" },              2.0f, 8.0f, 50.0f, 0.0f, 0.7f, 5);
		spawn("clay_bank",     { "Shore", "Meadow" },          15.0f, 6.0f, 15.0f, 9.0f, 0.4f, 12);
		spawn("reed_bed",      { "Shore", "Meadow" },         25.0f, 4.0f, 15.0f, 6.0f, 0.2f, 24);
		spawn("herb_patch",    { "Meadow", "Forest" },          2.0f, 7.0f, 25.0f, 0.0f, 0.5f, 20);
	}
}
