#include "EditorFarmZones.h"
#include "GB/Game/Tribe.h"
#include <cstdlib>

namespace GB::Editor
{
	CommandResult FarmZoneCommand(Tribe* t, const std::string& sub, const std::vector<std::string>& a)
	{
		static const char* kUsage = "usage: game zone farm <x0> <z0> <x1> <z1> [crop] | game zone farm poly <x0> <z0> <x1> <z1> <x2> <z2> ... [crop] | game zone <id> crop <crop|any> | plan <sow|harvest|fallow> | auto on|off | remove | game zones";
		if (!t) return { false, "no clan of yours" };
		FarmZones& zones = t->Zones();
		if (sub == "zones" || a.empty()) return { true, zones.List(*t) };
		if (a[0] == "farm" && a.size() > 1 && a[1] == "poly")
		{
			// x z pairs, then an optional crop (a word that is no number)
			std::vector<std::string> w(a.begin() + 2, a.end());
			std::string crop;
			if (!w.empty())
			{
				char* end = nullptr;
				std::strtof(w.back().c_str(), &end);
				if (!end || *end != '\0') { crop = w.back(); w.pop_back(); }
			}
			if (w.size() < 6 || w.size() % 2) return { false, kUsage };
			std::vector<glm::vec2> points;
			for (size_t i = 0; i < w.size(); i += 2)
			{
				char* ex = nullptr;
				char* ez = nullptr;
				const float x = std::strtof(w[i].c_str(), &ex), z = std::strtof(w[i + 1].c_str(), &ez);
				if (!ex || *ex != '\0' || !ez || *ez != '\0') return { false, kUsage };
				points.push_back({ x, z });
			}
			const FarmZones::Reply r = zones.PaintOutline(*t, points, crop);
			return { r.Ok, r.Text };
		}
		if (a[0] == "farm")
		{
			if (a.size() != 5 && a.size() != 6) return { false, kUsage };
			float v[4];
			for (int i = 0; i < 4; ++i)
			{
				char* end = nullptr;
				v[i] = std::strtof(a[static_cast<size_t>(i) + 1].c_str(), &end);
				if (!end || *end != '\0') return { false, kUsage };
			}
			const FarmZones::Reply r = zones.Paint(*t, { v[0], v[1] }, { v[2], v[3] }, a.size() == 6 ? a[5] : std::string());
			return { r.Ok, r.Text };
		}
		if (a.size() == 2 && a[1] == "remove") { const FarmZones::Reply r = zones.Remove(*t, a[0]); return { r.Ok, r.Text }; }
		if (a.size() == 3 && a[1] == "crop") { const FarmZones::Reply r = zones.SetCrop(*t, a[0], a[2]); return { r.Ok, r.Text }; }
		if (a.size() == 3 && a[1] == "auto" && (a[2] == "on" || a[2] == "off")) { const FarmZones::Reply r = zones.SetAuto(*t, a[0], a[2] == "on"); return { r.Ok, r.Text }; }
		if (a.size() == 3 && a[1] == "plan") { const FarmZones::Reply r = zones.SetPlan(*t, a[0], a[2]); return { r.Ok, r.Text }; }
		if (a.size() == 1)
		{
			const Field* f = FarmZones::Find(*t, a[0]);
			if (!f) return { false, "the clan has no zone " + a[0] };
			const FarmZones::Zone* z = zones.Of(f->Name);
			return { true, f->Report() + (z ? " [crop " + (z->Auto ? std::string("auto") : z->Crop.empty() ? std::string("any") : z->Crop) + ", plan " + z->Plan + "]" : std::string(" [the clan's, auto]")) };
		}
		return { false, kUsage };
	}
}
