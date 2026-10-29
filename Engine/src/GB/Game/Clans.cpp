// H13 · clans and diplomacy: several tribes in one world and what lies between them (see GB/Game/Clans.h and docs/CLANES.md). The tribes know
// nothing of each other; the clan world hooks into each (Tribe::SetOutside) to add the "envoy" option to the members it sends to a meeting, to
// carry it out, and to hear of every gather (whose land it was on). The rest it does on its own clocks: encounters, territory, trade, weddings,
// pacts and the days. Deterministic: clans in id order, pairs (a < b), members and regions in order, maps by key; the only chance is a hash.
#include "GB/Game/Clans.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kAtCamp = 8.0f;     // an envoy this close to its camp is home (fetches the goods, puts away what it got)
		constexpr size_t kLogLines = 400;   // the log of the clan world is a ring
		constexpr size_t kHistory = 32;     // meetings remembered
		constexpr size_t kRecent = 8;       // what moved a relation lately

		void Go(char* buffer, size_t size, const glm::vec2& at, const char* tail)
		{
			std::snprintf(buffer, size, "goto %.2f %.2f | %s", static_cast<double>(at.x), static_cast<double>(at.y), tail);
		}

		std::string Place(const glm::vec2& p)
		{
			return "(" + std::to_string(static_cast<int>(std::lround(p.x))) + ", " + std::to_string(static_cast<int>(std::lround(p.y))) + ")";
		}

		std::string Num(float v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
			return b;
		}

		std::string Goods(const std::map<std::string, int>& goods)
		{
			std::string s;
			for (const auto& [item, n] : goods) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + item;
			return s.empty() ? std::string("nothing") : s;
		}

		// Deterministic 0..1 from the seed and a text (FNV-1a and a final mix).
		double Roll(uint32_t seed, const std::string& text)
		{
			uint64_t h = 1469598103934665603ull ^ (static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull);
			for (const char c : text) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
			h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;
			return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
		}

		std::pair<std::string, std::string> Names(const std::string& a, const std::string& b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

		Json Vec(const glm::vec2& v) { return Json::array({ v.x, v.y }); }
		glm::vec2 ReadVec(const Json& j) { return j.is_array() && j.size() == 2 ? glm::vec2(j[0].get<float>(), j[1].get<float>()) : glm::vec2(0.0f); }
		Json GoodsJson(const std::map<std::string, int>& g) { Json o = Json::object(); for (const auto& [k, n] : g) o[k] = n; return o; }
		std::map<std::string, int> ReadGoods(const Json& j)
		{
			std::map<std::string, int> out;
			if (j.is_object()) for (const auto& [k, n] : j.items()) if (n.is_number_integer()) out[k] = n.get<int>();
			return out;
		}
	}

	ClanWorld::~ClanWorld() { Clear(); }

	// ------------------------------------------------------------------ the clans
	Clan& ClanWorld::Add(const std::string& name, Tribe* external, ClanId id, const TribeConfig* config)
	{
		if (id >= 0)
			if (Clan* there = Find(id)) { if (!name.empty()) there->Name = name; return *there; }
		auto c = std::make_unique<Clan>();
		c->Id = id >= 0 ? id : m_NextId;
		m_NextId = std::max(m_NextId, c->Id + 1);
		c->Name = name.empty() ? "Clan " + std::to_string(c->Id + 1) : name;
		if (external) c->Folk = external;
		else
		{
			c->Own = std::make_unique<Tribe>();
			c->Folk = c->Own.get();
			if (config)
			{
				c->Folk->Config = *config;
				c->Folk->Config.FireName = config->FireName + " " + c->Name;   // its own campfire (the interactives are found by name)
			}
		}
		Clan& ref = *c;
		const auto at = std::find_if(m_Clans.begin(), m_Clans.end(), [&](const std::unique_ptr<Clan>& x) { return x->Id > ref.Id; });
		m_Clans.insert(at, std::move(c));
		Hook(ref);
		return ref;
	}

	void ClanWorld::Hook(Clan& c)
	{
		const ClanId id = c.Id;
		TribeOutside o;
		o.Kinds = { "envoy" };
		o.Options = [this, id](Tribe& t, TribeMember& m, std::vector<UtilityOption>& options) { EnvoyOptions(id, t, m, options); };
		o.Execute = [this, id](Tribe& t, TribeMember& m, const UtilityOption& option) { return EnvoyExecute(id, t, m, option); };
		o.Finish = [this, id](Tribe& t, TribeMember& m, const std::string&) { EnvoyFinish(id, t, m); };
		o.Gathered = [this, id](Tribe& t, const TribeMember& m, const std::string& node, int) { Gathered(id, t, m, node); };
		c.Folk->SetOutside(std::move(o));
		c.Folk->SetNameTaken([this](const std::string& n) { return NameTaken(n); });
		if (m_Telemetry) c.Folk->SetTelemetry(m_Telemetry, id, m_TelemetryEvery);
	}

	void ClanWorld::SetTelemetry(TelemetrySink* sink, double every)
	{
		m_Telemetry = sink;
		m_TelemetryEvery = every;
		for (auto& c : m_Clans)
			if (c->Folk) c->Folk->SetTelemetry(sink, c->Id, every);
	}

	void ClanWorld::Unhook(Clan& c)
	{
		if (!c.Folk) return;
		c.Folk->SetOutside(TribeOutside());
		c.Folk->SetNameTaken(nullptr);
		c.Folk->SetThinkBudget(-1);   // (a tribe the host keeps thinks by its own rule again)
		if (m_Telemetry && c.Folk->Telemetry() == m_Telemetry) c.Folk->SetTelemetry(nullptr);
	}

	bool ClanWorld::Remove(ClanId id)
	{
		const auto it = std::find_if(m_Clans.begin(), m_Clans.end(), [&](const std::unique_ptr<Clan>& c) { return c->Id == id; });
		if (it == m_Clans.end()) return false;
		Unhook(**it);
		for (auto r = m_Relations.begin(); r != m_Relations.end();) r = r->first.first == id || r->first.second == id ? m_Relations.erase(r) : std::next(r);
		for (auto s = m_Stances.begin(); s != m_Stances.end();) s = s->first.first == id || s->first.second == id ? m_Stances.erase(s) : std::next(s);
		for (auto b = m_Bonds.begin(); b != m_Bonds.end();) b = b->second.A == id || b->second.B == id ? m_Bonds.erase(b) : std::next(b);
		m_Meetings.erase(std::remove_if(m_Meetings.begin(), m_Meetings.end(), [&](const Meeting& m) { return m.A == id || m.B == id; }), m_Meetings.end());
		m_Territory.Forget(id);
		m_Clans.erase(it);
		return true;
	}

	void ClanWorld::Clear()
	{
		for (auto& c : m_Clans) Unhook(*c);
		m_Clans.clear();
		m_NextId = 0;
		m_Relations.clear();
		m_Bonds.clear();
		m_Greeted.clear();
		m_Stances.clear();
		m_TheftAt.clear();
		m_Territory.Clear();
		m_Meetings.clear();
		m_History.clear();
		m_NextMeeting = 1;
		m_Time = 0.0;
		m_EncounterTimer = m_TerritoryTimer = m_TradeTimer = m_WeddingTimer = 0.0f;
		m_DayTimer = 0.0;
		m_Days = 0;
		m_Log.clear();
	}

	Clan* ClanWorld::Find(ClanId id)
	{
		for (auto& c : m_Clans) if (c->Id == id) return c.get();
		return nullptr;
	}

	const Clan* ClanWorld::Find(ClanId id) const
	{
		for (const auto& c : m_Clans) if (c->Id == id) return c.get();
		return nullptr;
	}

	const Clan* ClanWorld::Find(const std::string& idOrName) const
	{
		for (const auto& c : m_Clans) if (c->Name == idOrName) return c.get();
		if (!idOrName.empty() && std::all_of(idOrName.begin(), idOrName.end(), [](char ch) { return ch >= '0' && ch <= '9'; })) return Find(std::atoi(idOrName.c_str()));
		return nullptr;
	}

	const Clan* ClanWorld::ClanOf(const std::string& member) const
	{
		for (const auto& c : m_Clans)
			for (const TribeMember& m : c->Folk->Members()) if (m.Name == member) return c.get();
		return nullptr;
	}

	bool ClanWorld::NameTaken(const std::string& name) const
	{
		for (const auto& c : m_Clans)
		{
			if (c->Folk->People().Find(name)) return true;
			for (const TribeMember& m : c->Folk->Members()) if (m.Name == name) return true;
		}
		return false;
	}

	std::string ClanWorld::ClanName(ClanId id) const
	{
		const Clan* c = Find(id);
		return c ? c->Name : "clan " + std::to_string(id);
	}

	int ClanWorld::Alive(const Clan& c) const
	{
		int n = 0;
		for (const TribeMember& m : c.Folk->Members()) n += !m.Dead && m.Body ? 1 : 0;
		return n;
	}

	TribeMember* ClanWorld::Member(Clan& c, const std::string& name)
	{
		for (TribeMember& m : c.Folk->Members()) if (m.Name == name) return &m;
		return nullptr;
	}

	float ClanWorld::DaySeconds() const
	{
		return m_Clans.empty() ? 1200.0f : std::max(1.0f, m_Clans.front()->Folk->Config.Needs.DaySeconds);
	}

	void ClanWorld::SetViewer(const glm::vec2& pos) { for (auto& c : m_Clans) c->Folk->SetViewer(pos); }
	void ClanWorld::SetWorldClock(double seconds) { for (auto& c : m_Clans) c->Folk->SetWorldClock(seconds); }

	bool ClanWorld::ChooseHome(float minApart, const std::function<bool(const glm::vec2&)>& usable, CampSite& out) const
	{
		std::vector<glm::vec2> camps;
		for (const auto& c : m_Clans) if (!c->Folk->Members().empty()) camps.push_back(c->Folk->Config.Camp);
		auto nearest = [&](const glm::vec2& p)
		{
			float d = 1e9f;
			for (const glm::vec2& c : camps) d = std::min(d, glm::distance(p, c));
			return d;
		};
		if (m_SiteChooser)   // the settlement work's chooser (H8), asked for a place a home radius from the camps' centre; kept when it is far enough from all
		{
			CampSiteRequest request;
			for (const glm::vec2& c : camps) request.From += c / static_cast<float>(camps.size());
			request.HomeRadius = minApart;
			CampSite site;
			if (m_SiteChooser(request, site) && nearest(site.At) >= minApart && (!usable || usable(site.At))) { out = site; return true; }
		}
		if (!m_Regions) return false;
		float best = -1e9f;
		bool found = false;
		for (const RegionSummary& g : m_Regions->Regions())
		{
			if (g.Water) continue;
			const float apart = nearest(g.Anchor);
			if (apart < minApart || (usable && !usable(g.Anchor))) continue;
			int nodes = 0;
			for (const auto& [kind, n] : g.Resources) nodes += n;
			const bool held = OwnerOf(g.Id) >= 0;
			const float score = static_cast<float>(std::min(nodes, 40)) / 40.0f + (g.WaterDistance < 60.0f ? 0.3f : 0.0f) - 0.5f * g.Danger - (held ? 0.5f : 0.0f)
				+ 0.001f * std::min(apart, 300.0f);
			if (score <= best) continue;
			best = score;
			found = true;
			out.At = g.Anchor;
			out.Score = score;
			out.Region = g.Id;
			out.Why = g.Name + ": " + std::to_string(nodes) + " wild nodes" + (g.WaterDistance < 60.0f ? ", water near" : "") + ", " + std::to_string(static_cast<int>(apart)) + " m from the nearest camp"
				+ (held ? ", but another clan holds it" : "");
		}
		return found;
	}

	// ------------------------------------------------------------------ the loop
	void ClanWorld::Update(float dt)
	{
		using Clock = std::chrono::steady_clock;
		const auto t0 = Clock::now();
		m_Cost = FrameCost();
		// H16: one budget of thinking turns for all the clans (see ThinkPerFrame). Each gets what it needs not to fall behind; the rest is shared by
		// members (largest remainder, ties by clan order). Stateless, so it is the same after a load.
		const size_t n = m_Clans.size();
		if (ThinkPerFrame > 0 && n > 1)
		{
			std::vector<int> share(n, 0);
			int needed = 0, members = 0;
			for (size_t i = 0; i < n; ++i)
			{
				const Tribe& t = *m_Clans[i]->Folk;
				share[i] = t.Members().empty() ? 0 : std::max(1, t.ThinksNeeded(dt) - 1);   // (the tribe's own rule adds one each; once is enough here)
				needed += share[i];
				members += static_cast<int>(t.Members().size());
			}
			int left = std::max(ThinkPerFrame, needed + 1) - needed;
			if (members > 0 && left > 0)
			{
				std::vector<std::pair<int, size_t>> remainders;   // (-remainder, clan) so the sort puts the largest first, then clan order
				int given = 0;
				for (size_t i = 0; i < n; ++i)
				{
					const int size = static_cast<int>(m_Clans[i]->Folk->Members().size());
					const int whole = left * size / members;
					share[i] += whole;
					given += whole;
					remainders.emplace_back(-(left * size % members), i);
				}
				std::sort(remainders.begin(), remainders.end());
				for (size_t k = 0; k < remainders.size() && given < left; ++k, ++given) ++share[remainders[k].second];
			}
			for (size_t i = 0; i < n; ++i) m_Clans[i]->Folk->SetThinkBudget(share[i]);
		}
		else for (auto& c : m_Clans) c->Folk->SetThinkBudget(-1);
		for (auto& c : m_Clans)
		{
			c->Folk->Update(dt);
			const Tribe::FrameCost& cost = c->Folk->LastCost();
			m_Cost.Thinks += cost.Thinks;
			m_Cost.Budget += cost.Budget;
			m_Cost.Due += cost.Due;
			m_Cost.ThinkMs += cost.ThinkMs;
		}
		const auto t1 = Clock::now();
		StepDiplomacy(dt);
		m_Cost.TribesMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
		m_Cost.DiplomacyMs = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
	}

	void ClanWorld::StepDiplomacy(float dt)
	{
		m_Time += static_cast<double>(dt);
		if (m_Clans.empty()) return;
		m_EncounterTimer += dt;
		if (m_EncounterTimer >= Rules.Encounters.Every) { m_EncounterTimer = 0.0f; Encounters(); }
		m_TerritoryTimer += dt;
		if (m_TerritoryTimer >= Rules.Territory.Every) { const float step = m_TerritoryTimer; m_TerritoryTimer = 0.0f; StepTerritory(step); }
		StepMeetings();
		m_TradeTimer += dt;
		if (m_TradeTimer >= Rules.Trade.Every) { m_TradeTimer = 0.0f; CheckPacts(); Trade(); CheckStances(); }
		m_WeddingTimer += dt;
		if (m_WeddingTimer >= Rules.Marriage.Every) { m_WeddingTimer = 0.0f; Weddings(); }
		m_DayTimer += static_cast<double>(dt);
		if (m_DayTimer >= static_cast<double>(DaySeconds())) { m_DayTimer -= static_cast<double>(DaySeconds()); Day(); }
	}

	void ClanWorld::Emit(const std::string& kind, ClanId a, ClanId b, const std::string& text)
	{
		char t[32];
		std::snprintf(t, sizeof t, "%7.1f ", m_Time);
		m_Log.push_back(t + text);
		if (m_Log.size() > kLogLines + kLogLines / 4) m_Log.erase(m_Log.begin(), m_Log.begin() + static_cast<std::ptrdiff_t>(m_Log.size() - kLogLines));
		DiplomacyEvent e{ kind, a, b, text, m_Time };
		for (const Listener& l : m_Listeners) l(e);
	}

	// ------------------------------------------------------------------ relations
	ClanRelation& ClanWorld::RelationRef(ClanId a, ClanId b) { return m_Relations[Key(a, b)]; }

	const ClanRelation* ClanWorld::Relation(ClanId a, ClanId b) const
	{
		const auto it = m_Relations.find(Key(a, b));
		return it == m_Relations.end() ? nullptr : &it->second;
	}

	float ClanWorld::MemberBond(const std::string& a, const std::string& b) const
	{
		const auto it = m_Bonds.find(Names(a, b));
		return it == m_Bonds.end() ? 0.0f : it->second.Value;
	}

	float ClanWorld::MemberBonds(ClanId a, ClanId b) const
	{
		float sum = 0.0f;
		int n = 0;
		for (const auto& [names, bond] : m_Bonds)
			if ((bond.A == a && bond.B == b) || (bond.A == b && bond.B == a)) { sum += bond.Value; ++n; }
		return n ? sum / static_cast<float>(n) : 0.0f;
	}

	float ClanWorld::Attitude(ClanId a, ClanId b) const
	{
		const ClanRelation* r = Relation(a, b);
		if (!r) return 0.0f;
		const float grievance = std::max(r->Grievance[0], r->Grievance[1]);
		return std::clamp(r->Base + Rules.Relations.MemberWeight * MemberBonds(a, b) - Rules.Relations.GrievanceWeight * grievance, -1.0f, 1.0f);
	}

	float ClanWorld::Trust(ClanId a, ClanId b) const
	{
		const ClanRelation* r = Relation(a, b);
		return r ? r->Trust : 0.0f;
	}

	float ClanWorld::Grievance(ClanId holder, ClanId against) const
	{
		const ClanRelation* r = Relation(holder, against);
		return r ? r->Grievance[holder < against ? 0 : 1] : 0.0f;
	}

	bool ClanWorld::HasPact(ClanId a, ClanId b, const std::string& kind) const
	{
		const ClanRelation* r = Relation(a, b);
		if (!r) return false;
		return std::any_of(r->Pacts.begin(), r->Pacts.end(), [&](const Pact& p) { return p.Kind == kind && p.Until > m_Time; });
	}

	std::string ClanWorld::Stance(ClanId a, ClanId b) const
	{
		const ClanRelation* r = Relation(a, b);
		if (!r || (r->Encounters == 0 && r->Pacts.empty() && r->Trades == 0)) return "unknown";
		if (HasPact(a, b, "alliance")) return "allied";
		if (HasPact(a, b, "truce")) return "truce";
		const float att = Attitude(a, b);
		if (att <= Rules.Relations.Hostile || std::max(r->Grievance[0], r->Grievance[1]) >= Rules.Relations.GrievanceHostile) return "hostile";
		if (att >= Rules.Relations.Friendly) return "friendly";
		return att < 0.0f ? "wary" : "neutral";
	}

	namespace
	{
		// 1 + sum of weight x (2 v - 1) over the clan's norms and the mean personality of its living adults (see ClanRelationRules::Openness).
		float CultureFactor(const Tribe& t, const std::map<std::string, float>& weights)
		{
			const SocietyRules& rules = t.Config.Society;
			std::map<std::string, float> sum;
			int n = 0;
			for (const TribeMember& m : t.Members())
			{
				if (m.Dead || !m.Body || t.People().IsChild(rules, m.Name)) continue;
				const Person* p = t.People().Find(m.Name);
				if (!p) continue;
				for (const auto& [trait, v] : p->Traits) sum[trait] += v;
				++n;
			}
			float f = 1.0f;
			for (const auto& [key, w] : weights)
			{
				float v;
				if (rules.Culture.Norms.count(key)) v = t.People().Norm(rules, key);
				else if (n > 0 && sum.count(key)) v = sum[key] / static_cast<float>(n);
				else continue;
				f += w * (2.0f * v - 1.0f);
			}
			return f;
		}
	}

	float ClanWorld::Openness(ClanId clan) const
	{
		const Clan* c = Find(clan);
		return c ? std::clamp(CultureFactor(*c->Folk, Rules.Relations.Openness), 0.2f, 2.0f) : 1.0f;
	}

	float ClanWorld::Aggression(ClanId clan) const
	{
		const Clan* c = Find(clan);
		return c ? std::clamp(CultureFactor(*c->Folk, Rules.Relations.Aggression), 0.0f, 2.0f) : 1.0f;
	}

	void ClanWorld::Relate(ClanId a, ClanId b, const std::string& event, float scale, const std::string& why, ClanId victim)
	{
		if (a == b || !Find(a) || !Find(b)) return;
		ClanRelation& r = RelationRef(a, b);
		const auto e = Rules.Relations.Events.find(event);
		float w = e != Rules.Relations.Events.end() ? e->second * scale : 0.0f;
		if (w > 0.0f) w *= 0.5f * (Openness(a) + Openness(b));   // open clans make more of a good turn
		r.Base = std::clamp(r.Base + w, -1.0f, 1.0f);
		r.Trust = std::clamp(r.Trust + (w > 0.0f ? Rules.Relations.TrustGain * w : Rules.Relations.TrustLoss * w), 0.0f, 1.0f);
		if (victim == a || victim == b)
			if (const auto g = Rules.Relations.Grievances.find(event); g != Rules.Relations.Grievances.end())
				r.Grievance[victim == std::min(a, b) ? 0 : 1] += g->second * scale;
		if (event == "trade") ++r.Trades;
		else if (event == "gift") ++r.Gifts;
		else if (event == "visit") ++r.Visits;
		else if (event == "teach") ++r.Lessons;
		else if (event == "theft") ++r.Thefts;
		else if (event == "marriage") ++r.Marriages;
		else if (event == "raid") ++r.Raids;
		if (why.empty()) return;
		char t[32];
		std::snprintf(t, sizeof t, "%.1f ", m_Time);
		r.Recent.push_back(t + event + ": " + why);
		if (r.Recent.size() > kRecent) r.Recent.erase(r.Recent.begin());
		Emit(event, a, b, ClanName(a) + " and " + ClanName(b) + ": " + why);
	}

	float ClanWorld::Hostility(ClanId a, ClanId b) const
	{
		if (a == b || HasPact(a, b, "alliance") || HasPact(a, b, "truce")) return 0.0f;
		return Aggression(a) * (std::max(0.0f, -Attitude(a, b)) + Grievance(a, b));
	}

	bool ClanWorld::MayRaid(ClanId a, ClanId b) const { return a != b && Stance(a, b) == "hostile"; }

	std::vector<std::pair<int, std::vector<ClanId>>> ClanWorld::Contested() const
	{
		std::vector<std::pair<int, std::vector<ClanId>>> out;
		for (auto& [region, clans] : m_Territory.Overlaps(Rules.Territory))
		{
			bool shared = true;
			for (size_t i = 0; i < clans.size() && shared; ++i)
				for (size_t j = i + 1; j < clans.size() && shared; ++j) shared = HasPact(clans[i], clans[j], "alliance");
			if (!shared) out.emplace_back(region, clans);
		}
		return out;
	}

	// ------------------------------------------------------------------ encounters
	void ClanWorld::Encounters()
	{
		if (m_Clans.size() < 2) return;
		struct Spot { Clan* C; TribeMember* M; glm::vec2 P; };
		std::vector<Spot> spots;
		for (auto& c : m_Clans)
			for (TribeMember& m : c->Folk->Members())
				if (!m.Dead && m.Body) spots.push_back({ c.get(), &m, c->Folk->Position(m) });
		const float radius = Rules.Encounters.Radius;
		auto cellOf = [&](const glm::vec2& p, int dx, int dz)
		{
			return (static_cast<long long>(static_cast<int>(std::floor(p.x / radius)) + dx) << 32) ^ static_cast<long long>(static_cast<unsigned>(static_cast<int>(std::floor(p.y / radius)) + dz));
		};
		std::unordered_map<long long, std::vector<size_t>> grid;
		for (size_t i = 0; i < spots.size(); ++i) grid[cellOf(spots[i].P, 0, 0)].push_back(i);
		std::vector<std::pair<size_t, size_t>> pairs;
		for (size_t i = 0; i < spots.size(); ++i)
			for (int dx = -1; dx <= 1; ++dx)
				for (int dz = -1; dz <= 1; ++dz)
				{
					const auto cell = grid.find(cellOf(spots[i].P, dx, dz));
					if (cell == grid.end()) continue;
					for (const size_t j : cell->second)
						if (j > i && spots[j].C != spots[i].C && glm::distance(spots[i].P, spots[j].P) <= radius) pairs.emplace_back(i, j);
				}
		std::sort(pairs.begin(), pairs.end());
		for (const auto& [i, j] : pairs) Greet(*spots[i].C, *spots[i].M, *spots[j].C, *spots[j].M);
		for (auto it = m_Greeted.begin(); it != m_Greeted.end();)   // the memory of greetings only needs the cooldown
			it = m_Time - it->second > static_cast<double>(Rules.Encounters.Cooldown) ? m_Greeted.erase(it) : std::next(it);
	}

	void ClanWorld::Greet(Clan& ca, TribeMember& ma, Clan& cb, TribeMember& mb)
	{
		const auto key = Names(ma.Name, mb.Name);
		if (const auto g = m_Greeted.find(key); g != m_Greeted.end() && m_Time - g->second < static_cast<double>(Rules.Encounters.Cooldown)) return;
		m_Greeted[key] = m_Time;
		ClanRelation& r = RelationRef(ca.Id, cb.Id);
		const bool first = r.Encounters == 0;
		++r.Encounters;
		if (first) r.FirstMet = m_Time;
		r.LastMet = m_Time;
		const float open = 0.5f * (Openness(ca.Id) + Openness(cb.Id));
		CrossBond& bond = m_Bonds[key];
		bond.A = key.first == ma.Name ? ca.Id : cb.Id;
		bond.B = key.first == ma.Name ? cb.Id : ca.Id;
		bond.Value = std::clamp(bond.Value + Rules.Relations.MemberBond * open, -1.0f, 1.0f);
		ca.Folk->Note(ma, "greets " + mb.Name + " of " + cb.Name);
		cb.Folk->Note(mb, "greets " + ma.Name + " of " + ca.Name);
		Relate(ca.Id, cb.Id, "meet");
		ShareBeliefs(ca.Id, cb.Id);   // social lane: shared beliefs, trust (ClansBeliefs.cpp)
		if (first)
		{
			Emit("contact", ca.Id, cb.Id, ca.Name + " and " + cb.Name + ": first contact: " + ma.Name + " of " + ca.Name + " meets " + mb.Name + " of " + cb.Name + " at " + Place(ca.Folk->Position(ma)));
			ca.Folk->NoteClan("meets the " + cb.Name + " for the first time");
			cb.Folk->NoteClan("meets the " + ca.Name + " for the first time");
		}
	}

	// ------------------------------------------------------------------ territory
	void ClanWorld::StepTerritory(float dt)
	{
		if (!m_Regions) return;
		std::vector<ClanFootprint> clans;
		for (const auto& c : m_Clans)
		{
			const int alive = Alive(*c);
			if (alive > 0) clans.push_back({ c->Id, c->Folk->Config.Camp, alive, c->Buildings });
		}
		m_Territory.Step(*m_Regions, clans, Rules.Territory, dt, DaySeconds());
		// overlapping claims of clans that are not allies cost them a little goodwill a day
		const float share = dt / DaySeconds();
		for (const auto& [region, owners] : Contested())
			for (size_t i = 0; i < owners.size(); ++i)
				for (size_t j = i + 1; j < owners.size(); ++j)
					if (!HasPact(owners[i], owners[j], "alliance")) Relate(owners[i], owners[j], "overlap", share);
	}

	void ClanWorld::Gathered(ClanId clan, Tribe& t, const TribeMember& m, const std::string& node)
	{
		if (!m_Regions) return;
		const int region = m_Regions->RegionAt(t.Position(m));
		const ClanId owner = region >= 0 ? OwnerOf(region) : -1;
		if (owner < 0 || owner == clan || HasPact(clan, owner, "alliance")) return;
		// taking from land another clan holds is theft: the owner keeps a grievance (told once a minute; every theft counts)
		double& last = m_TheftAt[Key(clan, owner)];
		const bool tell = m_Time - last >= 60.0 || last == 0.0;
		last = m_Time;
		const RegionSummary* g = m_Regions->Find(region);
		Relate(clan, owner, "theft", 1.0f, tell ? m.Name + " of " + ClanName(clan) + " takes from " + node + " in " + (g ? g->Name : std::string("their land")) + ", which the " + ClanName(owner) + " hold" : std::string(), owner);
	}

	// ------------------------------------------------------------------ trade, visits and parleys
	StoreView ClanWorld::View(const Clan& c) const
	{
		// What the clan holds is what its main store holds (the envoy packs from there; the stockpiles of H18 count as held, so they are not asked
		// for, but are not given away); what it wants is its stock target (the "tribe" section), never more than its main store takes (the cap, H18).
		StoreView v;
		const Tribe& t = *c.Folk;
		for (const ItemSlot& s : t.Store().Stacks()) v.Have[s.Id] += s.Count;
		const float cap = static_cast<float>(t.StoreCap());
		auto want = [&](const std::string& item)
		{
			const float held = static_cast<float>(t.Stored(item));
			const int main = v.Have.count(item) ? v.Have.at(item) : 0;
			// (the units in stockpiles cover part of the target: the need is what neither the store nor the stockpiles have)
			v.Want[item] = std::min(t.StockWanted(item), cap) - (held - static_cast<float>(main));
		};
		if (!Rules.Trade.Values.empty())
			for (const auto& [item, value] : Rules.Trade.Values) want(item);
		else
		{
			for (const auto& [item, target] : t.Config.Economy.Stock) want(item);
			for (const auto& [item, n] : std::map<std::string, int>(v.Have)) want(item);
		}
		return v;
	}

	bool ClanWorld::IsEnvoy(const std::string& member) const
	{
		for (const Meeting& m : m_Meetings)
			for (int s = 0; s < 2; ++s) if (m.Envoy[s] == member && m.Leg[s] < 4) return true;
		return false;
	}

	// Its most sociable free adult (not a child, not out on an expedition, not already an envoy), nearer the camp first; ties by the order of the members.
	std::string ClanWorld::ChooseEnvoy(Clan& c) const
	{
		const Tribe& t = *c.Folk;
		const Expedition& e = t.Exploration().Current;
		std::string best;
		float bestScore = -1e9f;
		for (const TribeMember& m : t.Members())
		{
			if (m.Dead || !m.Body || m.Player || t.People().IsChild(t.Config.Society, m.Name) || IsEnvoy(m.Name)) continue;   // (a player goes itself: PlayerVisit, H15)
			if (e.Active() && std::find(e.Party.begin(), e.Party.end(), m.Name) != e.Party.end()) continue;
			float sociable = 0.5f;
			if (const Person* p = t.People().Find(m.Name))
				if (const auto it = p->Traits.find("sociability"); it != p->Traits.end()) sociable = it->second;
			const float score = sociable + 0.5f / (1.0f + glm::distance(t.Position(m), t.Config.Camp) / 50.0f);
			if (score > bestScore) { bestScore = score; best = m.Name; }
		}
		return best;
	}

	bool ClanWorld::TechToShow(const Clan& from, const std::string& teacher, const Clan& to, const std::string& learner, std::string& tech) const
	{
		const AgentKnowledge* k = from.Folk->Knowledge().Of(teacher);
		if (!k || !to.Folk->Knowledge().Gated()) return false;
		for (const std::string& t : k->Known)
			if (to.Folk->Knowledge().CanLearn(learner, t)) { tech = t; return true; }
		return false;
	}

	std::string ClanWorld::PostMeeting(ClanId a, ClanId b, const std::string& purpose, const Barter& deal)
	{
		Clan* ca = Find(a);
		Clan* cb = Find(b);
		if (!ca || !cb) return "no such clan";
		Meeting m;
		m.Envoy[0] = ChooseEnvoy(*ca);
		m.Envoy[1] = ChooseEnvoy(*cb);
		if (m.Envoy[0].empty()) return "the " + ca->Name + " have nobody free to send";
		if (m.Envoy[1].empty()) return "the " + cb->Name + " have nobody free to send";
		m.Id = m_NextMeeting++;
		m.A = a;
		m.B = b;
		m.Purpose = purpose;
		m.Give[0] = deal.Give[0];
		m.Give[1] = deal.Give[1];
		m.Posted = m_Time;
		// between the camps, where both envoys can walk to (the middle first, then nearer one camp or the other)
		const glm::vec2 from = ca->Folk->Config.Camp, to = cb->Folk->Config.Camp;
		const TribeMember* ea = Member(*ca, m.Envoy[0]);
		const TribeMember* eb = Member(*cb, m.Envoy[1]);
		m.Point = (from + to) * 0.5f;
		for (const float f : { 0.5f, 0.4f, 0.6f, 0.3f, 0.7f, 0.2f, 0.8f })
		{
			const glm::vec2 p = from + (to - from) * f;
			if (ea->Body->CanReach(p) && eb->Body->CanReach(p)) { m.Point = p; break; }
		}
		RelationRef(a, b).LastMeeting = m_Time;
		std::string what = purpose;
		if (purpose == "trade" || purpose == "gift") what += " (" + ca->Name + " give " + Goods(m.Give[0]) + "; " + cb->Name + " give " + Goods(m.Give[1]) + ")";
		Emit("meeting", a, b, ca->Name + " and " + cb->Name + " send " + m.Envoy[0] + " and " + m.Envoy[1] + " to meet at " + Place(m.Point) + ": " + what);
		ca->Folk->NoteClan("sends " + m.Envoy[0] + " to meet the " + cb->Name + " at " + Place(m.Point) + ": " + what);
		cb->Folk->NoteClan("sends " + m.Envoy[1] + " to meet the " + ca->Name + " at " + Place(m.Point) + ": " + what);
		m_Meetings.push_back(m);
		return {};
	}

	void ClanWorld::Trade()
	{
		for (size_t i = 0; i < m_Clans.size(); ++i)
			for (size_t j = i + 1; j < m_Clans.size(); ++j)
			{
				Clan& ca = *m_Clans[i];
				Clan& cb = *m_Clans[j];
				const ClanId a = ca.Id, b = cb.Id;
				if (Alive(ca) == 0 || Alive(cb) == 0) continue;
				if (std::any_of(m_Meetings.begin(), m_Meetings.end(), [&](const Meeting& m) { return Key(m.A, m.B) == Key(a, b); })) continue;
				const ClanRelation* r = Relation(a, b);
				if (!r || r->Encounters == 0) continue;   // they have to have met
				if (m_Time - r->LastMeeting < static_cast<double>(Rules.Trade.Cooldown)) continue;
				const float att = Attitude(a, b);
				if (Stance(a, b) == "hostile")
				{
					// open clans that hold small grievances send envoys to parley
					if (std::max(r->Grievance[0], r->Grievance[1]) < Rules.Pacts.TruceGrievance && std::min(Openness(a), Openness(b)) >= Rules.Pacts.TruceOpenness)
						PostMeeting(a, b, "parley", Barter());
					continue;
				}
				if (att < Rules.Trade.MinAttitude) continue;
				const Barter deal = PlanBarter(Rules.Trade, View(ca), View(cb), att >= Rules.Trade.Gift);
				if (!deal.Empty()) { PostMeeting(a, b, deal.Give[0].empty() || deal.Give[1].empty() ? "gift" : "trade", deal); continue; }
				if (att < Rules.Exchange.MinAttitude || m_Time - r->LastMeeting < static_cast<double>(Rules.Exchange.VisitEvery)) continue;
				// nothing to trade, but something to teach: a visit
				auto gap = [](const Clan& x, const Clan& y)
				{
					for (const std::string& t : x.Folk->Knowledge().Clan()) if (!y.Folk->Knowledge().Clan().count(t)) return true;
					return false;
				};
				if (ca.Folk->Knowledge().Gated() && (gap(ca, cb) || gap(cb, ca))) PostMeeting(a, b, "visit", Barter());
			}
	}

	std::string ClanWorld::ProposeTrade(ClanId a, ClanId b, bool force)
	{
		Clan* ca = Find(a);
		Clan* cb = Find(b);
		if (!ca || !cb || a == b) return "two different clans are needed";
		if (Alive(*ca) == 0 || Alive(*cb) == 0) return "both clans need living members";
		if (std::any_of(m_Meetings.begin(), m_Meetings.end(), [&](const Meeting& m) { return Key(m.A, m.B) == Key(a, b); })) return "their envoys are already on their way to a meeting";
		const float att = Attitude(a, b);
		if (!force)
		{
			if (!Met(a, b)) return "they have never met";
			if (Stance(a, b) == "hostile") return "they are hostile";
			if (att < Rules.Trade.MinAttitude) return "they are not fond enough of each other (" + Num(att) + ")";
		}
		const Barter deal = PlanBarter(Rules.Trade, View(*ca), View(*cb), !force && att >= Rules.Trade.Gift);
		if (deal.Empty()) return "nothing to trade: neither has to spare what the other lacks";
		return PostMeeting(a, b, deal.Give[0].empty() || deal.Give[1].empty() ? "gift" : "trade", deal);
	}

	std::string ClanWorld::ProposeVisit(ClanId a, ClanId b, bool force)
	{
		Clan* ca = Find(a);
		Clan* cb = Find(b);
		if (!ca || !cb || a == b) return "two different clans are needed";
		if (Alive(*ca) == 0 || Alive(*cb) == 0) return "both clans need living members";
		if (std::any_of(m_Meetings.begin(), m_Meetings.end(), [&](const Meeting& m) { return Key(m.A, m.B) == Key(a, b); })) return "their envoys are already on their way to a meeting";
		if (!force && !Met(a, b)) return "they have never met";
		if (!force && Stance(a, b) == "hostile") return "they are hostile";
		return PostMeeting(a, b, "visit", Barter());
	}

	// ------------------------------------------------------------------ the envoys (the options of the members, through the tribe's hooks)
	void ClanWorld::EnvoyOptions(ClanId clan, Tribe& t, TribeMember& m, std::vector<UtilityOption>& options)
	{
		for (const Meeting& mt : m_Meetings)
		{
			const int side = mt.A == clan && mt.Envoy[0] == m.Name ? 0 : mt.B == clan && mt.Envoy[1] == m.Name ? 1 : -1;
			if (side < 0 || mt.Leg[side] >= 4) continue;
			const int leg = mt.Leg[side];
			const std::string other = ClanName(side == 0 ? mt.B : mt.A);
			UtilityOption o;
			o.Kind = "envoy";
			o.Target = other;
			o.Where = leg == 0 || leg == 3 ? t.CampSpotOf(m) : mt.Point;
			o.Score = Rules.Trade.Score;
			o.Commit = 20.0f;
			o.Factors.push_back({ "envoy", o.Score });
			o.Note = leg == 0 ? "takes up what the clan sends to the " + other
				: leg == 1 ? "walks to meet the envoy of the " + other + " at " + Place(mt.Point)
				: leg == 2 ? "waits for the envoy of the " + other
				: "brings home what it got from the " + other;
			options.push_back(o);
			return;
		}
	}

	bool ClanWorld::EnvoyExecute(ClanId clan, Tribe& t, TribeMember& m, const UtilityOption& o)
	{
		for (Meeting& mt : m_Meetings)
		{
			const int side = mt.A == clan && mt.Envoy[0] == m.Name ? 0 : mt.B == clan && mt.Envoy[1] == m.Name ? 1 : -1;
			if (side < 0 || mt.Leg[side] >= 4) continue;
			const int leg = mt.Leg[side];
			char go[96];
			if (leg == 1 && !m.Body->CanReach(mt.Point))
			{
				t.Note(m, "cannot find a way to the meeting point " + Place(mt.Point) + ": it goes home");
				mt.Leg[side] = 3;
				return false;
			}
			Go(go, sizeof go, o.Where, leg == 2 ? "wait 3" : "wait 1");
			return t.Send(m, go, "envoy", o.Target);
		}
		return false;
	}

	void ClanWorld::EnvoyFinish(ClanId clan, Tribe& t, TribeMember& m)
	{
		for (Meeting& mt : m_Meetings)
		{
			const int side = mt.A == clan && mt.Envoy[0] == m.Name ? 0 : mt.B == clan && mt.Envoy[1] == m.Name ? 1 : -1;
			if (side < 0 || mt.Leg[side] >= 4) continue;
			const glm::vec2 pos = t.Position(m);
			const std::string other = ClanName(side == 0 ? mt.B : mt.A);
			int& leg = mt.Leg[side];
			if (leg == 0)
			{
				if (glm::distance(pos, t.Config.Camp) > kAtCamp) return;   // (did not get there: it chooses again)
				// the goods leave the store in its pack (the meeting holds them, not its bag: it does not eat them or leave them in the store on the way)
				std::map<std::string, int> taken;
				for (const auto& [item, n] : mt.Give[side])
					if (const int k = t.Store().Remove(item, std::min(n, t.Store().Count(item))); k > 0) taken[item] = k;
				mt.Give[side] = taken;
				if (!taken.empty()) t.Note(m, "packs " + Goods(taken) + " for the " + other);
				leg = 1;
			}
			else if (leg == 1)
			{
				if (glm::distance(pos, mt.Point) > Rules.Trade.Meet) return;
				leg = 2;
				mt.Arrived[side] = m_Time;
				t.Note(m, "is at the meeting point " + Place(mt.Point) + " and waits for the envoy of the " + other);
			}
			else if (leg == 3)
			{
				if (glm::distance(pos, t.Config.Camp) > kAtCamp) return;
				// what it brings home: what it got (they met) or what it took (they did not)
				const std::map<std::string, int>& goods = mt.Met >= 0.0 ? mt.Give[1 - side] : mt.Give[side];
				const ItemLibrary* lib = t.Items();
				int put = 0;
				if (lib) for (const auto& [item, n] : goods) put += t.Store().Add(*lib, item, n);
				if (put > 0) t.Note(m, "puts " + Goods(goods) + " in the store" + (mt.Met >= 0.0 ? " (from the " + other + ")" : " again"));
				leg = 4;
			}
			return;
		}
	}

	void ClanWorld::StepMeetings()
	{
		for (size_t i = 0; i < m_Meetings.size();)
		{
			Meeting& mt = m_Meetings[i];
			Clan* clans[2] = { Find(mt.A), Find(mt.B) };
			for (int s = 0; s < 2; ++s)
			{
				const TribeMember* e = clans[s] ? Member(*clans[s], mt.Envoy[s]) : nullptr;
				if ((!e || e->Dead || !e->Body) && mt.Leg[s] < 4)
				{
					mt.Leg[s] = 4;   // gone (dead, moved away): what it carried is lost
					if (mt.Outcome.empty()) mt.Outcome = mt.Envoy[s] + " never came back";
				}
			}
			if (mt.Met < 0.0)
			{
				if (mt.Leg[0] == 2 && mt.Leg[1] == 2) Exchange(mt);
				else
				{
					for (int s = 0; s < 2; ++s)
						if (mt.Leg[s] == 2 && m_Time - mt.Arrived[s] > static_cast<double>(Rules.Trade.Wait))
						{
							mt.Leg[s] = 3;
							if (mt.Outcome.empty()) mt.Outcome = mt.Envoy[s] + " waited in vain";
							if (TribeMember* e = clans[s] ? Member(*clans[s], mt.Envoy[s]) : nullptr) clans[s]->Folk->Note(*e, "waited in vain for the envoy of the " + ClanName(s == 0 ? mt.B : mt.A) + ": it goes home");
						}
					if (m_Time - mt.Posted > 4.0 * static_cast<double>(Rules.Trade.Wait))   // nobody got there in time: everyone goes home
						for (int s = 0; s < 2; ++s) if (mt.Leg[s] < 3) { mt.Leg[s] = 3; if (mt.Outcome.empty()) mt.Outcome = "the envoys never met"; }
				}
			}
			if (mt.Leg[0] >= 4 && mt.Leg[1] >= 4) Close(i);
			else ++i;
		}
	}

	void ClanWorld::Exchange(Meeting& mt)
	{
		Clan* ca = Find(mt.A);
		Clan* cb = Find(mt.B);
		TribeMember* ea = ca ? Member(*ca, mt.Envoy[0]) : nullptr;
		TribeMember* eb = cb ? Member(*cb, mt.Envoy[1]) : nullptr;
		if (!ea || !eb) return;
		mt.Met = m_Time;
		mt.Leg[0] = mt.Leg[1] = 3;
		const ClanId a = mt.A, b = mt.B;
		const std::string who = mt.Envoy[0] + " of the " + ca->Name + " and " + mt.Envoy[1] + " of the " + cb->Name;
		std::string outcome;
		// the goods (and the two envoys like each other a bit more)
		const bool two = !mt.Give[0].empty() && !mt.Give[1].empty(), one = mt.Give[0].empty() != mt.Give[1].empty();
		if (two) outcome = "trade: " + Goods(mt.Give[0]) + " for " + Goods(mt.Give[1]);
		else if (one) outcome = "gift: " + (mt.Give[0].empty() ? Goods(mt.Give[1]) + " from the " + cb->Name : Goods(mt.Give[0]) + " from the " + ca->Name);
		else outcome = mt.Purpose;
		Relate(a, b, two ? "trade" : one ? "gift" : "visit", 1.0f, who + " meet: " + outcome);
		const auto key = Names(mt.Envoy[0], mt.Envoy[1]);
		CrossBond& bond = m_Bonds[key];
		bond.A = key.first == mt.Envoy[0] ? a : b;
		bond.B = key.first == mt.Envoy[0] ? b : a;
		bond.Value = std::clamp(bond.Value + 2.0f * Rules.Relations.MemberBond * 0.5f * (Openness(a) + Openness(b)), -1.0f, 1.0f);
		ca->Folk->Note(*ea, "meets " + mt.Envoy[1] + " of the " + cb->Name + (two || one ? " (" + outcome + ")" : ""));
		cb->Folk->Note(*eb, "meets " + mt.Envoy[0] + " of the " + ca->Name + (two || one ? " (" + outcome + ")" : ""));
		// what they know (H11): fond clans show each other a technology the other can learn
		if (Attitude(a, b) >= Rules.Exchange.MinAttitude && mt.Purpose != "parley")
		{
			Clan* side[2] = { ca, cb };
			TribeMember* envoy[2] = { ea, eb };
			for (int s = 0; s < 2; ++s)
			{
				std::string tech;
				if (!TechToShow(*side[s], envoy[s]->Name, *side[1 - s], envoy[1 - s]->Name, tech)) continue;
				const std::vector<KnowledgeEvent> events = side[1 - s]->Folk->Knowledge().Instruct(envoy[1 - s]->Name, tech, Rules.Exchange.Teach,
					"from " + envoy[s]->Name + " of the " + side[s]->Name);
				for (const KnowledgeEvent& e : events) side[1 - s]->Folk->Note(*envoy[1 - s], e.Text);
				if (!events.empty())
				{
					const std::string name = side[1 - s]->Folk->Knowledge().Name(tech);
					Relate(a, b, "teach", 1.0f, envoy[s]->Name + " shows " + envoy[1 - s]->Name + " how " + name + " is done");
					outcome += "; " + envoy[s]->Name + " shows " + name;
				}
			}
		}
		// pacts: a parley ends in a truce; trusting, fond clans agree (or renew) an alliance
		const double until = m_Time + static_cast<double>(Rules.Pacts.Days * DaySeconds());
		auto pact = [&](const std::string& kind)
		{
			ClanRelation& r = RelationRef(a, b);
			const bool had = HasPact(a, b, kind);
			r.Pacts.erase(std::remove_if(r.Pacts.begin(), r.Pacts.end(), [&](const Pact& p) { return p.Kind == kind; }), r.Pacts.end());
			r.Pacts.push_back({ kind, m_Time, until });
			if (!had) Relate(a, b, "pact", 1.0f, who + " agree " + (kind == "alliance" ? "an alliance" : "a truce") + " for " + Num(Rules.Pacts.Days) + " days");
			outcome += had ? "; they renew their " + kind : "; they agree " + std::string(kind == "alliance" ? "an alliance" : "a truce");
		};
		if (mt.Purpose == "parley") pact("truce");
		else if (Attitude(a, b) >= Rules.Pacts.AllianceAttitude && Trust(a, b) >= Rules.Pacts.AllianceTrust) pact("alliance");
		mt.Outcome = outcome;
	}

	void ClanWorld::Close(size_t index)
	{
		const Meeting& mt = m_Meetings[index];
		MeetingRecord r;
		r.Id = mt.Id;
		r.A = mt.A;
		r.B = mt.B;
		r.Purpose = mt.Purpose;
		r.Outcome = mt.Outcome.empty() ? std::string("nothing came of it") : mt.Outcome;
		r.Met = mt.Met >= 0.0;
		r.At = m_Time;
		if (!r.Met) Emit("missed", mt.A, mt.B, ClanName(mt.A) + " and " + ClanName(mt.B) + ": the " + mt.Purpose + " came to nothing (" + r.Outcome + ")");
		m_History.push_back(r);
		if (m_History.size() > kHistory) m_History.erase(m_History.begin());
		m_Meetings.erase(m_Meetings.begin() + static_cast<std::ptrdiff_t>(index));
	}

	// ------------------------------------------------------------------ marriages and moves
	std::string ClanWorld::MoveMember(const std::string& name, ClanId to, const std::string& why)
	{
		Clan* from = const_cast<Clan*>(ClanOf(name));
		Clan* dest = Find(to);
		if (!from) return "no member called '" + name + "'";
		if (!dest) return "no such clan";
		if (from == dest) return name + " is of the " + dest->Name + " already";
		if (IsEnvoy(name)) return name + " is an envoy on its way to a meeting";
		const Expedition& e = from->Folk->Exploration().Current;
		if (e.Active() && std::find(e.Party.begin(), e.Party.end(), name) != e.Party.end()) return name + " is away on an expedition";
		const TribeMember* m = Member(*from, name);
		if (!m || m->Dead || !m->Body) return name + " is not alive";
		MemberTransfer x;
		if (!from->Folk->Detach(name, x)) return "cannot take " + name + " from the " + from->Name;
		dest->Folk->Admit(std::move(x));
		// its bonds with the members of its new clan are its clan's now (they start again there); those with others keep its new clan
		for (auto it = m_Bonds.begin(); it != m_Bonds.end();)
		{
			if (it->first.first != name && it->first.second != name) { ++it; continue; }
			const bool mine = it->first.first == name;
			const std::string other = mine ? it->first.second : it->first.first;
			const Clan* oc = ClanOf(other);
			if (!oc || oc == dest) { it = m_Bonds.erase(it); continue; }
			(mine ? it->second.A : it->second.B) = dest->Id;
			++it;
		}
		Emit("move", from->Id, dest->Id, name + " leaves the " + from->Name + " for the " + dest->Name + (why.empty() ? "" : " (" + why + ")"));
		from->Folk->NoteClan("loses " + name + " to the " + dest->Name + (why.empty() ? "" : " (" + why + ")"));
		dest->Folk->NoteClan("welcomes " + name + " from the " + from->Name + (why.empty() ? "" : " (" + why + ")"));
		return {};
	}

	std::string ClanWorld::Wed(const std::string& a, const std::string& b)
	{
		const Clan* ca = ClanOf(a);
		const Clan* cb = ClanOf(b);
		if (!ca || !cb) return "both must be members of a clan";
		if (ca == cb) return "they are of the same clan (their own society pairs them)";
		for (const auto& [name, clan] : { std::make_pair(a, ca), std::make_pair(b, cb) })
		{
			const Person* p = clan->Folk->People().Find(name);
			if (!p || p->Dead) return name + " is not alive";
			if (!p->Partner.empty()) return name + " has a partner already";
		}
		// the one of the larger clan moves to the smaller (ties: the one of the higher id moves)
		const int na = Alive(*ca), nb = Alive(*cb);
		const bool aMoves = na > nb || (na == nb && ca->Id > cb->Id);
		const std::string mover = aMoves ? a : b, stays = aMoves ? b : a;
		const Clan* home = aMoves ? cb : ca;
		const Clan* left = aMoves ? ca : cb;
		const ClanId leftId = left->Id, homeId = home->Id;
		if (std::string why = MoveMember(mover, homeId, "marries " + stays); !why.empty()) return why;
		Clan* h = Find(homeId);
		h->Folk->People().Marry(h->Folk->Config.Society, mover, stays);
		Relate(leftId, homeId, "marriage", 1.0f, mover + " of the " + ClanName(leftId) + " weds " + stays + " of the " + ClanName(homeId) + " and moves to them");
		return {};
	}

	void ClanWorld::Weddings()
	{
		for (size_t i = 0; i < m_Clans.size(); ++i)
			for (size_t j = i + 1; j < m_Clans.size(); ++j)
			{
				const ClanId a = m_Clans[i]->Id, b = m_Clans[j]->Id;
				if (!Met(a, b) || Attitude(a, b) < Rules.Marriage.MinAttitude || Stance(a, b) == "hostile") continue;
				// the couple of the two clans who like each other best, of an age to marry (each clan's own rules) and free
				std::string best[2];
				float bestBond = -1e9f;
				for (const auto& [names, bond] : m_Bonds)
				{
					if (!((bond.A == a && bond.B == b) || (bond.A == b && bond.B == a)) || bond.Value < Rules.Marriage.MinBond || bond.Value <= bestBond) continue;
					bool ok = true;
					const Person* p[2] = { nullptr, nullptr };
					float gap = 0.0f;
					for (int k = 0; k < 2 && ok; ++k)
					{
						const std::string& n = k == 0 ? names.first : names.second;
						Clan* c = const_cast<Clan*>(ClanOf(n));
						const TribeMember* m = c ? Member(*c, n) : nullptr;
						p[k] = c ? c->Folk->People().Find(n) : nullptr;
						ok = c && m && !m->Dead && m->Body && !m->Player && p[k] && !p[k]->Dead && p[k]->Partner.empty() && p[k]->Age >= c->Folk->Config.Society.Births.PairAge && !IsEnvoy(n);
						if (ok) gap = c->Folk->Config.Society.Births.AgeGap;
					}
					if (!ok || p[0]->Female == p[1]->Female || std::fabs(p[0]->Age - p[1]->Age) > gap) continue;
					bestBond = bond.Value;
					best[0] = names.first;
					best[1] = names.second;
				}
				if (best[0].empty()) continue;
				if (Roll(Rules.Seed, best[0] + "|" + best[1] + "|" + std::to_string(static_cast<long long>(m_Time))) >= static_cast<double>(Rules.Marriage.Chance)) continue;
				Wed(best[0], best[1]);
				return;   // one wedding a look: the clans changed
			}
	}

	// ------------------------------------------------------------------ the days, pacts and stances
	void ClanWorld::Day()
	{
		++m_Days;
		for (auto& [key, r] : m_Relations)
		{
			r.Base *= 1.0f - Rules.Relations.DecayPerDay;
			for (float& g : r.Grievance) g = std::max(0.0f, g - Rules.Relations.GrievanceDecay);
		}
		for (auto it = m_Bonds.begin(); it != m_Bonds.end();)
		{
			it->second.Value *= 1.0f - Rules.Relations.DecayPerDay;
			it = std::fabs(it->second.Value) < 0.01f ? m_Bonds.erase(it) : std::next(it);
		}
	}

	void ClanWorld::CheckPacts()
	{
		for (auto& [key, r] : m_Relations)
		{
			const ClanId a = key.first, b = key.second;
			for (size_t i = 0; i < r.Pacts.size();)
			{
				const Pact& p = r.Pacts[i];
				const bool broken = p.Kind == "alliance" && Attitude(a, b) < Rules.Pacts.BreakBelow;
				if (p.Until > m_Time && !broken) { ++i; continue; }
				const std::string text = ClanName(a) + " and " + ClanName(b) + ": " + (broken ? "the " + p.Kind + " breaks (attitude " + Num(Attitude(a, b)) + ")" : "the " + p.Kind + " ends");
				const std::string kind = broken ? "break" : "expire";
				r.Pacts.erase(r.Pacts.begin() + static_cast<std::ptrdiff_t>(i));
				Emit(kind, a, b, text);
			}
		}
	}

	void ClanWorld::CheckStances()
	{
		for (const auto& [key, r] : m_Relations)
		{
			if (r.Encounters == 0 && r.Pacts.empty()) continue;
			const std::string s = Stance(key.first, key.second);
			std::string& was = m_Stances[key];
			if (s == was) continue;
			if (!was.empty()) Emit("stance", key.first, key.second, ClanName(key.first) + " and " + ClanName(key.second) + " are " + s + " now (were " + was + ")");
			was = s;
		}
	}

	// ------------------------------------------------------------------ invariants (H20)
	std::vector<std::pair<std::string, std::string>> ClanWorld::CheckMembers() const
	{
		std::vector<std::pair<std::string, std::string>> out;
		std::map<std::string, std::vector<std::string>> where;   // member -> the clans that list it
		std::set<ClanId> ids;
		for (const auto& c : m_Clans)
		{
			if (!ids.insert(c->Id).second) out.emplace_back(c->Name, "two clans have the id " + std::to_string(c->Id));
			std::set<std::string> here;
			for (const TribeMember& m : c->Folk->Members()) { where[m.Name].push_back(c->Name); here.insert(m.Name); }
			for (const auto& [name, p] : c->Folk->People().People())
				if (!here.count(name)) where[name].push_back(c->Name + " (its society only)");
		}
		for (const auto& [name, clans] : where)
			if (clans.size() != 1)
			{
				std::string list;
				for (const std::string& c : clans) list += (list.empty() ? "" : ", ") + c;
				out.emplace_back(name, "is in " + std::to_string(clans.size()) + " clans: " + list);
			}
		for (const auto& [key, r] : m_Relations)
			if (!Find(key.first) || !Find(key.second)) out.emplace_back("relation " + std::to_string(key.first) + "-" + std::to_string(key.second), "names a clan that does not exist");
		for (const Meeting& m : m_Meetings)
			for (int s = 0; s < 2; ++s)
			{
				const Clan* c = Find(s == 0 ? m.A : m.B);
				const Clan* of = ClanOf(m.Envoy[s]);
				if (m.Leg[s] < 4 && (!c || (of && of != c))) out.emplace_back(m.Envoy[s], "is the envoy of " + ClanName(s == 0 ? m.A : m.B) + " to meeting #" + std::to_string(m.Id) + " but not of that clan");
			}
		return out;
	}

	std::vector<std::pair<std::string, std::string>> ClanWorld::CheckTerritory() const
	{
		std::vector<std::pair<std::string, std::string>> out;
		if (!m_Regions) return out;
		for (const auto& [clan, claims] : m_Territory.Claims())
		{
			if (!Find(clan)) { out.emplace_back("clan " + std::to_string(clan), "claims land but does not exist"); continue; }
			for (const auto& [region, s] : claims)
			{
				const RegionSummary* g = m_Regions->Find(region);
				if (!g) out.emplace_back(ClanName(clan), "claims region " + std::to_string(region) + ", which does not exist");
				else if (g->Water) out.emplace_back(ClanName(clan), "claims the water of " + g->Name);
				if (!std::isfinite(s) || s < 0.0f) out.emplace_back(ClanName(clan), "has a claim of " + std::to_string(s) + " on region " + std::to_string(region));
			}
		}
		std::vector<ClanFootprint> clans;
		for (const auto& c : m_Clans)
			if (const int alive = Alive(*c); alive > 0) clans.push_back({ c->Id, c->Folk->Config.Camp, alive, c->Buildings });
		TerritoryMap one = m_Territory, two = m_Territory;
		one.Step(*m_Regions, clans, Rules.Territory, Rules.Territory.Every, DaySeconds());
		two.Step(*m_Regions, clans, Rules.Territory, Rules.Territory.Every, DaySeconds());
		if (!(one == two)) out.emplace_back("territory", "the same claims stepped twice with the same clans differ");
		return out;
	}

	// ------------------------------------------------------------------ for people
	std::string ClanWorld::Report() const
	{
		std::string s;
		for (const auto& c : m_Clans)
		{
			const Tribe& t = *c->Folk;
			int stored = 0;
			for (const ItemSlot& slot : t.Store().Stacks()) stored += slot.Count;
			s += std::to_string(c->Id) + " " + c->Name + ": " + std::to_string(Alive(*c)) + " alive of " + std::to_string(t.Members().size()) + ", camp " + Place(t.Config.Camp)
				+ ", " + std::to_string(stored) + " things in store";
			if (m_Regions) s += ", holds " + std::to_string(m_Territory.Held(c->Id, Rules.Territory).size()) + " regions";
			s += ", openness " + Num(Openness(c->Id)) + ", aggression " + Num(Aggression(c->Id));
			for (const auto& o : m_Clans) if (o->Id != c->Id) s += "; " + Stance(c->Id, o->Id) + " to the " + o->Name;
			s += "\n";
		}
		return s.empty() ? std::string("no clans\n") : s;
	}

	std::string ClanWorld::RelationsReport() const
	{
		std::string s;
		for (size_t i = 0; i < m_Clans.size(); ++i)
			for (size_t j = i + 1; j < m_Clans.size(); ++j)
			{
				const ClanId a = m_Clans[i]->Id, b = m_Clans[j]->Id;
				s += m_Clans[i]->Name + " - " + m_Clans[j]->Name + ": " + Stance(a, b);
				if (const ClanRelation* r = Relation(a, b))
				{
					s += ", attitude " + Num(Attitude(a, b)) + " (base " + Num(r->Base) + ", members " + Num(MemberBonds(a, b)) + "), trust " + Num(r->Trust)
						+ ", grievances " + Num(r->Grievance[0]) + "/" + Num(r->Grievance[1]) + ", met " + std::to_string(r->Encounters) + " times, " + std::to_string(r->Trades) + " trades, "
						+ std::to_string(r->Gifts) + " gifts, " + std::to_string(r->Visits) + " visits, " + std::to_string(r->Lessons) + " lessons, " + std::to_string(r->Thefts) + " thefts, "
						+ std::to_string(r->Marriages) + " marriages, hostility " + Num(Hostility(a, b)) + "/" + Num(Hostility(b, a));
					for (const Pact& p : r->Pacts) s += ", " + p.Kind + " until " + Num(static_cast<float>(p.Until));
					for (const std::string& line : r->Recent) s += "\n    " + line;
				}
				s += "\n";
			}
		return s.empty() ? std::string("one clan or none: no relations\n") : s;
	}

	std::string ClanWorld::TerritoryReport() const
	{
		if (!m_Regions) return "no regions (a generated world): no territory\n";
		std::string s;
		for (const auto& c : m_Clans)
		{
			s += c->Name + " holds";
			const std::vector<int> held = m_Territory.Held(c->Id, Rules.Territory);
			for (const int r : held) if (const RegionSummary* g = m_Regions->Find(r)) s += " " + g->Name + " (" + Num(m_Territory.Claim(c->Id, r)) + ")";
			if (held.empty()) s += " nothing yet";
			s += "\n";
		}
		for (const auto& [region, clans] : Contested())
		{
			const RegionSummary* g = m_Regions->Find(region);
			s += "contested: " + (g ? g->Name : std::to_string(region)) + " by";
			for (const ClanId c : clans) s += " " + ClanName(c) + " (" + Num(m_Territory.Claim(c, region)) + ")";
			s += "\n";
		}
		return s;
	}

	std::string ClanWorld::MeetingsReport() const
	{
		std::string s;
		for (const Meeting& m : m_Meetings)
			s += "#" + std::to_string(m.Id) + " " + m.Purpose + " " + ClanName(m.A) + " (" + m.Envoy[0] + ", leg " + std::to_string(m.Leg[0]) + ", gives " + Goods(m.Give[0]) + ") and "
				+ ClanName(m.B) + " (" + m.Envoy[1] + ", leg " + std::to_string(m.Leg[1]) + ", gives " + Goods(m.Give[1]) + ") at " + Place(m.Point) + "\n";
		for (const MeetingRecord& r : m_History)
			s += "  done #" + std::to_string(r.Id) + " " + r.Purpose + " " + ClanName(r.A) + " - " + ClanName(r.B) + ": " + r.Outcome + "\n";
		return s.empty() ? std::string("no meetings yet\n") : s;
	}

	// ------------------------------------------------------------------ saving
	std::string ClanWorld::ToJson() const
	{
		Json j;
		j["time"] = m_Time;
		j["timers"] = Json::array({ m_EncounterTimer, m_TerritoryTimer, m_TradeTimer, m_WeddingTimer });
		j["day"] = m_DayTimer;
		j["days"] = m_Days;
		j["nextClan"] = m_NextId;
		j["nextMeeting"] = m_NextMeeting;
		Json clans = Json::array();
		for (const auto& c : m_Clans)
		{
			Json o{ { "id", c->Id }, { "name", c->Name }, { "buildings", c->Buildings }, { "kept", c->Kept() } };
			if (c->Kept())
			{
				o["tribe"] = Json::parse(c->Folk->ToJson());
				o["knowledge"] = Json::parse(c->Folk->Knowledge().ToJson());
				Json known = Json::object();
				for (const TribeMember& m : c->Folk->Members())
				{
					Json names = Json::array();
					for (const std::string& n : c->Folk->Knowledge().Names(m.Name)) names.push_back(n);
					known[m.Name] = names;
				}
				o["known"] = known;
			}
			clans.push_back(o);
		}
		j["clans"] = clans;
		Json relations = Json::array();
		for (const auto& [key, r] : m_Relations)
		{
			Json pacts = Json::array();
			for (const Pact& p : r.Pacts) pacts.push_back(Json{ { "kind", p.Kind }, { "since", p.Since }, { "until", p.Until } });
			relations.push_back(Json{ { "a", key.first }, { "b", key.second }, { "base", r.Base }, { "trust", r.Trust }, { "grievance", Json::array({ r.Grievance[0], r.Grievance[1] }) },
				{ "counts", Json::array({ r.Encounters, r.Trades, r.Gifts, r.Visits, r.Lessons, r.Thefts, r.Marriages, r.Raids }) }, { "firstMet", r.FirstMet }, { "lastMet", r.LastMet },
				{ "lastMeeting", r.LastMeeting }, { "pacts", pacts }, { "recent", r.Recent } });
		}
		j["relations"] = relations;
		Json bonds = Json::array();
		for (const auto& [names, b] : m_Bonds) bonds.push_back(Json::array({ names.first, names.second, b.A, b.B, b.Value }));
		j["bonds"] = bonds;
		Json greeted = Json::array();
		for (const auto& [names, t] : m_Greeted) greeted.push_back(Json::array({ names.first, names.second, t }));
		j["greeted"] = greeted;
		Json stances = Json::array();
		for (const auto& [key, s] : m_Stances) stances.push_back(Json::array({ key.first, key.second, s }));
		j["stances"] = stances;
		Json thefts = Json::array();
		for (const auto& [key, t] : m_TheftAt) thefts.push_back(Json::array({ key.first, key.second, t }));
		j["thefts"] = thefts;
		j["territory"] = Json::parse(m_Territory.ToJson());
		Json meetings = Json::array();
		for (const Meeting& m : m_Meetings)
			meetings.push_back(Json{ { "id", m.Id }, { "a", m.A }, { "b", m.B }, { "purpose", m.Purpose }, { "point", Vec(m.Point) }, { "envoys", Json::array({ m.Envoy[0], m.Envoy[1] }) },
				{ "give", Json::array({ GoodsJson(m.Give[0]), GoodsJson(m.Give[1]) }) }, { "legs", Json::array({ m.Leg[0], m.Leg[1] }) }, { "posted", m.Posted },
				{ "arrived", Json::array({ m.Arrived[0], m.Arrived[1] }) }, { "met", m.Met }, { "outcome", m.Outcome } });
		j["meetings"] = meetings;
		Json history = Json::array();
		for (const MeetingRecord& r : m_History)
			history.push_back(Json{ { "id", r.Id }, { "a", r.A }, { "b", r.B }, { "purpose", r.Purpose }, { "outcome", r.Outcome }, { "met", r.Met }, { "at", r.At } });
		j["history"] = history;
		Json log = Json::array();
		for (size_t i = m_Log.size() > 100 ? m_Log.size() - 100 : 0; i < m_Log.size(); ++i) log.push_back(m_Log[i]);
		j["log"] = log;
		return j.dump();
	}

	std::vector<ClanWorld::SavedClan> ClanWorld::SavedClans(const std::string& json)
	{
		std::vector<SavedClan> out;
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("clans") || !j["clans"].is_array()) return out;
		for (const Json& c : j["clans"])
		{
			if (!c.is_object()) continue;
			SavedClan s;
			s.Id = c.value("id", -1);
			s.Name = c.value("name", std::string());
			s.Kept = c.value("kept", false);
			if (s.Kept && c.contains("tribe")) s.Members = Tribe::SavedMembers(c["tribe"].dump());
			if (s.Id >= 0) out.push_back(std::move(s));
		}
		return out;
	}

	bool ClanWorld::FromJson(const std::string& json, bool reissue)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("clans") || !j["clans"].is_array()) return false;
		TerritoryMap territory;
		if (j.contains("territory") && !territory.FromJson(j["territory"].dump())) return false;
		bool ok = true;
		// the tribes kept here first (their members were made by the host from SavedClans)
		for (const Json& o : j["clans"])
		{
			if (!o.is_object()) return false;
			Clan* c = Find(o.value("id", -1));
			if (!c) continue;
			c->Name = o.value("name", c->Name);
			c->Buildings = o.value("buildings", 0.0f);
			if (!c->Kept() || !o.contains("tribe")) continue;
			if (o.contains("known") && o["known"].is_object())
				for (const auto& [member, names] : o["known"].items())
					if (names.is_array()) c->Folk->Knowledge().SetNames(member, names.get<std::vector<std::string>>());
			if (o.contains("knowledge") && !c->Folk->Knowledge().FromJson(o["knowledge"].dump())) ok = false;
			if (!c->Folk->FromJson(o["tribe"].dump(), reissue)) ok = false;
		}
		m_Time = j.value("time", 0.0);
		if (j.contains("timers") && j["timers"].is_array() && j["timers"].size() == 4)
		{
			m_EncounterTimer = j["timers"][0].get<float>();
			m_TerritoryTimer = j["timers"][1].get<float>();
			m_TradeTimer = j["timers"][2].get<float>();
			m_WeddingTimer = j["timers"][3].get<float>();
		}
		m_DayTimer = j.value("day", 0.0);
		m_Days = j.value("days", 0);
		m_NextId = std::max(m_NextId, j.value("nextClan", 0));
		m_NextMeeting = j.value("nextMeeting", 1);
		m_Relations.clear();
		if (j.contains("relations") && j["relations"].is_array())
			for (const Json& o : j["relations"])
			{
				ClanRelation r;
				r.Base = o.value("base", 0.0f);
				r.Trust = o.value("trust", 0.0f);
				if (o.contains("grievance") && o["grievance"].size() == 2) { r.Grievance[0] = o["grievance"][0].get<float>(); r.Grievance[1] = o["grievance"][1].get<float>(); }
				if (o.contains("counts") && o["counts"].size() == 8)
				{
					int* counts[8] = { &r.Encounters, &r.Trades, &r.Gifts, &r.Visits, &r.Lessons, &r.Thefts, &r.Marriages, &r.Raids };
					for (size_t k = 0; k < 8; ++k) *counts[k] = o["counts"][k].get<int>();
				}
				r.FirstMet = o.value("firstMet", -1.0);
				r.LastMet = o.value("lastMet", -1.0);
				r.LastMeeting = o.value("lastMeeting", -1e9);
				if (o.contains("pacts"))
					for (const Json& p : o["pacts"]) r.Pacts.push_back({ p.value("kind", std::string()), p.value("since", 0.0), p.value("until", 0.0) });
				if (o.contains("recent")) r.Recent = o["recent"].get<std::vector<std::string>>();
				m_Relations[Key(o.value("a", -1), o.value("b", -1))] = r;
			}
		m_Bonds.clear();
		if (j.contains("bonds"))
			for (const Json& b : j["bonds"])
				if (b.is_array() && b.size() == 5) m_Bonds[{ b[0].get<std::string>(), b[1].get<std::string>() }] = { b[2].get<int>(), b[3].get<int>(), b[4].get<float>() };
		m_Greeted.clear();
		if (j.contains("greeted"))
			for (const Json& g : j["greeted"]) if (g.is_array() && g.size() == 3) m_Greeted[{ g[0].get<std::string>(), g[1].get<std::string>() }] = g[2].get<double>();
		m_Stances.clear();
		if (j.contains("stances"))
			for (const Json& s : j["stances"]) if (s.is_array() && s.size() == 3) m_Stances[Key(s[0].get<int>(), s[1].get<int>())] = s[2].get<std::string>();
		m_TheftAt.clear();
		if (j.contains("thefts"))
			for (const Json& t : j["thefts"]) if (t.is_array() && t.size() == 3) m_TheftAt[Key(t[0].get<int>(), t[1].get<int>())] = t[2].get<double>();
		m_Territory = std::move(territory);
		m_Meetings.clear();
		if (j.contains("meetings"))
			for (const Json& o : j["meetings"])
			{
				Meeting m;
				m.Id = o.value("id", 0);
				m.A = o.value("a", -1);
				m.B = o.value("b", -1);
				m.Purpose = o.value("purpose", std::string());
				m.Point = ReadVec(o.value("point", Json::array()));
				for (int s = 0; s < 2; ++s)
				{
					m.Envoy[s] = o["envoys"][static_cast<size_t>(s)].get<std::string>();
					m.Give[s] = ReadGoods(o["give"][static_cast<size_t>(s)]);
					m.Leg[s] = o["legs"][static_cast<size_t>(s)].get<int>();
					m.Arrived[s] = o["arrived"][static_cast<size_t>(s)].get<double>();
				}
				m.Posted = o.value("posted", 0.0);
				m.Met = o.value("met", -1.0);
				m.Outcome = o.value("outcome", std::string());
				m_Meetings.push_back(m);
			}
		m_History.clear();
		if (j.contains("history"))
			for (const Json& o : j["history"])
				m_History.push_back({ o.value("id", 0), o.value("a", -1), o.value("b", -1), o.value("purpose", std::string()), o.value("outcome", std::string()), o.value("met", false), o.value("at", 0.0) });
		m_Log.clear();
		if (j.contains("log")) for (const Json& l : j["log"]) if (l.is_string()) m_Log.push_back(l.get<std::string>());
		return ok;
	}
}
