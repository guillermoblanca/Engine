// PROTOTYPE (2026-11-20) of TALLERES.md section 10 (modules in a house's yard) and TL4 (which recipes each building makes), to try in the colony.
// The commands of the player's clan (HandClan); the rules are GB's (GB/Game/HouseModules.h, GB/Game/RecipeChoices.h). The sheet's buttons only
// queue these (ColonyYard.cpp). Names with _ for their spaces ("Wattle_hut_1").
//   game module <house>                                the yard: its modules and its free slots
//   game module add <house> <module> [now]             the module's site in the first free slot of its kind ("now": standing at once)
//   game module remove <house> <module|site|slot>      pulls it down (its materials back to the store)
//   game recipe <building>                             its recipes: auto, off or keep n
//   game recipe <building> <recipe> on|off|keep <n>
#include "EditorApp.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace GB::Editor
{
	void ToggleYardMenu(const std::string& host);   // (ColonyYard.cpp: the UI's list of modules, open or closed)

	namespace
	{
		std::string Unscored(std::string s)
		{
			for (char& c : s) if (c == '_') c = ' ';
			return s;
		}
	}

	CommandResult EditorApp::ModuleCommand(const std::string& sub, const std::vector<std::string>& a)
	{
		const Clan* c = m_Clans.Find(HandClan());
		if (!c || !c->Folk) return { false, "no clan of yours" };
		Tribe& t = *c->Folk;
		if (sub == "module")
		{
			const char* usage = "usage: game module <house> | add <house> <module> [now] | remove <house> <module|site|slot>";
			if (a.empty()) return { false, usage };
			if (a.size() == 1) { const std::string s = t.Modules().Status(t, Unscored(a[0])); return { s.rfind("no ", 0) != 0, s }; }
			if (a[0] == "menu" && a.size() == 2) { ToggleYardMenu(Unscored(a[1])); return { true, "the list of modules of " + Unscored(a[1]) + " opens or closes" }; }   // (as its click: the captures)
			if (a[0] == "add" && (a.size() == 3 || (a.size() == 4 && a[3] == "now")))
			{
				const HouseModules::Reply r = t.Modules().Add(t, Unscored(a[1]), a[2], a.size() == 4);
				if (r.Ok) SyncNewGameplay();   // (the site's system: as after RaiseBuilding)
				return { r.Ok, r.Text };
			}
			if (a[0] == "remove" && a.size() == 3)
			{
				HouseModules::Reply r = t.Modules().Remove(t, Unscored(a[1]), a[2]);
				if (!r.Ok) r = t.Modules().Remove(t, Unscored(a[1]), Unscored(a[2]));
				return { r.Ok, r.Text };
			}
			return { false, usage };
		}
		// recipe
		const char* usage = "usage: game recipe <building> [<recipe> on|off|keep <n>]";
		if (a.empty()) return { false, usage };
		const std::string building = Unscored(a[0]);
		if (a.size() == 1) { const std::string s = t.Choices().List(t, building); return { s.rfind("no ", 0) != 0, s }; }
		if (a.size() == 3 && (a[2] == "on" || a[2] == "auto" || a[2] == "off")) { const RecipeChoices::Reply r = t.Choices().Set(t, building, a[1], a[2]); return { r.Ok, r.Text }; }
		if (a.size() == 4 && a[2] == "keep")
		{
			char* end = nullptr;
			const long n = std::strtol(a[3].c_str(), &end, 10);
			if (end == a[3].c_str() || *end != '\0') return { false, usage };
			const RecipeChoices::Reply r = t.Choices().Set(t, building, a[1], "keep", static_cast<int>(n));
			return { r.Ok, r.Text };
		}
		return { false, usage };
	}
}
