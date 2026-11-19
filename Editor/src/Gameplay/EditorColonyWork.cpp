// T306 + T307 (C2, documentation/01_diseno/MODO_COLONIA.md §4.3): the commands of the tabs Work and Stock (the tabs are prog-ui's, T308; they
// only queue these). The tables and the goals are GB's (GB/Game/WorkPriorities.h, GB/Game/StockGoals.h), of the player's clan (HandClan).
//   game priority                                    every person's table, one per part
//   game priority <person>                           that person's table
//   game priority <person|sel|all> <category|job> <1-10|->   sets a number (sel: the person selected; all: everyone of the clan)
//   game priority <person|sel|all> reset             back to what its trade suggests
//   game stock                                       the goals: "have / goal status"
//   game stock <item|tag> <n>  |  game stock <item|tag> off
// T324 (ARBOL_TECNICO §11.2.4): the research queue (GB/Game/ResearchQueue.h); the tab Knowledge (ColonyTabs.cpp) only queues these.
//   game research                                    the queue, its progress and what the clan can research now
//   game research add|remove|top <tech>  |  game research clear
#include "EditorApp.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace GB::Editor
{
	CommandResult EditorApp::PriorityCommand(const std::vector<std::string>& a)
	{
		const Clan* c = m_Clans.Find(HandClan());
		if (!c || !c->Folk) return { false, "no clan of yours" };
		Tribe& t = *c->Folk;
		WorkPriorities& table = t.WorkTable();
		if (t.Config.Work.Categories.Empty()) return { false, "the data has no categories of work (work.categories)" };
		if (a.empty())
		{
			std::string s;
			for (const TribeMember& m : t.Members()) if (!m.Dead) s += (s.empty() ? "" : " | ") + table.Show(t, m.Name);
			return { true, s.empty() ? std::string("nobody in the clan") : s };
		}
		// who: a name, the person selected, or everyone
		std::vector<std::string> who;
		if (a[0] == "all") { for (const TribeMember& m : t.Members()) if (!m.Dead) who.push_back(m.Name); }
		else if (a[0] == "sel")
		{
			const std::string& sel = !m_Game.Selected.empty() ? m_Game.Selected : m_FreeSelected;
			if (sel.empty() || !t.FindMember(sel)) return { false, "the selection is nobody of your clan" };
			who.push_back(sel);
		}
		else
		{
			const TribeMember* m = t.FindMember(a[0]);
			if (!m || m->Dead) return { false, "there is nobody called " + a[0] + " in your clan" };
			who.push_back(a[0]);
		}
		if (a.size() == 1)
		{
			std::string s;
			for (const std::string& n : who) s += (s.empty() ? "" : " | ") + table.Show(t, n);
			return { true, s };
		}
		if (a[1] == "reset")
		{
			std::string s;
			for (const std::string& n : who) { const WorkPriorities::Reply r = table.Reset(t, n); if (!r.Ok) return { false, r.Text }; s += (s.empty() ? "" : "; ") + r.Text; }
			return { true, s };
		}
		int number = 0;
		if (a.size() != 3 || !WorkPriorities::Parse(a[2], number))
			return { false, "usage: game priority [<person>|sel|all] [<category|job> <1-10|->] | game priority <person|sel|all> reset" };
		if (who.size() == 1)
		{
			const WorkPriorities::Reply r = table.Set(t, who.front(), a[1], number);
			return { r.Ok, r.Text };
		}
		for (const std::string& n : who)
			if (const WorkPriorities::Reply r = table.Set(t, n, a[1], number); !r.Ok) return { false, r.Text };
		return { true, "all " + std::to_string(who.size()) + ": " + a[1] + " " + WorkPriorities::Text(number) };
	}

	CommandResult EditorApp::StockCommand(const std::vector<std::string>& a)
	{
		const Clan* c = m_Clans.Find(HandClan());
		if (!c || !c->Folk) return { false, "no clan of yours" };
		Tribe& t = *c->Folk;
		StockGoals& goals = t.Goals();
		if (a.empty()) return { true, goals.List(t) };
		if (a.size() != 2) return { false, "usage: game stock [<item|tag> <n>|off]" };
		if (a[1] == "off") { const StockGoals::Reply r = goals.Off(a[0]); return { r.Ok, r.Text }; }
		char* end = nullptr;
		const long n = std::strtol(a[1].c_str(), &end, 10);
		if (end == a[1].c_str() || *end != '\0') return { false, "usage: game stock [<item|tag> <n>|off]" };
		const StockGoals::Reply r = goals.Set(t, a[0], static_cast<int>(n));
		return { r.Ok, r.Text };
	}

	CommandResult EditorApp::ResearchCommand(const std::vector<std::string>& a)
	{
		const Clan* c = m_Clans.Find(HandClan());
		if (!c || !c->Folk) return { false, "no clan of yours" };
		Tribe& t = *c->Folk;
		ResearchQueue& queue = t.Research();
		const char* usage = "usage: game research [add|remove|top <tech> | clear]";
		if (a.empty()) return { true, queue.List(t) };
		if (a[0] == "clear" && a.size() == 1) { const ResearchQueue::Reply r = queue.Clear(t); return { r.Ok, r.Text }; }
		if (a.size() != 2) return { false, usage };
		const ResearchQueue::Reply r = a[0] == "add" ? queue.Add(t, a[1]) : a[0] == "remove" ? queue.Remove(t, a[1]) : a[0] == "top" ? queue.Top(t, a[1])
			: ResearchQueue::Reply{ false, usage };
		return { r.Ok, r.Text };
	}
}
