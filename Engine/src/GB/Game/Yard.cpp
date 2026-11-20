// PROTOTYPE of TALLERES.md section 10 (see Yard.h).
#include "GB/Game/Yard.h"
#include "GB/Gameplay/ShippedContent.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		// An offset in the building's own axes (x, z), turned as Transform.Rotation.y turns it (the same as SettlementPlanner's Turned).
		glm::vec2 Turned(const glm::vec2& local, float yawDegrees)
		{
			const float a = glm::radians(yawDegrees), c = std::cos(a), s = std::sin(a);
			return { local.x * c + local.y * s, -local.x * s + local.y * c };
		}

		bool Number(const Json& o, const char* key, float& out, float lo, float hi, std::string& error, const std::string& where)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_number() || it->get<float>() < lo || it->get<float>() > hi) { error = where + "." + key + ": a number out of range"; return false; }
			out = it->get<float>();
			return true;
		}

		bool Colour(const Json& o, const char* key, glm::vec3& out, std::string& error, const std::string& where)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) { error = where + "." + key + ": expected [r, g, b]"; return false; }
			out = { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
			return true;
		}

		bool Items(const Json& o, const char* key, std::map<std::string, int>& out, std::string& error, const std::string& where)
		{
			const auto it = o.find(key);
			if (it == o.end()) return true;
			if (!it->is_object()) { error = where + "." + key + ": expected an object of item -> units"; return false; }
			out.clear();
			for (const auto& [item, n] : it->items())
			{
				if (!n.is_number_integer() || n.get<int>() < 1 || n.get<int>() > 1000) { error = where + "." + key + "." + item + ": a whole number from 1"; return false; }
				out[item] = n.get<int>();
			}
			return true;
		}

		bool Swaps(const Json& o, std::map<std::string, std::vector<std::string>>& out, std::string& error, const std::string& where)
		{
			const auto it = o.find("substitutes");
			if (it == o.end()) return true;
			if (!it->is_object()) { error = where + ".substitutes: expected an object of item -> [ items ]"; return false; }
			out.clear();
			for (const auto& [item, list] : it->items())
			{
				if (!list.is_array()) { error = where + ".substitutes." + item + ": expected a list of items"; return false; }
				for (const Json& k : list)
				{
					if (!k.is_string()) { error = where + ".substitutes." + item + ": expected a list of items"; return false; }
					out[item].push_back(k.get<std::string>());
				}
			}
			return true;
		}

		bool ReadLook(const Json& f, YardFenceLook& l, std::string& error, const std::string& where)
		{
			if (!f.is_object()) { error = where + ": expected an object"; return false; }
			if (!Number(f, "postSpacing", l.PostSpacing, 0.3f, 10.0f, error, where) || !Number(f, "postHeight", l.PostHeight, 0.1f, 5.0f, error, where)
				|| !Number(f, "postWidth", l.PostWidth, 0.01f, 1.0f, error, where) || !Number(f, "gatePostHeight", l.GatePostHeight, 0.1f, 5.0f, error, where)
				|| !Number(f, "gatePostWidth", l.GatePostWidth, 0.01f, 1.0f, error, where) || !Number(f, "railWidth", l.RailWidth, 0.01f, 1.0f, error, where)
				|| !Colour(f, "postColor", l.PostColor, error, where) || !Colour(f, "railColor", l.RailColor, error, where)) return false;
			if (const auto solid = f.find("solid"); solid != f.end())
			{
				if (!solid->is_boolean()) { error = where + ".solid: true or false"; return false; }
				l.Solid = solid->get<bool>();
			}
			if (const auto rails = f.find("rails"); rails != f.end())
			{
				if (!rails->is_array()) { error = where + ".rails: expected a list of heights"; return false; }
				l.Rails.clear();
				for (const Json& h : *rails)
				{
					if (!h.is_number() || h.get<float>() <= 0.0f || h.get<float>() > 5.0f) { error = where + ".rails: heights from 0 to 5 m"; return false; }
					l.Rails.push_back(h.get<float>());
				}
			}
			return true;
		}
	}

	const YardClosure* YardRules::Closure(int level) const
	{
		for (const YardClosure& c : Closures) if (c.Level == level) return &c;
		return nullptr;
	}

	const YardClosure* YardRules::ClosureById(const std::string& id) const
	{
		for (const YardClosure& c : Closures) if (c.Id == id) return &c;
		return nullptr;
	}

	std::string LoadYardRules(const std::string& json, YardRules& live)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the content is not a JSON object";
		const auto section = doc.find("yards");
		if (section == doc.end()) return {};
		if (!section->is_object()) return "yards: expected an object";
		YardRules r = live;
		std::string error;
		if (const auto kinds = section->find("slotKinds"); kinds != section->end())
		{
			if (!kinds->is_object()) return "yards.slotKinds: expected an object of kind -> { reach, inset }";
			for (const auto& [kind, k] : kinds->items())
			{
				if (!k.is_object()) return "yards.slotKinds." + kind + ": expected an object";
				YardSlotKind s = r.SlotKinds.count(kind) ? r.SlotKinds.at(kind) : YardSlotKind{};
				if (!Number(k, "reach", s.Reach, 0.0f, 5.0f, error, "yards.slotKinds." + kind) || !Number(k, "inset", s.Inset, 0.0f, 5.0f, error, "yards.slotKinds." + kind)) return error;
				r.SlotKinds[kind] = s;
			}
		}
		if (const auto f = section->find("fence"); f != section->end())
			if (!ReadLook(*f, r.Fence, error, "yards.fence")) return error;
		// T367f: the levels of the closure
		if (const auto list = section->find("closures"); list != section->end())
		{
			if (!list->is_array()) return "yards.closures: expected a list of levels";
			r.Closures.clear();
			for (const Json& c : *list)
			{
				YardClosure k;
				k.Look = r.Fence;
				const std::string where = "yards.closures[" + std::to_string(r.Closures.size()) + "]";
				if (!c.is_object() || !c.contains("id") || !c["id"].is_string() || c["id"].get<std::string>().empty()) return where + ": an object with an id";
				k.Id = c["id"].get<std::string>();
				if (const auto l = c.find("level"); l != c.end())
				{
					if (!l->is_number_integer() || l->get<int>() < 1 || l->get<int>() > 20) return where + ".level: a whole number from 1 to 20";
					k.Level = l->get<int>();
				}
				if (const auto t = c.find("tech"); t != c.end())
				{
					if (!t->is_string()) return where + ".tech: a technology id";
					k.Tech = t->get<std::string>();
				}
				if (!Number(c, "costPer", k.CostPer, 0.5f, 100.0f, error, where) || !Number(c, "work", k.Work, 0.0f, 1000.0f, error, where)
					|| !Number(c, "defense", k.Defense, 0.0f, 5.0f, error, where) || !Items(c, "needs", k.Needs, error, where) || !Swaps(c, k.Substitutes, error, where)) return error;
				if (const auto lk = c.find("look"); lk != c.end() && !ReadLook(*lk, k.Look, error, where + ".look")) return error;
				for (const YardClosure& o : r.Closures)
					if (o.Id == k.Id || o.Level == k.Level) return where + ": each closure has its own id and its own level";
				r.Closures.push_back(std::move(k));
			}
			std::sort(r.Closures.begin(), r.Closures.end(), [](const YardClosure& a, const YardClosure& b) { return a.Level < b.Level; });
		}
		// T367c: the sizes of each line of houses, in their "order"
		if (const auto all = section->find("sizes"); all != section->end())
		{
			if (!all->is_object()) return "yards.sizes: expected an object of host -> { size -> its values }";
			for (const auto& [host, list] : all->items())
			{
				if (!list.is_object() || list.empty()) return "yards.sizes." + host + ": expected an object of size -> its values";
				std::vector<std::pair<float, YardSize>> sizes;
				for (const auto& [id, v] : list.items())
				{
					const std::string where = "yards.sizes." + host + "." + id;
					if (!v.is_object()) return where + ": expected an object";
					YardSize z;
					z.Id = id;
					float order = 0.0f;
					if (!Number(v, "order", order, 0.0f, 100.0f, error, where)) return error;
					if (const auto b = v.find("building"); b != v.end()) { if (!b->is_string()) return where + ".building: a building id"; z.Building = b->get<std::string>(); }
					if (const auto t = v.find("tech"); t != v.end()) { if (!t->is_string()) return where + ".tech: a technology id"; z.Tech = t->get<std::string>(); }
					if (const auto fp = v.find("footprint"); fp != v.end())
					{
						if (!fp->is_array() || fp->size() != 2 || !(*fp)[0].is_number() || !(*fp)[1].is_number() || (*fp)[0].get<float>() < 0.5f || (*fp)[1].get<float>() < 0.5f)
							return where + ".footprint: [x, z] in metres, at least 0.5";
						z.Footprint = { (*fp)[0].get<float>(), (*fp)[1].get<float>() };
					}
					if (!Number(v, "costScale", z.CostScale, 0.1f, 20.0f, error, where) || !Number(v, "workScale", z.WorkScale, 0.1f, 20.0f, error, where)
						|| !Number(v, "rest", z.Rest, 0.0f, 10.0f, error, where) || !Number(v, "warmth", z.Warmth, 0.0f, 10.0f, error, where)) return error;
					for (const auto& [key, field] : std::initializer_list<std::pair<const char*, int*>>{ { "shelter", &z.Shelter }, { "storeCap", &z.StoreCap }, { "foodCap", &z.FoodCap }, { "maxModules", &z.MaxModules } })
						if (const auto n = v.find(key); n != v.end())
						{
							if (!n->is_number_integer() || n->get<int>() < 0 || n->get<int>() > 10000) return where + "." + key + ": a whole number from 0";
							*field = n->get<int>();
						}
					sizes.emplace_back(order, z);
				}
				std::stable_sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.Id < b.second.Id; });
				std::vector<YardSize>& line = r.Sizes[host];
				line.clear();
				for (const auto& [order, z] : sizes) line.push_back(z);
			}
		}
		if (const auto rules = section->find("rules"); rules != section->end())
		{
			if (!rules->is_object()) return "yards.rules: expected an object";
			float after = static_cast<float>(r.AiClosureAfterModules);
			if (!Number(*rules, "defenseCap", r.DefenseCap, 0.0f, 10.0f, error, "yards.rules") || !Number(*rules, "growMinWork", r.GrowMinWork, 0.0f, 1.0f, error, "yards.rules")
				|| !Number(*rules, "aiClosureAfterModules", after, 0.0f, 16.0f, error, "yards.rules")) return error;
			r.AiClosureAfterModules = static_cast<int>(after);
		}
		live = std::move(r);
		return {};
	}

	const YardRules& DefaultYardRules()
	{
		static const YardRules rules = ShippedSection<YardRules>([](const std::string& doc, YardRules& r) { return LoadYardRules(doc, r); });
		return rules;
	}

	glm::vec2 YardTurned(const glm::vec2& local, float yawDegrees) { return Turned(local, yawDegrees); }

	YardShape ComputeYard(const BuildingDef& host, const glm::vec2& pos, float yawDegrees, const YardRules& rules)
	{
		YardShape out;
		const BuildingYard& y = host.Yard;
		if (!y.Any()) return out;
		auto kind = [&](const char* k) { const auto it = rules.SlotKinds.find(k); return it != rules.SlotKinds.end() ? it->second : YardSlotKind{}; };
		auto world = [&](const glm::vec2& local) { return pos + Turned(local, yawDegrees); };
		const float fx = 0.5f * host.Footprint.x, fz = 0.5f * host.Footprint.y;
		const float hx = fx + y.Eave + y.Margin, zb = -(fz + y.Eave + y.MarginBack), zf = fz + y.Eave + y.MarginFront;
		const float r = std::clamp(y.CornerR, 0.0f, std::min(hx, 0.5f * (zf - zb)) - 0.05f);
		const float gate = std::clamp(0.5f * y.GateW, 0.2f, hx - r - 0.1f);

		// the line of the fence in the house's own axes, from the right side of the gate round the back to its left side
		std::vector<glm::vec2> line;
		auto arc = [&](const glm::vec2& centre, float from, float to)
		{
			constexpr int kSteps = 6;
			for (int i = 1; i <= kSteps; ++i)
			{
				const float a = glm::radians(from + (to - from) * static_cast<float>(i) / static_cast<float>(kSteps));
				line.push_back(centre + r * glm::vec2(std::cos(a), std::sin(a)));
			}
		};
		// (here x is +x and the second coordinate is +z: the front at zf; angles are measured in that plane)
		line.push_back({ gate, zf });
		line.push_back({ hx - r, zf });
		if (r > 0.0f) arc({ hx - r, zf - r }, 90.0f, 0.0f);
		line.push_back({ hx, zb + r });
		if (r > 0.0f) arc({ hx - r, zb + r }, 0.0f, -90.0f);
		line.push_back({ -hx + r, zb });
		if (r > 0.0f) arc({ -hx + r, zb + r }, -90.0f, -180.0f);
		line.push_back({ -hx, zf - r });
		if (r > 0.0f) arc({ -hx + r, zf - r }, 180.0f, 90.0f);
		line.push_back({ -gate, zf });
		line.erase(std::unique(line.begin(), line.end(), [](const glm::vec2& a, const glm::vec2& b) { return glm::distance(a, b) < 1e-4f; }), line.end());

		std::vector<float> at(line.size(), 0.0f);   // the length along the line to each of its points
		for (size_t i = 1; i < line.size(); ++i) at[i] = at[i - 1] + glm::distance(line[i - 1], line[i]);
		out.Length = at.back();
		auto pointAt = [&](float s)
		{
			const size_t i = static_cast<size_t>(std::upper_bound(at.begin(), at.end(), s) - at.begin());
			if (i == 0) return line.front();
			if (i >= line.size()) return line.back();
			const float t = (s - at[i - 1]) / std::max(at[i] - at[i - 1], 1e-5f);
			return glm::mix(line[i - 1], line[i], t);
		};
		const int bays = std::max(1, static_cast<int>(std::ceil(out.Length / std::max(rules.Fence.PostSpacing, 0.3f))));
		for (int i = 0; i <= bays; ++i) out.Posts.push_back(world(pointAt(out.Length * static_cast<float>(i) / static_cast<float>(bays))));
		for (const glm::vec2& p : line) out.Outline.push_back(world(p));
		out.Outline.push_back(out.Outline.front());   // (closed across the gate)
		out.GateR = world(line.front());
		out.GateL = world(line.back());

		// the slots: the corners, beside the gate, along the side fences (from the gate back), against the house's side walls
		auto slot = [&](const std::string& id, const std::string& k, const glm::vec2& local, const glm::vec2& frontLocal)
		{
			YardSlot s;
			s.Id = id;
			s.Kind = k;
			s.Pos = world(local);
			s.Front = glm::normalize(Turned(frontLocal, yawDegrees));
			s.Use = s.Pos + s.Front * kind(k.c_str()).Reach;
			s.Yaw = glm::degrees(std::atan2(s.Front.x, s.Front.y));
			out.Slots.push_back(s);
		};
		const float ci = kind("corner").Inset, gi = kind("gate").Inset, wi = kind("wall").Inset, li = kind("lean_to").Inset;
		const float inward = 0.70710678f;
		slot("corner_FL", "corner", { -hx + ci, zf - ci }, { inward, -inward });
		slot("corner_FR", "corner", { hx - ci, zf - ci }, { -inward, -inward });
		slot("corner_BL", "corner", { -hx + ci, zb + ci }, { inward, inward });
		slot("corner_BR", "corner", { hx - ci, zb + ci }, { -inward, inward });
		slot("gate_L", "gate", { -(gate + gi), zf - gi }, { 0.0f, -1.0f });
		slot("gate_R", "gate", { gate + gi, zf - gi }, { 0.0f, -1.0f });
		int n = 0;
		for (float z = zf - ci - 0.6f * y.SlotStep - 0.5f; z >= zb + ci + 0.6f * y.SlotStep; z -= y.SlotStep, ++n)
		{
			slot("wall_W" + std::to_string(n), "wall", { -hx + wi, z }, { 1.0f, 0.0f });
			slot("wall_E" + std::to_string(n), "wall", { hx - wi, z }, { -1.0f, 0.0f });
		}
		slot("house_W0", "lean_to", { -(fx + li), 0.0f }, { -1.0f, 0.0f });
		slot("house_E0", "lean_to", { fx + li, 0.0f }, { 1.0f, 0.0f });
		out.Valid = true;
		return out;
	}
}
