// T368 (2026-11-20, from the prototype of the same day) of TALLERES.md sections 10.8 (the yard's block of a house's sheet) and TL4 (the recipes of a
// building with a station). Two sections more in the sheet on the right of a building (DrawColonyThing, ColonyView.cpp) and, with a house selected,
// its slots, work points, the colour of each module's zone and the ghost of the module chosen in "Add a module" on the ground. The UI decides
// nothing: every button queues a `game module ...`, `game recipe ...`, `game yard ... closure ...` or `game grow ...` (Gameplay/EditorModules.cpp).
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Game/HouseModules.h"
#include "GB/Game/RecipeChoices.h"
#include "GB/Game/StockGoals.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <map>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;
		std::string s_MenuFor;      // the house whose list of modules is open ("" none): UI state only
		std::string s_GhostModule;  // the module chosen in that list (`game module menu <house> <module>`): its ghost on the ground
		std::string s_HoverGhost;   // the module whose row the mouse is on in that list (this frame's ghost, before the chosen one)

		std::string Under(std::string s)
		{
			for (char& c : s) if (c == ' ') c = '_';
			return s;
		}

		// The atlas has no icon yet for some products and stations: the nearest one it has (T368: noted for art in the report)
		std::string IconFor(const std::string& id)
		{
			static const std::map<std::string, std::string> nearest = {
				{ "baked_roots", "roots" }, { "cooked_fish", "fish" }, { "flour", "grain" }, { "parched_grain", "grain" }, { "porridge", "grain" },
				{ "quern", "grain" },
			};
			const auto it = nearest.find(id);
			return it != nearest.end() ? it->second : id;
		}

		// an icon for a module by its category (the atlas has no module icons yet: the thing it makes or keeps)
		std::string CategoryIcon(const std::string& category)
		{
			if (category == "kitchen") return "cooked_meat";
			if (category == "craft") return "stone_axe";
			if (category == "store") return "basket_wicker";
			if (category == "mill") return "grain";
			return "build";
		}

		// the English word of a category (Lang translates it)
		const char* CategoryWord(const std::string& category)
		{
			if (category == "kitchen") return "Kitchen";
			if (category == "craft") return "Workshop";
			if (category == "store") return "Store";
			if (category == "mill") return "Mill";
			return "Module";
		}

		// STUB (T368c): the colour of a module's zone by its category, from the theme's tokens. TODO(T367e): read `zoneColor` from the visuals
		// (GB/Visual, VisualDefs) when it exists. The category's word goes with it in the sheet, so the colour is never the only channel.
		ImU32 ZoneColor(const std::string& category)
		{
			if (category == "kitchen") return T::Bad;
			if (category == "craft") return T::Ochre;
			if (category == "store") return T::Good;
			if (category == "mill") return T::News;
			return T::BoneDim;
		}

		// STUB (T368a/d): what the engine does not give yet about a house's size and its closure. TODO(T367c/f/g, prog-gameplay): replace the
		// body with the engine's API (the house's size S/M/L and the next one with its cost and what it gains; the closure's level and the next
		// with its cost by metres) when it is merged. Until then nothing is known and the sheet shows "?" for it; the buttons queue their
		// commands all the same (`game yard <house> closure <level>`, `game grow <house> M|L`), which answer "unknown command" until then.
		struct YardUpgrades
		{
			bool SizeKnown = false;
			std::string Size;                       // "S", "M", "L"
			std::string NextSize = "M";             // what `game grow` asks for ("" = it cannot grow more)
			std::map<std::string, int> GrowCost;    // empty = unknown
			bool ClosureKnown = false;
			std::string Closure;                    // "fence", "wicker", "drystone" ("" = none yet)
			std::string NextClosure = "wicker";     // ("" = the best one already)
			std::map<std::string, int> ClosureCost; // empty = unknown
		};
		YardUpgrades YardUpgradesOf(const Tribe& tribe, const std::string& host)
		{
			(void)tribe;
			(void)host;
			return YardUpgrades{};
		}

		// The slot a module would take: as HouseModules::Add, the first free one of its kinds in the module's order (the engine may still skip
		// it when the ground refuses its site, and take the next).
		const HouseModules::SlotView* SlotFor(const std::vector<HouseModules::SlotView>& slots, const BuildingDef& module)
		{
			for (const std::string& kind : module.Module.Slots)
				for (const HouseModules::SlotView& v : slots)
					if (v.Site.empty() && v.Slot.Kind == kind) return &v;
			return nullptr;
		}
	}

	// `game module menu <house> [module]`: opens or closes the list of modules of a house's sheet (as the click on "Add a module": the captures);
	// with a module, opens it with that module chosen and its ghost on the ground
	void ToggleYardMenu(const std::string& host, const std::string& module)
	{
		if (!module.empty()) { s_MenuFor = host; s_GhostModule = module; return; }
		s_MenuFor = s_MenuFor == host ? std::string() : host;
		s_GhostModule.clear();
	}

	void EditorApp::DrawYardSheet(const std::string& name, const BuildingDef& def, float width)
	{
		const Tribe* own = ColonyTribe();
		if (!own) return;
		bool mine = false;
		for (const auto& site : own->SettlementSites()) if (site.Name == name) mine = true;
		if (!mine) return;
		const Lang& L = m_Lang;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImGuiStyle& style = ImGui::GetStyle();
		const float lh = ImGui::GetTextLineHeight();
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto itemName = [&](const std::string& id) { const ItemDef* d = m_Items.Find(id); return L.Name("item", id, d && !d->Name.empty() ? d->Name : std::string()); };
		auto buildingName = [&](const std::string& id) { const auto it = m_Buildings.Defs.find(id); return Lang::Cap(L.Name("building", id, it != m_Buildings.Defs.end() ? it->second.Name : id)); };
		auto title = [&](const std::string& text)
		{
			ImGui::Dummy(ImVec2(1.0f, 4.0f));
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", text.c_str());
			dl->AddLine(ImVec2(at.x, at.y + lh + 2.0f), ImVec2(at.x + width, at.y + lh + 2.0f), T::Faded(T::Ochre, 0.45f), 1.0f);
			ImGui::Dummy(ImVec2(1.0f, 3.0f));
		};
		auto icon = [&](const std::string& id, float side, unsigned tint)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(side, side));
			if (!IconAt(dl, IconFor(id), at.x, at.y, side, tint)) dl->AddRect(at, ImVec2(at.x + side, at.y + side), T::BoneDim, 3.0f);
		};
		// a word on the draw list, cut where `right` begins (the recipes' names before their column of buttons); true when it was cut
		auto clipped = [&](ImVec2 at, ImU32 ink, const std::string& text, float right)
		{
			const ImVec4 clip(at.x, at.y, right, at.y + lh + 2.0f);
			dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, ink, text.c_str(), nullptr, 0.0f, &clip);
			return ImGui::CalcTextSize(text.c_str()).x > right - at.x;
		};
		// the "?" of what the clan does not know yet, in a tile
		auto unknownTile = [&](ImVec2 at, float side)
		{
			dl->AddRectFilled(at, ImVec2(at.x + side, at.y + side), T::Hide, 3.0f);
			dl->AddRect(at, ImVec2(at.x + side, at.y + side), T::HideLine, 3.0f);
			const ImVec2 q = ImGui::CalcTextSize("?");
			dl->AddText(ImVec2(at.x + (side - q.x) * 0.5f, at.y + (side - q.y) * 0.5f), T::BoneDim, "?");
		};
		// a small icon at the start of a line of the yard's block (as the sheet's rows)
		auto lineIcon = [&](const std::string& id)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(lh + 2.0f, lh));
			if (!IconAt(dl, id, at.x, at.y - 1.0f, lh + 2.0f)) dl->AddRect(at, ImVec2(at.x + lh, at.y + lh), T::BoneDim, 2.0f);
			ImGui::SameLine(0.0f, 6.0f);
		};
		const std::string under = Under(name);
		const float left = ImGui::GetCursorScreenPos().x;
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);   // (popped at the end: every line of the sections wraps inside the sheet)

		// ================= the yard of a house (T368a)
		s_HoverGhost.clear();
		if (def.Yard.Any())
		{
			const HouseModules& mods = own->Modules();
			const std::vector<HouseModules::SlotView> slots = mods.Slots(*own, name);
			const YardShape yard = HouseModules::YardOf(*own, name);
			const YardUpgrades up = YardUpgradesOf(*own, name);
			const int count = mods.Count(name), max = def.Yard.MaxModules;
			title(L.Text("Yard"));
			// a small button on the right of its line, with its tooltip (the closure's and the size's improvements)
			auto rightButton = [&](const char* id, const std::string& label, const std::string& tip, const std::string& command)
			{
				const float bw = ImGui::CalcTextSize(label.c_str()).x + 2.0f * style.FramePadding.x;
				ImGui::SameLine();
				ImGui::SetCursorScreenPos(ImVec2(std::max(ImGui::GetCursorScreenPos().x, left + width - bw), ImGui::GetCursorScreenPos().y));
				if (ImGui::SmallButton((label + "###" + id).c_str())) run(command);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
			};
			auto costText = [&](const std::map<std::string, int>& cost)
			{
				if (cost.empty()) return L.Text("Its cost is not known yet");
				std::string text;
				for (const auto& [item, n] : cost) text += (text.empty() ? "" : ", ") + std::to_string(n) + " " + itemName(item);
				return L.Text("It costs {0}", text);
			};
			auto closureName = [&](const std::string& level) { return L.Text(level == "drystone" ? "Dry-stone wall" : level == "wicker" ? "Wicker hedge" : "Fence of stakes"); };
			// ---- the size of the house, and growing it (T368d: the button; its cost and the work under way come with T367g)
			lineIcon("building.shelter");
			ImGui::TextDisabled("%s", L.Text("Size").c_str());
			ImGui::SameLine();
			ImGui::TextUnformatted(up.SizeKnown ? up.Size.c_str() : "?");
			if (!up.SizeKnown && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The size of the house is not known yet").c_str());
			if (!up.NextSize.empty())
				rightButton("grow", L.Text("Grow to {0}", up.NextSize), L.Text("The house grows where it stands: more beds and more slots in its yard") + "\n" + costText(up.GrowCost),
					"game grow " + under + " " + up.NextSize);
			// ---- the closure: its level, its metres and its improvement
			lineIcon("stake");
			if (count == 0 && (!up.ClosureKnown || up.Closure.empty())) ImGui::TextDisabled("%s", L.Text("No fence yet: it goes up with the first module").c_str());
			else ImGui::TextUnformatted((closureName(up.ClosureKnown ? up.Closure : std::string("fence")) + " \xC2\xB7 " + L.Num(yard.Length, 0) + " m").c_str());
			if (!up.NextClosure.empty())
				rightButton("closure", L.Text("Improve"), L.Text("The fence becomes: {0}", closureName(up.NextClosure)) + "\n" + costText(up.ClosureCost) + "\n" +
					L.Text("The house stays in use while the work goes on"), "game yard " + under + " closure " + up.NextClosure);
			// ---- the modules it has and takes
			int freeCount = 0;
			for (const HouseModules::SlotView& v : slots) if (v.Site.empty()) ++freeCount;
			lineIcon("build");
			ImGui::TextUnformatted(L.Text("Modules {0}/{1}", count, max).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", L.Text("\xC2\xB7 {0} free slots", freeCount).c_str());
			// ---- the slots as a grid: the taken ones with their module's icon in the colour of its zone, then a "+" for each module the house
			// still takes (a click opens the list); the rest of the free slots counted after them
			{
				const float side = lh + 14.0f, gap = 4.0f;
				std::vector<const HouseModules::SlotView*> order;
				for (const HouseModules::SlotView& v : slots) if (!v.Site.empty()) order.push_back(&v);
				int freeShown = 0, freeLeft = 0;
				for (const HouseModules::SlotView& v : slots)
					if (v.Site.empty()) { if (freeShown < std::max(0, max - count)) { order.push_back(&v); ++freeShown; } else ++freeLeft; }
				const int fit = std::max(1, static_cast<int>((width + gap) / (side + gap)));
				const ImVec2 start = ImGui::GetCursorScreenPos();
				int col = 0, row = 0;
				for (size_t i = 0; i < order.size(); ++i)
				{
					const HouseModules::SlotView& v = *order[i];
					const ImVec2 p(start.x + static_cast<float>(col) * (side + gap), start.y + static_cast<float>(row) * (side + gap)), q(p.x + side, p.y + side);
					ImGui::SetCursorScreenPos(p);
					ImGui::PushID(static_cast<int>(i));
					const bool clicked = ImGui::InvisibleButton("slot", ImVec2(side, side));
					const bool hovered = ImGui::IsItemHovered();
					if (!v.Site.empty())
					{
						const auto d = m_Buildings.Defs.find(v.Module);
						const std::string category = d != m_Buildings.Defs.end() ? d->second.Module.Category : std::string();
						const ImU32 zone = ZoneColor(category);
						dl->AddRectFilled(p, q, hovered ? T::HideHover : T::HideRaised, 4.0f);
						dl->AddRectFilled(p, q, T::Faded(zone, 0.18f), 4.0f);
						dl->AddRect(p, q, zone, 4.0f, 0, v.Done ? 1.5f : 1.0f);
						IconAt(dl, CategoryIcon(category), p.x + 4.0f, p.y + 4.0f, side - 8.0f, v.Done ? 0xFFFFFFFFu : 0x90FFFFFFu);
						if (hovered)
							ImGui::SetTooltip("%s", (buildingName(v.Module) + "\n" + L.Text(CategoryWord(category)) + " \xC2\xB7 " + L.Name("slot", v.Slot.Kind, v.Slot.Kind) +
								" \xC2\xB7 " + (v.Done ? L.Text("stands") : L.Text("a work under way"))).c_str());
					}
					else
					{
						dl->AddRectFilled(p, q, hovered ? T::HideHover : T::Hide, 4.0f);
						dl->AddRect(p, q, T::Faded(T::Ochre, 0.8f), 4.0f, 0, 1.0f);
						const ImVec2 c(p.x + side * 0.5f, p.y + side * 0.5f);
						const float arm = side * 0.22f;
						dl->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), T::Ochre, 2.0f);
						dl->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), T::Ochre, 2.0f);
						if (clicked && s_MenuFor != name) ToggleYardMenu(name, std::string());
						if (hovered) ImGui::SetTooltip("%s", (L.Text("Free slot: {0}", L.Name("slot", v.Slot.Kind, v.Slot.Kind)) + "\n" + L.Text("Click to add a module")).c_str());
					}
					ImGui::PopID();
					if (++col >= fit) { col = 0; ++row; }
				}
				float endY = start.y + static_cast<float>(row + (col > 0 ? 1 : 0)) * (side + gap);
				ImGui::SetCursorScreenPos(ImVec2(start.x, endY));
				if (freeLeft > 0)
				{
					// (the slots the house has but cannot fill now: they count for a bigger size or a module that replaces another)
					ImGui::TextDisabled("%s", L.Text("{0} more places in the yard", freeLeft).c_str());
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The yard has more places than modules the house takes").c_str());
				}
				else ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
			// ---- the modules: their name, slot and state, and "Take apart"
			for (const HouseModules::SlotView& v : slots)
			{
				if (v.Site.empty()) continue;
				ImGui::PushID(v.Site.c_str());
				const auto d = m_Buildings.Defs.find(v.Module);
				const std::string category = d != m_Buildings.Defs.end() ? d->second.Module.Category : std::string();
				const ImVec2 at = ImGui::GetCursorScreenPos();
				dl->AddRectFilled(ImVec2(at.x, at.y), ImVec2(at.x + 3.0f, at.y + lh), ZoneColor(category), 1.0f);
				IconAt(dl, CategoryIcon(category), at.x + 6.0f, at.y - 1.0f, lh + 2.0f);
				const std::string label = L.Text("Take apart");
				const float bw = ImGui::CalcTextSize(label.c_str()).x + 2.0f * style.FramePadding.x, right = left + width - bw - 8.0f;
				const ImVec2 textAt(at.x + lh + 14.0f, at.y);
				const std::string moduleName = buildingName(v.Module);
				clipped(textAt, T::Bone, moduleName, right);
				const float nameW = std::min(ImGui::CalcTextSize(moduleName.c_str()).x, right - textAt.x);
				clipped(ImVec2(textAt.x + nameW + 8.0f, textAt.y), T::BoneDim, L.Name("slot", v.Slot.Kind, v.Slot.Kind) + " \xC2\xB7 " + (v.Done ? L.Text("stands") : L.Text("a work under way")), right);
				ImGui::SetCursorScreenPos(ImVec2(left + width - bw, at.y - 1.0f));
				if (ImGui::SmallButton((label + "###rm").c_str())) run("game module remove " + under + " " + Under(v.Site));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("It comes down; all it cost goes back to the store").c_str());
				ImGui::SetCursorScreenPos(ImVec2(at.x, std::max(at.y + lh + 4.0f, ImGui::GetItemRectMax().y + 2.0f)));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
				ImGui::PopID();
			}
			// ---- "Add a module": the modules that fit, with their cost; what the clan does not know yet, a "?" with its technology
			const bool open = s_MenuFor == name;
			ImGui::BeginDisabled(count >= max);
			if (IconLabelButton("yardadd", "build", (open ? L.Text("Close the list") : L.Text("Add a module")), open)) ToggleYardMenu(name, std::string());
			ImGui::EndDisabled();
			if (count >= max && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("The house has all the modules it takes").c_str());
			if (open && count < max)
			{
				const ImVec2 top = ImGui::GetCursorScreenPos();
				ImDrawListSplitter split;   // (its own splitter: the sheet's cards split the window's draw list too)
				split.Split(dl, 3);   // 0 the card, 1 the lit row, 2 the rows
				split.SetCurrentChannel(dl, 2);
				ImGui::Indent(8.0f);
				ImGui::Dummy(ImVec2(1.0f, 2.0f));
				for (const HouseModules::Offer& o : mods.Offers(*own, name))
				{
					const auto d = m_Buildings.Defs.find(o.Id);
					if (d == m_Buildings.Defs.end()) continue;
					ImGui::PushID(o.Id.c_str());
					const bool can = o.Known && o.Room;
					const ImVec2 row = ImGui::GetCursorScreenPos();
					const bool chosen = s_GhostModule == o.Id;
					if (o.Known) icon(CategoryIcon(d->second.Module.Category), lh + 8.0f, can ? 0xFFFFFFFFu : 0x80FFFFFFu);
					else { unknownTile(row, lh + 8.0f); ImGui::Dummy(ImVec2(lh + 8.0f, lh + 8.0f)); }
					ImGui::SameLine(0.0f, 6.0f);
					ImGui::BeginGroup();
					if (!o.Known) ImGui::TextDisabled("?");
					else if (can) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(chosen ? T::Ochre : T::Bone), "%s", buildingName(o.Id).c_str());
					else ImGui::TextDisabled("%s", buildingName(o.Id).c_str());
					if (o.Known)
					{
						// its cost as icons and numbers
						bool first = true;
						for (const auto& [item, n] : d->second.TotalNeeds())
						{
							if (!first) ImGui::SameLine(0.0f, 4.0f);
							first = false;
							const ImVec2 at = ImGui::GetCursorScreenPos();
							const std::string num = std::to_string(n);
							ImGui::Dummy(ImVec2(lh + 4.0f + ImGui::CalcTextSize(num.c_str()).x, lh));
							if (!IconAt(dl, IconFor(item), at.x, at.y, lh, can ? 0xFFFFFFFFu : 0x80FFFFFFu)) dl->AddRect(at, ImVec2(at.x + lh, at.y + lh), T::BoneDim, 2.0f);
							dl->AddText(ImVec2(at.x + lh + 2.0f, at.y), can ? T::Bone : T::BoneDim, num.c_str());
							if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (Lang::Cap(itemName(item)) + " x" + num).c_str());
						}
						if (!o.Room) ImGui::TextDisabled("%s", L.Text("no free slot of its kind").c_str());
					}
					else
					{
						std::string techs;
						for (const std::string& t : o.Techs) techs += (techs.empty() ? "" : L.Text(" or ")) + Lang::Cap(L.Name("tech", t, t));
						ImGui::TextDisabled("%s", L.Text("needs {0}", techs).c_str());
					}
					ImGui::EndGroup();
					const float rowEnd = ImGui::GetItemRectMax().y;
					if (can)
					{
						const std::string add = L.Text("Add");
						const float bw = ImGui::CalcTextSize(add.c_str()).x + 2.0f * style.FramePadding.x;
						ImGui::SetCursorScreenPos(ImVec2(left + width - bw - 8.0f, row.y + 2.0f));
						if (ImGui::SmallButton((add + "###add").c_str())) { run("game module add " + under + " " + o.Id); ToggleYardMenu(name, std::string()); }
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan brings its materials and builds it in the slot shown on the ground").c_str());
						ImGui::SetCursorScreenPos(ImVec2(row.x, std::max(rowEnd, ImGui::GetItemRectMax().y) + style.ItemSpacing.y));
						ImGui::Dummy(ImVec2(0.0f, 0.0f));
						// the row under the mouse (or the chosen one): lit, and its ghost on the ground in the slot the engine would take
						if (ImGui::IsMouseHoveringRect(row, ImVec2(left + width, rowEnd))) s_HoverGhost = o.Id;
						if (chosen || s_HoverGhost == o.Id)
						{
							split.SetCurrentChannel(dl, 1);
							dl->AddRectFilled(ImVec2(row.x - 4.0f, row.y - 1.0f), ImVec2(left + width - 3.0f, rowEnd + 1.0f), T::HideHover, 3.0f);
							split.SetCurrentChannel(dl, 2);
						}
					}
					ImGui::PopID();
				}
				ImGui::Dummy(ImVec2(1.0f, 2.0f));
				ImGui::Unindent(8.0f);
				const ImVec2 end(top.x + width, ImGui::GetCursorScreenPos().y);
				split.SetCurrentChannel(dl, 0);
				dl->AddRectFilled(top, end, T::HideRaised, T::Radius);
				dl->AddRect(top, end, T::Faded(T::Ochre, 0.5f), T::Radius);
				split.Merge(dl);
			}
		}

		// ================= the recipes of a building with a station: one row each, its controls in one column
		//   [icon] Name ......................  [ Auto | No | Keep 4 - + ]
		//          2 [in] + 1 [in]  >  3 [out]                 have 3 of 4
		if (!def.Station.empty())
		{
			const RecipeChoices& choices = own->Choices();
			std::vector<const Recipe*> known;
			int unknown = 0;
			for (const std::string& id : m_Recipes.Ids())
				if (const Recipe* r = m_Recipes.Find(id); r && r->Station == def.Station) { if (RecipeChoices::Known(*own, *r)) known.push_back(r); else ++unknown; }
			title(L.Text("Recipes"));
			if (known.empty()) ImGui::TextDisabled("%s", L.Text("The clan knows no recipe of this station yet").c_str());
			// the control's measures, once for every row (the widest "Keep" a goal can take), so the column lines up
			const float padX = 6.0f, segH = lh + 4.0f, pm = lh + 2.0f;
			const std::string autoText = L.Text("Auto"), noText = L.Text("No");
			const float wAuto = ImGui::CalcTextSize(autoText.c_str()).x + 2.0f * padX, wNo = ImGui::CalcTextSize(noText.c_str()).x + 2.0f * padX;
			const float wKeep = std::max(ImGui::CalcTextSize(L.Text("Keep {0}", 99).c_str()).x, ImGui::CalcTextSize(L.Text("Keep some").c_str()).x) + 2.0f * padX;
			const float ctrlW = wAuto + wNo + wKeep + 2.0f * pm;
			const float colX = left + width - ctrlW;
			const float side = 2.0f * lh + 4.0f;   // the product's icon, as tall as the two lines of the row
			for (size_t i = 0; i < known.size(); ++i)
			{
				const Recipe* r = known[i];
				ImGui::PushID(r->Id.c_str());
				const int state = choices.State(name, r->Id);
				const bool off = state == RecipeChoices::kOff;
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const float rowH = side + 4.0f;
				if (i > 0) dl->AddLine(ImVec2(at.x, at.y - 2.0f), ImVec2(at.x + width, at.y - 2.0f), T::Faded(T::HideLine, 0.5f), 1.0f);
				// the product: its icon and its name (dim when the building does not make it)
				if (!IconAt(dl, IconFor(r->Output), at.x, at.y + 2.0f, side, off ? 0x60FFFFFFu : 0xFFFFFFFFu))
					dl->AddRect(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + side, at.y + 2.0f + side), T::BoneDim, 3.0f);
				const float tx = at.x + side + 6.0f;
				const std::string product = Lang::Cap(itemName(r->Output));
				const bool cut = clipped(ImVec2(tx, at.y + 2.0f), off ? T::BoneDim : T::Bone, product, colX - 6.0f);
				if (ImGui::IsMouseHoveringRect(at, ImVec2(colX - 6.0f, at.y + lh + 4.0f)) && (cut || off))
					ImGui::SetTooltip("%s", (product + (off ? "\n" + L.Text("Never made here") : std::string())).c_str());

				// ---- the segmented control: Auto | No | Keep N - +, the lit segment ochre; a lit one does nothing when clicked again
				const std::string base = "game recipe " + under + " " + r->Id + " ";
				const ImVec2 c0(colX, at.y);
				dl->AddRectFilled(c0, ImVec2(c0.x + ctrlW, c0.y + segH), T::Hide, 4.0f);
				float x = c0.x;
				auto segment = [&](const char* id, const std::string& label, float w, bool lit, bool enabled, const std::string& command, const std::string& tip, ImDrawFlags corners)
				{
					ImGui::SetCursorScreenPos(ImVec2(x, c0.y));
					const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, segH));
					const bool hovered = ImGui::IsItemHovered();
					const ImVec2 a(x, c0.y), b(x + w, c0.y + segH);
					if (lit) dl->AddRectFilled(a, b, hovered ? T::OchreHover : T::Ochre, 4.0f, corners);
					else if (hovered && enabled) dl->AddRectFilled(a, b, T::HideHover, 4.0f, corners);
					const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
					dl->AddText(ImVec2(a.x + (w - ts.x) * 0.5f, a.y + (segH - ts.y) * 0.5f), lit ? T::Ink : enabled ? T::Bone : T::Faded(T::BoneDim, 0.45f), label.c_str());
					if (pressed && enabled && !lit && !command.empty()) run(command);
					if (hovered && !tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
					x += w;
				};
				segment("auto", autoText, wAuto, state == 0, true, base + "on", L.Text("The clan makes it here when it needs it"), ImDrawFlags_RoundCornersLeft);
				segment("off", noText, wNo, off, true, base + "off", L.Text("Never made here"), ImDrawFlags_RoundCornersNone);
				const std::string keepTip = L.Text("The clan keeps that many in the store and makes more when it has fewer");
				if (state > 0)
				{
					segment("keep", L.Text("Keep {0}", state), wKeep, true, true, std::string(), keepTip, ImDrawFlags_RoundCornersNone);
					segment("less", "-", pm, false, state > 1, base + "keep " + std::to_string(state - 1), L.Text("Keep one less"), ImDrawFlags_RoundCornersNone);
					segment("more", "+", pm, false, state < StockGoals::kMost, base + "keep " + std::to_string(state + 1), L.Text("Keep one more"), ImDrawFlags_RoundCornersRight);
				}
				else segment("keep", L.Text("Keep some"), wKeep + 2.0f * pm, false, true, base + "keep 5", keepTip, ImDrawFlags_RoundCornersRight);
				// the edges between the segments, and round the control (ochre while the building makes it)
				{
					std::vector<float> seams = { c0.x + wAuto, c0.x + wAuto + wNo };
					if (state > 0) { seams.push_back(c0.x + wAuto + wNo + wKeep); seams.push_back(c0.x + wAuto + wNo + wKeep + pm); }
					for (const float sx : seams) dl->AddLine(ImVec2(sx, c0.y + 3.0f), ImVec2(sx, c0.y + segH - 3.0f), T::HideLine, 1.0f);
					dl->AddRect(c0, ImVec2(c0.x + ctrlW, c0.y + segH), off ? T::HideLine : T::Faded(T::Ochre, 0.6f), 4.0f);
				}

				// ---- the second line: its inputs as numbers and icons (a missing one in the warning colour, with a "!"), then what it gives
				const float y2 = at.y + lh + 6.0f, small = lh;
				float px = tx;
				const ImU32 dimInk = off ? T::Faded(T::BoneDim, 0.7f) : T::BoneDim;
				const unsigned tint = off ? 0x60FFFFFFu : 0xFFFFFFFFu;
				bool first = true;
				for (const auto& [item, n] : r->Inputs)
				{
					if (!first)
					{
						dl->AddText(ImVec2(px, y2), dimInk, "+");
						px += ImGui::CalcTextSize("+").x + 4.0f;
					}
					first = false;
					const int have = StockGoals::Have(*own, item);
					const bool missing = have < n;
					const std::string num = std::to_string(n);
					const float start = px;
					dl->AddText(ImVec2(px, y2), off ? dimInk : missing ? T::Warn : T::Bone, num.c_str());
					px += ImGui::CalcTextSize(num.c_str()).x + 2.0f;
					if (!IconAt(dl, IconFor(item), px, y2 - 1.0f, small + 2.0f, tint)) dl->AddRect(ImVec2(px, y2), ImVec2(px + small, y2 + small), T::BoneDim, 2.0f);
					px += small + 2.0f;
					if (missing && !off)
					{
						dl->AddText(ImVec2(px, y2), T::Warn, "!");
						px += ImGui::CalcTextSize("!").x + 2.0f;
					}
					px += 4.0f;
					if (ImGui::IsMouseHoveringRect(ImVec2(start, y2), ImVec2(px, y2 + small)))
						ImGui::SetTooltip("%s", (missing ? L.Text("{0} is missing: the clan has {1} and this takes {2}", Lang::Cap(itemName(item)), have, n)
							: L.Text("{0}: the clan has {1} and this takes {2}", Lang::Cap(itemName(item)), have, n)).c_str());
				}
				if (r->Inputs.empty())
				{
					const std::string nothing = L.Text("nothing");
					dl->AddText(ImVec2(px, y2), dimInk, nothing.c_str());
					px += ImGui::CalcTextSize(nothing.c_str()).x + 4.0f;
				}
				// an arrow, drawn (the font has no arrows), then the output's number and icon
				{
					const float my = y2 + small * 0.5f;
					dl->AddLine(ImVec2(px, my), ImVec2(px + 9.0f, my), dimInk, 1.5f);
					dl->AddTriangleFilled(ImVec2(px + 8.0f, my - 3.5f), ImVec2(px + 13.0f, my), ImVec2(px + 8.0f, my + 3.5f), dimInk);
					px += 18.0f;
					const std::string num = std::to_string(std::max(1, r->OutputCount));
					dl->AddText(ImVec2(px, y2), off ? dimInk : T::Bone, num.c_str());
					px += ImGui::CalcTextSize(num.c_str()).x + 2.0f;
					IconAt(dl, IconFor(r->Output), px, y2 - 1.0f, small + 2.0f, tint);
					px += small + 6.0f;
					// (TODO: the work time of a recipe, when Recipe has one: GB/Gameplay/Items.h has no time field today)
				}
				// what the clan has of the product now ("of N": the goal of a Keep), on the right under the control
				{
					const int have = StockGoals::Have(*own, r->Output);
					const std::string text = state > 0 ? L.Text("have {0} of {1}", have, state) : L.Text("have {0}", have);
					const float tw = ImGui::CalcTextSize(text.c_str()).x;
					const float hx = std::max(px + 4.0f, left + width - tw);
					const ImU32 ink = off ? dimInk : state > 0 && have < state ? T::Warn : T::BoneDim;
					dl->AddText(ImVec2(hx, y2), ink, text.c_str());
					if (ImGui::IsMouseHoveringRect(ImVec2(hx, y2), ImVec2(hx + tw, y2 + lh)))
						ImGui::SetTooltip("%s", L.Text("The clan has {0} of it (store and bags)", have).c_str());
				}
				ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + rowH + 2.0f));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
				ImGui::PopID();
			}
			// the ones the clan has not discovered: one line, folded
			if (unknown > 0)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos();
				if (!known.empty()) dl->AddLine(ImVec2(at.x, at.y - 2.0f), ImVec2(at.x + width, at.y - 2.0f), T::Faded(T::HideLine, 0.5f), 1.0f);
				unknownTile(ImVec2(at.x, at.y + 1.0f), lh + 2.0f);
				ImGui::Dummy(ImVec2(lh + 2.0f, lh + 2.0f));
				ImGui::SameLine(0.0f, 8.0f);
				ImGui::TextDisabled("%s", L.Text("{0} more to discover", unknown).c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("And {0} more the clan has not discovered yet", unknown).c_str());
			}
		}
		ImGui::PopTextWrapPos();
	}

	void EditorApp::DrawYardWorld(float ox, float oy, float w, float h, const std::string& name)
	{
		const Tribe* own = ColonyTribe();
		if (!own) return;
		const YardShape yard = HouseModules::YardOf(*own, name);
		if (!yard.Valid) return;
		const std::vector<HouseModules::SlotView> slots = own->Modules().Slots(*own, name);
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		const bool built = m_World.IsBuilt();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		auto screen = [&](const glm::vec2& g, ImVec2& s)
		{
			const float y = built ? m_World.GetTerrain().GetHeight(g.x, g.y) + 0.06f : 0.06f;
			glm::vec2 v;
			if (!ProjectToScreen(viewProj, glm::vec3(g.x, y, g.y), glm::vec2(w, h), v)) return false;
			s = ImVec2(ox + v.x, oy + v.y);
			return true;
		};
		// a rectangle on the ground round `c`, `half` along the slot's side (x) and its front (z), as a module stands in it
		auto rect = [&](const glm::vec2& c, const glm::vec2& f, const glm::vec2& half, ImVec2 (&q)[4])
		{
			const glm::vec2 side(-f.y, f.x);
			const glm::vec2 corners[4] = { c + half.x * side + half.y * f, c - half.x * side + half.y * f, c - half.x * side - half.y * f, c + half.x * side - half.y * f };
			for (int k = 0; k < 4; ++k) if (!screen(corners[k], q[k])) return false;
			return true;
		};
		auto moduleOf = [&](const std::string& id) -> const BuildingDef*
		{
			const auto it = m_Buildings.Defs.find(id);
			return it != m_Buildings.Defs.end() ? &it->second : nullptr;
		};
		// the yard's line (dashed ochre while it has no fence)
		const bool fenced = own->Modules().Count(name) > 0;
		for (size_t i = 0; i + 1 < yard.Outline.size(); ++i)
		{
			if (fenced) break;
			ImVec2 a, b;
			if (i % 2 == 0 && screen(yard.Outline[i], a) && screen(yard.Outline[i + 1], b)) dl->AddLine(a, b, T::Faded(T::Ochre, 0.75f), 2.0f);
		}
		// each slot: a free one is an ochre square with the disc of its work point and a tick towards the slot; a taken one, its module's
		// footprint tinted with the colour of its zone (T368c) and a small disc where its worker stands
		for (const HouseModules::SlotView& v : slots)
		{
			const glm::vec2 c = v.Slot.Pos, f = v.Slot.Front;
			ImVec2 q[4], u, s;
			if (v.Site.empty())
			{
				const float half = v.Slot.Kind == "lean_to" ? 0.7f : 0.55f;
				if (rect(c, f, glm::vec2(half), q))
				{
					dl->AddQuadFilled(q[0], q[1], q[2], q[3], T::Faded(T::Ochre, 0.18f));
					dl->AddQuad(q[0], q[1], q[2], q[3], T::Outline, 3.5f);
					dl->AddQuad(q[0], q[1], q[2], q[3], T::Ochre, 1.8f);
				}
				if (screen(v.Slot.Use, u) && screen(c, s))
				{
					dl->AddLine(u, ImVec2(u.x + (s.x - u.x) * 0.45f, u.y + (s.y - u.y) * 0.45f), T::Outline, 3.0f);
					dl->AddLine(u, ImVec2(u.x + (s.x - u.x) * 0.45f, u.y + (s.y - u.y) * 0.45f), T::Ochre, 1.5f);
					dl->AddCircleFilled(u, 5.5f, T::Outline, 16);
					dl->AddCircleFilled(u, 4.0f, T::Ochre, 16);
				}
				continue;
			}
			if (const BuildingDef* d = moduleOf(v.Module))
			{
				const ImU32 zone = ZoneColor(d->Module.Category);
				if (rect(c, f, 0.5f * d->Footprint, q))
				{
					dl->AddQuadFilled(q[0], q[1], q[2], q[3], T::Faded(zone, 0.25f));
					dl->AddQuad(q[0], q[1], q[2], q[3], T::Faded(zone, 0.85f), 1.5f);
				}
			}
			if (screen(v.Slot.Use, u))
			{
				dl->AddCircleFilled(u, 4.0f, T::Outline, 12);
				dl->AddCircleFilled(u, 2.5f, T::Bone, 12);
			}
		}
		// the ghost of the module chosen (or under the mouse) in "Add a module", in the slot the engine would take (T368b)
		if (s_MenuFor != name) return;
		const std::string ghost = !s_HoverGhost.empty() ? s_HoverGhost : s_GhostModule;
		const BuildingDef* d = ghost.empty() ? nullptr : moduleOf(ghost);
		const HouseModules::SlotView* v = d ? SlotFor(slots, *d) : nullptr;
		if (!v) return;
		const ImU32 zone = ZoneColor(d->Module.Category);
		ImVec2 q[4], u;
		if (rect(v->Slot.Pos, v->Slot.Front, 0.5f * d->Footprint, q))
		{
			dl->AddQuadFilled(q[0], q[1], q[2], q[3], T::Faded(zone, 0.35f));
			// a dashed edge: what is not there yet
			for (int k = 0; k < 4; ++k)
			{
				const ImVec2 a = q[k], b = q[(k + 1) % 4];
				for (int seg = 0; seg < 7; seg += 2)
				{
					const float t0 = static_cast<float>(seg) / 7.0f, t1 = static_cast<float>(seg + 1) / 7.0f;
					dl->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0), ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1), zone, 2.0f);
				}
			}
		}
		if (screen(v->Slot.Use, u)) { dl->AddCircleFilled(u, 5.5f, T::Outline, 16); dl->AddCircleFilled(u, 4.0f, zone, 16); }
	}
}
