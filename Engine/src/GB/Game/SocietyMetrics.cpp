#include "GB/Game/SocietyMetrics.h"
#include "GB/Game/Clans.h"
#include "GB/Game/History.h"
#include "GB/Game/Society.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Vitals.h"
#include "GB/Telemetry/TelemetrySink.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>

// T204 (S1): the measure of a society. Only reads: see SocietyMetrics.h.
namespace GB
{
	using Json = nlohmann::json;

	const std::vector<std::string>& QolParts()
	{
		static const std::vector<std::string> parts = { "needs", "shelter", "food", "tools", "health", "longevity", "techs" };
		return parts;
	}

	namespace
	{
		// The numbers of the section: key, field, lowest (allowed itself or not) and highest.
		struct MetricNumber { const char* Key; float SocietyMetricRules::* Field; double Min; bool MinIn; double Max; };
		const std::vector<MetricNumber>& MetricNumbers()
		{
			static const std::vector<MetricNumber> numbers = {
				{ "needsFloor", &SocietyMetricRules::NeedsFloor, 0.0, true, 0.99 },
				{ "foodHalf", &SocietyMetricRules::FoodHalf, 0.0, false, 1000.0 },
				{ "bedsPerPerson", &SocietyMetricRules::BedsPerPerson, 0.0, false, 1000.0 },
				{ "bedComfort", &SocietyMetricRules::BedComfort, 0.0, false, 1000.0 },
				{ "toolPower", &SocietyMetricRules::ToolPower, 0.0, false, 1000.0 },
				{ "techsHalf", &SocietyMetricRules::TechsHalf, 0.0, false, 10000.0 },
				{ "lossDays", &SocietyMetricRules::LossDays, 0.0, false, 1000.0 },
				{ "lossScale", &SocietyMetricRules::LossScale, 0.0, true, 100.0 },
			};
			return numbers;
		}
	}

