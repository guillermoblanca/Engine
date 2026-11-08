// T5c: trails and built paths (see Trails.h). The grid and its rules, then the tribe's part: reading the members' walks, the fading, marking a
// stretch to pave and the "pave" option (carried out and finished here; the hooks in Tribe.cpp are one line each).
#include "GB/Game/Trails.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }

		// The fields of an object, each read only if present; the first mistake is kept.
		struct Reader
		{
			const Json& O;
			std::string Where;
			std::string Error;
			void Num(const char* key, float& v, float lo, float hi)
			{
				if (!Error.empty() || !O.contains(key)) return;
				const Json& j = O[key];
				if (!j.is_number()) { Error = Where + key + ": expected a number"; return; }
				const float f = j.get<float>();
				if (!(f >= lo && f <= hi)) { Error = Where + key + ": out of range"; return; }
				v = f;
			}
			void Int(const char* key, int& v, int lo, int hi)
			{
				if (!Error.empty() || !O.contains(key)) return;
				const Json& j = O[key];
				if (!j.is_number_integer()) { Error = Where + key + ": expected an integer"; return; }
				const int n = j.get<int>();
				if (n < lo || n > hi) { Error = Where + key + ": out of range"; return; }
				v = n;
			}
			void Bool(const char* key, bool& v)
			{
				if (!Error.empty() || !O.contains(key)) return;
				const Json& j = O[key];
				if (!j.is_boolean()) { Error = Where + key + ": expected true or false"; return; }
				v = j.get<bool>();
			}
			void Colour(const char* key, glm::vec3& v)
			{
				if (!Error.empty() || !O.contains(key)) return;
				const Json& j = O[key];
				if (!j.is_array() || j.size() != 3) { Error = Where + key + ": expected [r, g, b]"; return; }
				for (int k = 0; k < 3; ++k)
				{
					if (!j[static_cast<size_t>(k)].is_number()) { Error = Where + key + ": expected [r, g, b]"; return; }
					v[k] = std::clamp(j[static_cast<size_t>(k)].get<float>(), 0.0f, 2.0f);
				}
			}
			void Only(std::initializer_list<const char*> keys)
			{
				if (!Error.empty()) return;
				for (const auto& [key, v] : O.items())
				{
					(void)v;
					if (!key.empty() && key[0] == '_') continue;   // "_doc": a note for people
					bool known = false;
					for (const char* k : keys) known = known || key == k;
					if (!known) { Error = Where + "unknown key " + key; return; }
				}
			}
		};

		Json Triple(float a, float b, float c) { return Json::array({ a, b, c }); }
	}

	TrailRules DefaultTrailRules() { return TrailRules(); }

	std::string LoadTrailRules(const std::string& json, TrailRules& live)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("trails");
		if (section == doc.end()) return {};
		if (!section->is_object()) return "trails: expected an object";
		TrailRules r = live;
		Reader top{ *section, "trails: " };
		top.Only({ "enabled", "cellSize", "extent", "sampleSeconds", "wearPerPass", "maxWear", "decayPerDay", "decaySeconds", "trailAt", "wornAt", "maxJump", "speed", "navEase", "pave", "visual" });
		top.Bool("enabled", r.Enabled);
		top.Num("cellSize", r.CellSize, 0.5f, 16.0f);
		top.Num("extent", r.Extent, 16.0f, 2000.0f);
		top.Num("sampleSeconds", r.SampleSeconds, 0.05f, 30.0f);
		top.Num("wearPerPass", r.WearPerPass, 0.1f, 100.0f);
		top.Num("maxWear", r.MaxWear, 1.0f, 6000.0f);
		top.Num("decayPerDay", r.DecayPerDay, 0.0f, 6000.0f);
		top.Num("decaySeconds", r.DecaySeconds, 1.0f, 3600.0f);
		top.Num("trailAt", r.TrailAt, 0.1f, 6000.0f);
		top.Num("wornAt", r.WornAt, 0.1f, 6000.0f);
		top.Num("maxJump", r.MaxJump, 1.0f, 1000.0f);
		if (!top.Error.empty()) return top.Error;
		auto tiers = [&](const char* key, float& trail, float& worn, float& paved, float lo, float hi) -> std::string
		{
			if (!section->contains(key)) return {};
			const Json& o = (*section)[key];
			if (!o.is_object()) return std::string("trails: ") + key + ": expected an object";
			Reader t{ o, std::string("trails: ") + key + ": " };
			t.Only({ "trail", "worn", "paved" });
			t.Num("trail", trail, lo, hi);
			t.Num("worn", worn, lo, hi);
			t.Num("paved", paved, lo, hi);
			return t.Error;
		};
		if (std::string e = tiers("speed", r.TrailSpeed, r.WornSpeed, r.PavedSpeed, 0.5f, 3.0f); !e.empty()) return e;
		if (std::string e = tiers("navEase", r.TrailEase, r.WornEase, r.PavedEase, 0.0f, 1.0f); !e.empty()) return e;
		if (r.WornAt < r.TrailAt) return "trails: wornAt must not be below trailAt";
		if (r.MaxWear < r.WornAt) return "trails: maxWear must not be below wornAt";
		if (const auto p = section->find("pave"); p != section->end())
		{
			if (!p->is_object()) return "trails: pave: expected an object";
			TrailPaveRules& pr = r.Pave;
			Reader t{ *p, "trails: pave: " };
			t.Only({ "enabled", "planSeconds", "paveAt", "extendAt", "minCells", "maxCells", "cellsPerTrip", "workSeconds", "reserve", "timeoutSeconds", "score", "materials" });
			t.Bool("enabled", pr.Enabled);
			t.Num("planSeconds", pr.PlanSeconds, 1.0f, 36000.0f);
			t.Num("paveAt", pr.PaveAt, 0.1f, 6000.0f);
			t.Num("extendAt", pr.ExtendAt, 0.1f, 6000.0f);
			t.Int("minCells", pr.MinCells, 1, 64);
			t.Int("maxCells", pr.MaxCells, 1, 256);
			t.Int("cellsPerTrip", pr.CellsPerTrip, 1, 64);
			t.Num("workSeconds", pr.WorkSeconds, 0.0f, 600.0f);
			t.Num("reserve", pr.Reserve, 0.0f, 10.0f);
			t.Num("timeoutSeconds", pr.TimeoutSeconds, 10.0f, 1e7f);
			t.Num("score", pr.Score, 0.0f, 5.0f);
			if (!t.Error.empty()) return t.Error;
			if (pr.MaxCells < pr.MinCells) return "trails: pave: maxCells must not be below minCells";
			if (const auto m = p->find("materials"); m != p->end())
			{
				if (!m->is_array() || m->empty()) return "trails: pave: materials: expected a list of { \"item\", \"perCell\" }";
				std::vector<PaveMaterial> list;
				for (const Json& e : *m)
				{
					if (!e.is_object() || !e.contains("item") || !e["item"].is_string() || e["item"].get<std::string>().empty()) return "trails: pave: materials: each needs an \"item\"";
					PaveMaterial pm;
					pm.Item = e["item"].get<std::string>();
					Reader me{ e, "trails: pave: materials: " };
					me.Only({ "item", "perCell" });
					me.Int("perCell", pm.PerCell, 1, 100);
					if (!me.Error.empty()) return me.Error;
					list.push_back(pm);
				}
				pr.Materials = std::move(list);
			}
		}
		if (const auto v = section->find("visual"); v != section->end())
		{
			if (!v->is_object()) return "trails: visual: expected an object";
			TrailVisualRules& vr = r.Visual;
			Reader t{ *v, "trails: visual: " };
			t.Only({ "grass", "tint", "dirt", "stone", "refreshSeconds" });
			t.Colour("dirt", vr.Dirt);
			t.Colour("stone", vr.Stone);
			t.Num("refreshSeconds", vr.RefreshSeconds, 0.0f, 600.0f);
			if (!t.Error.empty()) return t.Error;
			if (v->contains("grass"))
			{
				const Json& g = (*v)["grass"];
				if (!g.is_object()) return "trails: visual: grass: expected an object";
				Reader gr{ g, "trails: visual: grass: " };
				gr.Only({ "trail", "worn", "paved" });
				gr.Num("trail", vr.GrassTrail, 0.0f, 1.0f);
				gr.Num("worn", vr.GrassWorn, 0.0f, 1.0f);
				gr.Num("paved", vr.GrassPaved, 0.0f, 1.0f);
				if (!gr.Error.empty()) return gr.Error;
			}
			if (v->contains("tint"))
			{
				const Json& g = (*v)["tint"];
				if (!g.is_object()) return "trails: visual: tint: expected an object";
				Reader tr{ g, "trails: visual: tint: " };
				tr.Only({ "trail", "worn" });
				tr.Num("trail", vr.TintTrail, 0.0f, 1.0f);
				tr.Num("worn", vr.TintWorn, 0.0f, 1.0f);
				if (!tr.Error.empty()) return tr.Error;
			}
		}
		live = r;
		return {};
	}

	std::string TrailRulesToJson(const TrailRules& r)
	{
		Json materials = Json::array();
		for (const PaveMaterial& m : r.Pave.Materials) materials.push_back(Json{ { "item", m.Item }, { "perCell", m.PerCell } });
		Json j{
			{ "enabled", r.Enabled }, { "cellSize", r.CellSize }, { "extent", r.Extent }, { "sampleSeconds", r.SampleSeconds }, { "wearPerPass", r.WearPerPass },
			{ "maxWear", r.MaxWear }, { "decayPerDay", r.DecayPerDay }, { "decaySeconds", r.DecaySeconds }, { "trailAt", r.TrailAt }, { "wornAt", r.WornAt },
			{ "maxJump", r.MaxJump },
			{ "speed", { { "trail", r.TrailSpeed }, { "worn", r.WornSpeed }, { "paved", r.PavedSpeed } } },
			{ "navEase", { { "trail", r.TrailEase }, { "worn", r.WornEase }, { "paved", r.PavedEase } } },
			{ "pave", { { "enabled", r.Pave.Enabled }, { "planSeconds", r.Pave.PlanSeconds }, { "paveAt", r.Pave.PaveAt }, { "extendAt", r.Pave.ExtendAt },
				{ "minCells", r.Pave.MinCells }, { "maxCells", r.Pave.MaxCells }, { "cellsPerTrip", r.Pave.CellsPerTrip }, { "workSeconds", r.Pave.WorkSeconds },
				{ "reserve", r.Pave.Reserve }, { "timeoutSeconds", r.Pave.TimeoutSeconds }, { "score", r.Pave.Score }, { "materials", materials } } },
			{ "visual", { { "grass", { { "trail", r.Visual.GrassTrail }, { "worn", r.Visual.GrassWorn }, { "paved", r.Visual.GrassPaved } } },
				{ "tint", { { "trail", r.Visual.TintTrail }, { "worn", r.Visual.TintWorn } } },
				{ "dirt", Triple(r.Visual.Dirt.x, r.Visual.Dirt.y, r.Visual.Dirt.z) }, { "stone", Triple(r.Visual.Stone.x, r.Visual.Stone.y, r.Visual.Stone.z) },
				{ "refreshSeconds", r.Visual.RefreshSeconds } } } };
		return j.dump();
	}

	// ------------------------------------------------------------------ the grid
	void TrailState::Reset(const glm::vec2& centre, const TrailRules& rules)
	{
		m_Cell = std::max(rules.CellSize, 0.5f);
		m_N = std::max(4, static_cast<int>(std::ceil(rules.Extent / m_Cell)));
		m_Origin = centre - glm::vec2(0.5f * static_cast<float>(m_N) * m_Cell);
		const size_t n = static_cast<size_t>(m_N) * static_cast<size_t>(m_N);
		m_Wear.assign(n, 0);
		m_Paved.assign(n, 0);
		m_Flag.assign(n, 0);
		m_Changed.clear();
		m_AllChanged = true;
		m_Nav.clear();
		m_NavAll = false;
		m_DecayCarry = 0.0f;
		Paving = Stretch();
		Last.clear();
	}

	bool TrailState::CellOf(const glm::vec2& p, int& i, int& j) const
	{
		if (m_N <= 0) return false;
		const glm::vec2 f = (p - m_Origin) / m_Cell;
		if (f.x < 0.0f || f.y < 0.0f) return false;
		i = static_cast<int>(f.x);
		j = static_cast<int>(f.y);
		return i < m_N && j < m_N;
	}

	glm::vec2 TrailState::Centre(int index) const
	{
		return m_Origin + (glm::vec2(static_cast<float>(index % m_N), static_cast<float>(index / m_N)) + 0.5f) * m_Cell;
	}

	TrailTier TrailState::TierAt(int index, const TrailRules& rules) const
	{
		const size_t k = static_cast<size_t>(index);
		if (m_Paved[k]) return TrailTier::Paved;
		const float w = static_cast<float>(m_Wear[k]);
		if (w >= rules.WornAt * kScale) return TrailTier::Worn;
		if (w >= rules.TrailAt * kScale) return TrailTier::Trail;
		return TrailTier::None;
	}

	TrailTier TrailState::TierAt(const glm::vec2& p, const TrailRules& rules) const
	{
		int i = 0, j = 0;
		return CellOf(p, i, j) ? TierAt(Index(i, j), rules) : TrailTier::None;
	}

	float TrailState::Pace(const glm::vec2& p, const TrailRules& rules) const
	{
		switch (TierAt(p, rules))
		{
		case TrailTier::Paved: return rules.PavedSpeed;
		case TrailTier::Worn: return rules.WornSpeed;
		case TrailTier::Trail: return rules.TrailSpeed;
		default: return 1.0f;
		}
	}

	float TrailState::Ease(const glm::vec2& p, const TrailRules& rules) const
	{
		switch (TierAt(p, rules))
		{
		case TrailTier::Paved: return rules.PavedEase;
		case TrailTier::Worn: return rules.WornEase;
		case TrailTier::Trail: return rules.TrailEase;
		default: return 0.0f;
		}
	}

	float TrailState::Smooth(const glm::vec2& p, const TrailRules& rules, float trail, float worn, float paved) const
	{
		if (m_N <= 0) return 0.0f;
		const glm::vec2 f = (p - m_Origin) / m_Cell - 0.5f;
		const float fx = std::floor(f.x), fy = std::floor(f.y);
		const int i0 = static_cast<int>(fx), j0 = static_cast<int>(fy);
		if (i0 < -1 || j0 < -1 || i0 >= m_N || j0 >= m_N) return 0.0f;
		const float tx = f.x - fx, ty = f.y - fy;
		auto at = [&](int i, int j) -> float
		{
			if (i < 0 || j < 0 || i >= m_N || j >= m_N) return 0.0f;
			switch (TierAt(Index(i, j), rules))
			{
			case TrailTier::Paved: return paved;
			case TrailTier::Worn: return worn;
			case TrailTier::Trail: return trail;
			default: return 0.0f;
			}
		};
		return (at(i0, j0) * (1.0f - tx) + at(i0 + 1, j0) * tx) * (1.0f - ty) + (at(i0, j0 + 1) * (1.0f - tx) + at(i0 + 1, j0 + 1) * tx) * ty;
	}

	void TrailState::Note(int index, TrailTier before, const TrailRules& rules)
	{
		if (TierAt(index, rules) == before) return;
		if (!m_NavAll) { if (m_Nav.size() >= 8192) m_NavAll = true; else m_Nav.push_back(index); }
		if (m_AllChanged) return;
		const size_t k = static_cast<size_t>(index);
		if (m_Flag[k]) return;
		if (m_Changed.size() >= 4096) { m_AllChanged = true; return; }
		m_Flag[k] = 1;
		m_Changed.push_back(index);
	}

	int TrailState::Walk(const glm::vec2& a, const glm::vec2& b, const TrailRules& rules)
	{
		if (m_N <= 0) return 0;
		const int add = std::max(1, static_cast<int>(std::lround(rules.WearPerPass * kScale)));
		const int top = std::clamp(static_cast<int>(std::lround(rules.MaxWear * kScale)), 1, 65535);
		const float length = glm::distance(a, b);
		const int steps = std::max(1, static_cast<int>(std::ceil(length / (0.4f * m_Cell))));
		int last = -1, worn = 0;
		{
			int i = 0, j = 0;
			if (CellOf(a, i, j)) last = Index(i, j);   // the cell it stood in at the last read was worn then
		}
		for (int s = 1; s <= steps; ++s)
		{
			const glm::vec2 p = glm::mix(a, b, static_cast<float>(s) / static_cast<float>(steps));
			int i = 0, j = 0;
			if (!CellOf(p, i, j)) { last = -1; continue; }
			const int index = Index(i, j);
			if (index == last) continue;
			last = index;
			const size_t k = static_cast<size_t>(index);
			const TrailTier before = TierAt(index, rules);
			m_Wear[k] = static_cast<uint16_t>(std::min(top, static_cast<int>(m_Wear[k]) + add));
			Note(index, before, rules);
			++worn;
		}
		return worn;
	}

	void TrailState::Decay(float seconds, float daySeconds, const TrailRules& rules)
	{
		if (m_N <= 0 || rules.DecayPerDay <= 0.0f) return;
		const float amount = rules.DecayPerDay * kScale * seconds / std::max(daySeconds, 1.0f) + m_DecayCarry;
		const int whole = static_cast<int>(std::floor(amount));
		m_DecayCarry = amount - static_cast<float>(whole);
		if (whole <= 0) return;
		for (size_t k = 0; k < m_Wear.size(); ++k)
		{
			if (m_Wear[k] == 0 || m_Paved[k]) continue;
			const TrailTier before = TierAt(static_cast<int>(k), rules);
			m_Wear[k] = static_cast<uint16_t>(std::max(0, static_cast<int>(m_Wear[k]) - whole));
			Note(static_cast<int>(k), before, rules);
		}
	}

	void TrailState::SetPaved(int index, const TrailRules& rules)
	{
		const size_t k = static_cast<size_t>(index);
		const TrailTier before = TierAt(index, rules);
		m_Paved[k] = 1;
		m_Wear[k] = static_cast<uint16_t>(std::clamp(static_cast<int>(std::lround(rules.MaxWear * kScale)), 1, 65535));
		Note(index, before, rules);
	}

	std::vector<int> TrailState::TakeNav(bool& all)
	{
		all = m_NavAll;
		m_NavAll = false;
		std::vector<int> out;
		out.swap(m_Nav);
		return out;
	}

	std::vector<int> TrailState::TakeChanged(bool& all)
	{
		all = m_AllChanged;
		std::vector<int> out;
		out.swap(m_Changed);
		for (int k : out) m_Flag[static_cast<size_t>(k)] = 0;
		m_AllChanged = false;
		return out;
	}

	int TrailState::Counts(TrailTier tier, const TrailRules& rules) const
	{
		int n = 0;
		for (size_t k = 0; k < m_Wear.size(); ++k) if (TierAt(static_cast<int>(k), rules) == tier) ++n;
		return n;
	}

	float TrailState::MaxWearNow() const
	{
		uint16_t top = 0;
		for (size_t k = 0; k < m_Wear.size(); ++k) if (!m_Paved[k]) top = std::max(top, m_Wear[k]);
		return static_cast<float>(top) / static_cast<float>(kScale);
	}

	std::string TrailState::ToJson() const
	{
		Json j;
		j["n"] = m_N;
		j["cell"] = m_Cell;
		j["origin"] = Json::array({ m_Origin.x, m_Origin.y });
		Json cells = Json::array();
		for (size_t k = 0; k < m_Wear.size(); ++k)
			if (m_Wear[k] || m_Paved[k]) cells.push_back(Json::array({ k, m_Wear[k], m_Paved[k] }));
		j["cells"] = cells;
		j["carry"] = m_DecayCarry;
		j["timers"] = Json::array({ SampleTimer, DecayTimer, PlanTimer });
		Json last = Json::object();
		for (const auto& [name, p] : Last) last[name] = Json::array({ p.x, p.y });
		j["last"] = last;
		if (Paving.Active())
			j["paving"] = Json{ { "cells", Paving.Cells }, { "item", Paving.Item }, { "perCell", Paving.PerCell }, { "since", Paving.Since } };
		j["counts"] = Json::array({ Passes, StretchesMarked, StretchesDone, StretchesDropped, PaveTrips, CellsPaved });
		return j.dump();
	}

	bool TrailState::FromJson(const std::string& json, const TrailRules& rules)
	{
		(void)rules;
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return false;
		TrailState s;
		try
		{
			s.m_N = j.value("n", 0);
			if (s.m_N < 0 || s.m_N > 4096) return false;
			s.m_Cell = j.value("cell", 2.0f);
			if (j.contains("origin") && j["origin"].is_array() && j["origin"].size() == 2) s.m_Origin = { j["origin"][0].get<float>(), j["origin"][1].get<float>() };
			const size_t n = static_cast<size_t>(s.m_N) * static_cast<size_t>(s.m_N);
			s.m_Wear.assign(n, 0);
			s.m_Paved.assign(n, 0);
			s.m_Flag.assign(n, 0);
			if (j.contains("cells") && j["cells"].is_array())
				for (const Json& c : j["cells"])
				{
					if (!c.is_array() || c.size() != 3) return false;
					const size_t k = c[0].get<size_t>();
					if (k >= n) return false;
					s.m_Wear[k] = c[1].get<uint16_t>();
					s.m_Paved[k] = c[2].get<uint8_t>();
				}
			s.m_DecayCarry = j.value("carry", 0.0f);
			if (j.contains("timers") && j["timers"].is_array() && j["timers"].size() == 3)
			{
				s.SampleTimer = j["timers"][0].get<float>();
				s.DecayTimer = j["timers"][1].get<float>();
				s.PlanTimer = j["timers"][2].get<float>();
			}
			if (j.contains("last") && j["last"].is_object())
				for (const auto& [name, p] : j["last"].items())
					if (p.is_array() && p.size() == 2) s.Last[name] = { p[0].get<float>(), p[1].get<float>() };
			if (j.contains("paving") && j["paving"].is_object())
			{
				const Json& p = j["paving"];
				s.Paving.Cells = p.value("cells", std::vector<int>());
				for (int c : s.Paving.Cells) if (c < 0 || static_cast<size_t>(c) >= n) return false;
				s.Paving.Item = p.value("item", std::string());
				s.Paving.PerCell = p.value("perCell", 1);
				s.Paving.Since = p.value("since", 0.0);
			}
			if (j.contains("counts") && j["counts"].is_array() && j["counts"].size() == 6)
			{
				const Json& c = j["counts"];
				s.Passes = c[0].get<long long>();
				s.StretchesMarked = c[1].get<int>(); s.StretchesDone = c[2].get<int>(); s.StretchesDropped = c[3].get<int>();
				s.PaveTrips = c[4].get<int>(); s.CellsPaved = c[5].get<int>();
			}
		}
		catch (const Json::exception&) { return false; }
		s.m_AllChanged = true;   // the views draw it whole
		s.m_NavAll = true;       // ... and the navigation reads it whole
		*this = std::move(s);
		return true;
	}

	// ------------------------------------------------------------------ the tribe's part
	// What the clan does with its paths, in the log and in the telemetry (TellClan writes only the log: every line of the log has its "say" event)
	void Tribe::TrailNote(const std::string& text)
	{
		TellClan(text);
		if (m_Telemetry) EmitEvent("say", m_ClanName, std::string(), std::string(), "the clan " + text);
	}

	float Tribe::TrailPace(const glm::vec2& at) const
	{
		return Config.Trails.Enabled && m_Trails.Ready() ? m_Trails.Pace(at, Config.Trails) : 1.0f;
	}

	// The navigation reads again the ground under the cells whose tier changed (in blocks of kNavBlock metres, in order): within the tick, so a
	// run is the same whatever its frames. `all`: every cell with a trail (switched on or off, a load, a grid left behind).
	namespace { constexpr float kNavBlock = 8.0f; }

	std::set<std::pair<int, int>> Tribe::TrailNavBlocks(bool all)
	{
		TrailState& t = m_Trails;
		bool everything = false;
		std::vector<int> cells = t.TakeNav(everything);
		std::set<std::pair<int, int>> blocks;
		if (!m_TrailNav || !t.Ready()) return blocks;
		if (all || everything)
		{
			cells.clear();
			for (int k = 0; k < t.Side() * t.Side(); ++k) if (t.Wear(k) > 0.0f || t.Paved(k)) cells.push_back(k);
		}
		for (int k : cells)
		{
			const glm::vec2 c = t.Centre(k);
			blocks.insert({ static_cast<int>(std::floor(c.x / kNavBlock)), static_cast<int>(std::floor(c.y / kNavBlock)) });
		}
		return blocks;
	}

	void Tribe::TellTrailNav(const std::set<std::pair<int, int>>& blocks)
	{
		if (!m_TrailNav) return;
		for (const auto& [bx, bz] : blocks)
		{
			const glm::vec2 lo(static_cast<float>(bx) * kNavBlock, static_cast<float>(bz) * kNavBlock);
			m_TrailNav(lo, lo + glm::vec2(kNavBlock));
		}
	}

	void Tribe::FlushTrailNav(bool all) { TellTrailNav(TrailNavBlocks(all)); }

	void Tribe::ReplaceTrails(TrailState&& next)
	{
		auto area = [](const TrailState& s) { return std::make_pair(s.Origin(), s.Origin() + glm::vec2(static_cast<float>(s.Side()) * s.Cell())); };
		const bool had = m_Trails.Ready();
		const auto old = area(m_Trails);
		m_Trails = std::move(next);
		if (!m_TrailNav) return;
		if (had) m_TrailNav(old.first, old.second);   // (a grid's whole area: the ground it trod and the ground the new state treads read alike)
		if (m_Trails.Ready()) { const auto now = area(m_Trails); m_TrailNav(now.first, now.second); }
	}

	void Tribe::StepTrails(float dt)
	{
		const TrailRules& r = Config.Trails;
		TrailState& t = m_Trails;
		if (!r.Enabled)
		{
			if (t.PaceApplied)   // switched off: everyone walks at its own pace again
				for (TribeMember& m : m_Members) if (m.Body && !m.Dead) m.Body->SetPace(1.0f);
			t.PaceApplied = false;
			if (t.NavOn) { t.NavOn = false; FlushTrailNav(true); }   // (the host's ease reads nothing of a clan whose trails are off)
			return;
		}
		// the grid is round the camp: made the first time, made again when the camp moved off its middle (a migration) or the rules changed its cells
		const glm::vec2 camp = Config.Economy.Camp;
		const float half = 0.5f * static_cast<float>(t.Side()) * t.Cell();
		if (!t.Ready() || std::fabs(t.Cell() - std::max(r.CellSize, 0.5f)) > 1e-4f || glm::distance(camp, t.Origin() + glm::vec2(half)) > 0.3f * r.Extent)
		{
			const bool moved = t.Ready();
			const std::set<std::pair<int, int>> old = moved && t.NavOn ? TrailNavBlocks(true) : std::set<std::pair<int, int>>();
			t.Reset(camp, r);
			TellTrailNav(old);   // (the old cells, unworn now, are read again)
			if (moved) TrailNote("the trails round the old camp are left behind");
		}
		t.SampleTimer += dt;
		if (t.SampleTimer >= r.SampleSeconds)
		{
			t.SampleTimer = std::fmod(t.SampleTimer, std::max(r.SampleSeconds, 0.01f));
			const float step = 0.75f * t.Cell();
			for (TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) { t.Last.erase(m.Name); continue; }
				const glm::vec2 pos = Where(m);
				const auto it = t.Last.find(m.Name);
				if (it == t.Last.end()) t.Last.emplace(m.Name, pos);
				else
				{
					const float d = glm::distance(it->second, pos);
					if (d > r.MaxJump) it->second = pos;   // moved, not walked (a teleport, a load)
					else if (d >= step) { t.Passes += t.Walk(it->second, pos, r); it->second = pos; }
				}
				if (!m.Abstract) m.Body->SetPace(t.Pace(pos, r));   // (an abstract member's speed is read in StepAbstract)
			}
			t.PaceApplied = true;
		}
		t.DecayTimer += dt;
		if (t.DecayTimer >= r.DecaySeconds)
		{
			t.Decay(t.DecayTimer, Config.Needs.DaySeconds, r);
			t.DecayTimer = 0.0f;
		}
		PlanPaving(dt);
		// what changed in this step (walks, fading, paving since the last step) reaches the navigation now; all of it the first time (a load, switched on)
		const bool first = !t.NavOn;
		t.NavOn = true;
		FlushTrailNav(first);
	}

	// Spare material for paths: what the store holds above its stock target (times Reserve) and above what the building sites still lack.
	int Tribe::PaveSurplus(const std::string& item) const
	{
		const std::map<std::string, int> sites = SiteDemand();
		const auto site = sites.find(item);
		const int keep = static_cast<int>(std::ceil(StockWanted(item) * Config.Trails.Pave.Reserve)) + (site != sites.end() ? site->second : 0);
		return m_Store.Count(item) - keep;
	}

	void Tribe::PlanPaving(float dt)
	{
		const TrailRules& r = Config.Trails;
		const TrailPaveRules& p = r.Pave;
		TrailState& t = m_Trails;
		if (!p.Enabled || !t.Ready()) return;
		if (t.Paving.Active())
		{
			if (m_Time - t.Paving.Since > static_cast<double>(p.TimeoutSeconds))
			{
				bool working = false;
				for (const TribeMember& m : m_Members) working = working || (!m.Dead && m.Job == "pave");
				if (!working)
				{
					++t.StretchesDropped;
					TrailNote("gives up paving the path (" + std::to_string(t.Paving.Cells.size()) + " cells left)");
					t.Paving = TrailState::Stretch();
				}
			}
			return;
		}
		t.PlanTimer += dt;
		if (t.PlanTimer < p.PlanSeconds) return;
		t.PlanTimer = 0.0f;
		// the material: the first of the list the clan has to spare for a whole stretch
		const PaveMaterial* use = nullptr;
		for (const PaveMaterial& mat : p.Materials)
			if (m_Items && m_Items->Find(mat.Item) && PaveSurplus(mat.Item) >= mat.PerCell * p.MinCells) { use = &mat; break; }
		if (!use) return;
		// the most worn cell not paved yet, then along the most worn of its neighbours both ways
		const int n = t.Side();
		const ITribeBody* body = nullptr;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { body = m.Body; break; }
		auto standable = [&](int index) { return !body || body->CanStandAt(t.Centre(index)); };
		int seed = -1;
		float best = p.PaveAt - 1e-4f;
		for (int k = 0; k < n * n; ++k)
			if (!t.Paved(k) && t.Wear(k) > best && standable(k)) { best = t.Wear(k); seed = k; }
		if (seed < 0) return;
		std::vector<int> chain{ seed };
		auto grow = [&](bool atBack)
		{
			while (static_cast<int>(chain.size()) < p.MaxCells)
			{
				const int from = atBack ? chain.back() : chain.front();
				const int fi = from % n, fj = from / n;
				int next = -1;
				float top = p.ExtendAt - 1e-4f;
				for (int dj = -1; dj <= 1; ++dj)
					for (int di = -1; di <= 1; ++di)
					{
						const int i = fi + di, j = fj + dj;
						if ((di == 0 && dj == 0) || i < 0 || j < 0 || i >= n || j >= n) continue;
						const int k = t.Index(i, j);
						if (t.Paved(k) || t.Wear(k) <= top || std::find(chain.begin(), chain.end(), k) != chain.end() || !standable(k)) continue;
						top = t.Wear(k);
						next = k;
					}
				if (next < 0) return;
				if (atBack) chain.push_back(next); else chain.insert(chain.begin(), next);
			}
		};
		grow(true);
		grow(false);
		if (static_cast<int>(chain.size()) < p.MinCells) return;
		// it is laid from the end nearer the camp
		if (glm::distance(t.Centre(chain.back()), Config.Economy.Camp) < glm::distance(t.Centre(chain.front()), Config.Economy.Camp)) std::reverse(chain.begin(), chain.end());
		t.Paving.Cells = std::move(chain);
		t.Paving.Item = use->Item;
		t.Paving.PerCell = use->PerCell;
		t.Paving.Since = m_Time;
		++t.StretchesMarked;
		char line[160];
		const glm::vec2 at = t.Centre(t.Paving.Cells.front());
		std::snprintf(line, sizeof line, "marks %zu cells of its most worn path to pave with %s, from (%.0f, %.0f)", t.Paving.Cells.size(), use->Item.c_str(), static_cast<double>(at.x), static_cast<double>(at.y));
		TrailNote(line);
	}

	void Tribe::TrailOptions(TribeMember& m, std::vector<UtilityOption>& opts)
	{
		const TrailRules& r = Config.Trails;
		const TrailState& t = m_Trails;
		if (!r.Enabled || !r.Pave.Enabled || !t.Paving.Active() || !m.Body || m.Player || IsChild(m)) return;
		for (const TribeMember& o : m_Members) if (&o != &m && !o.Dead && o.Job == "pave") return;   // one paver at a time: the stretch is laid in order
		const TrailState::Stretch& s = t.Paving;
		const int carried = m.Body->Bag().Count(s.Item);
		if (carried < s.PerCell && PaveSurplus(s.Item) < s.PerCell) return;
		const glm::vec2 at = t.Centre(s.Cells.front());
		const float bw = std::max(0.5f, MemberWeight(m, "build"));   // anyone helps; a builder (or a member good at building) more
		const float reach = Falloff(glm::distance(Where(m), at), 120.0f);
		const float day = AgendaFactor(m.Role, "gather", Hour(), CurrentSeason());   // paving is work: the agenda of work
		UtilityOption o;
		o.Kind = "pave";
		o.Target = "path";
		o.Where = at;
		o.Score = r.Pave.Score * bw * reach * day;
		Factor(o, "role", bw);
		Factor(o, "distance", reach);
		if (day != 1.0f) Factor(o, "agenda", day);
		char note[128];
		std::snprintf(note, sizeof note, "the clan paves its most worn path: %zu cells left, with %s", s.Cells.size(), s.Item.c_str());
		o.Note = note;
		opts.push_back(o);
	}

	bool Tribe::ExecuteTrails(TribeMember& m, const UtilityOption& o)
	{
		(void)o;
		TrailState& t = m_Trails;
		TrailState::Stretch& s = t.Paving;
		const TrailPaveRules& p = Config.Trails.Pave;
		if (!s.Active() || !m.Body) return false;
		Inventory& bag = m.Body->Bag();
		int cells = std::min(static_cast<int>(s.Cells.size()), p.CellsPerTrip);
		const int want = cells * s.PerCell - bag.Count(s.Item);
		const int moved = want > 0 ? m_Store.Transfer(*m_Items, bag, s.Item, std::min(want, std::max(0, PaveSurplus(s.Item)))) : 0;
		cells = std::min(cells, bag.Count(s.Item) / std::max(1, s.PerCell));
		if (cells <= 0)
		{
			if (moved > 0) bag.Transfer(*m_Items, m_Store, s.Item, moved);
			return false;
		}
		std::string orders;
		char step[96];
		for (int i = 0; i < cells; ++i)
		{
			const glm::vec2 c = t.Centre(s.Cells[static_cast<size_t>(i)]);
			std::snprintf(step, sizeof step, "%sgoto %.2f %.2f | wait %.1f", i ? " | " : "", static_cast<double>(c.x), static_cast<double>(c.y), static_cast<double>(p.WorkSeconds));
			orders += step;
		}
		if (!Order(m, orders))
		{
			if (moved > 0) bag.Transfer(*m_Items, m_Store, s.Item, moved);
			return false;
		}
		m.Job = "pave";
		m.Detail = "path";
		s.Since = m_Time;
		++t.PaveTrips;
		if (m.Mind.Current.Kind != "pave") Say(m, "carries " + std::to_string(cells * s.PerCell) + " " + s.Item + " to pave " + std::to_string(cells) + " cells of the path");
		return true;
	}

	bool Tribe::FinishTrails(TribeMember& m, const std::string& job)
	{
		if (job != "pave") return false;
		TrailState& t = m_Trails;
		TrailState::Stretch& s = t.Paving;
		if (!s.Active() || !m.Body) return true;   // (given up while it was on its way: it keeps what it carries for the next deposit)
		Inventory& bag = m.Body->Bag();
		const int cells = std::min(static_cast<int>(s.Cells.size()), Config.Trails.Pave.CellsPerTrip);
		// how far along it got: the last cell of the trip it stands on (a failed trip stopped at the cell after it, which it cannot reach)
		const glm::vec2 pos = Where(m);
		int reached = -1;
		for (int i = 0; i < cells; ++i)
			if (glm::distance(pos, t.Centre(s.Cells[static_cast<size_t>(i)])) <= 1.25f * t.Cell()) reached = i;
		const bool failed = HasFailed(m);
		int done = std::min(reached + 1, bag.Count(s.Item) / std::max(1, s.PerCell));
		if (done < 0) done = 0;
		for (int i = 0; i < done; ++i)
		{
			bag.Remove(s.Item, s.PerCell);
			t.SetPaved(s.Cells[static_cast<size_t>(i)], Config.Trails);
			++t.CellsPaved;
		}
		s.Cells.erase(s.Cells.begin(), s.Cells.begin() + done);
		if (failed && !s.Cells.empty())   // the next cell could not be got to: it is left out of the stretch
		{
			Say(m, "cannot get to the next cell of the path; it is left as it is");
			s.Cells.erase(s.Cells.begin());
		}
		if (done > 0)
		{
			s.Since = m_Time;
			Practise(m, "build", "build", static_cast<float>(done));   // paving trains building
			Say(m, "paves " + std::to_string(done) + " cells of the path with " + s.Item);
		}
		if (s.Cells.empty())
		{
			++t.StretchesDone;
			TrailNote("finishes paving a stretch of path");
			s = TrailState::Stretch();
		}
		return true;
	}

	std::string Tribe::TrailsReport() const
	{
		const TrailRules& r = Config.Trails;
		const TrailState& t = m_Trails;
		char line[512];
		if (!t.Ready()) return std::string("trails ") + (r.Enabled ? "on" : "off") + ": no grid yet";
		float paceSum = 0.0f;
		int walkers = 0;
		for (const TribeMember& m : m_Members)
			if (!m.Dead && m.Body) { paceSum += TrailPace(Where(m)); ++walkers; }
		std::snprintf(line, sizeof line,
			"trails %s: grid %dx%d of %.1f m round (%.0f, %.0f); cells: %d trail, %d worn, %d paved; most worn %.1f passes; %lld cell passes walked; "
			"members on paths now: mean pace %.2f of %d; paving: %d stretches marked, %d done, %d given up, %d trips, %d cells paved",
			r.Enabled ? "on" : "off", t.Side(), t.Side(), static_cast<double>(t.Cell()), static_cast<double>(t.Origin().x + 0.5f * static_cast<float>(t.Side()) * t.Cell()),
			static_cast<double>(t.Origin().y + 0.5f * static_cast<float>(t.Side()) * t.Cell()), t.Counts(TrailTier::Trail, r), t.Counts(TrailTier::Worn, r), t.Counts(TrailTier::Paved, r),
			static_cast<double>(t.MaxWearNow()), t.Passes, walkers ? static_cast<double>(paceSum / static_cast<float>(walkers)) : 1.0, walkers,
			t.StretchesMarked, t.StretchesDone, t.StretchesDropped, t.PaveTrips, t.CellsPaved);
		std::string s = line;
		if (t.Paving.Active())
		{
			const glm::vec2 a = t.Centre(t.Paving.Cells.front());
			std::snprintf(line, sizeof line, "\n  stretch to pave: %zu cells with %s from (%.0f, %.0f), store has %d (%d to spare)", t.Paving.Cells.size(), t.Paving.Item.c_str(),
				static_cast<double>(a.x), static_cast<double>(a.y), m_Store.Count(t.Paving.Item), PaveSurplus(t.Paving.Item));
			s += line;
		}
		else
			for (const PaveMaterial& mat : r.Pave.Materials)
			{
				std::snprintf(line, sizeof line, "\n  %s: store %d, to spare %d (a stretch needs %d)", mat.Item.c_str(), m_Store.Count(mat.Item), PaveSurplus(mat.Item), mat.PerCell * r.Pave.MinCells);
				s += line;
			}
		return s;
	}
}
