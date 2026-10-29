#include "GB/Game/Tribe.h"
#include "GB/Telemetry/TelemetrySink.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		// A map of numbers as the JSON object the analysis reads (rounded: the sink is for series, not for the last bit of a float).
		std::string NumbersJson(const std::map<std::string, float>& values)
		{
			Json j = Json::object();
			for (const auto& [k, v] : values) j[k] = std::round(static_cast<double>(v) * 1000.0) / 1000.0;
			return j.dump();
		}
	}

	void Tribe::SetTelemetry(TelemetrySink* sink, int clanId, double every)
	{
		m_Telemetry = sink;
		m_TelemetryClan = clanId;
		m_TelemetryEvery = every > 1.0 ? every : 1.0;
		m_TelemetryKnown.clear();
		m_TelemetryNext = m_Time;   // the first rows are written at once
		m_TelemetryRelationsNext = m_Time;
	}

	void Tribe::EmitEvent(const char* kind, const std::string& who, const std::string& other, const std::string& result, const std::string& text)
	{
		if (!m_Telemetry) return;
		TelemetryEvent e;
		e.Clan = m_TelemetryClan;
		e.Time = m_Time;
		e.Kind = kind;
		e.Who = who;
		e.Other = other;
		e.Result = result;
		e.Text = text;
		m_Telemetry->Event(e);
	}

	// A decision is written when what a member does changes (another kind or target than the last one it wrote), with the best six options it weighed.
	void Tribe::EmitDecision(const TribeMember& m, const std::vector<UtilityOption>& options, const UtilityOption& chosen)
	{
		const std::string now = chosen.Kind + "|" + chosen.Target;
		std::string& last = m_TelemetryLast[m.Name];
		if (last == now) return;
		TelemetryDecision d;
		d.Clan = m_TelemetryClan;
		d.Time = m_Time;
		d.Member = m.Name;
		d.Chosen = chosen.Kind;
		d.Target = chosen.Target;
		d.Previous = last;
		last = now;
		std::vector<const UtilityOption*> best;
		for (const UtilityOption& o : options) best.push_back(&o);
		std::sort(best.begin(), best.end(), [](const UtilityOption* a, const UtilityOption* b) { return a->Score > b->Score; });
		if (best.size() > 6) best.resize(6);
		Json list = Json::array();
		for (const UtilityOption* o : best)
		{
			Json factors = Json::object();
			for (const ScoreFactor& f : o->Factors) factors[f.Name] = std::round(static_cast<double>(f.Value) * 1000.0) / 1000.0;
			list.push_back({ { "kind", o->Kind }, { "target", o->Target }, { "score", std::round(static_cast<double>(o->Score) * 1000.0) / 1000.0 }, { "factors", factors } });
		}
		d.Options = list.dump();
		m_Telemetry->Decision(d);
	}

	// One row per member (and its sheet the first time it is seen) and one for the clan. Only reads the simulation, never changes it.
	void Tribe::EmitTelemetry()
	{
		m_TelemetryNext = m_Time + m_TelemetryEvery;
		const double day = std::max(1.0, static_cast<double>(Config.Needs.DaySeconds));
		const int dayNo = static_cast<int>((m_Time / day) + static_cast<double>(Config.Needs.StartHour) / 24.0) + 1;
		const double hour = static_cast<double>(Hour());

		TelemetryClanSample clan;
		clan.Clan = m_TelemetryClan;
		clan.Time = m_Time;
		clan.Day = dayNo;
		clan.Hour = hour;
		clan.Tier = MaslowTier();
		clan.FoodDays = static_cast<double>(FoodDays());
		clan.Fire = FireBurning();
		clan.Beds = Beds();
		clan.Buildings = BuildingCount();
		Json store = Json::object();
		for (const std::string& id : m_Items->Ids())
			if (const int n = m_Store.Count(id)) store[id] = n;

		for (const TribeMember& m : m_Members)
		{
			const Person* person = m_Society.Find(m.Name);
			if (m_TelemetryKnown.insert(m.Name).second)
			{
				TelemetryIndividual who;
				who.Clan = m_TelemetryClan;
				who.Member = m.Name;
				who.Role = m.Role;
				who.Joined = m_Time;
				if (person)
				{
					who.Female = person->Female;
					who.Mother = person->Mother;
					who.Father = person->Father;
					who.Generation = person->Generation;
					who.Genes = NumbersJson(person->Genes);
					who.Traits = NumbersJson(person->Traits);
				}
				m_Telemetry->Individual(who);
			}
			TelemetrySample s;
			s.Clan = m_TelemetryClan;
			s.Member = m.Name;
			s.Role = m.Role;
			s.Job = m.Job;
			s.Detail = m.Detail;
			s.Time = m_Time;
			s.Day = dayNo;
			s.Hour = hour;
			s.Alive = !m.Dead;
			(m.Dead ? clan.Dead : clan.Alive) += 1;
			if (m.Body)
			{
				const glm::vec2 at = Position(m);
				s.X = static_cast<double>(at.x);
				s.Z = static_cast<double>(at.y);
				if (m_Regions) s.Region = m_Regions->RegionAt(at);
				std::map<std::string, float> bag;
				for (const ItemSlot& slot : m.Body->Bag().Stacks()) bag[slot.Id] += static_cast<float>(slot.Count);
				s.Bag = NumbersJson(bag);
			}
			const std::map<std::string, float> needs = NeedLevels(m);
			auto level = [&](const char* n) { const auto it = needs.find(n); return it == needs.end() ? -1.0 : static_cast<double>(it->second); };
			s.Hunger = level(NeedName::Hunger);
			s.Thirst = level(NeedName::Thirst);
			s.Rest = level(NeedName::Rest);
			s.Warmth = level(NeedName::Warmth);
			s.Health = level(NeedName::Health);
			if (person) s.Age = static_cast<double>(person->Age);
			s.Skills = NumbersJson(m.Skills);
			m_Telemetry->Sample(s);
		}
		if (m_Time >= m_TelemetryRelationsNext)   // the bonds that matter (|bond| over 0.05), each pair once
		{
			m_TelemetryRelationsNext = m_Time + day;
			for (const TribeMember& m : m_Members)
				for (const auto& [other, bond] : m_Society.BondsOf(m.Name))
				{
					if (m.Name >= other || std::fabs(bond) < 0.05f) continue;
					TelemetryRelation r;
					r.Clan = m_TelemetryClan;
					r.Time = m_Time;
					r.A = m.Name;
					r.B = other;
					r.Kin = m_Society.Kin(m.Name, other);
					r.Bond = static_cast<double>(bond);
					r.Affinity = static_cast<double>(m_Society.Affinity(Config.Society, m.Name, other));
					m_Telemetry->Relation(r);
				}
		}
		clan.Store = store.dump();
		m_Telemetry->ClanSample(clan);
	}
}
