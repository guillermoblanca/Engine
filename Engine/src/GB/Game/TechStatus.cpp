// T341: one state per technology for every view of knowledge (see TechStatus.h).
#include "GB/Game/TechStatus.h"
#include "GB/Game/Clans.h"
#include "GB/Game/ResearchQueue.h"
#include "GB/Game/TechTiers.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/UseLearning.h"
#include "GB/Gameplay/Techs.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	bool TechTierMap::Read(const std::string& json)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded()) return false;
		if (!doc.is_object()) return true;
		const auto techs = doc.find("techs");
		if (techs == doc.end() || !techs->is_array()) return true;
		for (const Json& t : *techs)
		{
			if (!t.is_object() || !t.contains("id") || !t["id"].is_string()) continue;
			const std::string id = t["id"].get<std::string>();
			if (const auto tier = t.find("tier"); tier != t.end() && tier->is_number_integer()) m_Of[id] = tier->get<int>();
			else m_Of.erase(id);   // (a later definition without a tier has none, as the last definition wins in LoadTechs)
		}
		return true;
	}

	TechTierMap TechTierMap::FromTree(const TechTree& tree)
	{
		TechTierMap t;
		for (const auto& [id, def] : tree.Techs) if (def.Tier >= 0) t.Set(id, def.Tier);
		return t;
	}

	int TechTierMap::Of(const std::string& tech) const
	{
		const auto it = m_Of.find(tech);
		return it == m_Of.end() ? -1 : it->second;
	}

	int TechStatus::OpenTier(const Tribe& tribe)
	{
		return TechTiers::Open(tribe);   // T323
	}

	bool TechStatus::Dealings(const ClanWorld& world, int a, int b)
	{
		if (a == b || !world.Met(a, b)) return false;
		const std::string stance = world.Stance(a, b);
		if (stance == "hostile" || stance == "unknown") return false;
		if (stance == "friendly" || stance == "truce" || stance == "allied") return true;
		const ClanRelation* r = world.Relation(a, b);
		return r && (r->Trades > 0 || r->Lessons > 0 || !r->Pacts.empty());
	}

	std::vector<TechNeighbour> TechStatus::Neighbours(const ClanWorld& world, int clan)
	{
		std::vector<TechNeighbour> out;
		for (const std::unique_ptr<Clan>& c : world.All())
			if (c && c->Folk && c->Id != clan && Dealings(world, clan, c->Id)) out.push_back({ c->Name, &c->Folk->Knowledge().Clan() });
		std::sort(out.begin(), out.end(), [](const TechNeighbour& x, const TechNeighbour& y) { return x.Name < y.Name; });
		return out;
	}

	TechStatus::TechStatus(const Tribe& tribe, const Input& input)
	{
		const ClanKnowledge& k = tribe.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) return;
		const std::set<std::string>& known = k.Clan();
		const ResearchQueue& queue = tribe.Research();
		const std::string current = queue.Current(tribe);
		const int open = input.OpenTier >= 0 ? input.OpenTier : OpenTier(tribe);
		const TechTierMap fromTree = input.Tiers ? TechTierMap{} : TechTierMap::FromTree(*tree);
		const TechTierMap& tiers = input.Tiers ? *input.Tiers : fromTree;
		const bool tiered = std::any_of(tree->Techs.begin(), tree->Techs.end(), [&](const auto& t) { return tiers.Of(t.first) >= 0; });
		std::map<std::string, const ClanKnowledge::ResearchLine*> lines;
		const std::vector<ClanKnowledge::ResearchLine> research = k.Research();
		for (const ClanKnowledge::ResearchLine& l : research) lines[l.Tech] = &l;

		for (const auto& [id, def] : tree->Techs)
		{
			Entry e;
			e.Tech = id;
			e.Tier = tiers.Of(id);
			e.Shown = !tiered || e.Tier >= 0;
			if (const auto l = lines.find(id); l != lines.end()) { e.Progress = l->second->Progress; e.Who = l->second->Who; }
			const auto at = std::find(queue.All().begin(), queue.All().end(), id);
			if (at != queue.All().end()) e.Queue = static_cast<int>(at - queue.All().begin()) + 1;
			if (known.count(id))
			{
				e.State = TechState::Known;
				e.Named = true;
				m_Entries[id] = std::move(e);
				continue;
			}
			// why it cannot be studied now, the first that applies: what it requires, its tier, what the world lacks, not by trying
			for (const std::string& r : def.Requires) if (!known.count(r)) e.Missing.push_back(r);
			if (!e.Missing.empty()) e.Why = TechWhy::Requires;
			else if (e.Tier > open) e.Why = TechWhy::Tier;
			else if (!k.Physical(id))
			{
				for (const std::string& item : def.Physical.Materials) if (UseLearning::Have(tribe, item) <= 0.0f) e.Missing.push_back(item);
				e.Why = e.Missing.empty() ? TechWhy::Station : TechWhy::Material;
				if (e.Why == TechWhy::Station) e.Missing = def.Physical.Stations;
			}
			else if (def.Discovery.Experiment <= 0.0f) e.Why = TechWhy::NotByTrying;
			const bool researchable = e.Why == TechWhy::None && ResearchQueue::Researchable(tribe, id);
			if (e.Why == TechWhy::None && !researchable) e.Why = TechWhy::NotByTrying;   // (the sim's gate said no for a reason not listed above)
			std::string from;
			for (const TechNeighbour& n : input.Neighbours) if (n.Known && n.Known->count(id)) { from = n.Name; break; }

			if (e.Queue > 0 && id == current && researchable) e.State = TechState::Studying;
			else if (e.Queue > 0) e.State = TechState::Queued;
			else if (e.Progress > 0.0f) e.State = TechState::OwnPace;
			else if (researchable) e.State = TechState::Studiable;
			else if (!from.empty() && e.Why != TechWhy::Material && e.Why != TechWhy::Station) e.State = TechState::OtherClan;   // (the world's gate holds for any way)
			else if (e.Why != TechWhy::Requires) e.State = TechState::Blocked;
			else e.State = TechState::Unknown;
			e.From = from;
			e.Named = e.State != TechState::Unknown;
			if (e.State == TechState::Studying || e.State == TechState::Studiable) { e.Why = TechWhy::None; e.Missing.clear(); }
			m_Entries[id] = std::move(e);
		}
		// the hints of the unknown: a prerequisite not known that is named
		for (auto& [id, e] : m_Entries)
			if (e.State == TechState::Unknown)
				for (const std::string& r : e.Missing)
					if (const auto it = m_Entries.find(r); it != m_Entries.end() && it->second.Named) { e.Hint = r; break; }
		for (const auto& [id, e] : m_Entries)
		{
			if (!e.Shown) continue;
			++m_Total;
			m_Known += e.State == TechState::Known ? 1 : 0;
			m_Studiable += e.State == TechState::Studiable ? 1 : 0;
			m_Unnamed += e.Named ? 0 : 1;
		}
	}

	const TechStatus::Entry& TechStatus::Of(const std::string& tech) const
	{
		const auto it = m_Entries.find(tech);
		if (it != m_Entries.end()) return it->second;
		return m_None;   // (State Unknown, not named; Shown stays true only for an empty status)
	}

	int TechStatus::UnnamedIn(int tier) const
	{
		int n = 0;
		for (const auto& [id, e] : m_Entries)
			if (e.Shown && !e.Named && (tier >= kEraBlockTier ? e.Tier >= kEraBlockTier : e.Tier == tier)) ++n;
		return n;
	}

	std::function<bool(const std::string&)> TechStatus::ShownFilter() const
	{
		std::set<std::string> shown;
		for (const auto& [id, e] : m_Entries) if (e.Shown) shown.insert(id);
		return [shown = std::move(shown)](const std::string& tech) { return shown.count(tech) != 0; };
	}

	const char* TechStatus::StateWord(TechState s)
	{
		switch (s)
		{
		case TechState::Known: return "known";
		case TechState::Studying: return "studying";
		case TechState::Queued: return "queued";
		case TechState::OwnPace: return "own pace";
		case TechState::Studiable: return "studiable";
		case TechState::Blocked: return "blocked";
		case TechState::OtherClan: return "other clan";
		case TechState::Unknown: return "unknown";
		}
		return "unknown";
	}

	const char* TechStatus::WhyWord(TechWhy w)
	{
		switch (w)
		{
		case TechWhy::None: return "";
		case TechWhy::Requires: return "requires";
		case TechWhy::Tier: return "tier";
		case TechWhy::Material: return "material";
		case TechWhy::Station: return "station";
		case TechWhy::NotByTrying: return "not by trying";
		}
		return "";
	}

	std::string TechStatus::Describe(const std::string& tech) const
	{
		const Entry& e = Of(tech);
		std::string s = StateWord(e.State);
		if (e.Queue > 0) s += " " + std::to_string(e.Queue);
		if (e.Why != TechWhy::None)
		{
			s += std::string(" (") + WhyWord(e.Why);
			for (size_t i = 0; i < e.Missing.size(); ++i) s += (i == 0 ? ": " : ", ") + e.Missing[i];
			s += ")";
		}
		if (!e.From.empty() && e.State == TechState::OtherClan) s += " " + e.From;
		if (!e.Hint.empty()) s += " after " + e.Hint;
		if (!e.Shown) s += " [not shown]";
		return s;
	}
}
