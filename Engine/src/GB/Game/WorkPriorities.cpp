// T306: the table of work priorities of each person of the player's clan (see WorkPriorities.h).
#include "GB/Game/WorkPriorities.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr int kRankStep = 16;   // rank = category x this + job (both 1..10)

		const WorkCategories& CategoriesOf(const Tribe& tribe) { return tribe.Config.Work.Categories; }

		bool Boosted(const UtilityOption& o)   // the pyramid of needs made it worth more (food when the clan is short of it, or the person hungry)
		{
			return std::any_of(o.Factors.begin(), o.Factors.end(), [](const ScoreFactor& f) { return f.Name == "survival" && f.Value > 1.0f; });
		}
	}

	bool WorkPriorities::Parse(const std::string& word, int& number)
	{
		if (word == "-" || word == "off") { number = kOff; return true; }
		if (word.empty() || word.size() > 2 || word.find_first_not_of("0123456789") != std::string::npos) return false;
		const int n = std::stoi(word);
		if (n < 1 || n > kMax) return false;
		number = n;
		return true;
	}

	const WorkPriorities::Table* WorkPriorities::Of(const std::string& member) const
	{
		const auto it = m_Tables.find(member);
		return it == m_Tables.end() ? nullptr : &it->second;
	}

	WorkPriorities::Table WorkPriorities::Suggested(const Tribe& tribe, const TribeMember& m)
	{
		Table t;
		for (const WorkCategoryDef& c : CategoriesOf(tribe).List)
		{
			const auto s = c.Suggest.find(m.Role);
			t.Categories[c.Id] = s != c.Suggest.end() ? s->second : c.Default;
		}
		return t;
	}

	bool WorkPriorities::Rules(const TribeMember& m) const
	{
		return m_Active && !m.Player && !m.Dead && m_Tables.count(m.Name) != 0;
	}

	WorkPriorities::Table& WorkPriorities::TableFor(const Tribe& tribe, const TribeMember& m)
	{
		auto it = m_Tables.find(m.Name);
		if (it == m_Tables.end()) it = m_Tables.emplace(m.Name, Suggested(tribe, m)).first;
		return it->second;
	}

	int WorkPriorities::Number(const Tribe& tribe, const std::string& member, const std::string& job) const
	{
		const WorkCategoryDef* cat = CategoriesOf(tribe).CategoryOfJob(job);
		const TribeMember* m = tribe.FindMember(member);
		if (!cat || !m) return -1;
		const auto own = m_Tables.find(member);
		const Table t = own != m_Tables.end() ? own->second : Suggested(tribe, *m);
		const auto c = t.Categories.find(cat->Id);
		const int catNumber = c != t.Categories.end() ? c->second : Suggested(tribe, *m).Categories[cat->Id];   // (a category the data added since)
		if (catNumber == kOff) return kOff;
		const auto j = t.Jobs.find(job);
		return j != t.Jobs.end() ? j->second : catNumber;
	}

	int WorkPriorities::Rank(const Tribe& tribe, const TribeMember& m, const std::string& job) const
	{
		const WorkCategoryDef* cat = CategoriesOf(tribe).CategoryOfJob(job);
		const auto own = m_Tables.find(m.Name);
		if (!cat || own == m_Tables.end()) return kFree;
		const auto c = own->second.Categories.find(cat->Id);
		const int catNumber = c != own->second.Categories.end() ? c->second : Suggested(tribe, m).Categories[cat->Id];
		if (catNumber == kOff) return -1;
		const auto j = own->second.Jobs.find(job);
		const int jobNumber = j != own->second.Jobs.end() ? j->second : catNumber;
		if (jobNumber == kOff) return -1;
		return catNumber * kRankStep + jobNumber;
	}

	std::string WorkPriorities::JobOf(const Tribe& tribe, const TribeMember* m, const std::string& kind, const std::string& target)
	{
		const WorkCategories& cats = CategoriesOf(tribe);
		if (cats.Empty()) return {};
		// the item the work is about: the target itself, a recipe's output, a node kind's item, or the item of the node it names
		std::string item;
		if (tribe.m_Items && !target.empty())
		{
			if (tribe.m_Items->Find(target)) item = target;
			else if (const Recipe* r = tribe.m_Recipes ? tribe.m_Recipes->Find(target) : nullptr) item = r->Output;
			else if (const auto d = tribe.m_Defs.find(target); d != tribe.m_Defs.end()) item = d->second.Item;
			else
			{
				std::string node;
				if (m)
					if (const MemoryEntry* e = m->Mind.Memory.Find(target)) node = e->Kind;
				if (node.empty() && (kind == "gather" || kind == "designate")) node = ColonyDesignations::KindOf(tribe, target);
				if (const auto n = tribe.m_Defs.find(node); n != tribe.m_Defs.end()) item = n->second.Item;
			}
		}
		static const std::vector<std::string> none;
		const ItemDef* def = item.empty() || !tribe.m_Items ? nullptr : tribe.m_Items->Find(item);
		return cats.JobOf(kind, target, item, def ? def->Tags : none);
	}

	int WorkPriorities::JobRank(const Tribe& tribe, const TribeMember& m, const Job& job) const
	{
		const std::string id = JobOf(tribe, &m, job.Kind, job.Target);
		if (id.empty()) return kFree;
		const int rank = Rank(tribe, m, id);
		const bool food = job.Kind == "hunt" || tribe.IsFood(job.Target);
		// the needs first: any food job for a person critically hungry or thirsty, even one at "-"; a food job with a number the pyramid
		// boosts (the clan short of food) before the table's order. "-" is never otherwise: the clan's shortage does not undo it.
		if (food && Critical(tribe, m)) return 0;
		if (rank < 0) return -1;
		return tribe.SurvivalFactor(job) > 1.0f ? 0 : rank;
	}

	bool WorkPriorities::Critical(const Tribe& tribe, const TribeMember& m)
	{
		const AbilitySystemComponent* asc = tribe.m_Gameplay ? tribe.m_Gameplay->Find(m.Name) : nullptr;
		auto level = [&](const char* need) { return asc && asc->Attributes().Has(need) ? asc->Attribute(need) : 100.0f; };
		const float critical = tribe.Config.Work.Jobs.CriticalHunger;
		return level(NeedName::Hunger) < critical || level(NeedName::Thirst) < critical;
	}

	int WorkPriorities::Choose(const std::vector<Candidate>& c, int held, float margin)
	{
		int best = -1;
		for (int i = 0; i < static_cast<int>(c.size()); ++i)
		{
			if (i == held) continue;
			if (best < 0 || c[i].Rank < c[best].Rank || (c[i].Rank == c[best].Rank && c[i].Score > c[best].Score)) best = i;
		}
		if (held < 0 || held >= static_cast<int>(c.size())) return best;
		if (best < 0) return held;
		const Candidate& h = c[static_cast<size_t>(held)];
		const Candidate& b = c[static_cast<size_t>(best)];
		return b.Rank < h.Rank || (b.Rank == h.Rank && b.Score > h.Score + margin) ? best : held;
	}

	bool WorkPriorities::Claim(Tribe& tribe, TribeMember& m, const std::function<float(const Job&)>& score)
	{
		if (!Rules(m) || CategoriesOf(tribe).Empty()) return false;   // (no table set: the usual claim)
		const JobRules& rules = tribe.Config.Work.Jobs;
		const Job* held = tribe.m_Board.ClaimOf(m.Name);
		std::vector<Candidate> candidates;
		std::vector<const Job*> jobs;
		int heldAt = -1;
		for (const auto& [id, job] : tribe.m_Board.Jobs())
		{
			const bool mine = &job == held;
			if (!mine && !tribe.m_Board.HasRoom(job)) continue;
			const float s = score(job);
			if (s <= 0.0f) continue;
			const int rank = JobRank(tribe, m, job);
			if (rank < 0) continue;   // "-"
			if (!mine && s < rules.MinScore) continue;
			if (mine) heldAt = static_cast<int>(candidates.size());
			candidates.push_back({ rank, s });
			jobs.push_back(&job);
		}
		if (held && heldAt < 0) { tribe.m_Board.Release(m.Name); held = nullptr; }   // it cannot do it now, or the player turned it off
		const int pick = Choose(candidates, heldAt, rules.Switch);
		if (pick < 0 || pick == heldAt) return true;
		const std::string id = jobs[static_cast<size_t>(pick)]->Id;
		if (tribe.m_Board.Claim(id, m.Name, tribe.m_Time)) tribe.Say(m, "takes the job " + id + " from the board");
		return true;
	}

	void WorkPriorities::Options(const Tribe& tribe, const TribeMember& m, std::vector<UtilityOption>& options) const
	{
		if (!Rules(m) || CategoriesOf(tribe).Empty()) return;
		std::vector<int> ranks(options.size(), 0);
		int best = -1;
		const bool critical = Critical(tribe, m);
		for (size_t i = 0; i < options.size(); ++i)
		{
			UtilityOption& o = options[i];
			if (o.Kind == "rest") continue;   // nothing to do is always there
			const std::string job = JobOf(tribe, &m, o.Kind, o.Target);
			if (job.empty()) continue;   // free: eating, sleeping, talking...
			const int rank = Rank(tribe, m, job);
			// the needs win over the table: what the pyramid boosts (food) goes out of the order; at "-" only for a person critically hungry or thirsty
			if (Boosted(o) && (rank >= 0 || critical)) continue;
			ranks[i] = rank;
			if (ranks[i] < 0)
			{
				o.Score = 0.0f;
				o.Factors.push_back({ "priority -", 0.0f });
				continue;
			}
			if (o.Score > 0.0f && (best < 0 || ranks[i] < best)) best = ranks[i];
		}
		if (best < 0) return;
		for (size_t i = 0; i < options.size(); ++i)
			if (ranks[i] > best)
			{
				options[i].Score *= kBelow;
				options[i].Factors.push_back({ "priority", kBelow });
			}
	}

	float WorkPriorities::MarkFactor(const Tribe& tribe, const TribeMember& m, const std::string& node) const
	{
		if (!Rules(m)) return 1.0f;
		const std::string job = JobOf(tribe, &m, "designate", node);
		return job.empty() || Rank(tribe, m, job) >= 0 ? 1.0f : 0.0f;
	}

	WorkPriorities::Reply WorkPriorities::Set(const Tribe& tribe, const std::string& member, const std::string& what, int number)
	{
		const TribeMember* m = tribe.FindMember(member);
		if (!m || m->Dead) return { false, "there is nobody called " + member + " in the clan" };
		if (number < kOff || number > kMax) return { false, "a priority is 1 (first) to 10 (last) or - (never)" };
		const WorkCategories& cats = CategoriesOf(tribe);
		const bool category = cats.Category(what) != nullptr;
		if (!category && !cats.CategoryOfJob(what)) return { false, "there is no category nor job called " + what };
		Table& t = TableFor(tribe, *m);
		if (category) t.Categories[what] = number;
		else t.Jobs[what] = number;
		return { true, member + ": " + what + " " + Text(number) };
	}

	WorkPriorities::Reply WorkPriorities::Reset(const Tribe& tribe, const std::string& member)
	{
		const TribeMember* m = tribe.FindMember(member);
		if (!m || m->Dead) return { false, "there is nobody called " + member + " in the clan" };
		m_Tables.erase(member);   // (no table: it decides as the clan's AI does, and is shown its trade's suggestion)
		return { true, member + ": back to what a " + m->Role + " is suggested" };
	}

	std::string WorkPriorities::Show(const Tribe& tribe, const std::string& member) const
	{
		const TribeMember* m = tribe.FindMember(member);
		if (!m) return "there is nobody called " + member + " in the clan";
		std::string s = member + " (" + m->Role + (m_Tables.count(member) ? "" : ", suggested") + "):";
		bool first = true;
		for (const WorkCategoryDef& c : CategoriesOf(tribe).List)
		{
			const auto own = m_Tables.find(member);
			const Table t = own != m_Tables.end() ? own->second : Suggested(tribe, *m);
			const auto n = t.Categories.find(c.Id);
			s += std::string(first ? " " : ", ") + c.Id + " " + Text(n != t.Categories.end() ? n->second : c.Default) + " [";
			first = false;
			for (size_t i = 0; i < c.Jobs.size(); ++i) s += (i ? ", " : "") + c.Jobs[i].Id + " " + Text(Number(tribe, member, c.Jobs[i].Id));
			s += "]";
		}
		return s;
	}

	std::string WorkPriorities::ToJson() const
	{
		Json people = Json::object();
		for (const auto& [name, t] : m_Tables) people[name] = Json{ { "categories", t.Categories }, { "jobs", t.Jobs } };
		return Json{ { "version", 1 }, { "people", people } }.dump();
	}

	bool WorkPriorities::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("people") || !j["people"].is_object()) return false;
		std::map<std::string, Table> tables;
		auto read = [](const Json& o, std::map<std::string, int>& out)
		{
			if (!o.is_object()) return false;
			for (const auto& [k, v] : o.items())
			{
				if (!v.is_number_integer() || v.get<int>() < kOff || v.get<int>() > kMax) return false;
				out[k] = v.get<int>();
			}
			return true;
		};
		for (const auto& [name, t] : j["people"].items())
		{
			if (!t.is_object() || !t.contains("categories") || !t.contains("jobs")) return false;
			Table table;
			if (!read(t["categories"], table.Categories) || !read(t["jobs"], table.Jobs)) return false;
			tables[name] = std::move(table);
		}
		m_Tables = std::move(tables);
		return true;
	}
}
