// T335: a clan moves a berry bush by its camp when it has none near (see BushKeeper.h).
#include "GB/Game/BushKeeper.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Transplant.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Techs.h"
#include <cmath>
#include <cstdio>
#include <set>

namespace GB
{
	std::string BushKeeper::Pick(const Tribe& tribe, glm::vec2& to)
	{
		const float near = tribe.Config.Econ.Farming.TransplantNear;
		if (near <= 0.0f || !tribe.m_Gameplay || !tribe.m_Entities || tribe.m_Zones.Player()) return {};
		for (const auto& [who, d] : tribe.m_Orders.All()) if (d.Verb == "transplant") return {};   // (one at a time)
		const TechTree* tree = tribe.m_Knowledge.Techs();
		if (!tree) return {};
		// the kinds it may move: food, with a transplant whose verb the tree names and the clan knows
		std::set<std::string> kinds;
		for (const auto& [id, def] : tribe.m_Defs)
			if (!def.Transplant.Item.empty() && tribe.IsFood(def.Item) && !tree->ForVerb(def.Transplant.Verb).empty() && TechVerbs::Knows(tribe, def.Transplant.Verb)) kinds.insert(id);
		if (kinds.empty()) return {};
		// none near the camp on the clan's map, and the nearest farther one on its home ground
		const glm::vec2 camp = tribe.Config.Economy.Camp;
		const float home = tribe.Config.Exploration.HomeRadius + tribe.Config.Exploration.HomeFalloff;
		std::string node;
		float best = 0.0f;
		for (const auto& [name, e] : tribe.m_Clan.All())
		{
			if (!kinds.count(e.Kind)) continue;
			const float d = glm::distance(e.Pos, camp);
			if (d <= near) return {};
			if (d > home || tribe.m_Claims.All().count(name) || !tribe.m_Gameplay->Find(name)) continue;
			if (node.empty() || d < best) { node = name; best = d; }   // (name order breaks a tie)
		}
		if (node.empty()) return {};
		// a member that may go, to try the spots with (Transplant::Assign then picks the nearest free one)
		const TribeMember* who = nullptr;
		for (const TribeMember& m : tribe.m_Members) if (!m.Dead && m.Body && !m.Player) { who = &m; break; }
		if (!who) return {};
		for (int ring = 0; ring < kRings; ++ring)
		{
			const float r = near * (0.4f + 0.2f * static_cast<float>(ring));
			for (int k = 0; k < kBearings; ++k)
			{
				const float a = 0.5f + 6.2831853f * static_cast<float>(k) / static_cast<float>(kBearings);
				const glm::vec2 spot = camp + glm::vec2(std::cos(a), std::sin(a)) * r;
				if (Transplant::WhyNot(tribe, *who, node, spot).empty()) { to = spot; return node; }
			}
		}
		return {};
	}

	void BushKeeper::Post(Tribe& tribe)
	{
		glm::vec2 to(0.0f);
		const std::string node = Pick(tribe, to);
		if (node.empty()) return;
		char where[64];
		std::snprintf(where, sizeof where, " to %.1f %.1f", static_cast<double>(to.x), static_cast<double>(to.y));
		const ColonyOrders::Reply r = Transplant::Assign(tribe, node + where);
		if (r.Ok) tribe.TellClan("moves " + node + where + ": it has no berries by the camp");
	}
}
