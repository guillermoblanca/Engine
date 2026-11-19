// The editor's side of H18: the `economy` and `field` commands over the tribe's economy (production chains, spoilage, stores and stockpiles,
// hauling, fields). The state lives in the tribe and is saved with it (the "ai" section); nothing here is kept.
#include "EditorApp.h"
#include "GB/World/TilledGround.h"
#include <cstdlib>

namespace GB::Editor
{
	namespace
	{
		std::string OneLine(std::string s)
		{
			while (!s.empty() && s.back() == '\n') s.pop_back();
			for (char& c : s) if (c == '\n') c = '|';   // one line: the console shows the first line of a message
			return s;
		}
	}

	CommandResult EditorApp::EconomyCommand(const std::vector<std::string>& a)
	{
		if (m_Tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status")
		{
			std::string s = m_Tribe.EconomyReport();
			if (const std::string problems = CheckEconomy(m_Tribe.Config.Econ, m_Items); !problems.empty()) s += "content: " + problems;
			return { true, OneLine(s) };
		}
		if (sub == "plan") return { true, OneLine(PlanReport(m_Tribe.PlanNow())) };
		if (sub == "give" && a.size() > 1)
		{
			const int n = a.size() > 2 ? std::atoi(a[2].c_str()) : 1;
			if (!m_Items.Find(a[1])) return { false, "no item '" + a[1] + "'" };
			const int put = m_Tribe.Store().Add(m_Items, a[1], std::max(1, n));
			return { put > 0, std::to_string(put) + " " + a[1] + " in the main store" };
		}
		if (sub == "stockpile" && a.size() > 1)
		{
			glm::vec2 at = m_Tribe.Config.Economy.Camp + glm::vec2(6.0f, -6.0f);
			std::string kind = "stockpile";
			if (a.size() > 3) at = { static_cast<float>(std::atof(a[2].c_str())), static_cast<float>(std::atof(a[3].c_str())) };
			if (a.size() > 4) kind = a[4];
			if (!m_Tribe.AddStockpile(a[1], at, kind)) return { false, "cannot put stockpile '" + a[1] + "' (the name is taken or there is no store kind '" + kind + "' in the economy section)" };
			return { true, "stockpile " + a[1] + " (" + kind + ") at " + std::to_string(static_cast<int>(at.x)) + ", " + std::to_string(static_cast<int>(at.y)) };
		}
		if (sub == "demand" && a.size() > 3)
		{
			m_Tribe.SetDemand(a[1], a[2], std::atoi(a[3].c_str()));
			return { true, a[1] + " now asks for " + a[3] + " " + a[2] };
		}
		return { false, "usage: economy status | plan | give <item> [n] | stockpile <name> [x z] [kind] | demand <source> <item> <n>" };
	}

	CommandResult EditorApp::FieldCommand(const std::vector<std::string>& a)
	{
		if (m_Tribe.Members().empty()) return { false, "no tribe (tribe build [model.fbx])" };
		const std::string sub = a.empty() ? "status" : a[0];
		if (sub == "status") return { true, OneLine(m_Tribe.FieldReport()) };
		if (sub == "mark")
		{
			glm::vec2 at = m_Tribe.Config.Economy.Camp + m_Tribe.Config.Econ.Farming.Offset;
			if (a.size() > 2) at = { static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())) };
			return { true, OneLine(m_Tribe.MarkField(at).Report()) };
		}
		if (sub == "probe" && a.size() >= 3)   // T332: how far inside each field's drawn edge a place is (TilledInside)
		{
			const glm::vec2 p(static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())));
			std::string out;
			for (const auto& c : m_Clans.All())
				if (c->Folk)
					for (const Field& f : c->Folk->Economy().Fields)
					{
						TilledShape shape;
						shape.Origin = f.Origin; shape.Columns = std::max(1, f.Columns); shape.Spacing = f.Spacing; shape.Plots = static_cast<int>(f.Plots.size()); shape.Cells = f.Cells;
						if (const FarmZones::Zone* z = c->Folk->Zones().Of(f.Name)) shape.Outline = z->Outline;
						out += f.Name + ": " + std::to_string(TilledInside(shape, m_Visuals.Library.Fields, TilledSeed(f.Name, f.Origin), p)) + " m inside (outline " + std::to_string(shape.Outline.size()) + " points); ";
					}
			return { true, out.empty() ? std::string("no fields") : out };
		}
		if (sub == "set" && a.size() >= 4)   // T332: a debug hand on the plots, for the captures of the fields (not saved apart: the plots are)
		{
			std::string name = a[1];
			for (char& c : name) if (c == '_') c = ' ';
			Field* field = nullptr;
			auto look = [&](Tribe& t) { for (Field& f : t.Economy().Fields) if (!field && f.Name == name) field = &f; };
			for (const auto& c : m_Clans.All()) if (c->Folk) look(*c->Folk);
			look(m_Tribe);
			if (!field) return { false, "no field '" + name + "'" };
			size_t from = 0, to = field->Plots.size();
			if (a[2] != "all")
			{
				const size_t dash = a[2].find('-');
				from = static_cast<size_t>(std::atoi(a[2].substr(0, dash).c_str()));
				to = dash == std::string::npos ? from + 1 : static_cast<size_t>(std::atoi(a[2].substr(dash + 1).c_str())) + 1;
			}
			const std::string state = a[3];
			if (state != "grass" && state != "dug" && state != "sown" && state != "sprout" && state != "grown") return { false, "state: grass, dug, sown, sprout or grown" };
			const std::string crop = a.size() > 4 && state != "grass" && state != "dug" ? a[4] : std::string();
			const float growth = a.size() > 5 ? static_cast<float>(std::atof(a[5].c_str())) : 1.0f;
			const float moisture = a.size() > 6 ? static_cast<float>(std::atof(a[6].c_str())) : 0.0f;
			int n = 0;
			for (size_t i = from; i < to && i < field->Plots.size(); ++i, ++n) field->Plots[i] = { state, crop, growth, moisture };
			return { true, std::to_string(n) + " plots of " + name + " now " + state + (crop.empty() ? "" : " " + crop) };
		}
		return { false, "usage: field status | mark [x z] | probe <x> <z> | set <field> <all|i|i-j> <grass|dug|sown|sprout|grown> [crop] [growth 0..1] [moisture seconds]" };
	}
}
