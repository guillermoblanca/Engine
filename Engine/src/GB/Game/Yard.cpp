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
		{
			if (!f->is_object()) return "yards.fence: expected an object";
			YardFenceLook& l = r.Fence;
			if (!Number(*f, "postSpacing", l.PostSpacing, 0.3f, 10.0f, error, "yards.fence") || !Number(*f, "postHeight", l.PostHeight, 0.1f, 5.0f, error, "yards.fence")
				|| !Number(*f, "postWidth", l.PostWidth, 0.01f, 1.0f, error, "yards.fence") || !Number(*f, "gatePostHeight", l.GatePostHeight, 0.1f, 5.0f, error, "yards.fence")
				|| !Number(*f, "gatePostWidth", l.GatePostWidth, 0.01f, 1.0f, error, "yards.fence") || !Number(*f, "railWidth", l.RailWidth, 0.01f, 1.0f, error, "yards.fence")
				|| !Colour(*f, "postColor", l.PostColor, error, "yards.fence") || !Colour(*f, "railColor", l.RailColor, error, "yards.fence")) return error;
			if (const auto rails = f->find("rails"); rails != f->end())
			{
				if (!rails->is_array()) return "yards.fence.rails: expected a list of heights";
				l.Rails.clear();
				for (const Json& h : *rails)
				{
					if (!h.is_number() || h.get<float>() <= 0.0f || h.get<float>() > 5.0f) return "yards.fence.rails: heights from 0 to 5 m";
					l.Rails.push_back(h.get<float>());
				}
			}
		}
		live = std::move(r);
		return {};
	}

	const YardRules& DefaultYardRules()
	{
		static const YardRules rules = ShippedSection<YardRules>([](const std::string& doc, YardRules& r) { return LoadYardRules(doc, r); });
		return rules;
	}

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