	std::string LoadSocietyMetricRules(const std::string& json, SocietyMetricRules& live)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("metrics");
		if (section == doc.end()) return {};
		if (!section->is_object()) return "metrics: expected an object";
		SocietyMetricRules r = live;
		for (const auto& [key, value] : section->items())
		{
			if (key != "qol") return "metrics: unknown key '" + key + "'";
			if (!value.is_object()) return "metrics: qol: expected an object";
			for (const auto& [k, v] : value.items())
			{
				const std::string where = "metrics: qol: " + k + ": ";
				if (k == "weights")
				{
					if (!v.is_object()) return where + "expected an object";
					std::map<std::string, float> weights;
					for (const auto& [part, w] : v.items())
					{
						const std::vector<std::string>& known = QolParts();
						if (std::find(known.begin(), known.end(), part) == known.end()) return where + "unknown part '" + part + "'";
						if (!w.is_number() || w.get<double>() < 0.0 || w.get<double>() > 100.0) return where + part + ": expected a number in 0..100";
						weights[part] = w.get<float>();
					}
					float sum = 0.0f;
					for (const auto& [part, w] : weights) sum += w;
					if (sum <= 0.0f) return where + "the weights add up to 0";
					r.Weights = std::move(weights);
				}
				else if (k == "needs")
				{
					if (!v.is_array()) return where + "expected a list of need names";
					std::vector<std::string> needs;
					for (const Json& n : v)
					{
						if (!n.is_string() || n.get<std::string>().empty()) return where + "expected a list of need names";
						needs.push_back(n.get<std::string>());
					}
					r.Needs = std::move(needs);
				}
				else
				{
					const auto& numbers = MetricNumbers();
					const auto n = std::find_if(numbers.begin(), numbers.end(), [&](const MetricNumber& m) { return k == m.Key; });
					if (n == numbers.end()) return "metrics: qol: unknown key '" + k + "'";
					const double x = v.is_number() ? v.get<double>() : -1.0;
					if (!v.is_number() || x > n->Max || (n->MinIn ? x < n->Min : x <= n->Min))
					{
						char range[96];
						std::snprintf(range, sizeof range, "expected a number %s %g up to %g", n->MinIn ? "from" : "over", n->Min, n->Max);
						return where + range;
					}
					r.*(n->Field) = static_cast<float>(x);
				}
			}
		}
		live = std::move(r);
		return {};
	}

	std::string SocietyMetricRulesToJson(const SocietyMetricRules& r)
	{
		Json weights = Json::object();
		for (const auto& [part, w] : r.Weights) weights[part] = std::round(static_cast<double>(w) * 10000.0) / 10000.0;
		Json qol = { { "weights", weights }, { "needs", r.Needs } };
		for (const MetricNumber& n : MetricNumbers()) qol[n.Key] = static_cast<double>(r.*(n.Field));
		return Json{ { "qol", qol } }.dump();
	}

	float Saturate(float n, float half) { return n > 0.0f ? n / (n + std::max(half, 1e-6f)) : 0.0f; }

	float Losses(float youngDead, int alive, const SocietyMetricRules& rules)
	{
		if (youngDead <= 0.0f) return 0.0f;
		return std::clamp(rules.LossScale * youngDead / (static_cast<float>(std::max(alive, 0)) + youngDead), 0.0f, 1.0f);
	}

	float Qol(const std::map<std::string, float>& parts, const SocietyMetricRules& rules)
	{
		double sum = 0.0, weight = 0.0;
		for (const auto& [part, w] : rules.Weights)
		{
			const auto it = parts.find(part);
			if (it == parts.end() || it->second < 0.0f || w <= 0.0f) continue;
			sum += static_cast<double>(w) * static_cast<double>(std::clamp(it->second, 0.0f, 1.0f));
			weight += static_cast<double>(w);
		}
		return weight > 0.0 ? static_cast<float>(sum / weight) : 0.0f;
	}

	std::vector<std::string> Lineages(const std::map<std::string, const Person*>& people, const std::vector<std::string>& living)
	{
		std::set<std::string> roots;
		for (const std::string& name : living)
		{
			const auto self = people.find(name);
			if (self == people.end() || !self->second || self->second->Generation <= 0) continue;   // (a founder is a line, not a descendant of one)
			std::vector<std::string> todo = { name };
			std::set<std::string> seen;
			while (!todo.empty())
			{
				const std::string at = todo.back();
				todo.pop_back();
				if (!seen.insert(at).second) continue;
				const auto it = people.find(at);
				const Person* p = it == people.end() ? nullptr : it->second;
				if (at != name && (!p || p->Generation <= 0)) { roots.insert(at); continue; }   // a founder, or the topmost ancestor known
				if (!p) continue;
				bool parent = false;
				for (const std::string* up : { &p->Mother, &p->Father })
					if (!up->empty()) { todo.push_back(*up); parent = true; }
				if (!parent && at != name) roots.insert(at);
			}
		}
		return { roots.begin(), roots.end() };
	}

	namespace
	{
		bool Under(const std::string& tag, const std::string& root) { return !root.empty() && (tag == root || (tag.size() > root.size() && tag.compare(0, root.size(), root) == 0 && tag[root.size()] == '.')); }

		// What a building does for the life of who lives by it (the generic provisions of docs/PROGRESION.md), not what it is called.
		bool BettersLife(const BuildingDef& b)
		{
			return b.Shelter > 0 || b.Rest > 0.0f || b.Warmth > 0.0f || b.StoreCap > 0 || b.Keep < 1.0f || b.Healing > 0.0f || b.Culture > 0.0f || b.Well || !b.Store.empty()
				|| !b.Provides.empty();
		}

		// What an item made does: food (not drink), a container or a tool (D33: a better axe, hoe or fish trap spares toil: the use of materials).
		bool BettersLife(const ItemDef& d, const TribeEconomy& econ)
		{
			if (d.BagSlots > 0) return true;
			for (const std::string& t : d.Tags) if (Under(t, "Item.Tool")) return true;
			for (const std::string& t : d.Tags) if (Under(t, econ.FoodTag) && !Under(t, econ.DrinkTag)) return true;
			return false;
		}

		// The technologies of the tree that make life better, by what they unlock (a tech known from the start is not progress).
		std::vector<std::string> LifeTechs(const Tribe& t)
		{
			std::vector<std::string> out;
			const TechTree* tree = t.Knowledge().Techs();
			if (!tree) return out;
			const std::map<std::string, BuildingDef>* buildings = t.Buildings();
			const ItemLibrary* items = t.Items();
			for (const auto& [id, tech] : tree->Techs)
			{
				if (tech.Known) continue;
				bool life = false;
				if (buildings)
				{
					for (const std::string& b : tech.Buildings)
						if (const auto it = buildings->find(b); it != buildings->end() && BettersLife(it->second)) life = true;
					for (const auto& [bid, def] : *buildings)
						if (def.Tech == id && BettersLife(def)) life = true;
				}
				if (items)
					for (const std::string& r : tech.Recipes)
						if (const ItemDef* d = items->Find(r); d && BettersLife(*d, t.Config.Economy)) life = true;
				if (life) out.push_back(id);
			}
			return out;
		}

		// name -> person over every society of the world (a living entry before a dead one of the same name: a member who moved keeps one)
		std::map<std::string, const Person*> PeopleOf(const Tribe& tribe, const ClanWorld* world)
		{
			std::map<std::string, const Person*> out;
			auto add = [&](const Tribe& t)
			{
				for (const auto& [name, p] : t.People().People())
				{
					const Person*& slot = out[name];
					if (!slot || (slot->Dead && !p.Dead)) slot = &p;
				}
			};
			if (world) for (const auto& c : world->All()) if (c->Folk) add(*c->Folk);
			if (!world || std::none_of(world->All().begin(), world->All().end(), [&](const auto& c) { return c->Folk == &tribe; })) add(tribe);
			return out;
		}

		std::vector<std::string> LivingOf(const Tribe& t)
		{
			std::vector<std::string> out;
			for (const TribeMember& m : t.Members()) if (!m.Dead) out.push_back(m.Name);
			return out;
		}

		double Round(float v, double scale = 1000.0) { return std::round(static_cast<double>(v) * scale) / scale; }
	}

	SocietyMeasure MeasureSociety(const Tribe& t, const SocietyMetricRules& rules, int clan, const ClanWorld* world)
	{
		SocietyMeasure out;
		out.Clan = clan;
		if (world) if (const Clan* c = world->Find(clan)) out.Name = c->Name;
		if (out.Name.empty()) out.Name = "clan " + std::to_string(clan);
		const Society& society = t.People();
		out.Births = society.Births();
		out.Deaths = society.Deaths();

		// ---- the living: needs, health, trades, tools, aptitudes, generations
		double needs = 0.0, health = 0.0, toolsHeld = 0.0;
		int withNeeds = 0, withHealth = 0, toolsWanted = 0, adultsWithSkills = 0;
		double entropy = 0.0;
		std::set<std::string> roles;
		const ItemLibrary* items = t.Items();
		for (const TribeMember& m : t.Members())
		{
			if (m.Dead) continue;
			++out.Alive;
			const std::map<std::string, float> levels = t.NeedLevels(m);
			double sum = 0.0;
			int n = 0;
			for (const std::string& need : rules.Needs)
				if (const auto it = levels.find(need); it != levels.end()) { sum += static_cast<double>(std::clamp(it->second, 0.0f, 100.0f)) / 100.0; ++n; }
			if (n) { needs += sum / n; ++withNeeds; }
			if (const auto it = levels.find(NeedName::Health); it != levels.end()) { health += static_cast<double>(std::clamp(it->second, 0.0f, 100.0f)) / 100.0; ++withHealth; }
			if (const Person* p = society.Find(m.Name)) out.Generations = std::max(out.Generations, p->Generation);
			if (society.IsChild(t.Config.Society, m.Name)) continue;
			roles.insert(m.Role);
			if (const auto role = t.Config.Roles.find(m.Role); role != t.Config.Roles.end() && !role->second.ToolTag.empty() && !m.Player)
			{
				++toolsWanted;
				// the best tool of its trade it carries: (1 + Power) / (1 + ToolPower), none 0
				int best = -1;
				if (items && m.Body)
					for (const ItemSlot& s : m.Body->Bag().Stacks())
					{
						const ItemDef* d = s.Count > 0 ? items->Find(s.Id) : nullptr;
						if (d && std::any_of(d->Tags.begin(), d->Tags.end(), [&](const std::string& tag) { return Under(tag, role->second.ToolTag); })) best = std::max(best, std::max(d->Power, 0));
					}
				if (best >= 0) toolsHeld += std::min(1.0, (1.0 + best) / (1.0 + static_cast<double>(rules.ToolPower)));
			}
			double total = 0.0;
			for (const auto& [family, level] : m.Skills) if (level > 0.0f) total += static_cast<double>(level);
			if (total > 0.0)
			{
				double h = 0.0;
				for (const auto& [family, level] : m.Skills)
					if (level > 0.0f) { const double p = static_cast<double>(level) / total; h -= p * std::log2(p); }
				entropy += h;
				++adultsWithSkills;
			}
		}
		out.Roles = static_cast<int>(roles.size());
		out.SkillEntropy = adultsWithSkills ? static_cast<float>(entropy / adultsWithSkills) : 0.0f;

		// ---- the dead: the age each reached, as a share of an elder's (at most 1)
		const float elder = std::max(1.0f, t.Config.Society.Life.ElderFrom);
		double deathAge = 0.0;
		int dead = 0;
		for (const auto& [name, p] : society.People()) if (p.Dead) { deathAge += std::min(1.0, static_cast<double>(p.Age) / elder); ++dead; }

		// ---- the beds: the best first, each worth its comfort
		std::vector<std::pair<float, int>> beds;   // (comfort 0..1, beds)
		if (const std::map<std::string, BuildingDef>* defs = t.Buildings())
		{
			std::map<std::string, const BuildingDef*> byName;   // (a site says its building's name)
			for (const auto& [id, def] : *defs) byName.emplace(def.Name, &def);
			for (const Tribe::SettlementSite& s : t.SettlementSites())
			{
				if (!s.State.Done || s.Beds <= 0) continue;
				const auto it = byName.find(s.Building);
				const float comfort = it == byName.end() ? 1.0f : (it->second->Rest + it->second->Warmth) / std::max(rules.BedComfort, 1e-6f);
				beds.push_back({ std::clamp(comfort, 0.0f, 1.0f), s.Beds });
			}
		}
		std::sort(beds.begin(), beds.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		const float wanted = rules.BedsPerPerson * static_cast<float>(out.Alive);
		float slept = 0.0f, room = wanted;
		for (const auto& [comfort, n] : beds)
		{
			const float take = std::min(room, static_cast<float>(n));
			slept += take * comfort;
			room -= take;
			if (room <= 0.0f) break;
		}

		// ---- the young dead of the last days (the history: "death", or "killed" for who died in a fight), the later the more each weighs
		const WorldHistory* past = world ? &world->Past() : t.History();
		if (past && rules.LossDays > 0.0f)
		{
			const float daySeconds = t.Config.Society.Life.DaySeconds > 0.0f ? t.Config.Society.Life.DaySeconds : t.Config.Needs.DaySeconds;
			const double now = past->HasClock() ? past->Clock() : t.WorldSeconds(), span = static_cast<double>(rules.LossDays) * std::max(1.0f, daySeconds);
			std::set<std::string> counted;
			for (auto e = past->Events().rbegin(); e != past->Events().rend(); ++e)
			{
				if (e->At < now - span || e->Clan != clan || (e->Kind != "death" && e->Kind != "killed") || e->Whom.empty() || e->At > now) continue;
				const Person* p = society.Find(e->Whom);
				if (!p || !p->Dead || p->Age >= elder || !counted.insert(e->Whom).second) continue;
				out.YoungDead += static_cast<float>(1.0 - (now - e->At) / span);
			}
		}
		out.Losses = Losses(out.YoungDead, out.Alive, rules);

		// ---- the parts of qol
		auto& parts = out.Parts;
		parts["needs"] = withNeeds ? std::clamp((static_cast<float>(needs / withNeeds) - rules.NeedsFloor) / std::max(1.0f - rules.NeedsFloor, 1e-6f), 0.0f, 1.0f) : -1.0f;
		parts["shelter"] = wanted > 0.0f ? std::min(1.0f, slept / wanted) : -1.0f;
		parts["food"] = out.Alive ? Saturate(t.FoodDays(), rules.FoodHalf) : -1.0f;
		parts["tools"] = toolsWanted ? static_cast<float>(toolsHeld / toolsWanted) : -1.0f;
		parts["health"] = withHealth ? static_cast<float>(health / withHealth) : -1.0f;
		parts["longevity"] = dead ? static_cast<float>(deathAge / dead) : -1.0f;
		const std::vector<std::string> life = LifeTechs(t);
		const std::set<std::string>& known = t.Knowledge().Clan();
		const auto knownLife = std::count_if(life.begin(), life.end(), [&](const std::string& id) { return known.count(id) > 0; });
		out.LifeTechs = static_cast<int>(knownLife);
		parts["techs"] = t.Knowledge().Techs() ? Saturate(static_cast<float>(knownLife), rules.TechsHalf) : -1.0f;
		out.Qol = out.Alive ? Qol(parts, rules) * (1.0f - out.Losses) : 0.0f;

		// ---- the rest
		out.Lineages = static_cast<int>(Lineages(PeopleOf(t, world), LivingOf(t)).size());
		const TrailState& trails = t.Trails();
		if (trails.Ready())
		{
			const int trail = trails.Counts(TrailTier::Trail, t.Config.Trails), worn = trails.Counts(TrailTier::Worn, t.Config.Trails), paved = trails.Counts(TrailTier::Paved, t.Config.Trails);
			out.TrailCells = worn + paved;
			out.PathCells = trail + worn + paved;
		}
		out.Techs = static_cast<int>(known.size());
		if (past)
		{
			std::set<std::string> living;
			for (const TribeMember& m : t.Members()) if (!m.Dead) living.insert(m.Name);
			// An inventor is the first of a clan to work a technology out by experimenting ("<who> (<role>) discovers <Tech> by experimenting..."):
			// everyone after it in that clan finds what was already known there. Counted where it lives now (one who married away is still one).
			std::set<std::pair<int, std::string>> found;   // (clan, technology) worked out already
			std::set<std::string> inventors;
			for (const HistoryEvent& e : past->Events())
			{
				if (e.Kind == "found" && e.Clan == clan) ++out.Splits;
				if (e.Kind != "discovery") continue;
				const size_t at = e.Text.find("discovers "), by = e.Text.find(" by experimenting");
				if (at == std::string::npos || by == std::string::npos || by < at + 10) continue;
				if (found.insert({ e.Clan, e.Text.substr(at + 10, by - at - 10) }).second && living.count(e.Who)) inventors.insert(e.Who);
			}
			out.Inventors = static_cast<int>(inventors.size());
		}
		if (world)
			for (const auto& c : world->All())
			{
				if (c->Id == clan) continue;
				if (const ClanRelation* r = world->Relation(clan, c->Id)) { out.TradesOut += r->Trades; out.Gifts += r->Gifts; }
			}
		out.Trades = out.TradesIn + out.TradesOut;
		return out;
	}

	std::vector<SocietyMeasure> MeasureSocieties(const ClanWorld& world)
	{
		std::vector<const Clan*> clans;
		for (const auto& c : world.All()) if (c->Folk) clans.push_back(c.get());
		std::sort(clans.begin(), clans.end(), [](const Clan* a, const Clan* b) { return a->Id < b->Id; });
		std::vector<SocietyMeasure> out;
		for (const Clan* c : clans) out.push_back(MeasureSociety(*c->Folk, world.Metrics, c->Id, &world));
		return out;
	}

	SocietyWorld MeasureWorld(const ClanWorld& world, const std::vector<SocietyMeasure>& clans)
	{
		SocietyWorld w;
		double qol = 0.0;
		for (const SocietyMeasure& m : clans)
		{
			++w.Clans;
			w.Alive += m.Alive;
			w.Splits += m.Splits;
			w.TradesOut += m.TradesOut;
			w.Generations = std::max(w.Generations, m.Generations);
			w.Techs = std::max(w.Techs, m.Techs);
			w.Inventors += m.Inventors;
			qol += static_cast<double>(m.Qol) * m.Alive;
		}
		w.TradesOut /= 2;   // (each barter is counted by both clans)
		w.Qol = w.Alive ? static_cast<float>(qol / w.Alive) : 0.0f;
		std::map<std::string, const Person*> people;
		std::vector<std::string> living;
		for (const auto& c : world.All())
		{
			if (!c->Folk) continue;
			for (const auto& [name, p] : c->Folk->People().People())
			{
				const Person*& slot = people[name];
				if (!slot || (slot->Dead && !p.Dead)) slot = &p;
			}
			for (const TribeMember& m : c->Folk->Members()) if (!m.Dead) living.push_back(m.Name);
		}
		w.Lineages = static_cast<int>(Lineages(people, living).size());
		return w;
	}

	std::string SocietyLine(const SocietyMeasure& m)
	{
		std::string parts;
		char b[64];
		for (const std::string& p : QolParts())
		{
			const auto it = m.Parts.find(p);
			if (it == m.Parts.end() || it->second < 0.0f) std::snprintf(b, sizeof b, " %s -", p.c_str());
			else std::snprintf(b, sizeof b, " %s %.2f", p.c_str(), static_cast<double>(it->second));
			parts += b;
		}
		char line[768];
		std::snprintf(line, sizeof line, "society: %s qol %.3f (%s losses %.2f ) young_dead %.2f life_techs %d alive %d births %d deaths %d generations %d lineages %d trail_cells %d path_cells %d splits %d trades %d trades_in %d trades_out %d gifts %d roles %d skill_entropy %.2f techs %d inventors %d",
			m.Name.c_str(), static_cast<double>(m.Qol), parts.c_str() + 1, static_cast<double>(m.Losses), static_cast<double>(m.YoungDead), m.LifeTechs, m.Alive, m.Births, m.Deaths, m.Generations, m.Lineages, m.TrailCells, m.PathCells, m.Splits, m.Trades, m.TradesIn, m.TradesOut,
			m.Gifts, m.Roles, static_cast<double>(m.SkillEntropy), m.Techs, m.Inventors);
		return line;
	}

	std::string SocietyWorldLine(const SocietyWorld& w)
	{
		char line[256];
		std::snprintf(line, sizeof line, "society world: clans %d alive %d qol %.3f generations %d lineages %d splits %d trades_out %d techs %d inventors %d", w.Clans, w.Alive,
			static_cast<double>(w.Qol), w.Generations, w.Lineages, w.Splits, w.TradesOut, w.Techs, w.Inventors);
		return line;
	}

	void WriteSocietyDays(const ClanWorld& world, TelemetrySink& sink, double time, int day)
	{
		if (!sink.Active()) return;
		const std::vector<SocietyMeasure> clans = MeasureSocieties(world);
		int splits = 0;
		for (const SocietyMeasure& m : clans) splits += m.Splits;
		for (const SocietyMeasure& m : clans)
		{
			TelemetrySocietyDay r;
			r.Clan = m.Clan;
			r.Time = time;
			r.Day = day;
			r.Alive = m.Alive;
			r.Qol = Round(m.Qol, 10000.0);
			auto part = [&](const char* p) { const auto it = m.Parts.find(p); return it == m.Parts.end() || it->second < 0.0f ? -1.0 : Round(it->second, 10000.0); };
			r.Needs = part("needs");
			r.Shelter = part("shelter");
			r.Food = part("food");
			r.Tools = part("tools");
			r.Health = part("health");
			r.Longevity = part("longevity");
			r.LifeTechs = part("techs");
			r.Generations = m.Generations;
			r.Lineages = m.Lineages;
			r.TrailCells = m.TrailCells;
			r.PathCells = m.PathCells;
			r.Splits = m.Splits;
			r.SplitsWorld = splits;
			r.Trades = m.Trades;
			r.TradesIn = m.TradesIn;
			r.TradesOut = m.TradesOut;
			r.Gifts = m.Gifts;
			r.Roles = m.Roles;
			r.SkillEntropy = Round(m.SkillEntropy);
			r.Techs = m.Techs;
			r.Inventors = m.Inventors;
			r.Births = m.Births;
			r.Deaths = m.Deaths;
			sink.SocietyDay(r);
		}
	}
}
