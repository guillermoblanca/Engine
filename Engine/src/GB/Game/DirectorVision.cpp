#include "GB/Game/WorldDirector.h"
#include "GB/Game/Clans.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/UseLearning.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <nlohmann/json.hpp>

// T240 (MODO_DIOS_DECISIONES.md §3.c): the vision. A dreamer of the clan dreams a technology and the clan takes it as its aim, as `game research`
// does (Tribe::SetAim; Tribe::UpdateGoals puts it first among the goals, after what it requires). It orients, it grants nothing: the physical gate
// of T181 still decides whether the clan can learn it at all. The aim lapses when the way makes no step forward in Vision.Days. Only the public
// face of the Tribe is used; nothing here runs while there is no vision.
namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		std::string Num(double v, int decimals = 1)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%.*f", decimals, v);
			return b;
		}
		Clan* ClanAt(ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
		const Clan* ClanAt(const ClanWorld* clans, int id)
		{
			if (!clans) return nullptr;
			const Clan* c = clans->Find(id);
			return c && c->Folk ? c : nullptr;
		}
		std::string NameOf(const Clan& c) { return c.Name.empty() && c.Folk ? c.Folk->ClanName() : c.Name; }
		std::string Join(const std::vector<std::string>& v, const char* sep = ", ")
		{
			std::string s;
			for (const std::string& x : v) s += (s.empty() ? "" : sep) + x;
			return s;
		}
		// Every technology `tech` requires (to depth 8) and itself.
		void Closure(const TechTree& tree, const std::string& tech, std::vector<std::string>& out, int depth = 0)
		{
			const TechDef* t = tree.Find(tech);
			if (!t || depth > 8 || std::find(out.begin(), out.end(), tech) != out.end()) return;
			for (const std::string& r : t->Requires) Closure(tree, r, out, depth + 1);
			out.push_back(tech);
		}
		// How far the clan is on the way to `tech`: each technology of it known counts 1, else the best understanding of any member.
		float WayProgress(const Tribe& t, const std::string& tech)
		{
			const TechTree* tree = t.Knowledge().Techs();
			if (!tree) return 0.0f;
			std::vector<std::string> all;
			Closure(*tree, tech, all);
			float sum = 0.0f;
			for (const std::string& id : all)
			{
				if (t.Knowledge().Clan().count(id)) { sum += 1.0f; continue; }
				float best = 0.0f;
				for (const auto& [name, k] : t.Knowledge().Agents())
				{
					if (k.Known.count(id)) best = 1.0f;
					else if (const auto p = k.Progress.find(id); p != k.Progress.end()) best = std::max(best, p->second);
				}
				sum += best;
			}
			return sum;
		}
		std::string VisionText(const DirectorVision& v, const std::string& clan)
		{
			return "#" + std::to_string(v.Id) + " " + clan + " " + v.Tech + " (" + v.Dreamer + "): " + v.Status;
		}
	}

	// { "vision": { "days", "fulfilled", "keep", "traits": {} } }   (the cost is "vision" in "costs")
	std::string ReadDirectorVisionRules(const Json& d, DirectorRules& r)
	{
		const auto it = d.find("vision");
		if (it == d.end()) return {};
		const std::string w = "director: vision: ";
		if (!it->is_object()) return w + "expected an object";
		DirectorVisionRules v = r.Vision;
		for (const auto& [k, val] : it->items())
		{
			if (k == "days" || k == "fulfilled")
			{
				if (!val.is_number() || val.get<double>() < (k == "days" ? 0.05 : 0.0) || val.get<double>() > 1000.0) return w + k + ": expected a number in " + (k == "days" ? "0.05" : "0") + "..1000";
				(k == "days" ? v.Days : v.FulfilledGain) = val.get<float>();
			}
			else if (k == "keep")
			{
				if (!val.is_number_integer() || val.get<int>() < 0 || val.get<int>() > 1000) return w + "keep: expected an integer in 0..1000";
				v.Keep = val.get<int>();
			}
			else if (k == "traits")
			{
				if (!val.is_object()) return w + "traits: expected an object";
				std::map<std::string, float> traits;
				for (const auto& [name, x] : val.items())
				{
					if (!x.is_number() || x.get<double>() < -10.0 || x.get<double>() > 10.0) return w + "traits: " + name + ": expected a number in -10..10";
					traits[name] = x.get<float>();
				}
				v.Traits = traits;
			}
			else return w + "unknown key '" + k + "'";
		}
		r.Vision = v;
		return {};
	}

	const DirectorVision* WorldDirector::VisionOf(int clan) const
	{
		for (const DirectorVision& v : m_Visions) if (v.Clan == clan && v.Status == "aim") return &v;
		return nullptr;
	}

	DirectorVisionPreview WorldDirector::PreviewVision(int clan, const std::string& tech) const
	{
		DirectorVisionPreview p;
		const auto cost = Rules.Costs.find("vision");
		p.Cost = cost == Rules.Costs.end() ? 0.0f : cost->second;
		p.Name = tech;
		const Clan* c = ClanAt(m_To.Clans, clan);
		if (!c) { p.Why = "no clan " + std::to_string(clan); return p; }
		const Tribe& t = *c->Folk;
		const std::string name = NameOf(*c);
		const TechTree* tree = t.Knowledge().Techs();
		if (!tree) { p.Why = "this world has no technologies"; return p; }
		const TechDef* def = tree->Find(tech);
		if (!def) { p.Why = "no technology '" + tech + "'"; return p; }
		p.Name = def->Name;
		if (t.Knowledge().Clan().count(tech)) { p.Why = name + " already knows " + def->Name; return p; }
		// the way: what it does not know yet, in the order it would seek it (as Tribe::UpdateGoals)
		std::vector<std::string> all;
		Closure(*tree, tech, all);
		for (const std::string& id : all) if (!t.Knowledge().Clan().count(id)) p.Way.push_back(id);
		// what the world must give first (T181's physical gate): the materials not found, and a station when none is there
		for (const std::string& item : def->Physical.Materials) if (UseLearning::Have(t, item) <= 0.0f) p.Missing.push_back(item);
		if (!def->Physical.Stations.empty())
		{
			const std::string& fire = tree->Rules.Use.FireStation;
			const bool station = std::any_of(def->Physical.Stations.begin(), def->Physical.Stations.end(), [&](const std::string& s)
				{ return (!fire.empty() && s == fire) ? t.HasFire() : t.StationAt(s); });
			if (!station) p.Missing.push_back(Join(def->Physical.Stations, " or "));
		}
		// the dreamer (§3.0): the adult with most curiosity x devotion whose intellect reaches the complexity; else the most curious (ties by name)
		const float complexity = Complexity(*tree, tech);
		float bestInventor = -1.0f, bestCurious = -1.0f;
		std::string inventor, curious;
		std::vector<const TribeMember*> adults;
		for (const TribeMember& m : t.Members())
			if (!m.Dead && !m.Player && !t.People().IsChild(t.Config.Society, m.Name)) adults.push_back(&m);
		std::sort(adults.begin(), adults.end(), [](const TribeMember* a, const TribeMember* b) { return a->Name < b->Name; });
		for (const TribeMember* m : adults)
		{
			float curiosity = 0.5f;
			if (const Person* who = t.People().Find(m->Name))
				if (const auto k = who->Traits.find("curiosity"); k != who->Traits.end()) curiosity = k->second;
			const float score = curiosity * DevotionOf(clan, m->Name);
			if (UseLearning::Intellect(t, m->Name) >= complexity && score > bestInventor) { bestInventor = score; inventor = m->Name; }
			if (curiosity > bestCurious) { bestCurious = curiosity; curious = m->Name; }
		}
		p.Inventor = !inventor.empty();
		p.Dreamer = p.Inventor ? inventor : curious;
		if (p.Dreamer.empty()) { p.Why = name + " has nobody to dream it"; return p; }
		if (const DirectorVision* held = VisionOf(clan))
		{
			p.Why = held->Tech == tech ? def->Name + " is already the aim of " + name : "a vision already guides " + name + " (" + held->Tech + "): let it go first";
			return p;
		}
		const DirectorCheck can = CanSpend("vision", clan, true);
		p.Cost = can.Cost;
		if (!can.Ok) { p.Why = can.Why; return p; }
		// (no cost in the vote, not even in survival: a vision only orients, it takes nobody from the food; the survival tier already damps the
		// experiments themselves, UseLearning::Room. A resist is the only no; a doubt still takes it as the aim)
		const DirectorVoteResult vote = ForeseeVote(clan, p.Dreamer, Rules.Vision.Traits, 0.0f);
		p.Yes = vote.Yes;
		p.Voters = vote.Voters;
		p.Outcome = vote.Outcome;
		p.Ok = true;
		return p;
	}

	DirectorReply WorldDirector::GiveVision(int clan, const std::string& tech)
	{
		const DirectorVisionPreview p = PreviewVision(clan, tech);
		if (!p.Ok) return { false, "director vision: cannot: " + p.Why };
		Clan* c = ClanAt(m_To.Clans, clan);
		Tribe& t = *c->Folk;
		const std::string name = NameOf(*c);
		for (const DirectorVision& old : m_Visions)   // T241: the god insists on a dream the clan refused within InsistDays
			if (old.Clan == clan && old.Tech == tech && old.Status == "refused" && Now() - old.Since < static_cast<double>(Rules.InsistDays * Rules.DaySeconds))
			{
				AddPressure(clan, Rules.PressureInsist, "the god insists on the dream of " + p.Name + " after the clan said no");
				break;
			}
		const DirectorCheck paid = Spend("vision", clan, true, p.Dreamer + " of " + name + " dreams of " + p.Name);
		if (!paid.Ok) return { false, "director vision: cannot: " + paid.Why };
		Voted();   // (D43: counted for the report)
		DirectorVision v;
		v.Id = m_NextVision++;
		v.Clan = clan;
		v.Tech = tech;
		v.Dreamer = p.Dreamer;
		v.Since = v.Advanced = Now();
		v.Paid = paid.Cost;
		const TribeMember* m = t.FindMember(p.Dreamer);
		const std::string vote = "vote " + std::to_string(p.Yes) + " of " + std::to_string(p.Voters);
		if (p.Outcome == "resist")
		{
			v.Status = "refused";
			if (m) t.Note(*m, "dreams of " + p.Name + "; the clan will not seek it (" + vote + ")");
			Refund(clan, 0.5f * paid.Cost, "the clan resists the vision #" + std::to_string(v.Id) + " (" + vote + ")");
			Refused(clan, "vision", p.Name);   // D43/T241: the faith drops, the clan murmurs
			m_Visions.push_back(v);
			return { true, "director vision: " + p.Dreamer + " dreams of " + p.Name + ", but " + name + " will not seek it (" + vote + "); "
				+ Num(0.5f * paid.Cost) + " back (influence " + Num(m_Influence) + ")" };
		}
		t.SetAim(tech, p.Dreamer);
		v.Progress = WayProgress(t, tech);
		if (m) t.Note(*m, "dreams of " + p.Name + " and tells it at the fire");
		m_Visions.push_back(v);
		std::string text = "director vision: " + p.Dreamer + " dreams of " + p.Name + "; " + name + " now seeks it (the way: " + Join(p.Way) + "; " + vote + ")";
		if (!p.Missing.empty()) text += "; they lack " + Join(p.Missing) + ": they will not learn it until they find it";
		if (!p.Inventor) text += "; nobody of " + name + " can work it out: only from another clan";
		return { true, text + " (influence " + Num(m_Influence) + ")" };
	}

	DirectorReply WorldDirector::EndVision(int clan, const std::string& why)
	{
		for (DirectorVision& v : m_Visions)
			if (v.Clan == clan && v.Status == "aim")
			{
				v.Status = why;
				Clan* c = ClanAt(m_To.Clans, clan);
				if (c && c->Folk->ResearchAim() == v.Tech) c->Folk->SetAim("", "");
				Write("vision_end", c ? NameOf(*c) : std::to_string(clan), 0.0f, "the vision of " + v.Tech + " ends: " + why, clan);
				return { true, "director vision: the vision of " + v.Tech + " ends (" + why + ")" };
			}
		return { false, "director vision: no vision guides that clan" };
	}

	void WorldDirector::UpdateVisions()
	{
		if (m_Visions.empty()) return;
		bool any = false;
		for (const DirectorVision& v : m_Visions) any = any || v.Status == "aim";
		if (!any) return;
		const double now = Now();
		for (DirectorVision& v : m_Visions)
		{
			if (v.Status != "aim") continue;
			Clan* c = ClanAt(m_To.Clans, v.Clan);
			if (!c) { v.Status = "gone"; continue; }
			Tribe& t = *c->Folk;
			if (t.Knowledge().Clan().count(v.Tech))
			{
				v.Status = "learnt";
				Fulfilled(v.Clan, Rules.Vision.FulfilledGain, NameOf(*c) + " has learnt " + v.Tech + ", as " + v.Dreamer + " dreamt");
				continue;
			}
			if (t.ResearchAim() != v.Tech) { v.Status = "replaced"; continue; }
			const float progress = WayProgress(t, v.Tech);
			if (progress > v.Progress + 1e-4f) { v.Progress = progress; v.Advanced = now; continue; }
			if (now - v.Advanced >= static_cast<double>(Rules.Vision.Days) * static_cast<double>(Rules.DaySeconds))
			{
				v.Status = "lapsed";
				t.SetAim("", "");
				t.NoteClan("forgets the dream of " + v.Tech + ": nothing came of it");
				Write("vision_end", NameOf(*c), 0.0f, "the vision of " + v.Tech + " lapses", v.Clan);
			}
		}
		// keep the last closed ones only
		int closed = 0;
		for (const DirectorVision& v : m_Visions) if (v.Status != "aim") ++closed;
		while (closed > Rules.Vision.Keep)
		{
			const auto it = std::find_if(m_Visions.begin(), m_Visions.end(), [](const DirectorVision& v) { return v.Status != "aim"; });
			if (it == m_Visions.end()) break;
			m_Visions.erase(it);
			--closed;
		}
	}

	DirectorReply WorldDirector::VisionCommand(const std::vector<std::string>& a)
	{
		// director vision [preview] <clan> <tech>|none ; director visions
		if (!m_To.Clans) return { false, "director: no clans" };
		if (a[0] == "visions")
		{
			std::string s = "director visions: " + std::to_string(m_Visions.size());
			for (const DirectorVision& v : m_Visions)
			{
				const Clan* c = ClanAt(m_To.Clans, v.Clan);
				s += "\n" + VisionText(v, c ? NameOf(*c) : std::to_string(v.Clan));
			}
			return { true, s };
		}
		const bool preview = a.size() >= 2 && a[1] == "preview";
		const size_t at = preview ? 2 : 1;
		if (a.size() < at + 2) return { false, "usage: director vision [preview] <clan> <tech>|none" };
		const Clan* c = m_To.Clans->Find(a[at]);
		if (!c || !c->Folk) return { false, "director vision: no clan '" + a[at] + "'" };
		const std::string& tech = a[at + 1];
		if (!preview && (tech == "none" || tech == "off")) return EndVision(c->Id, "dropped");
		if (!preview) return GiveVision(c->Id, tech);
		const DirectorVisionPreview p = PreviewVision(c->Id, tech);
		std::string s = "director vision preview " + NameOf(*c) + " " + tech + ": " + (p.Ok ? "ok" : "no: " + p.Why) + "; cost " + Num(p.Cost);
		if (!p.Way.empty()) s += "; the way: " + Join(p.Way);
		if (!p.Dreamer.empty()) s += "; dreamer " + p.Dreamer + (p.Inventor ? "" : " (nobody can work it out: only from another clan)");
		if (p.Ok) s += "; vote " + std::to_string(p.Yes) + " of " + std::to_string(p.Voters) + " (" + p.Outcome + ")";
		if (!p.Missing.empty()) s += "; they lack " + Join(p.Missing) + ": they will not learn it until they find it";
		return { true, s };
	}

	std::string WorldDirector::VisionsToJson() const
	{
		if (m_Visions.empty()) return {};
		Json j;
		Json list = Json::array();
		for (const DirectorVision& v : m_Visions)
			list.push_back({ { "id", v.Id }, { "clan", v.Clan }, { "tech", v.Tech }, { "dreamer", v.Dreamer }, { "status", v.Status }, { "since", v.Since },
				{ "advanced", v.Advanced }, { "progress", v.Progress }, { "paid", v.Paid } });
		j["list"] = list;
		j["next"] = m_NextVision;
		return j.dump();
	}

	bool WorldDirector::VisionsFromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		m_Visions.clear();
		if (const auto it = j.find("list"); it != j.end())
			for (const Json& e : *it)
			{
				DirectorVision v;
				v.Id = e.value("id", 0);
				v.Clan = e.value("clan", -1);
				v.Tech = e.value("tech", std::string());
				v.Dreamer = e.value("dreamer", std::string());
				v.Status = e.value("status", std::string("aim"));
				v.Since = e.value("since", 0.0);
				v.Advanced = e.value("advanced", 0.0);
				v.Progress = e.value("progress", 0.0f);
				v.Paid = e.value("paid", 0.0f);
				m_Visions.push_back(v);
			}
		m_NextVision = j.value("next", 1);
		return true;
	}
}
