// H15: the player as its clan's envoy (H13). Two NPC clans trade through a meeting of envoys that their brains carry out (ClanWorld::PostMeeting,
// the "envoy" option); a player walks there itself. At another clan's camp it barters what its clan lacks for what the other spares (the same
// PlanBarter of two envoys, the goods going store to store as if the envoys had carried them), or visits; the clans' relation, the bond between
// the player and the member it meets and the lessons (a technology shown either way) follow the rules of a meeting (Exchange in Clans.cpp).
#include "GB/Game/Clans.h"
#include <algorithm>

namespace GB
{
	namespace
	{
		std::string GoodsText(const std::map<std::string, int>& goods)
		{
			std::string s;
			for (const auto& [item, n] : goods) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + item;
			return s.empty() ? std::string("nothing") : s;
		}
	}

	std::string ClanWorld::PlayerVisit(const std::string& player, ClanId to, const std::string& purpose, std::string* outcome)
	{
		const Clan* own = ClanOf(player);
		Clan* home = own ? Find(own->Id) : nullptr;
		Clan* host = Find(to);
		if (!home) return player + " is in no clan";
		if (!host || host == home) return "no other clan " + std::to_string(to);
		if (purpose != "trade" && purpose != "visit") return "trade or visit";
		TribeMember* me = Member(*home, player);
		if (!me || me->Dead || !me->Body || !me->Player) return player + " is not a player of the " + home->Name;
		Tribe& a = *home->Folk;
		Tribe& b = *host->Folk;
		const glm::vec2 at = a.Position(*me);
		if (glm::distance(at, b.Config.Economy.Camp) > a.Config.Player.VisitRadius)
			return "the camp of the " + host->Name + " is " + std::to_string(static_cast<int>(glm::distance(at, b.Config.Economy.Camp))) + " m away: go there first";
		if (Alive(*host) == 0) return "nobody lives at the camp of the " + host->Name;
		if (Stance(home->Id, to) == "hostile") return "the " + host->Name + " are hostile: they turn you away";
		// the member who receives the player: the nearest living adult of the host
		TribeMember* them = nullptr;
		float best = 1e30f;
		for (TribeMember& o : b.Members())
		{
			if (o.Dead || !o.Body || b.People().IsChild(b.Config.Society, o.Name)) continue;
			const float d = glm::distance(b.Position(o), at);
			if (d < best) { best = d; them = &o; }
		}
		if (!them) return "nobody at the camp of the " + host->Name + " can receive you";
		const ClanId ia = home->Id;
		std::string text;
		if (purpose == "trade")
		{
			const float att = Attitude(ia, to);
			if (att < Rules.Trade.MinAttitude) return "the " + host->Name + " do not trust your clan enough to trade";
			const Barter deal = PlanBarter(Rules.Trade, View(*home), View(*host), Attitude(to, ia) >= Rules.Trade.Gift);
			if (deal.Empty()) return "nothing to trade: neither clan spares what the other lacks";
			const ItemLibrary* items = a.Items();
			if (!items) return "the clans have no items";
			std::map<std::string, int> moved[2];
			for (const auto& [item, n] : deal.Give[0]) if (const int k = a.Store().Transfer(*items, b.Store(), item, n); k > 0) moved[0][item] = k;
			for (const auto& [item, n] : deal.Give[1]) if (const int k = b.Store().Transfer(*items, a.Store(), item, n); k > 0) moved[1][item] = k;
			const bool two = !moved[0].empty() && !moved[1].empty();
			text = two ? "trade: " + GoodsText(moved[0]) + " for " + GoodsText(moved[1]) : "gift: " + (moved[0].empty() ? GoodsText(moved[1]) + " from the " + host->Name : GoodsText(moved[0]) + " from the " + home->Name);
			Relate(ia, to, two ? "trade" : "gift", 1.0f, player + " of the " + home->Name + " meets " + them->Name + " of the " + host->Name + ": " + text);
		}
		else
		{
			text = "a visit";
			Relate(ia, to, "visit", 1.0f, player + " of the " + home->Name + " visits the " + host->Name);
		}
		RelationRef(ia, to).LastMeeting = m_Time;
		// the two who met like each other a bit more (as two envoys do)
		const auto key = player < them->Name ? std::make_pair(player, them->Name) : std::make_pair(them->Name, player);
		CrossBond& bond = m_Bonds[key];
		bond.A = key.first == player ? ia : to;
		bond.B = key.first == player ? to : ia;
		bond.Value = std::clamp(bond.Value + 2.0f * Rules.Relations.MemberBond * 0.5f * (Openness(ia) + Openness(to)), -1.0f, 1.0f);
		// what they know (H11): fond clans show each other a technology the other can learn
		if (Attitude(ia, to) >= Rules.Exchange.MinAttitude)
		{
			std::string tech;
			if (TechToShow(*home, player, *host, them->Name, tech))
			{
				for (const KnowledgeEvent& e : b.Knowledge().Instruct(them->Name, tech, Rules.Exchange.Teach, "from " + player + " of the " + home->Name)) b.Note(*them, e.Text);
				text += "; you show " + them->Name + " " + b.Knowledge().Name(tech);
				a.Credit(player, "teach", 1.0f);
			}
			if (TechToShow(*host, them->Name, *home, player, tech))
			{
				for (const KnowledgeEvent& e : a.Knowledge().Instruct(player, tech, Rules.Exchange.Teach, "from " + them->Name + " of the " + host->Name)) a.Note(*me, e.Text);
				text += "; " + them->Name + " shows you " + a.Knowledge().Name(tech);
			}
		}
		a.Credit(player, "trade", 1.0f);
		a.Note(*me, "goes to the camp of the " + host->Name + " and meets " + them->Name + " (" + text + ")");
		b.Note(*them, "receives " + player + " of the " + home->Name + " (" + text + ")");
		Emit(purpose, ia, to, player + " of the " + home->Name + " and " + them->Name + " of the " + host->Name + ": " + text);
		if (outcome) *outcome = text + "; the " + host->Name + " are " + Stance(ia, to) + " to your clan";
		return {};
	}
}
