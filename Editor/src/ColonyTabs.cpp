// T308 (C2-c, documentation/01_diseno/MODO_COLONIA.md §4.1 and §4.3): the colony's tabs Work and Stock, drawn over the god's bar ([Work] and
// [Stock] next to [Build] and [Research]), and the person's row of priorities in its sheet.
//   Work:  a person x category table of the clan's priorities (GB/Game/WorkPriorities.h). A category column unfolds into its jobs. A click on a
//          cell puts it before (a lower number), a right click after, the wheel either way; "-" is never. A person whose table the player never
//          touched decides as the clans of the AI: its row is dimmed and shows what its trade suggests. [As the AI] gives a table back.
//   Stock: the clan's goals "keep N of X" (GB/Game/StockGoals.h): what it has against what it wants, and what it is doing about it.
//   Knowledge (T324): the research queue (GB/Game/ResearchQueue.h): its entries with their progress (first, remove, drag to move) and what the
//          clan can research now to add, with the states of GB/Game/TechStatus.h (T341); what it cannot name is never named (ARBOL_TECNICO
//          §11.2.5), only "? N more". It queues `game research ...`.
// The UI decides nothing: every cell and button queues a `colony work ...` / `game stock ...` command, and `colony work step` runs the same
// code as the click (the tests drive it headless, as `colony radial do` does for the radial).
#include "EditorApp.h"
#include "GameTheme.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <imgui.h>
#include <map>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
		ImTextureID TextureId(const Texture2D& t) { return static_cast<ImTextureID>(static_cast<intptr_t>(t.GetID())); }

		// the icon of the atlas for a category or a job of era1.json's work.categories ("" = none: the label alone)
		const char* WorkIcon(const std::string& id)
		{
			static const std::pair<const char*, const char*> kIcons[] = {
				{ "food", "berry" }, { "construction", "build" }, { "crafting", "make" }, { "field", "till" }, { "materials", "chop" }, { "care", "care" },
				{ "knowledge", "experiment" }, { "hunt", "hunt" }, { "fish", "fish" }, { "forage", "gather" }, { "cook", "roast" }, { "preserve", "smoked_meat" },
				{ "build", "build" }, { "haul", "haul" }, { "repair", "build" }, { "pave", "stone" }, { "tools", "stone_axe" }, { "make", "make" },
				{ "till", "till" }, { "sow", "sow" }, { "tend", "tend" }, { "harvest", "harvest" }, { "wood", "chop" }, { "stone", "mine" }, { "clay", "clay" },
				{ "fiber", "fiber" }, { "other", "collect" }, { "heal", "care" }, { "fire", "fire" }, { "research", "experiment" }, { "teach", "talk" },
			};
			for (const auto& [job, icon] : kIcons) if (id == job) return icon;
			return "";
		}

		// the number a table gives a category (its data default when the table has none) and a job (its own, else its category's)
		struct WorkCell { int Number = 0; bool Own = false; };
		WorkCell CategoryCell(const WorkPriorities::Table& tab, const WorkCategoryDef& c)
		{
			const auto it = tab.Categories.find(c.Id);
			return { it != tab.Categories.end() ? it->second : c.Default, true };
		}
		WorkCell JobCell(const WorkPriorities::Table& tab, const WorkCategoryDef& c, const std::string& job)
		{
			const WorkCell cat = CategoryCell(tab, c);
			if (cat.Number == WorkPriorities::kOff) return { WorkPriorities::kOff, false };   // (a category at "-" turns all its jobs off)
			const auto it = tab.Jobs.find(job);
			return it != tab.Jobs.end() ? WorkCell{ it->second, true } : WorkCell{ cat.Number, false };
		}

		// the family of aptitude a job trains (the skills' families of the data: the kind of its patterns, or the item tag they are about walked up
		// from the most specific: "gather:Item.Resource.Wood" -> "Resource.Wood" -> woodcutting); "" when none
		std::string JobFamily(const Tribe& t, const WorkJobDef& job)
		{
			const std::map<std::string, std::string>& families = t.Config.Work.Skills.Families;
			for (const std::string& pattern : job.Match)
			{
				const size_t colon = pattern.find(':');
				const std::string kind = pattern.substr(0, colon);
				if (colon != std::string::npos)
				{
					std::string what = pattern.substr(colon + 1);
					if (what.rfind("Item.", 0) == 0) what = what.substr(5);
					while (!what.empty())
					{
						if (const auto it = families.find(what); it != families.end()) return it->second;
						const size_t dot = what.rfind('.');
						if (dot == std::string::npos) break;
						what.resize(dot);
					}
				}
				if (const auto it = families.find(kind); it != families.end()) return it->second;
			}
			return {};
		}
	}

	// ------------------------------------------------------------------ the shared steps (the click and `colony work step` run these)
	namespace { constexpr int kWorkLevels = 4; }   // T336: the levels the table offers (the data keeps 1..10: `game priority` takes any)

	int EditorApp::WorkLevel(int number)
	{
		return number == WorkPriorities::kOff ? WorkPriorities::kOff : std::clamp(number, 1, kWorkLevels);
	}

	std::string EditorApp::WorkStep(const std::string& person, const std::string& what, bool up) const
	{
		const Tribe* t = ColonyTribe();
		const TribeMember* m = t ? t->FindMember(person) : nullptr;
		if (!m || m->Dead) return {};
		const WorkCategories& cats = t->Config.Work.Categories;
		const WorkPriorities::Table* own = t->WorkTable().Of(person);
		const WorkPriorities::Table tab = own ? *own : WorkPriorities::Suggested(*t, *m);
		int now = -1;
		if (const WorkCategoryDef* c = cats.Category(what)) now = CategoryCell(tab, *c).Number;
		else if (const WorkCategoryDef* c2 = cats.CategoryOfJob(what)) now = JobCell(tab, *c2, what).Number;
		if (now < 0) return {};
		// T336: the table shows 4 levels (a number over 4 of the data shows as 4): before: 4 .. 1 (1 stays 1; "-" comes back as 4); after:
		// 1 .. 4, then "-" (which stays)
		const int level = WorkLevel(now);
		const int next = up ? (level == WorkPriorities::kOff ? kWorkLevels : std::max(1, level - 1))
			: (level == WorkPriorities::kOff || level >= kWorkLevels ? WorkPriorities::kOff : level + 1);
		if (next == now) return {};
		return "game priority " + person + " " + what + " " + WorkPriorities::Text(next);
	}

	CommandResult EditorApp::ColonyTabCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a[0];
		const char* usage = "colony tab [work|stock|field|clan|chronicle|board|sites|homes|research|relations|off] | colony work fold|unfold <category> | colony work step <person> <category|job> up|down | colony work reset <person> "
			"| colony stock picker | colony stock pick <item|tag> [n] | colony stock set <item|tag> <n> | colony stock off <item|tag>";
		if (sub == "tab")
		{
			if (a.size() == 1) return { true, m_Colony.Tab.empty() ? std::string("no tab open") : "tab " + m_Colony.Tab + " open" };
			if (a[1] == "off") { m_Colony.Tab.clear(); return { true, "tab closed" }; }
			// T336: the sheets of the clan's big sheet open by the same order (village and knowledge: the god's book's names)
			const std::string tab = a[1] == "village" ? "clan" : a[1] == "knowledge" ? "research" : a[1];
			if (tab != "work" && tab != "stock" && tab != "field" && !IsColonySheet(tab)) return { false, usage };
			if (!ColonyTribe()) return { false, "no colony: director colony on, and a world with clans" };
			if (!m_Hand.Menu.empty() || !m_Hand.Building.empty()) Execute("director hand off");   // (one panel over the bar at a time)
			m_Colony.Tab = tab;
			return { true, "tab " + tab + " open" };
		}
		const Tribe* t = ColonyTribe();
		if (!t) return { false, "no colony: director colony on, and a world with clans" };
		if (sub == "work" && a.size() >= 3 && (a[1] == "fold" || a[1] == "unfold"))
		{
			if (!t->Config.Work.Categories.Category(a[2])) return { false, "no category " + a[2] };
			if (a[1] == "fold") m_Colony.Unfolded.erase(a[2]);
			else m_Colony.Unfolded.insert(a[2]);
			return { true, a[2] + (a[1] == "fold" ? " folded" : " unfolded") };
		}
		if (sub == "work" && a.size() == 5 && a[1] == "step" && (a[4] == "up" || a[4] == "down"))
		{
			const std::string command = WorkStep(a[2], a[3], a[4] == "up");
			if (command.empty()) return { true, "no change: " + a[2] + " " + a[3] };
			const CommandResult r = Execute(command);
			return { r.Ok, command + " -> " + r.Message };
		}
		if (sub == "work" && a.size() == 3 && a[1] == "reset")
		{
			const std::string command = "game priority " + a[2] + " reset";
			const CommandResult r = Execute(command);
			return { r.Ok, command + " -> " + r.Message };
		}
		if (sub == "stock" && a.size() == 2 && a[1] == "picker") { m_Colony.OpenPicker = true; return { true, "the picker of things opens" }; }   // (as its click: the captures)
		if (sub == "stock" && a.size() >= 3 && a[1] == "pick")
		{
			m_Colony.StockPick = a[2];
			if (a.size() > 3) m_Colony.StockCount = std::clamp(std::atoi(a[3].c_str()), 1, StockGoals::kMost);
			return { true, "picked " + a[2] + " x" + std::to_string(m_Colony.StockCount) };
		}
		if (sub == "stock" && a.size() == 4 && a[1] == "set")
		{
			const std::string command = "game stock " + a[2] + " " + a[3];
			const CommandResult r = Execute(command);
			return { r.Ok, command + " -> " + r.Message };
		}
		if (sub == "stock" && a.size() == 3 && a[1] == "off")
		{
			const std::string command = "game stock " + a[2] + " off";
			const CommandResult r = Execute(command);
			return { r.Ok, command + " -> " + r.Message };
		}
		return { false, usage };
	}

	// ------------------------------------------------------------------ texts
	std::string EditorApp::WorkLabel(const std::string& id) const
	{
		return Lang::Cap(m_Lang.Name("work", id));
	}

	std::string EditorApp::GoalLabel(const Tribe& t, const std::string& what) const
	{
		if (what.rfind("Item.", 0) == 0)
		{
			const size_t dot = what.rfind('.');
			return Lang::Cap(m_Lang.Name("tag", what, what.substr(dot + 1)));
		}
		const ItemDef* def = t.Items() ? t.Items()->Find(what) : nullptr;
		return Lang::Cap(m_Lang.Name("item", what, def ? def->Name : what));
	}

	// StockGoals::Status in the player's words, with its colour: done, under way or stuck (and why)
	std::pair<std::string, unsigned> EditorApp::StockStatusText(const std::string& status) const
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		auto after = [&](const char* prefix, std::string& rest) { const size_t n = std::char_traits<char>::length(prefix); if (status.rfind(prefix, 0) != 0) return false; rest = status.substr(n); return true; };
		std::string rest;
		if (status == "ok") return { L.Text("enough"), T::Good };
		if (status == "making") return { L.Text("making it"), T::Bone };
		if (status == "gathering") return { L.Text("gathering it"), T::Bone };
		if (status == "hunting") return { L.Text("hunting for it"), T::Bone };
		if (status == "does not know how to make it") return { L.Text("does not know how to make it"), T::Bad };
		if (status == "no animal to hunt") return { L.Text("no animal to hunt"), T::Warn };
		if (status == "no way to get it") return { L.Text("no way to get it"), T::Bad };
		if (after("gathering ", rest)) return { L.Text("gathering {0}", L.Name("item", rest)), T::Bone };
		if (after("hunting for ", rest)) return { L.Text("hunting for {0}", L.Name("item", rest)), T::Bone };
		if (after("making ", rest)) return { L.Text("making {0}", L.Name("item", rest)), T::Bone };
		if (after("lacks ", rest)) return { L.Text("lacks {0}: nothing it knows gives it", L.Name("item", rest)), T::Bad };
		static const std::string kAt = " to make it at";
		if (after("no ", rest) && rest.size() > kAt.size() && rest.compare(rest.size() - kAt.size(), kAt.size(), kAt) == 0)
			return { L.Text("no {0} to make it at", L.Name("station", rest.substr(0, rest.size() - kAt.size()))), T::Warn };
		return { status, T::Bone };
	}

	// ------------------------------------------------------------------ the panels over the bar
	void EditorApp::DrawColonyTabs(float ox, float oy, float w, float h)
	{
		m_Colony.TabBox = {};
		if (!ColonyScreen()) { m_Colony.Tab.clear(); return; }
		if (!m_Hand.Menu.empty() || !m_Hand.Building.empty()) m_Colony.Tab.clear();   // (the hand's menu took the place over the bar)
		if (m_Colony.Tab.empty()) return;
		const Tribe* t = ColonyTribe();
		if (!t) return;
		namespace T = GameTheme;
		// between the clans' list and the book when they reach down that far, over the bar (as the hand's menu)
		const float bottomZone = oy + h - 90.0f;
		const ObserverState::Box& list = m_Observer.ClansBox.X1 > m_Observer.ClansBox.X0 ? m_Observer.ClansBox : m_Observer.CardBox;
		const float left = list.X1 > list.X0 && list.Y1 > bottomZone ? list.X1 + T::Gap : ox + T::Margin;
		const float right = m_Observer.BookBox.X1 > m_Observer.BookBox.X0 ? m_Observer.BookBox.X0 - T::Gap : ox + w - T::Margin;
		const float room = std::max(right - left, 240.0f);
		const float bottom = m_Hand.Box.Y1 > m_Hand.Box.Y0 ? m_Hand.Box.Y0 - T::Gap : oy + h - T::Margin;
		const float maxH = std::max(std::min(h * 0.5f, bottom - m_Observer.BarBottom - T::Gap), 160.0f);
		// T336 (L8/L9, propuesta p4): the clan's sheets in a big sheet in the centre, with a column of icon tabs
		if (IsColonySheet(m_Colony.Tab))
		{
			const float top = std::max(m_Observer.BarBottom, list.Y1) + T::Gap;
			const float sheetW = std::min(room, 940.0f), sheetH = std::clamp(bottom - top, 200.0f, 600.0f);
			ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, bottom), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2(sheetW, sheetH), ImGuiCond_Always);
			const ImGuiWindowFlags sheetFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
			if (ImGui::Begin("ColonySheet##colony", nullptr, sheetFlags))
			{
				DrawColonySheet(*t, ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
				m_Colony.TabBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
			}
			ImGui::End();
			return;
		}
		ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, bottom), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(room, maxH));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		if (ImGui::Begin("ColonyTab##colony", nullptr, flags))
		{
			if (m_Colony.Tab == "work") DrawWorkTab(*t, room - 2.0f * T::Pad, maxH);
			else if (m_Colony.Tab == "field") DrawFieldTab(*t, room - 2.0f * T::Pad);   // T281 (ColonyZones.cpp)
			else DrawStockTab(*t, room - 2.0f * T::Pad, maxH);
			m_Colony.TabBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
	}

	// T336: the small pieces the colony's panels share
	const char* EditorApp::WorkIconOf(const std::string& id) { return WorkIcon(id); }

	bool EditorApp::IconAt(ImDrawList* dl, const std::string& id, float x, float y, float size, unsigned tint)
	{
		const ImVec2 min(x, y);
		glm::vec2 a, b;
		if (id.empty() || !IconUv(id, a, b)) return false;
		dl->AddImage(TextureId(*m_IconTexture), min, ImVec2(min.x + size, min.y + size), ImVec2(a.x, a.y), ImVec2(b.x, b.y), tint);
		return true;
	}

	std::string EditorApp::RoleName(const Tribe& t, const TribeMember& m) const
	{
		// the "role_f" names when the person is a woman and the language has one for that trade ("recolectora"), else the "role" ones
		static const std::string kNone = "\x01";
		if (const Person* p = t.People().Find(m.Name); p && p->Female)
			if (const std::string f = m_Lang.Name("role_f", m.Role, kNone); f != kNone) return f;
		return m_Lang.Name("role", m.Role);
	}

	std::string EditorApp::WorkShort(const std::string& id) const
	{
		static const std::string kNone = "\x01";
		const std::string s = m_Lang.Name("workshort", id, kNone);
		return s == kNone ? WorkLabel(id) : s;
	}

	// the title of a panel (T336): its name in ochre and a "?" whose tooltip is the help (the help lives in the control, not in a long line);
	// then, at the right of `width` (0: right after the title), what `extra` draws and the close
	void EditorApp::ColonyTabTitle(const std::string& title, const std::string& help, float width, const std::function<void()>& extra, float extraW)
	{
		namespace T = GameTheme;
		const float x0 = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", title.c_str());
		ImGui::SameLine(0.0f, 10.0f);
		const float box = ImGui::GetFrameHeight();
		ImGui::Button("?###tabhelp", ImVec2(box, box));
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help.c_str());
		const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x;
		const float right = x0 + width - box - (extra ? extraW + ImGui::GetStyle().ItemSpacing.x : 0.0f);
		if (width > 0.0f && right > after + 12.0f) ImGui::SameLine(right);
		else ImGui::SameLine(0.0f, 24.0f);
		if (extra) { extra(); if (!m_Colony.BareTitle) ImGui::SameLine(); }
		if (!m_Colony.BareTitle)   // (inside the clan's big sheet the sheet has the close)
		{
			if (ImGui::Button("\xC3\x97###tabclose", ImVec2(box, box))) m_UiCommands.push_back("colony tab off");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_Lang.Text("Close the panel (Esc)").c_str());
		}
		else if (!extra) ImGui::NewLine();
		ImGui::Separator();
	}

	// T336 (L4, propuesta p2): a person x category table of 4 levels. The head of a column is the icon of its category with its short name under it
	// (a click unfolds its jobs); a cell is its number over a bar of 4 segments (1 fills it, 4 leaves one, "-" is an empty cell with a dash); the
	// dot before a name says whether it follows the player's table. A click puts a cell sooner, a right click later, the wheel either way: each
	// queues `colony work step`; a drag sideways moves it along 1 2 3 4 - and queues one `game priority` when it is let go.
	void EditorApp::DrawWorkTab(const Tribe& t, float width, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const WorkCategories& cats = t.Config.Work.Categories;
		const WorkPriorities& table = t.WorkTable();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		std::vector<const TribeMember*> people;
		for (const TribeMember& m : t.Members()) if (!m.Dead && !m.Player) people.push_back(&m);
		int owned = 0;
		for (const TribeMember* m : people) owned += table.Of(m->Name) ? 1 : 0;

		// the columns: who, then each category (and its jobs when it is unfolded)
		struct Column { const WorkCategoryDef* Cat; const WorkJobDef* Job; float W; };
		std::vector<Column> columns;
		const ImGuiStyle& style = ImGui::GetStyle();
		const float cellW = 42.0f, cellH = 36.0f, icon = 20.0f, lineH = ImGui::GetTextLineHeight();
		for (const WorkCategoryDef& c : cats.List)
		{
			columns.push_back({ &c, nullptr, std::max(cellW, ImGui::CalcTextSize(WorkShort(c.Id).c_str()).x + 6.0f) });
			if (m_Colony.Unfolded.count(c.Id))
				for (const WorkJobDef& j : c.Jobs) columns.push_back({ &c, &j, std::max(cellW, ImGui::CalcTextSize(WorkShort(j.Id).c_str()).x + 6.0f) });
		}
		float whoW = 0.0f;
		for (const TribeMember* m : people) whoW = std::max(whoW, ImGui::CalcTextSize((m->Name + " " + RoleName(t, *m)).c_str()).x);
		whoW += 10.0f + 6.0f + 10.0f;   // (the dot, its gap and some air)
		float need = whoW;
		for (const Column& c : columns) need += c.W;
		need += (static_cast<float>(columns.size()) + 1.0f) * (style.CellPadding.x * 2.0f + 1.0f) + 4.0f;
		const float tableW = std::min(need, width);

		const std::string help = L.Text("Who does what first. Each cell is the number of a work for a person: 1 first, 4 last, - never.") + "\n"
			+ L.Text("Click: sooner   Right click: later   Wheel or a drag sideways: either way") + "\n"
			+ L.Text("A click on the head of a column unfolds its jobs; a right click on a name gives it back to the AI");
		const std::string allAi = L.Text("All as the AI");
		const float allW = ImGui::CalcTextSize(allAi.c_str()).x + style.FramePadding.x * 2.0f;
		ColonyTabTitle(L.Text("Work"), help, std::max(tableW, 360.0f), [&]()
		{
			ImGui::BeginDisabled(owned == 0);
			if (ImGui::Button((allAi + "###allai").c_str()))
				for (const TribeMember* m : people) if (table.Of(m->Name)) run("colony work reset " + m->Name);
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("%s", owned == 0 ? L.Text("Everyone already decides as the AI").c_str() : L.Text("Drop every table of yours: they all decide again as the AI does").c_str());
		}, allW);
		if (cats.Empty()) { ImGui::TextDisabled("%s", L.Text("The data has no categories of work").c_str()); return; }
		if (people.empty()) { ImGui::TextDisabled("%s", L.Text("Nobody in the clan").c_str()); return; }

		const float headH = icon + lineH + 6.0f, rowH = cellH + 2.0f;
		const float tableH = std::min(headH + style.CellPadding.y * 2.0f + static_cast<float>(people.size()) * (rowH + style.CellPadding.y * 2.0f) + 6.0f,
			maxH - ImGui::GetFrameHeightWithSpacing() * 3.0f);
		const ImGuiTableFlags tflags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("work", static_cast<int>(columns.size()) + 1, tflags, ImVec2(tableW, std::max(tableH, rowH * 2.0f)))) return;
		ImGui::TableSetupScrollFreeze(1, 1);   // (who stays in sight when the jobs scroll)
		ImGui::TableSetupColumn("who", ImGuiTableColumnFlags_WidthFixed, whoW);
		for (size_t i = 0; i < columns.size(); ++i) ImGui::TableSetupColumn(columns[i].Job ? columns[i].Job->Id.c_str() : columns[i].Cat->Id.c_str(), ImGuiTableColumnFlags_WidthFixed, columns[i].W);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		// the head: an icon over its short name; the categories fold and unfold, their jobs follow under a line of the category's colour
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers, headH);
		ImGui::TableNextColumn();
		for (const Column& c : columns)
		{
			ImGui::TableNextColumn();
			const std::string id = c.Job ? c.Job->Id : c.Cat->Id;
			const bool open = !c.Job && m_Colony.Unfolded.count(c.Cat->Id) != 0;
			ImGui::PushID(id.c_str());
			const ImVec2 at = ImGui::GetCursorScreenPos();
			bool hovered = false;
			if (!c.Job)
			{
				if (ImGui::InvisibleButton("fold", ImVec2(c.W, headH))) run(std::string("colony work ") + (open ? "fold " : "unfold ") + c.Cat->Id);
				hovered = ImGui::IsItemHovered();
				if (hovered)
				{
					std::string jobs;
					for (const WorkJobDef& j : c.Cat->Jobs) jobs += (jobs.empty() ? "" : ", ") + L.Name("work", j.Id);
					ImGui::SetTooltip("%s", (L.Text(open ? "Fold {0}" : "Unfold {0} into its jobs", WorkLabel(c.Cat->Id)) + "\n" + jobs).c_str());
				}
			}
			else ImGui::Dummy(ImVec2(c.W, headH));
			if (hovered) dl->AddRectFilled(at, ImVec2(at.x + c.W, at.y + headH), T::HideHover, 3.0f);
			if (c.Job) dl->AddLine(ImVec2(at.x + 2.0f, at.y), ImVec2(at.x + c.W - 2.0f, at.y), T::Faded(T::Ochre, 0.6f), 2.0f);   // (a job of an unfolded column)
			if (!IconAt(dl, WorkIcon(id), at.x + (c.W - icon) * 0.5f, at.y + 2.0f, icon, c.Job ? T::Faded(IM_COL32_WHITE, 0.8f) : IM_COL32_WHITE))
				dl->AddCircle(ImVec2(at.x + c.W * 0.5f, at.y + 2.0f + icon * 0.5f), icon * 0.35f, T::BoneDim, 12, 1.5f);   // (no icon: a ring)
			if (!c.Job)   // the fold's arrow at the icon's right: right when folded, down when open
			{
				const ImVec2 p(at.x + (c.W + icon) * 0.5f + 3.0f, at.y + 6.0f);
				const ImU32 ac = open ? T::Ochre : hovered ? T::OchreHover : T::BoneDim;
				if (open) dl->AddTriangleFilled(ImVec2(p.x - 3.0f, p.y - 2.0f), ImVec2(p.x + 3.0f, p.y - 2.0f), ImVec2(p.x, p.y + 2.0f), ac);
				else dl->AddTriangleFilled(ImVec2(p.x - 2.0f, p.y - 3.0f), ImVec2(p.x - 2.0f, p.y + 3.0f), ImVec2(p.x + 2.0f, p.y), ac);
			}
			const std::string label = WorkShort(id);
			const ImVec2 ls = ImGui::CalcTextSize(label.c_str());
			dl->AddText(ImVec2(at.x + (c.W - ls.x) * 0.5f, at.y + icon + 4.0f), open || hovered ? T::Ochre : c.Job ? T::BoneDim : T::Bone, label.c_str());
			ImGui::PopID();
		}

		// a bar of 4 segments: `filled` of them lit
		auto segments = [&](ImVec2 min, float w, int filled, ImU32 on)
		{
			const float gap = 2.0f, sw = (w - 3.0f * gap) / 4.0f, sh = 5.0f;
			for (int i = 0; i < 4; ++i)
			{
				const ImVec2 a(min.x + static_cast<float>(i) * (sw + gap), min.y), b(a.x + sw, a.y + sh);
				if (i < filled) dl->AddRectFilled(a, b, on, 1.5f);
				else dl->AddRect(a, b, T::HideLine, 1.5f);
			}
		};
		const ImGuiIO& io = ImGui::GetIO();
		for (const TribeMember* m : people)
		{
			const WorkPriorities::Table* own = table.Of(m->Name);
			const WorkPriorities::Table tab = own ? *own : WorkPriorities::Suggested(t, *m);
			const bool selected = std::find(m_Colony.Selection.begin(), m_Colony.Selection.end(), m->Name) != m_Colony.Selection.end();
			ImGui::PushID(m->Name.c_str());
			ImGui::TableNextRow(0, rowH);
			ImGui::TableNextColumn();
			{
				// the dot (filled: your table; a ring: as the AI), the name and the trade; a right click gives the person back to the AI
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::InvisibleButton("who", ImVec2(whoW, rowH), ImGuiButtonFlags_MouseButtonRight);
				if (own && ImGui::IsItemClicked(ImGuiMouseButton_Right)) run("colony work reset " + m->Name);
				const bool hovered = ImGui::IsItemHovered();
				if (hovered)
					ImGui::SetTooltip("%s", own ? (L.Text("{0} follows your table", m->Name) + "\n" + L.Text("Right click: as the AI")).c_str()
						: L.Text("{0} decides as the AI. The numbers are what its trade ({1}) suggests: the first one you change starts its table from them", m->Name, RoleName(t, *m)).c_str());
				const ImVec2 dot(at.x + 6.0f, at.y + rowH * 0.5f);
				if (own) dl->AddCircleFilled(dot, 4.5f, T::Ochre, 12);
				else dl->AddCircle(dot, 4.5f, T::BoneDim, 12, 1.5f);
				const float ty = at.y + (rowH - lineH) * 0.5f;
				dl->AddText(ImVec2(at.x + 16.0f, ty), selected || hovered ? T::Ochre : T::Bone, m->Name.c_str());
				dl->AddText(ImVec2(at.x + 16.0f + ImGui::CalcTextSize((m->Name + " ").c_str()).x, ty), T::BoneDim, RoleName(t, *m).c_str());
			}
			for (const Column& c : columns)
			{
				ImGui::TableNextColumn();
				const WorkCell cell = c.Job ? JobCell(tab, *c.Cat, c.Job->Id) : CategoryCell(tab, *c.Cat);
				const std::string what = c.Job ? c.Job->Id : c.Cat->Id;
				const std::string key = m->Name + " " + what;
				ImGui::PushID(what.c_str());
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const bool pressed = ImGui::InvisibleButton("cell", ImVec2(c.W, cellH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				const bool hovered = ImGui::IsItemHovered();
				const int shown = WorkLevel(cell.Number);
				int value = shown;
				int step = 0;   // -1: sooner (a lower number), +1: later
				// the drag: along 1 2 3 4 - (18 px a step), shown while it lasts, queued once when let go
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
				{
					if (m_Colony.Drag != key) { m_Colony.Drag = key; m_Colony.DragX = io.MouseClickedPos[0].x; m_Colony.DragFrom = shown; }
					const int from = m_Colony.DragFrom == WorkPriorities::kOff ? kWorkLevels : m_Colony.DragFrom - 1;
					const int index = std::clamp(from + static_cast<int>(std::lround((io.MousePos.x - m_Colony.DragX) / 18.0f)), 0, kWorkLevels);
					m_Colony.DragTo = index == kWorkLevels ? WorkPriorities::kOff : index + 1;
					value = m_Colony.DragTo;
				}
				if (ImGui::IsItemDeactivated() && m_Colony.Drag == key)
				{
					if (m_Colony.DragTo != m_Colony.DragFrom) run("game priority " + m->Name + " " + what + " " + WorkPriorities::Text(m_Colony.DragTo));
					m_Colony.Drag.clear();
				}
				else if (pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) step = -1;
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) step = 1;
				else if (hovered && io.MouseWheel != 0.0f) step = io.MouseWheel > 0.0f ? -1 : 1;
				if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // (the wheel sets the number: the table does not scroll under it)
				if (step != 0) run("colony work step " + m->Name + " " + what + (step < 0 ? " up" : " down"));
				const bool dragging = m_Colony.Drag == key;
				if (hovered || dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);   // (the cell takes a drag sideways)
				// the cell: a box, its number and its 4 segments; "-" an empty box with a dash. Your numbers bright (the first ochre), inherited
				// ones dim; a person as the AI all dimmed
				const float bx = at.x + (c.W - cellW) * 0.5f;   // (the box keeps its size in a wider column)
				const ImVec2 b0(bx + 2.0f, at.y + 1.0f), b1(bx + cellW - 2.0f, at.y + cellH - 1.0f);
				dl->AddRectFilled(b0, b1, hovered || dragging ? T::HideHover : T::HideRaised, 3.0f);
				dl->AddRect(b0, b1, dragging ? T::Ochre : hovered ? T::OchreHover : T::HideLine, 3.0f, 0, dragging ? 2.0f : 1.0f);
				const float mid = (b0.x + b1.x) * 0.5f;
				if (value == WorkPriorities::kOff)
					dl->AddLine(ImVec2(mid - 6.0f, (b0.y + b1.y) * 0.5f), ImVec2(mid + 6.0f, (b0.y + b1.y) * 0.5f), own ? T::BoneDim : T::Faded(T::BoneDim, 0.7f), 2.0f);
				else
				{
					const std::string text = std::to_string(value);
					ImU32 colour = !cell.Own ? T::BoneDim : value == 1 ? T::Ochre : T::Bone;
					if (!own) colour = T::Faded(T::BoneDim, 0.7f);
					if (dragging) colour = T::OchreHover;
					const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
					dl->AddText(ImVec2(mid - ts.x * 0.5f, b0.y + 3.0f), colour, text.c_str());
					segments(ImVec2(b0.x + 5.0f, b1.y - 9.0f), b1.x - b0.x - 10.0f, kWorkLevels + 1 - value, own && cell.Own ? T::Ochre : T::Faded(T::Ochre, 0.55f));
				}
				if (hovered && !dragging)
				{
					std::string tip = m->Name + ": " + (c.Job ? WorkLabel(c.Job->Id) + " (" + L.Name("work", c.Cat->Id) + ")" : WorkLabel(c.Cat->Id)) + " " + WorkPriorities::Text(shown);
					if (cell.Number > kWorkLevels) tip += "  " + L.Text("({0} in the data: the table shows it as 4)", cell.Number);
					if (c.Job && !cell.Own) tip += "\n" + (cell.Number == WorkPriorities::kOff && CategoryCell(tab, *c.Cat).Number == WorkPriorities::kOff
						? L.Text("Off because {0} is at -", L.Name("work", c.Cat->Id)) : L.Text("The number of {0} (it has none of its own)", L.Name("work", c.Cat->Id)));
					// the aptitude in that work (the skills' family it trains), else the trade
					std::string family;
					if (c.Job) family = JobFamily(t, *c.Job);
					else for (const WorkJobDef& j : c.Cat->Jobs) if (family.empty()) family = JobFamily(t, j);
					if (!family.empty()) tip += "\n" + L.Text("Aptitude in {0}: {1} of 100", L.Name("skill", family), L.Num(SkillLevel(m->Skills, family), 0));
					else tip += "\n" + L.Text("No aptitude is measured for it; trade: {0}", RoleName(t, *m));
					if (!own) tip += "\n" + L.Text("Decides as the AI: this is what its trade suggests");
					tip += "\n" + L.Text("Click: sooner   Right click: later   Wheel or a drag sideways: either way");
					ImGui::SetTooltip("%s", tip.c_str());
				}
				ImGui::PopID();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
		dl = ImGui::GetWindowDrawList();   // (the panel's again: the table's scrolling child clipped the cells)

		// the legend, drawn with the same marks the cells use
		{
			auto mark = [&](float w, const std::function<void(ImVec2)>& draw, const std::string& text)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(w, lineH));
				draw(ImVec2(at.x, at.y + lineH * 0.5f));
				ImGui::SameLine(0.0f, 4.0f);
				ImGui::TextDisabled("%s", text.c_str());
				ImGui::SameLine(0.0f, 14.0f);
			};
			mark(28.0f, [&](ImVec2 p) { segments(ImVec2(p.x, p.y - 2.5f), 28.0f, 4, T::Ochre); }, L.Text("1 first"));
			mark(28.0f, [&](ImVec2 p) { segments(ImVec2(p.x, p.y - 2.5f), 28.0f, 1, T::Ochre); }, L.Text("4 last"));
			mark(14.0f, [&](ImVec2 p) { dl->AddLine(ImVec2(p.x + 1.0f, p.y), ImVec2(p.x + 13.0f, p.y), T::BoneDim, 2.0f); }, L.Text("never"));
			mark(10.0f, [&](ImVec2 p) { dl->AddCircleFilled(ImVec2(p.x + 5.0f, p.y), 4.5f, T::Ochre, 12); }, L.Text("your table"));
			mark(10.0f, [&](ImVec2 p) { dl->AddCircle(ImVec2(p.x + 5.0f, p.y), 4.5f, T::BoneDim, 12, 1.5f); }, L.Text("follows the AI"));
			ImGui::NewLine();
		}
	}

	// T324: the research queue (ResearchQueue.h). The buttons only queue `game research ...`.
	// T341: every state and every reason is TechStatus's (the tree says the same); a row dragged onto another queues `game research move <tech> <n>`
	// (T339); what the clan cannot study yet is named only when it could name it (D2), the rest is "? N more" (only the technologies with a tier, D6).
	void EditorApp::DrawResearchTab(const Tribe& t, float width, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const ResearchQueue& queue = t.Research();
		const TechTree* tree = t.Knowledge().Techs();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto name = [&](const std::string& id) { const TechDef* d = tree ? tree->Find(id) : nullptr; return Lang::Cap(L.Name("tech", id, d ? d->Name : id)); };
		ColonyTabTitle(L.Text("Knowledge"), L.Text("What the clan studies, in order: whoever works on knowledge tries the first it can"));
		if (!tree) { ImGui::TextDisabled("%s", L.Text("This world has no technologies").c_str()); return; }
		const TechStatus status = TechStatusOf(t);
		if (queue.Empty()) ImGui::TextDisabled("%s", L.Text("The study queue is empty: add one below or from the tree").c_str());
		else if (ImGui::BeginTable("research", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
			ImVec2(std::min(width, 760.0f), std::min((static_cast<float>(queue.All().size()) + 1.0f) * (ImGui::GetTextLineHeight() + 10.0f) + 4.0f, maxH - 110.0f))))
		{
			ImGui::TableSetupColumn("tech", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("top", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(L.Text("First").c_str()).x + 12.0f);
			ImGui::TableSetupColumn("off", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(L.Text("Remove").c_str()).x + 12.0f);
			for (size_t i = 0; i < queue.All().size(); ++i)
			{
				const std::string& id = queue.All()[i];
				const TechStatus::Entry& e = status.Of(id);
				ImGui::PushID(id.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				// the row's name is its handle: drag it onto another row to put it in that place
				ImGui::Selectable((std::to_string(i + 1) + ". " + name(id) + "###row").c_str(), false, ImGuiSelectableFlags_None, ImVec2(0.0f, 0.0f));
				if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ImGui::SetTooltip("%s", L.Text("Drag it onto another row to move it there").c_str());
				if (ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("GB_RESEARCH", id.c_str(), id.size() + 1);
					ImGui::TextUnformatted(name(id).c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_RESEARCH"))
					{
						const std::string moved(static_cast<const char*>(p->Data));
						if (moved != id) run("game research move " + moved + " " + std::to_string(i + 1));
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::TableNextColumn();
				ImGui::Text("%d%%", static_cast<int>(std::lround(e.Progress * 100.0f)));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("How far the one furthest on it has worked it out").c_str());
				ImGui::TableNextColumn();
				// TechStatus in the player's words: being studied, next, or what it waits for (a prerequisite named only when the clan can name it)
				std::pair<std::string, unsigned> said{ L.Text("next"), T::Bone };
				if (e.State == TechState::Studying) said = { L.Text("being studied"), T::Good };
				else if (e.Why != TechWhy::None) said = { TechWhyText(status, id), T::Warn };
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(said.second), "%s", said.first.c_str());
				ImGui::TableNextColumn();
				if (i > 0)
				{
					if (ImGui::SmallButton((L.Text("First") + "###top").c_str())) run("game research top " + id);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Study {0} before the rest", name(id)).c_str());
				}
				ImGui::TableNextColumn();
				if (ImGui::SmallButton((L.Text("Remove") + "###off").c_str())) run("game research remove " + id);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Take {0} out of the queue", name(id)).c_str());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::Spacing();
		ImGui::TextDisabled("%s", L.Text("The clan can study now").c_str());
		int shown = 0;
		for (const auto& [id, e] : status.All())
		{
			if (e.State != TechState::Studiable || !e.Shown) continue;
			if (shown++ > 0) ImGui::SameLine();
			if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name(id).c_str()).x + 16.0f) ImGui::NewLine();
			if (ImGui::SmallButton((name(id) + "###add" + id).c_str())) run("game research add " + id);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Add {0} to the end of the queue", name(id)).c_str());
		}
		if (shown == 0) ImGui::TextDisabled("%s", L.Text("Nothing new: it must learn more first").c_str());
		// what it cannot study yet and can name: each with why (no flint at hand, only from others, another clan knows it)
		std::vector<std::string> later;
		for (const auto& [id, e] : status.All())
			if (e.Shown && e.Queue == 0 && (e.State == TechState::Blocked || e.State == TechState::OtherClan)) later.push_back(id);
		if (!later.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("%s", L.Text("Not yet").c_str());
			for (const std::string& id : later)
			{
				const TechStatus::Entry& e = status.Of(id);
				ImGui::Bullet();
				ImGui::SameLine();
				ImGui::TextUnformatted(name(id).c_str());
				ImGui::SameLine();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.State == TechState::OtherClan ? T::News : T::Warn), "%s", TechStateText(status, id).c_str());
			}
		}
		if (const int hidden = status.UnnamedCount(); hidden > 0)
		{
			ImGui::TextDisabled("%s", L.Text("? {0} more", hidden).c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("What the clan cannot study yet: it needs what it does not know").c_str());
		}
	}

	// T336 (L5, propuesta p3): the goals by kind (food, materials, tools, clothing), each kind folding with its summary; a goal is its icon and
	// name, what the clan has, one piece that is the bar and the slider (the fill is what it has, the ochre handle what it wants: let go, it queues
	// one `game stock <what> <n>`), what it is doing about it with an icon (a stuck one in red with why) and its removal. [Add a goal] opens the grid
	// of things: a thing picked becomes a goal of the default number (`colony stock set`), set later on its row.
	void EditorApp::DrawStockTab(const Tribe& t, float width, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const StockGoals& goals = t.Goals();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const ImGuiStyle& style = ImGui::GetStyle();
		const float panelW = std::min(width, 660.0f), lineH = ImGui::GetTextLineHeight();
		ColonyTabTitle(L.Text("Stock"), L.Text("What the clan keeps: when it has less, it makes, gathers or hunts the rest") + "\n"
			+ L.Text("The bar is what it has; drag the ochre handle to say how many it keeps (it is set when you let go)"), panelW);

		// the kind of a goal: by its tag, or by the tags of its item
		const ItemLibrary* items = t.Items();
		auto kindOf = [&](const std::string& what)
		{
			std::vector<std::string> tags;
			if (what.rfind("Item.", 0) == 0) tags.push_back(what);
			else if (const ItemDef* d = items ? items->Find(what) : nullptr) tags = d->Tags;
			for (const std::string& tag : tags)
			{
				if (tag.rfind("Item.Food", 0) == 0) return 0;
				if (tag.rfind("Item.Tool", 0) == 0 || tag.rfind("Item.Weapon", 0) == 0) return 2;
				if (tag.rfind("Item.Clothing", 0) == 0) return 3;
			}
			return 1;
		};
		static const char* kKinds[][2] = { { "food", "Food" }, { "materials", "Materials" }, { "tools", "Tools" }, { "clothing", "Clothing and cover" } };
		static const char* kIcons[] = { "smoked_meat", "log", "stone_axe", "clothing" };
		std::vector<std::pair<std::string, int>> byKind[4];
		for (const auto& [what, n] : goals.All()) byKind[kindOf(what)].push_back({ what, n });

		if (goals.All().empty()) ImGui::TextDisabled("%s", L.Text("No goals yet: add one below").c_str());
		else
		{
			const float rowH = 30.0f, headH = ImGui::GetFrameHeightWithSpacing();
			float contentH = 0.0f;
			for (const auto& list : byKind) if (!list.empty()) contentH += headH + static_cast<float>(list.size()) * (rowH + style.CellPadding.y * 2.0f);
			ImGui::BeginChild("goals", ImVec2(panelW, std::min(contentH + 8.0f, std::max(maxH - 120.0f, 120.0f))));
			const float iconS = 22.0f, nameW = 170.0f, haveW = 30.0f, barW = 220.0f, offW = ImGui::GetFrameHeight() + 4.0f;
			for (int k = 0; k < 4; ++k)
			{
				if (byKind[k].empty()) continue;
				ImGui::PushID(k);
				// the kind's head: its icon and name, and on the right its summary (the food: its days; the rest: how many goals, "all well")
				const ImVec2 hat = ImGui::GetCursorScreenPos();
				const bool open = ImGui::TreeNodeEx("##kind", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding);
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const float tx = hat.x + ImGui::GetTreeNodeToLabelSpacing();
				const float fy = hat.y + (ImGui::GetFrameHeight() - 18.0f) * 0.5f;
				const float lx = IconAt(dl, kIcons[k], tx, fy, 18.0f) ? tx + 22.0f : tx;
				dl->AddText(ImVec2(lx, hat.y + style.FramePadding.y), T::Bone, L.Text(kKinds[k][1]).c_str());
				bool allOk = true;
				for (const auto& [what, n] : byKind[k]) allOk = allOk && StockGoals::Have(t, what) >= n;
				const int count = static_cast<int>(byKind[k].size());
				std::string summary = k == 0 ? L.Text("for {0} days", L.Num(t.FoodDays(), 0)) : L.Text(count == 1 ? "{0} goal" : "{0} goals", count);
				if (k != 0 && allOk) summary += " \xC2\xB7 " + L.Text("all well");
				const ImVec2 ss = ImGui::CalcTextSize(summary.c_str());
				dl->AddText(ImVec2(hat.x + ImGui::GetContentRegionAvail().x - ss.x - 4.0f, hat.y + style.FramePadding.y), T::BoneDim, summary.c_str());
				if (open && ImGui::BeginTable("rows", 6, ImGuiTableFlags_SizingFixedFit))
				{
					ImGui::TableSetupColumn("item", ImGuiTableColumnFlags_WidthFixed, iconS + 6.0f + nameW);
					ImGui::TableSetupColumn("have", ImGuiTableColumnFlags_WidthFixed, haveW);
					ImGui::TableSetupColumn("bar", ImGuiTableColumnFlags_WidthFixed, barW);
					ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthFixed, 0.0f);
					ImGui::TableSetupColumn("off", ImGuiTableColumnFlags_WidthFixed, offW);
					dl = ImGui::GetWindowDrawList();
					for (const auto& [what, n] : byKind[k])
					{
						ImGui::PushID(what.c_str());
						ImGui::TableNextRow(0, rowH);
						const std::string item = StockGoals::ItemFor(t, what);
						const std::string label = GoalLabel(t, what);
						const int have = StockGoals::Have(t, what);
						// the thing
						ImGui::TableNextColumn();
						{
							const ImVec2 at = ImGui::GetCursorScreenPos();
							ImGui::Dummy(ImVec2(iconS + 6.0f + nameW, rowH));
							if (!IconAt(dl, what, at.x, at.y + (rowH - iconS) * 0.5f, iconS)) IconAt(dl, item, at.x, at.y + (rowH - iconS) * 0.5f, iconS);
							dl->AddText(ImVec2(at.x + iconS + 6.0f, at.y + (rowH - lineH) * 0.5f), T::Bone, label.c_str());
							if (what.rfind("Item.", 0) == 0 && ImGui::IsItemHovered())
								ImGui::SetTooltip("%s", (item.empty() ? L.Text("Any of this kind; the clan has no way to get one yet") : L.Text("Any of this kind; now it goes for {0}", L.Name("item", item))).c_str());
						}
						// what it has
						ImGui::TableNextColumn();
						{
							const ImVec2 at = ImGui::GetCursorScreenPos();
							ImGui::Dummy(ImVec2(haveW, rowH));
							const std::string h = std::to_string(have);
							const ImVec2 hs = ImGui::CalcTextSize(h.c_str());
							dl->AddText(ImVec2(at.x + haveW - hs.x, at.y + (rowH - lineH) * 0.5f), T::Bone, h.c_str());
						}
						// the bar and its handle, one piece: a drag moves the handle, the goal is queued once on letting go
						ImGui::TableNextColumn();
						{
							const ImVec2 at = ImGui::GetCursorScreenPos();
							ImGui::InvisibleButton("goal", ImVec2(barW, rowH));
							const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
							const bool dragging = m_Colony.Drag == "stock " + what;
							// the scale: up to twice the goal or what it has, in tens (fixed while the handle is held)
							const int base = dragging ? m_Colony.DragFrom : n;
							const int scale = std::clamp(((std::max({ 2 * base, have, 10 }) + 9) / 10) * 10, 10, StockGoals::kMost);
							const float x0 = at.x + 4.0f, x1 = at.x + barW - 4.0f;
							int goal = n;
							if (active)
							{
								if (!dragging) { m_Colony.Drag = "stock " + what; m_Colony.DragFrom = n; }
								const float f = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
								m_Colony.DragTo = std::clamp(static_cast<int>(std::lround(f * static_cast<float>(scale))), 1, StockGoals::kMost);
								goal = m_Colony.DragTo;
							}
							if (ImGui::IsItemDeactivated() && dragging)
							{
								if (m_Colony.DragTo != m_Colony.DragFrom) run("game stock " + what + " " + std::to_string(m_Colony.DragTo));
								m_Colony.Drag.clear();
							}
							if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
							const float by = at.y + 6.0f, bh = 10.0f;
							const float fill = std::clamp(static_cast<float>(have) / static_cast<float>(scale), 0.0f, 1.0f);
							const float hx = x0 + (x1 - x0) * std::clamp(static_cast<float>(goal) / static_cast<float>(scale), 0.0f, 1.0f);
							dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + bh), T::HideRaised, 3.0f);
							if (fill > 0.0f) dl->AddRectFilled(ImVec2(x0, by), ImVec2(x0 + (x1 - x0) * fill, by + bh), have >= goal ? T::Good : T::Warn, 3.0f);
							dl->AddRect(ImVec2(x0, by), ImVec2(x1, by + bh), T::HideLine, 3.0f);
							dl->AddRectFilled(ImVec2(hx - 4.0f, by - 4.0f), ImVec2(hx + 4.0f, by + bh + 4.0f), active ? T::OchreHover : T::Ochre, 2.0f);
							dl->AddRect(ImVec2(hx - 4.0f, by - 4.0f), ImVec2(hx + 4.0f, by + bh + 4.0f), T::Outline, 2.0f);
							const std::string g = L.Text("goal {0}", goal);
							const ImVec2 gs = ImGui::CalcTextSize(g.c_str());
							dl->AddText(ImVec2(std::clamp(hx - gs.x * 0.5f, at.x, at.x + barW - gs.x), by + bh + 2.0f), active ? T::OchreHover : T::Ochre, g.c_str());
							if (hovered && !active)
								ImGui::SetTooltip("%s", (label + " \xC2\xB7 " + L.Text("goal {0}", n) + "\n" + L.Text("Drag the handle: the goal is set when you let go") + "\n"
									+ L.Text("The store and the bags of the clan hold {0}; you want {1}", have, n)).c_str());
						}
						// what the clan does about it: an icon and a word; stuck, a red cross and why
						ImGui::TableNextColumn();
						const std::string raw = StockGoals::Status(t, what, n);
						const auto [status, colour] = StockStatusText(raw);
						{
							const ImVec2 at = ImGui::GetCursorScreenPos();
							const float s = 18.0f, sy = at.y + (rowH - s) * 0.5f;
							const char* icon = raw == "ok" ? "" : raw.rfind("making", 0) == 0 ? "make" : raw.rfind("gathering", 0) == 0 ? "gather" : raw.rfind("hunting", 0) == 0 ? "hunt" : "";
							const bool stuck = colour == T::Bad || colour == T::Warn;
							ImGui::Dummy(ImVec2(s, rowH));
							if (stuck)   // (a cross: the colour is not the only sign)
							{
								dl->AddLine(ImVec2(at.x + 3.0f, sy + 3.0f), ImVec2(at.x + s - 3.0f, sy + s - 3.0f), colour, 2.5f);
								dl->AddLine(ImVec2(at.x + s - 3.0f, sy + 3.0f), ImVec2(at.x + 3.0f, sy + s - 3.0f), colour, 2.5f);
							}
							else if (raw == "ok")   // (a tick)
							{
								dl->AddLine(ImVec2(at.x + 3.0f, sy + s * 0.55f), ImVec2(at.x + s * 0.42f, sy + s - 4.0f), T::Good, 2.5f);
								dl->AddLine(ImVec2(at.x + s * 0.42f, sy + s - 4.0f), ImVec2(at.x + s - 3.0f, sy + 4.0f), T::Good, 2.5f);
							}
							else if (!IconAt(dl, icon, at.x, sy, s)) dl->AddCircle(ImVec2(at.x + s * 0.5f, sy + s * 0.5f), 4.0f, T::BoneDim, 10, 1.5f);
							ImGui::SameLine(0.0f, 6.0f);
							// (the words in bone: "bad" is no colour for text on the panel, its contrast is 4.4)
							ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, at.y + (rowH - lineH) * 0.5f));
							ImGui::TextUnformatted(status.c_str());
							if (stuck && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan cannot keep it now: {0}", status).c_str());
						}
						ImGui::TableNextColumn();
						// its removal (no trash can in the atlas yet, ART-1: a bone cross, not the red one of a stuck goal)
						ImGui::TableNextColumn();
						ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + (rowH - ImGui::GetFrameHeight()) * 0.5f));
						if (ImGui::Button("\xC3\x97###off", ImVec2(ImGui::GetFrameHeight(), 0.0f))) run("colony stock off " + what);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan stops keeping {0}", label).c_str());
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();
		}
		// add a goal: the grid of things (a few kinds first); the thing picked becomes a goal of the default number, set later on its row
		ImGui::Separator();
		if (ImGui::Button(("+ " + L.Text("Add a goal") + "###stockpick").c_str()) || m_Colony.OpenPicker) ImGui::OpenPopup("pick");
		m_Colony.OpenPicker = false;
		ImGui::SameLine();
		ImGui::TextDisabled("%s", L.Text("opens the grid of things").c_str());
		if (ImGui::BeginPopup("pick"))
		{
			auto choose = [&](const std::string& id) { run("colony stock pick " + id); run("colony stock set " + id + " " + std::to_string(m_Colony.StockCount)); ImGui::CloseCurrentPopup(); };
			// the kinds first (any food that keeps, any tool...), then every item with its icon, by its name
			static const char* kTags[] = { "Item.Food", "Item.Food.Preserved", "Item.Tool", "Item.Weapon", "Item.Container" };
			ImGui::TextDisabled("%s", L.Text("A kind of thing").c_str());
			auto carried = [&](const std::string& kind)   // (some item of the data carries that tag or one under it)
			{
				if (items)
					for (const std::string& id : items->Ids())
						if (const ItemDef* d = items->Find(id))
							for (const std::string& tag : d->Tags) if (tag.rfind(kind, 0) == 0) return true;
				return false;
			};
			for (const char* kind : kTags)
			{
				if (!carried(kind)) continue;
				if (ImGui::SmallButton((GoalLabel(t, kind) + "###k" + kind).c_str())) choose(kind);
				ImGui::SameLine();
			}
			ImGui::NewLine();
			ImGui::Separator();
			ImGui::TextDisabled("%s", L.Text("A thing").c_str());
			std::vector<std::pair<std::string, std::string>> sorted;   // (label, id)
			if (items) for (const std::string& id : items->Ids()) sorted.push_back({ GoalLabel(t, id), id });
			std::sort(sorted.begin(), sorted.end());
			ImGui::BeginChild("items", ImVec2(std::min(width, 520.0f), std::min(300.0f, maxH)), ImGuiChildFlags_None);
			const float cell = 36.0f, room = ImGui::GetContentRegionAvail().x;
			float x = 0.0f;
			for (const auto& [label, id] : sorted)
			{
				glm::vec2 a, b;
				if (!IconUv(id, a, b)) continue;
				if (x > 0.0f && x + cell + style.ItemSpacing.x <= room) ImGui::SameLine();
				else x = 0.0f;
				x += cell + style.ItemSpacing.x;
				if (IconButton(("###i" + id).c_str(), id, cell - 8.0f)) choose(id);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label.c_str());
			}
			// the things the atlas has no picture of: their name
			bool first = true;
			x = 0.0f;
			for (const auto& [label, id] : sorted)
			{
				glm::vec2 a, b;
				if (IconUv(id, a, b)) continue;
				if (first) { ImGui::NewLine(); first = false; }
				const float bw = ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
				if (x > 0.0f && x + bw + style.ItemSpacing.x <= room) ImGui::SameLine();
				else x = 0.0f;
				x += bw + style.ItemSpacing.x;
				if (ImGui::SmallButton((label + "###n" + id).c_str())) choose(id);
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
	}

	// ------------------------------------------------------------------ the person's sheet: why, and its row of priorities
	void EditorApp::DrawPersonWork(const Tribe& t, const TribeMember& m)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const WorkPriorities& table = t.WorkTable();
		const WorkPriorities::Table* own = table.Of(m.Name);
		const WorkPriorities::Table tab = own ? *own : WorkPriorities::Suggested(t, m);
		ImGui::TextDisabled("%s", L.Text("Priorities").c_str());
		ImGui::SameLine(110.0f);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(own ? T::Ochre : T::BoneDim), "%s", (own ? L.Text("follows your table") : L.Text("decides as the AI")).c_str());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", own ? L.Text("{0} takes the work of its lowest number first", m.Name).c_str()
				: L.Text("{0} decides as the AI. The numbers are what its trade ({1}) suggests: the first one you change starts its table from them", m.Name, L.Name("role", m.Role)).c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton((L.Text("Change") + "###worktab").c_str())) m_UiCommands.push_back("colony tab work");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Open the Work table").c_str());
		// "Alimento 1  Construcción 2 ..." wrapping in the sheet's width
		ImGui::Indent(12.0f);
		const float room = ImGui::GetContentRegionAvail().x;
		float x = 0.0f;
		bool first = true;
		for (const WorkCategoryDef& c : t.Config.Work.Categories.List)
		{
			const int n = CategoryCell(tab, c).Number;
			const std::string text = WorkLabel(c.Id) + " " + WorkPriorities::Text(n);
			const float tw = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetTextLineHeight() + 3.0f;
			if (!first && x + 14.0f + tw <= room) { ImGui::SameLine(0.0f, 14.0f); x += 14.0f + tw; }
			else x = tw;
			first = false;
			ImGui::BeginGroup();
			if (Icon(WorkIcon(c.Id), ImGui::GetTextLineHeight())) ImGui::SameLine(0.0f, 3.0f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(!own || n == WorkPriorities::kOff ? T::BoneDim : n == 1 ? T::Ochre : T::Bone), "%s", text.c_str());
			ImGui::EndGroup();
		}
		ImGui::Unindent(12.0f);
	}

	// the reason of what a person does now, in the player's words ("" = none to tell)
	std::string EditorApp::PersonWhy(const Tribe& t, const TribeMember& m) const
	{
		const Lang& L = m_Lang;
		if (const ColonyOrders::Mandate* order = t.Colony().Of(m.Name))
		{
			const std::string& v = order->Verb;
			const std::string what = v == "goto" ? L.Text("go there") : v == "use" ? L.Text("gather at {0}", L.Entity(order->Target))
				: v == "build" ? L.Text("build {0}", L.Entity(order->Target)) : v == "deliver" ? L.Text("carry material to {0}", L.Entity(order->Target))
				: v == "talk" ? L.Text("talk to {0}", order->Target) : v == "follow" ? L.Text("follow {0}", order->Target) : L.Doing(v, order->Target);
			return L.Text("your order: {0}", what);
		}
		if (m.Job == "rest" && !m.Why.empty()) return Lang::Cap(L.Line(m.Why));
		const AgentMind& mind = m.Mind;
		if (mind.Chosen < 0 || static_cast<size_t>(mind.Chosen) >= mind.Options.size()) return {};
		const UtilityOption& o = mind.Options[static_cast<size_t>(mind.Chosen)];
		if (o.Note.empty()) return {};
		// T336 (L2): "... does not know Hide working" names the technology by its English name: its id, so that the phrase finds its name
		std::string note = o.Note;
		static const std::string kKnow = " does not know ";
		if (const size_t k = note.find(kKnow); k != std::string::npos)
			if (const TechTree* tree = t.Knowledge().Techs())
			{
				const size_t from = k + kKnow.size(), to = std::min(note.find(';', from), note.size());
				const std::string name = note.substr(from, to - from);
				for (const auto& [id, def] : tree->Techs) if (def.Name == name) { note.replace(from, to - from, id); break; }
			}
		return Lang::Cap(L.Line(note));
	}
}
