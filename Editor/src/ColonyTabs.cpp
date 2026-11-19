// T308 (C2-c, documentation/01_diseno/MODO_COLONIA.md §4.1 and §4.3): the colony's tabs Work and Stock, drawn over the god's bar ([Work] and
// [Stock] next to [Build] and [Research]), and the person's row of priorities in its sheet.
//   Work:  a person x category table of the clan's priorities (GB/Game/WorkPriorities.h). A category column unfolds into its jobs. A click on a
//          cell puts it before (a lower number), a right click after, the wheel either way; "-" is never. A person whose table the player never
//          touched decides as the clans of the AI: its row is dimmed and shows what its trade suggests. [As the AI] gives a table back.
//   Stock: the clan's goals "keep N of X" (GB/Game/StockGoals.h): what it has against what it wants, and what it is doing about it.
//   Knowledge (T324): the research queue (GB/Game/ResearchQueue.h): its entries with their progress (first, remove) and what the clan can
//          research now to add; what it cannot yet is never named (ARBOL_TECNICO §11.2.5), only "? N more". It queues `game research ...`.
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
#include <optional>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
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
		// before: 10 .. 1 (1 stays 1; "-" comes back as 10); after: 1 .. 10, then "-" (which stays)
		const int next = up ? (now == WorkPriorities::kOff ? WorkPriorities::kMax : std::max(1, now - 1))
			: (now == WorkPriorities::kOff || now >= WorkPriorities::kMax ? WorkPriorities::kOff : now + 1);
		if (next == now) return {};
		return "game priority " + person + " " + what + " " + WorkPriorities::Text(next);
	}

	CommandResult EditorApp::ColonyTabCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a[0];
		const char* usage = "colony tab [work|stock|research|off] | colony work fold|unfold <category> | colony work step <person> <category|job> up|down | colony work reset <person> "
			"| colony stock picker | colony stock pick <item|tag> [n] | colony stock set <item|tag> <n> | colony stock off <item|tag>";
		if (sub == "tab")
		{
			if (a.size() == 1) return { true, m_Colony.Tab.empty() ? std::string("no tab open") : "tab " + m_Colony.Tab + " open" };
			if (a[1] == "off") { m_Colony.Tab.clear(); return { true, "tab closed" }; }
			if (a[1] != "work" && a[1] != "stock" && a[1] != "research") return { false, usage };
			if (!ColonyTribe()) return { false, "no colony: director colony on, and a world with clans" };
			if (!m_Hand.Menu.empty() || !m_Hand.Building.empty()) Execute("director hand off");   // (one panel over the bar at a time)
			m_Colony.Tab = a[1];
			return { true, "tab " + a[1] + " open" };
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
		ImGui::SetNextWindowPos(ImVec2((left + right) * 0.5f, bottom), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(room, maxH));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
		if (ImGui::Begin("ColonyTab##colony", nullptr, flags))
		{
			if (m_Colony.Tab == "work") DrawWorkTab(*t, room - 2.0f * T::Pad, maxH);
			else if (m_Colony.Tab == "research") DrawResearchTab(*t, room - 2.0f * T::Pad, maxH);
			else DrawStockTab(*t, room - 2.0f * T::Pad, maxH);
			m_Colony.TabBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		}
		ImGui::End();
	}

	// the title of a tab: its name, a line of help and [Close]
	void EditorApp::ColonyTabTitle(const std::string& title, const std::string& help)
	{
		namespace T = GameTheme;
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", title.c_str());
		ImGui::SameLine(0.0f, 12.0f);
		ImGui::TextDisabled("%s", help.c_str());
		ImGui::SameLine(0.0f, 12.0f);
		if (ImGui::SmallButton((m_Lang.Text("Close") + "###tabclose").c_str())) m_UiCommands.push_back("colony tab off");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_Lang.Text("Close the panel (Esc)").c_str());
		ImGui::Separator();
	}

	void EditorApp::DrawWorkTab(const Tribe& t, float width, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const WorkCategories& cats = t.Config.Work.Categories;
		const WorkPriorities& table = t.WorkTable();
		ColonyTabTitle(L.Text("Work"), L.Text("1 first, 10 last, - never.  Click: sooner   Right click: later   Wheel: either way"));
		if (cats.Empty()) { ImGui::TextDisabled("%s", L.Text("The data has no categories of work").c_str()); return; }
		std::vector<const TribeMember*> people;
		for (const TribeMember& m : t.Members()) if (!m.Dead && !m.Player) people.push_back(&m);
		if (people.empty()) { ImGui::TextDisabled("%s", L.Text("Nobody in the clan").c_str()); return; }

		// the columns: who, then each category (and its jobs when it is unfolded), then [As the AI]
		struct Column { const WorkCategoryDef* Cat; const WorkJobDef* Job; float W; };
		std::vector<Column> columns;
		const ImGuiStyle& style = ImGui::GetStyle();
		const float icon = ImGui::GetTextLineHeight();
		for (const WorkCategoryDef& c : cats.List)
		{
			const float head = ImGui::CalcTextSize(("[+] " + WorkLabel(c.Id)).c_str()).x + icon + 8.0f;
			columns.push_back({ &c, nullptr, std::max(head, 46.0f) });
			if (m_Colony.Unfolded.count(c.Id))
				for (const WorkJobDef& j : c.Jobs) columns.push_back({ &c, &j, std::max(ImGui::CalcTextSize(WorkLabel(j.Id).c_str()).x + icon + 8.0f, 40.0f) });
		}
		float whoW = 0.0f;
		for (const TribeMember* m : people) whoW = std::max(whoW, ImGui::CalcTextSize((m->Name + "  " + L.Name("role", m->Role)).c_str()).x);
		whoW += 8.0f;
		const std::string asAi = L.Text("As the AI");
		const std::string yours = L.Text("your table");
		const float lastW = std::max(ImGui::CalcTextSize(yours.c_str()).x + 6.0f + ImGui::CalcTextSize(asAi.c_str()).x + style.FramePadding.x * 2.0f,
			ImGui::CalcTextSize(L.Text("as the AI").c_str()).x) + 4.0f;
		float need = whoW + lastW;
		for (const Column& c : columns) need += c.W;
		need += (static_cast<float>(columns.size()) + 2.0f) * (style.CellPadding.x * 2.0f + 1.0f) + style.ScrollbarSize;   // (room for the vertical bar: no horizontal one when it all fits)
		const float rowH = ImGui::GetFrameHeight();
		const float tableH = std::min((static_cast<float>(people.size()) + 2.0f) * (rowH + style.CellPadding.y * 2.0f) + 6.0f, maxH - ImGui::GetFrameHeightWithSpacing() * 3.0f);
		const ImGuiTableFlags tflags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
		if (!ImGui::BeginTable("work", static_cast<int>(columns.size()) + 2, tflags, ImVec2(std::min(need, width), std::max(tableH, rowH * 3.0f)))) return;
		ImGui::TableSetupScrollFreeze(2, 2);   // (who and whether it follows a table stay in sight when the jobs scroll)
		ImGui::TableSetupColumn("who", ImGuiTableColumnFlags_WidthFixed, whoW);
		ImGui::TableSetupColumn("ai", ImGuiTableColumnFlags_WidthFixed, lastW);
		for (size_t i = 0; i < columns.size(); ++i) ImGui::TableSetupColumn(columns[i].Job ? columns[i].Job->Id.c_str() : columns[i].Cat->Id.c_str(), ImGuiTableColumnFlags_WidthFixed, columns[i].W);

		auto iconText = [&](const std::string& id, const std::string& text, ImU32 colour)
		{
			if (Icon(WorkIcon(id), icon)) ImGui::SameLine(0.0f, 3.0f);
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour), "%s", text.c_str());
		};
		// header, first row: the categories (a click folds or unfolds one)
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", L.Text("Who").c_str());
		ImGui::TableNextColumn();
		ImGui::TextDisabled("%s", L.Text("Decides by").c_str());
		for (const Column& c : columns)
		{
			ImGui::TableNextColumn();
			if (c.Job) continue;
			const bool open = m_Colony.Unfolded.count(c.Cat->Id) != 0;
			ImGui::PushID(c.Cat->Id.c_str());
			const ImVec2 at = ImGui::GetCursorScreenPos();
			if (ImGui::InvisibleButton("fold", ImVec2(c.W, ImGui::GetTextLineHeight()))) m_UiCommands.push_back(std::string("colony work ") + (open ? "fold " : "unfold ") + c.Cat->Id);
			const bool hovered = ImGui::IsItemHovered();
			if (hovered)
			{
				std::string jobs;
				for (const WorkJobDef& j : c.Cat->Jobs) jobs += (jobs.empty() ? "" : ", ") + L.Name("work", j.Id);
				ImGui::SetTooltip("%s", (L.Text(open ? "Fold {0}" : "Unfold {0} into its jobs", WorkLabel(c.Cat->Id)) + "\n" + jobs).c_str());
			}
			ImGui::SetCursorScreenPos(at);
			iconText(c.Cat->Id, std::string(open ? "[-] " : "[+] ") + WorkLabel(c.Cat->Id), hovered ? T::OchreHover : open ? T::Ochre : T::Bone);
			ImGui::PopID();
		}
		// header, second row: the jobs of the unfolded categories
		ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
		ImGui::TableNextColumn();
		ImGui::TableNextColumn();
		for (const Column& c : columns)
		{
			ImGui::TableNextColumn();
			if (c.Job) iconText(c.Job->Id, WorkLabel(c.Job->Id), T::BoneDim);
			else if (m_Colony.Unfolded.count(c.Cat->Id)) ImGui::TextDisabled("%s", L.Text("all").c_str());
		}

		const ImGuiIO& io = ImGui::GetIO();
		for (const TribeMember* m : people)
		{
			const WorkPriorities::Table* own = table.Of(m->Name);
			const WorkPriorities::Table tab = own ? *own : WorkPriorities::Suggested(t, *m);
			const bool selected = std::find(m_Colony.Selection.begin(), m_Colony.Selection.end(), m->Name) != m_Colony.Selection.end();
			ImGui::PushID(m->Name.c_str());
			ImGui::TableNextRow(0, rowH);
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(selected ? T::Ochre : own ? T::Bone : T::BoneDim), "%s", m->Name.c_str());
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", own ? L.Text("{0} follows your table", m->Name).c_str()
					: L.Text("{0} decides as the AI. The numbers are what its trade ({1}) suggests: the first one you change starts its table from them", m->Name, L.Name("role", m->Role)).c_str());
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextDisabled("%s", L.Name("role", m->Role).c_str());
			ImGui::TableNextColumn();
			if (own)
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", yours.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} follows your table", m->Name).c_str());
				ImGui::SameLine(0.0f, 6.0f);
				if (ImGui::SmallButton((asAi + "###reset").c_str())) m_UiCommands.push_back("colony work reset " + m->Name);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Drop the table of {0}: it decides again as the AI does", m->Name).c_str());
			}
			else
			{
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled("%s", L.Text("as the AI").c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} has no table of yours: change any number to give it one", m->Name).c_str());
			}
			for (const Column& c : columns)
			{
				ImGui::TableNextColumn();
				const WorkCell cell = c.Job ? JobCell(tab, *c.Cat, c.Job->Id) : CategoryCell(tab, *c.Cat);
				const std::string what = c.Job ? c.Job->Id : c.Cat->Id;
				ImGui::PushID(what.c_str());
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::InvisibleButton("cell", ImVec2(c.W, rowH), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				const bool hovered = ImGui::IsItemHovered();
				int step = 0;   // -1: sooner (a lower number), +1: later
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) step = -1;
				else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) step = 1;
				else if (hovered && io.MouseWheel != 0.0f) step = io.MouseWheel > 0.0f ? -1 : 1;
				if (hovered) ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // (the wheel sets the number: the table does not scroll under it)
				if (step != 0) m_UiCommands.push_back("colony work step " + m->Name + " " + what + (step < 0 ? " up" : " down"));
				ImDrawList* dl = ImGui::GetWindowDrawList();
				if (hovered) dl->AddRectFilled(at, ImVec2(at.x + c.W, at.y + rowH), T::HideHover, 3.0f);
				const std::string text = WorkPriorities::Text(cell.Number);
				// own numbers bright (the first ochre), inherited ones and "-" dim; a person as the AI all dim
				ImU32 colour = cell.Number == WorkPriorities::kOff || !cell.Own ? T::BoneDim : cell.Number == 1 ? T::Ochre : T::Bone;
				if (!own) colour = T::Faded(T::BoneDim, 0.7f);
				const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
				dl->AddText(ImVec2(at.x + (c.W - ts.x) * 0.5f, at.y + (rowH - ts.y) * 0.5f), colour, text.c_str());
				if (hovered)
				{
					std::string tip = m->Name + ": " + (c.Job ? WorkLabel(c.Job->Id) + " (" + L.Name("work", c.Cat->Id) + ")" : WorkLabel(c.Cat->Id)) + " " + text;
					if (c.Job && !cell.Own) tip += "\n" + (cell.Number == WorkPriorities::kOff && CategoryCell(tab, *c.Cat).Number == WorkPriorities::kOff
						? L.Text("Off because {0} is at -", L.Name("work", c.Cat->Id)) : L.Text("The number of {0} (it has none of its own)", L.Name("work", c.Cat->Id)));
					// the aptitude in that work (the skills' family it trains), else the trade
					std::string family;
					if (c.Job) family = JobFamily(t, *c.Job);
					else for (const WorkJobDef& j : c.Cat->Jobs) if (family.empty()) family = JobFamily(t, j);
					if (!family.empty()) tip += "\n" + L.Text("Aptitude in {0}: {1} of 100", L.Name("skill", family), L.Num(SkillLevel(m->Skills, family), 0));
					else tip += "\n" + L.Text("No aptitude is measured for it; trade: {0}", L.Name("role", m->Role));
					if (!own) tip += "\n" + L.Text("Decides as the AI: this is what its trade suggests");
					tip += "\n" + L.Text("Click: sooner   Right click: later   Wheel: either way");
					ImGui::SetTooltip("%s", tip.c_str());
				}
				ImGui::PopID();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// T324: the research queue (ResearchQueue.h). The buttons only queue `game research ...`.
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
		const std::vector<std::string> open = ResearchQueue::Open(t);
		if (queue.Empty()) ImGui::TextDisabled("%s", L.Text("The queue is empty: the clan chooses what to seek. Add one below").c_str());
		else if (ImGui::BeginTable("research", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
			ImVec2(std::min(width, 760.0f), std::min((static_cast<float>(queue.All().size()) + 1.0f) * (ImGui::GetTextLineHeight() + 10.0f) + 4.0f, maxH - 110.0f))))
		{
			std::map<std::string, float> progress;
			for (const ClanKnowledge::ResearchLine& l : t.Knowledge().Research()) progress[l.Tech] = l.Progress;
			const std::string current = queue.Current(t);
			ImGui::TableSetupColumn("tech", ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("progress", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("top", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(L.Text("First").c_str()).x + 12.0f);
			ImGui::TableSetupColumn("off", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(L.Text("Remove").c_str()).x + 12.0f);
			for (size_t i = 0; i < queue.All().size(); ++i)
			{
				const std::string& id = queue.All()[i];
				ImGui::PushID(id.c_str());
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted((std::to_string(i + 1) + ". " + name(id)).c_str());
				ImGui::TableNextColumn();
				const auto p = progress.find(id);
				ImGui::Text("%d%%", static_cast<int>(std::lround((p == progress.end() ? 0.0f : p->second) * 100.0f)));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("How far the one furthest on it has worked it out").c_str());
				ImGui::TableNextColumn();
				// ResearchQueue::Status in the player's words; a prerequisite is named only when the clan could research it now
				const std::string s = ResearchQueue::Status(t, id, id == current);
				std::pair<std::string, unsigned> said{ s, T::Bone };
				if (s == "researching") said = { L.Text("being studied"), T::Good };
				else if (s == "next") said = { L.Text("next"), T::Bone };
				else if (s == "cannot be found by trying") said = { L.Text("cannot be found by trying: it is learnt from others"), T::Warn };
				else if (s == "the world does not let it") said = { L.Text("the world does not give what it needs yet"), T::Warn };
				else if (s.rfind("waits for ", 0) == 0)
				{
					const std::string need = s.substr(10);
					said = { std::find(open.begin(), open.end(), need) != open.end() ? L.Text("waits for {0}", name(need)) : L.Text("waits for something the clan does not know yet"), T::Warn };
				}
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
		for (const std::string& id : open)
		{
			if (std::find(queue.All().begin(), queue.All().end(), id) != queue.All().end()) continue;
			if (shown++ > 0) ImGui::SameLine();
			if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name(id).c_str()).x + 16.0f) ImGui::NewLine();
			if (ImGui::SmallButton((name(id) + "###add" + id).c_str())) run("game research add " + id);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Add {0} to the end of the queue", name(id)).c_str());
		}
		if (shown == 0) ImGui::TextDisabled("%s", L.Text("Nothing new: it must learn more first").c_str());
		if (const int hidden = ResearchQueue::Hidden(t); hidden > 0)
		{
			ImGui::TextDisabled("%s", L.Text("? {0} more", hidden).c_str());
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("What the clan cannot study yet: it needs what it does not know").c_str());
		}
	}

	void EditorApp::DrawStockTab(const Tribe& t, float width, float maxH)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		const StockGoals& goals = t.Goals();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		ColonyTabTitle(L.Text("Stock"), L.Text("What the clan keeps: when it has less, it makes, gathers or hunts the rest"));
		const float icon = ImGui::GetTextLineHeight() + 4.0f;
		if (goals.All().empty()) ImGui::TextDisabled("%s", L.Text("No goals yet: add one below").c_str());
		else if (ImGui::BeginTable("stock", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
			ImVec2(std::min(width, 820.0f), std::min((static_cast<float>(goals.All().size()) + 1.0f) * (icon + 6.0f) + 4.0f, maxH - 110.0f))))
		{
			ImGui::TableSetupColumn("item", ImGuiTableColumnFlags_WidthFixed, 170.0f);
			ImGui::TableSetupColumn("have", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("n", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("off", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize(L.Text("Remove").c_str()).x + 12.0f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
			ImGui::TableNextColumn(); ImGui::TextDisabled("%s", L.Text("Keep").c_str());
			ImGui::TableNextColumn(); ImGui::TextDisabled("%s", L.Text("Have / want").c_str());
			ImGui::TableNextColumn(); ImGui::TextDisabled("%s", L.Text("What the clan does").c_str());
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			for (const auto& [what, n] : goals.All())
			{
				ImGui::PushID(what.c_str());
				ImGui::TableNextRow(0, icon + 4.0f);
				ImGui::TableNextColumn();
				const std::string item = StockGoals::ItemFor(t, what);
				if (!Icon(what, icon) && !Icon(item, icon)) ImGui::Dummy(ImVec2(icon, icon));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(GoalLabel(t, what).c_str());
				if (what.rfind("Item.", 0) == 0 && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", (item.empty() ? L.Text("Any of this kind; the clan has no way to get one yet") : L.Text("Any of this kind; now it goes for {0}", L.Name("item", item))).c_str());
				ImGui::TableNextColumn();
				const int have = StockGoals::Have(t, what);
				const float v01 = std::clamp(static_cast<float>(have) / static_cast<float>(std::max(n, 1)), 0.0f, 1.0f);
				ImGui::AlignTextToFramePadding();
				const ImVec2 at = ImGui::GetCursorScreenPos();
				ImGui::Dummy(ImVec2(70.0f, ImGui::GetTextLineHeight()));
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const float y0 = at.y + (ImGui::GetTextLineHeight() - 10.0f) * 0.5f + 1.0f;
				dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + 70.0f, y0 + 10.0f), T::HideRaised, 3.0f);
				if (v01 > 0.0f) dl->AddRectFilled(ImVec2(at.x, y0), ImVec2(at.x + 70.0f * v01, y0 + 10.0f), have >= n ? T::Good : T::Ochre, 3.0f);
				dl->AddRect(ImVec2(at.x, y0), ImVec2(at.x + 70.0f, y0 + 10.0f), T::HideLine, 3.0f);
				ImGui::SameLine();
				ImGui::TextUnformatted(L.Text("{0} / {1}", have, n).c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The store and the bags of the clan hold {0}; you want {1}", have, n).c_str());
				ImGui::TableNextColumn();
				const auto [status, colour] = StockStatusText(StockGoals::Status(t, what, n));
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour), "%s", status.c_str());
				ImGui::TableNextColumn();
				if (ImGui::SmallButton("-###less")) run(n > 1 ? "game stock " + what + " " + std::to_string(n - 1) : "game stock " + what + " off");
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Keep one less").c_str());
				ImGui::SameLine(0.0f, 4.0f);
				if (ImGui::SmallButton("+###more")) run("game stock " + what + " " + std::to_string(std::min(n + 1, StockGoals::kMost)));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Keep one more").c_str());
				ImGui::TableNextColumn();
				if (ImGui::SmallButton((L.Text("Remove") + "###off").c_str())) run("colony stock off " + what);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan stops keeping {0}", GoalLabel(t, what)).c_str());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		// add a goal: what (a grid of items with their icons, and a few kinds), how many, [Add]
		ImGui::Separator();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", L.Text("New goal").c_str());
		ImGui::SameLine();
		const std::string& pick = m_Colony.StockPick;
		if (!pick.empty())
		{
			if (Icon(pick, ImGui::GetFrameHeight())) ImGui::SameLine(0.0f, 4.0f);
		}
		if (ImGui::Button(((pick.empty() ? L.Text("Choose what") : GoalLabel(t, pick)) + "###stockpick").c_str()) || m_Colony.OpenPicker) ImGui::OpenPopup("pick");
		m_Colony.OpenPicker = false;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The thing (or the kind of thing) the clan keeps").c_str());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		int count = m_Colony.StockCount;
		if (ImGui::InputInt("###stockcount", &count)) m_Colony.StockCount = std::clamp(count, 1, StockGoals::kMost);   // (UI state only: nothing changes until [Add])
		ImGui::SameLine();
		ImGui::BeginDisabled(pick.empty());
		{
			std::optional<T::PrimaryScope> lit;
			if (!pick.empty()) lit.emplace();
			if (ImGui::Button((L.Text("Add") + "###stockadd").c_str())) run("colony stock set " + pick + " " + std::to_string(m_Colony.StockCount));
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", pick.empty() ? L.Text("Choose what first").c_str() : L.Text("The clan keeps {0} of {1}", m_Colony.StockCount, GoalLabel(t, pick)).c_str());
		if (ImGui::BeginPopup("pick"))
		{
			const ItemLibrary* items = t.Items();
			// the kinds first (any food that keeps, any tool...), then every item with its icon, by its name
			static const char* kKinds[] = { "Item.Food", "Item.Food.Preserved", "Item.Tool", "Item.Weapon", "Item.Container" };
			ImGui::TextDisabled("%s", L.Text("A kind of thing").c_str());
			auto carried = [&](const std::string& kind)   // (some item of the data carries that tag or one under it)
			{
				if (items)
					for (const std::string& id : items->Ids())
						if (const ItemDef* d = items->Find(id))
							for (const std::string& tag : d->Tags) if (tag.rfind(kind, 0) == 0) return true;
				return false;
			};
			for (const char* kind : kKinds)
			{
				if (!carried(kind)) continue;
				if (ImGui::SmallButton((GoalLabel(t, kind) + "###k" + kind).c_str())) { run(std::string("colony stock pick ") + kind); ImGui::CloseCurrentPopup(); }
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
				if (x > 0.0f && x + cell + ImGui::GetStyle().ItemSpacing.x <= room) ImGui::SameLine();
				else x = 0.0f;
				x += cell + ImGui::GetStyle().ItemSpacing.x;
				if (IconButton(("###i" + id).c_str(), id, cell - 8.0f)) { run("colony stock pick " + id); ImGui::CloseCurrentPopup(); }
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
				const float bw = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				if (x > 0.0f && x + bw + ImGui::GetStyle().ItemSpacing.x <= room) ImGui::SameLine();
				else x = 0.0f;
				x += bw + ImGui::GetStyle().ItemSpacing.x;
				if (ImGui::SmallButton((label + "###n" + id).c_str())) { run("colony stock pick " + id); ImGui::CloseCurrentPopup(); }
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
		return o.Note.empty() ? std::string() : Lang::Cap(L.Line(o.Note));
	}
}
