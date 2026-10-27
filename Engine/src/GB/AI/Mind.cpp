#include "GB/AI/Mind.h"
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	std::string AgentMind::ToJson() const
	{
		Json j;
		j["memory"] = Json::parse(Memory.ToJson());
		j["kind"] = Current.Kind;
		j["target"] = Current.Target;
		j["since"] = Current.Since;
		j["hold"] = Current.Hold;
		j["fx"] = Facing.x;
		j["fz"] = Facing.y;
		j["lx"] = LastPos.x;
		j["lz"] = LastPos.y;
		j["has"] = HasLast ? 1 : 0;
		j["scan"] = ScanAngle;
		j["heard"] = HeardSeq;
		j["explore"] = ExploreCount;
		j["orders"] = Orders;
		j["next"] = NextThink;
		j["shared"] = SharedAt;
		j["expired"] = ExpiredAt;
		return j.dump();
	}

	bool AgentMind::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("memory")) return false;
		AgentMind m;
		if (!m.Memory.FromJson(j["memory"].dump())) return false;
		m.Current.Kind = j.value("kind", std::string());
		m.Current.Target = j.value("target", std::string());
		m.Current.Since = j.value("since", 0.0);
		m.Current.Hold = j.value("hold", 0.0f);
		m.Facing = { j.value("fx", 0.0f), j.value("fz", 1.0f) };
		m.LastPos = { j.value("lx", 0.0f), j.value("lz", 0.0f) };
		m.HasLast = j.value("has", 0) != 0;
		m.ScanAngle = j.value("scan", 0.0f);
		m.HeardSeq = j.value("heard", static_cast<uint64_t>(0));
		m.ExploreCount = j.value("explore", 0);
		m.Orders = j.value("orders", std::string());
		m.NextThink = j.value("next", 0.0);
		m.SharedAt = j.value("shared", -1e9);
		m.ExpiredAt = j.value("expired", -1e9);
		*this = std::move(m);
		return true;
	}
}
