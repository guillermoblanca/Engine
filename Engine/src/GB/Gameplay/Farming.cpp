#include "GB/Gameplay/Farming.h"
#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	bool CropDef::SowsIn(Season s) const
	{
		if (Sow.empty()) return s != Season::Winter;
		const std::string name = SeasonClock::Name(s);
		return std::find(Sow.begin(), Sow.end(), name) != Sow.end();
	}

	Field::Field(const std::string& name, const glm::vec2& origin, int plots, int columns, float spacing)
		: Name(name), Origin(origin), Columns(std::max(1, columns)), Spacing(spacing), Plots(static_cast<size_t>(std::max(0, plots)))
	{
	}

	glm::vec2 Field::PlotPos(size_t i) const
	{
		const int c = std::max(1, Columns);
		return Origin + glm::vec2(static_cast<float>(static_cast<int>(i) % c), static_cast<float>(static_cast<int>(i) / c)) * Spacing;
	}

	glm::vec2 Field::Centre() const
	{
		if (Plots.empty()) return Origin;
		return (PlotPos(0) + PlotPos(Plots.size() - 1)) * 0.5f;
	}

	FieldEvents Field::Update(float dt, Season season, bool rain, float moistureSeconds, const std::map<std::string, CropDef>& crops)
	{
		FieldEvents ev;
		const float rate = SeasonClock::GrowthRate(season);
		for (FieldPlot& p : Plots)
		{
			const bool growing = p.State == "sown" || p.State == "sprout" || p.State == "grown";
			if (season == Season::Winter && growing)
			{
				// the frost: what is in the ground (or ripe and left there) is lost; the soil stays dug for the spring
				p = FieldPlot{ "dug", "", 0.0f, 0.0f };
				++ev.Ruined;
				continue;
			}
			if (rain && p.State != "grass") p.Moisture = std::max(p.Moisture, moistureSeconds);
			if (p.State != "sown" && p.State != "sprout") { p.Moisture = std::max(0.0f, p.Moisture - dt); continue; }
			const auto crop = crops.find(p.Crop);
			if (crop == crops.end()) continue;
			if (p.Moisture > 0.0f)
			{
				p.Growth += dt * rate / std::max(crop->second.StageSeconds, 0.01f);
				if (p.Growth >= 1.0f)
				{
					p.Growth = 0.0f;
					if (p.State == "sown") { p.State = "sprout"; ++ev.Sprouted; }
					else { p.State = "grown"; ++ev.Ripened; }
				}
			}
			p.Moisture = std::max(0.0f, p.Moisture - dt);
		}
		return ev;
	}

	int Field::Count(const std::string& state) const
	{
		return static_cast<int>(std::count_if(Plots.begin(), Plots.end(), [&](const FieldPlot& p) { return p.State == state; }));
	}

	std::vector<size_t> Field::Find(const std::string& state, size_t most) const
	{
		std::vector<size_t> out;
		for (size_t i = 0; i < Plots.size() && out.size() < most; ++i) if (Plots[i].State == state) out.push_back(i);
		return out;
	}

	std::vector<size_t> Field::Dry(size_t most) const
	{
		std::vector<size_t> out;
		for (size_t i = 0; i < Plots.size() && out.size() < most; ++i)
			if ((Plots[i].State == "sown" || Plots[i].State == "sprout") && Plots[i].Moisture <= 0.0f) out.push_back(i);
		return out;
	}

	std::string Field::Report() const
	{
		char head[96];
		std::snprintf(head, sizeof head, "%s at %.0f, %.0f: %d plots:", Name.c_str(), static_cast<double>(Origin.x), static_cast<double>(Origin.y), static_cast<int>(Plots.size()));
		std::string s = head;
		for (const char* state : { "grass", "dug", "sown", "sprout", "grown" })
		{
			const int n = Count(state);
			if (!n) continue;
			s += " " + std::to_string(n) + " " + state;
			std::string crops;
			for (const FieldPlot& p : Plots)
				if (p.State == state && !p.Crop.empty() && crops.find(p.Crop) == std::string::npos) crops += (crops.empty() ? "" : ", ") + p.Crop;
			if (!crops.empty()) s += " (" + crops + ")";
			s += ",";
		}
		if (s.back() == ',') s.pop_back();
		int dry = static_cast<int>(Dry(Plots.size()).size());
		if (dry) s += "; " + std::to_string(dry) + " need water";
		return s;
	}

	std::string FieldsToJson(const std::vector<Field>& fields)
	{
		Json a = Json::array();
		for (const Field& f : fields)
		{
			Json plots = Json::array();
			for (const FieldPlot& p : f.Plots) plots.push_back(Json::array({ p.State, p.Crop, p.Growth, p.Moisture }));
			a.push_back(Json{ { "name", f.Name }, { "origin", Json::array({ f.Origin.x, f.Origin.y }) }, { "columns", f.Columns }, { "spacing", f.Spacing }, { "plots", plots } });
		}
		return a.dump();
	}

	bool FieldsFromJson(const std::string& json, std::vector<Field>& out)
	{
		const Json a = Json::parse(json, nullptr, false);
		if (a.is_discarded() || !a.is_array()) return false;
		std::vector<Field> fields;
		try
		{
			for (const Json& o : a)
			{
				if (!o.is_object() || !o.contains("plots") || !o["plots"].is_array() || !o.contains("origin") || !o["origin"].is_array() || o["origin"].size() != 2) return false;
				Field f;
				f.Name = o.value("name", std::string("field"));
				f.Origin = { o["origin"][0].get<float>(), o["origin"][1].get<float>() };
				f.Columns = std::max(1, o.value("columns", 3));
				f.Spacing = o.value("spacing", 2.0f);
				for (const Json& p : o["plots"])
				{
					if (!p.is_array() || p.size() != 4) return false;
					FieldPlot plot{ p[0].get<std::string>(), p[1].get<std::string>(), p[2].get<float>(), p[3].get<float>() };
					if (plot.State != "grass" && plot.State != "dug" && plot.State != "sown" && plot.State != "sprout" && plot.State != "grown") return false;
					f.Plots.push_back(plot);
				}
				fields.push_back(std::move(f));
			}
		}
		catch (const Json::exception&)
		{
			return false;
		}
		out = std::move(fields);
		return true;
	}
}
