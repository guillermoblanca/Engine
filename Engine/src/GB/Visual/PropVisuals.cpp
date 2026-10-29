#include "GB/Visual/PropVisuals.h"
#include <algorithm>

namespace GB
{
	namespace
	{
		bool HasTagUnder(const ItemDef& def, const std::string& prefix)
		{
			for (const std::string& t : def.Tags)
				if (t == prefix || (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0 && t[prefix.size()] == '.')) return true;
			return false;
		}

		bool IsTool(const ItemDef& def) { return HasTagUnder(def, "Item.Tool"); }
		bool IsContainer(const ItemDef& def) { return HasTagUnder(def, "Item.Container"); }
	}

	std::string ActivityKey(const std::string& job, const std::string& detail, const std::string& stage)
	{
		if (job == "farm")
		{
			const size_t space = detail.find(' ');
			return "farm." + detail.substr(0, space);
		}
		if (job == "talk" || job == "rite")   // the social lane: "talk.story", "rite.funeral" (docs/VIDA_SOCIAL.md)
		{
			const size_t space = detail.find(' ');
			return job + "." + detail.substr(0, space);
		}
		if (job == "socialize" && stage == "elder") return "teach";
		return job;
	}

	PropChoice ChooseProps(const PropRules& rules, const PropContext& c)
	{
		PropChoice out;
		if (!c.Bag || !c.Items) return out;
		const Inventory& bag = *c.Bag;
		const ItemLibrary& items = *c.Items;
		auto lookOf = [&](const std::string& item) -> std::string { const auto it = rules.Items.find(item); return it == rules.Items.end() ? std::string() : it->second; };
		auto socketTaken = [&](const std::string& socket) { for (const PropPlacement& p : out.Props) if (p.Socket == socket) return true; return false; };
		auto place = [&](PropPlacement p) { if (!p.Look.empty() && !socketTaken(p.Socket) && static_cast<int>(out.Props.size()) < rules.MaxShown) out.Props.push_back(std::move(p)); };

		// the work in hand: an ability that uses a tool family, or else an activity that does
		std::string workFamily;
		if (!c.Ability.empty()) { const auto it = rules.WorkTools.find(c.Ability); if (it != rules.WorkTools.end()) workFamily = it->second; }
		if (workFamily.empty() && !c.Activity.empty()) { const auto it = rules.WorkTools.find(c.Activity); if (it != rules.WorkTools.end()) workFamily = it->second; }

		// 1. what the hands hold for the activity (firewood to the fire, berries to eat) and the tool of the work. A work may name several families
		// in order of preference ("Item.Tool.Axe|Item.Tool.Pickaxe": a fight takes the axe, or else the pickaxe, as its weapon)
		for (size_t from = 0; !workFamily.empty() && from <= workFamily.size();)
		{
			const size_t bar = workFamily.find('|', from);
			const std::string family = workFamily.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
			if (const ItemDef* tool = family.empty() ? nullptr : bag.BestTool(items, family); tool && !lookOf(tool->Id).empty())   // (a weapon without a look: the next family)
			{
				place({ "tool", lookOf(tool->Id), "hand_r", tool->Id });   // ("tool": the name an ability's EquipProp gives it, so the look replaces its box)
				break;
			}
			if (bar == std::string::npos) break;
			from = bar + 1;
		}
		if (const auto it = rules.HandLooks.find(c.Activity); it != rules.HandLooks.end())
			place({ "hand", it->second, "hand_l", "" });

		// 2. a load in the arms on the way to a store
		const bool carrying = std::find(rules.CarryJobs.begin(), rules.CarryJobs.end(), c.Activity) != rules.CarryJobs.end();
		if (carrying)
		{
			int units = 0, most = 0;
			std::string main;
			for (const ItemSlot& s : bag.Stacks())
			{
				const ItemDef* def = items.Find(s.Id);
				if (!def || IsTool(*def) || IsContainer(*def)) continue;
				units += s.Count;
				// the main item: the most units (ties: the lowest id, as the stacks are not ordered by id)
				int of = 0;
				for (const ItemSlot& t : bag.Stacks()) if (t.Id == s.Id) of += t.Count;
				if (of > most || (of == most && s.Id < main)) { most = of; main = s.Id; }
			}
			const ItemDef* def = main.empty() ? nullptr : items.Find(main);
			for (const CarryRule& rule : rules.Carry)
			{
				if (!def || units < rule.MinUnits) continue;
				bool match = rule.Tags.empty();
				for (const std::string& t : rule.Tags) match = match || HasTagUnder(*def, t) || main == t;
				if (!match) continue;
				// (the arms are full: whatever the hands held for the activity is dropped, the load comes first)
				out.Props.erase(std::remove_if(out.Props.begin(), out.Props.end(), [](const PropPlacement& p) { return p.Socket == "hand_l" || p.Socket == "hand_r"; }), out.Props.end());
				place({ "load", rule.Look, "carry", main });
				out.Set = rule.Set;
				out.LoadUnits = units;
				break;
			}
		}

		// a thing at rest can have its own look ("<look>_rest": the axe hangs head up from the belt instead of pointing forward as in the hand)
		auto restLook = [&](const std::string& item) { std::string l = lookOf(item); if (!l.empty() && rules.Looks.count(l + "_rest")) l += "_rest"; return l; };
		// 3. the container on the back (before the resting tools: the basket is what shows the member gathers)
		if (const ItemDef* basket = bag.BestContainer(items))
		{
			std::string socket = "back";
			for (const auto& [family, s] : rules.RestSockets) if (HasTagUnder(*basket, family)) socket = s;
			place({ "container", restLook(basket->Id), socket, basket->Id });
		}
		// 4. the tools that are not in use rest where their family rests
		for (const auto& [family, socket] : rules.RestSockets)
		{
			if (family.rfind("Item.Container", 0) == 0) continue;
			const ItemDef* tool = bag.BestTool(items, family);
			if (!tool) continue;
			if (std::any_of(out.Props.begin(), out.Props.end(), [&](const PropPlacement& p) { return p.Name == "tool" && p.Item == tool->Id; })) continue;   // (in the hand)
			place({ "tool:" + family, restLook(tool->Id), socket, tool->Id });
		}
		return out;
	}
}
