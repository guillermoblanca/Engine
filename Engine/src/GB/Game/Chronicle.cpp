#include "GB/Game/Chronicle.h"
#include <algorithm>
#include <cstdio>
#include <set>

namespace GB
{
	namespace
	{
		std::string Fixed(double v, int decimals = 0)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%.*f", decimals, v);
			return b;
		}

		std::string Clock(const ChronicleRow& r)
		{
			char b[48];
			std::snprintf(b, sizeof b, "day %d %02d:%02d", r.Day, static_cast<int>(r.Hour), static_cast<int>((r.Hour - static_cast<float>(static_cast<int>(r.Hour))) * 60.0f));
			return b;
		}

		std::string Top(const std::map<std::string, int>& store, size_t n)
		{
			std::vector<std::pair<std::string, int>> items(store.begin(), store.end());
			std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			std::string s;
			for (size_t i = 0; i < items.size() && i < n; ++i) s += (s.empty() ? "" : ", ") + items[i].first + " " + std::to_string(items[i].second);
			return s.empty() ? "empty" : s;
		}

		bool Key(const std::string& text)
		{
			for (const char* k : { "dies", "is born", "takes up the trade", "finishes", "lights", "tradition", "learns", "agrees", "expedition", "sets off", "starv", "hungry", "builds", "moves", "raid" })
				if (text.find(k) != std::string::npos) return true;
			return false;
		}
	}

	void Chronicle::Clear()
	{
		m_Rows.clear();
		m_Events.clear();
		m_Next = 0.0;
	}

	void Chronicle::Add(ChronicleRow row)
	{
		m_Next = row.Time + std::max(Every, 1.0);
		if (m_Rows.size() >= MaxRows) m_Rows.erase(m_Rows.begin(), m_Rows.begin() + static_cast<std::ptrdiff_t>(MaxRows / 4));
		m_Rows.push_back(std::move(row));
	}

	void Chronicle::Event(double time, const std::string& who, const std::string& text)
	{
		if (!Enabled) return;
		if (m_Events.size() >= MaxEvents) m_Events.erase(m_Events.begin(), m_Events.begin() + static_cast<std::ptrdiff_t>(MaxEvents / 4));
		m_Events.push_back({ time, who, text });
	}

	std::string Chronicle::Summary() const
	{
		if (m_Rows.empty()) return "the chronicle is empty (it records while the clan lives: tribe chronicle report | csv | clear)";
		const ChronicleRow& r = m_Rows.back();
		return std::to_string(m_Rows.size()) + " rows, " + Clock(r) + ": " + std::to_string(r.Alive) + " alive, " + std::to_string(r.Dead) + " dead, tier " + r.Tier + ", "
			+ Fixed(static_cast<double>(r.FoodDays), 1) + " days of food, " + std::to_string(m_Events.size()) + " events";
	}

	std::string Chronicle::Csv() const
	{
		std::string s = "time,day,hour,tier,food_days,alive,dead,fire,beds,buildings,member,role,job,detail,hunger,thirst,rest,warmth,health,dead_member\n";
		for (const ChronicleRow& r : m_Rows)
			for (const ChronicleMember& m : r.Members)
				s += Fixed(r.Time, 0) + "," + std::to_string(r.Day) + "," + Fixed(static_cast<double>(r.Hour), 2) + "," + r.Tier + "," + Fixed(static_cast<double>(r.FoodDays), 2) + ","
					+ std::to_string(r.Alive) + "," + std::to_string(r.Dead) + "," + (r.Fire ? "1" : "0") + "," + std::to_string(r.Beds) + "," + std::to_string(r.Buildings) + ","
					+ m.Name + "," + m.Role + "," + m.Job + "," + m.Detail + "," + Fixed(static_cast<double>(m.Hunger), 0) + "," + Fixed(static_cast<double>(m.Thirst), 0) + ","
					+ Fixed(static_cast<double>(m.Rest), 0) + "," + Fixed(static_cast<double>(m.Warmth), 0) + "," + Fixed(static_cast<double>(m.Health), 0) + "," + (m.Dead ? "1" : "0") + "\n";
		return s;
	}

	std::string Chronicle::Markdown(const std::string& title) const
	{
		std::string s = "# " + title + "\n\n";
		if (m_Rows.empty()) return s + "Nothing recorded yet.\n";
		const ChronicleRow& first = m_Rows.front();
		s += Summary() + ".\n\n";

		// ---- milestones: the first time each thing was so
		s += "## Milestones\n\n";
		auto firstWhere = [&](auto pred) -> const ChronicleRow* { for (const ChronicleRow& r : m_Rows) if (pred(r)) return &r; return nullptr; };
		auto milestone = [&](const std::string& what, const ChronicleRow* r) { s += "- " + what + ": " + (r ? Clock(*r) : std::string("never")) + "\n"; };
		milestone("food for a day and a half in hand", firstWhere([](const ChronicleRow& r) { return r.FoodDays >= 1.5f; }));
		milestone("campfire lit", firstWhere([](const ChronicleRow& r) { return r.Fire; }));
		milestone("first roof (a bed)", firstWhere([](const ChronicleRow& r) { return r.Beds > 0; }));
		milestone("first building done", firstWhere([](const ChronicleRow& r) { return r.Buildings > 0; }));
		milestone("every need covered (tier growth)", firstWhere([](const ChronicleRow& r) { return r.Tier == "growth"; }));
		milestone("first death", firstWhere([&first](const ChronicleRow& r) { return r.Dead > first.Dead; }));
		milestone("first time food ran short (tier survival)", firstWhere([](const ChronicleRow& r) { return r.Tier == "survival"; }));

		// ---- the clan day by day
		s += "\n## Day by day\n\n| day | alive | dead | tier (most of the day) | food days (min - max) | fire | beds | buildings | the store at the end of the day |\n|---|---|---|---|---|---|---|---|---|\n";
		for (size_t i = 0; i < m_Rows.size();)
		{
			size_t j = i;
			while (j < m_Rows.size() && m_Rows[j].Day == m_Rows[i].Day) ++j;
			std::map<std::string, int> tiers;
			float lo = 1e9f, hi = -1.0f;
			for (size_t k = i; k < j; ++k) { ++tiers[m_Rows[k].Tier]; lo = std::min(lo, m_Rows[k].FoodDays); hi = std::max(hi, m_Rows[k].FoodDays); }
			std::string tier;
			int most = 0;
			for (const auto& [t, n] : tiers) if (n > most) { most = n; tier = t; }
			const ChronicleRow& end = m_Rows[j - 1];
			s += "| " + std::to_string(m_Rows[i].Day) + " | " + std::to_string(end.Alive) + " | " + std::to_string(end.Dead) + " | " + tier + " | " + Fixed(static_cast<double>(lo), 1) + " - "
				+ Fixed(static_cast<double>(hi), 1) + " | " + (end.Fire ? "yes" : "no") + " | " + std::to_string(end.Beds) + " | " + std::to_string(end.Buildings) + " | " + Top(end.Store, 5) + " |\n";
			i = j;
		}

		// ---- each member
		std::vector<std::string> names;
		for (const ChronicleRow& r : m_Rows)
			for (const ChronicleMember& m : r.Members)
				if (std::find(names.begin(), names.end(), m.Name) == names.end()) names.push_back(m.Name);
		s += "\n## Members\n";
		for (const std::string& name : names)
		{
			std::map<std::string, int> jobs;
			int rows = 0;
			float minHunger = 1e9f, minThirst = 1e9f, minRest = 1e9f, sumHunger = 0.0f;
			int hungerRows = 0;
			const ChronicleRow* from = nullptr;
			const ChronicleRow* to = nullptr;
			const ChronicleRow* hungry = nullptr;   // the first time it was below 30
			const ChronicleRow* died = nullptr;
			std::string role;
			for (const ChronicleRow& r : m_Rows)
				for (const ChronicleMember& m : r.Members)
				{
					if (m.Name != name) continue;
					if (!from) from = &r;
					to = &r;
					role = m.Role;
					if (m.Dead) { if (!died) died = &r; continue; }
					++rows;
					++jobs[m.Job.empty() ? "idle" : m.Job];
					if (m.Hunger >= 0.0f) { minHunger = std::min(minHunger, m.Hunger); sumHunger += m.Hunger; ++hungerRows; if (m.Hunger < 30.0f && !hungry) hungry = &r; }
					if (m.Thirst >= 0.0f) minThirst = std::min(minThirst, m.Thirst);
					if (m.Rest >= 0.0f) minRest = std::min(minRest, m.Rest);
				}
			s += "\n### " + name + " (" + role + ")\n\n";
			s += "- in the chronicle from " + Clock(*from) + " to " + Clock(*to) + (died ? "; **dead by " + Clock(*died) + "**" : std::string("; alive at the end")) + "\n";
			std::vector<std::pair<std::string, int>> share(jobs.begin(), jobs.end());
			std::stable_sort(share.begin(), share.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			std::string doing;
			for (size_t i = 0; i < share.size() && i < 6; ++i) doing += (doing.empty() ? "" : ", ") + share[i].first + " " + Fixed(100.0 * share[i].second / std::max(rows, 1), 0) + "%";
			s += "- what it did (share of its time): " + (doing.empty() ? std::string("nothing recorded") : doing) + "\n";
			if (hungerRows)
				s += "- hunger: lowest " + Fixed(static_cast<double>(minHunger), 0) + ", average " + Fixed(static_cast<double>(sumHunger) / hungerRows, 0) + ", thirst lowest " + Fixed(static_cast<double>(minThirst), 0)
					+ ", rest lowest " + Fixed(static_cast<double>(minRest), 0) + (hungry ? "; first below 30 hunger at " + Clock(*hungry) : std::string()) + "\n";
			int shown = 0;
			for (const ChronicleEvent& e : m_Events)
			{
				if (e.Who != name || !Key(e.Text) || shown >= 12) continue;
				char t[32];
				std::snprintf(t, sizeof t, "%.0f s", e.Time);
				s += std::string("  - ") + t + ": " + e.Text + "\n";
				++shown;
			}
		}
		return s;
	}
}
