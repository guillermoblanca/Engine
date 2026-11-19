// PROTOTYPE (2026-11-20) of TALLERES.md section 10.8 (the yard's block of a house's sheet) and TL4 (the recipes of a building with a station), to
// try in the colony. Two sections more in the sheet on the right of a building (DrawColonyThing, ColonyView.cpp) and, with a house selected, its
// slots and work points on the ground. The UI decides nothing: every button queues a `game module ...` or `game recipe ...` (Gameplay/EditorModules.cpp).
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Game/HouseModules.h"
#include "GB/Game/RecipeChoices.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;
		std::string s_MenuFor;   // the house whose list of modules is open ("" none): UI state only

		std::string Under(std::string s)
		{
			for (char& c : s) if (c == ' ') c = '_';
			return s;
		}

		// an icon for a module by its category (the atlas has no module icons yet: the thing it makes or keeps)
		std::string CategoryIcon(const std::string& category)
		{
			if (category == "kitchen") return "cooked_meat";
			if (category == "craft") return "stone_axe";
			if (category == "store") return "basket_wicker";
			if (category == "mill") return "flour";
			return "build";
		}
	}

	// `game module menu <house>`: opens or closes the list of modules of a house's sheet (as the click on "Add a module": the captures)
	void ToggleYardMenu(const std::string& host) { s_MenuFor = s_MenuFor == host ? std::string() : host; }

	void EditorApp::DrawYardSheet(const std::string& name, const BuildingDef& def, float width)
	{
		const Tribe* own = ColonyTribe();
		if (!own) return;
		bool mine = false;
		for (const auto& site : own->SettlementSites()) if (site.Name == name) mine = true;
		if (!mine) return;
		const Lang& L = m_Lang;
		ImDrawList* dl = ImGui::GetWindowDrawList();
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
		auto icon = [&](const std::string& id, float side)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::Dummy(ImVec2(side, side));
			if (!IconAt(dl, id, at.x, at.y, side)) dl->AddRect(at, ImVec2(at.x + side, at.y + side), T::BoneDim, 3.0f);
		};
		const std::string under = Under(name);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);   // (popped at the end: every line of the sections wraps inside the sheet)

		// ================= the yard of a house
		if (def.Yard.Any())
		{
			const HouseModules& mods = own->Modules();
			const std::vector<HouseModules::SlotView> slots = mods.Slots(*own, name);
			const YardShape yard = HouseModules::YardOf(*own, name);
			const int count = mods.Count(name), max = def.Yard.MaxModules;
			title(L.Text("Yard"));
			ImGui::TextUnformatted(L.Text("Modules {0}/{1}", count, max).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", (count > 0 ? L.Text("\xC2\xB7 fence of stakes, {0} m", L.Num(yard.Length, 0)) : L.Text("\xC2\xB7 the fence goes up with the first module")).c_str());
			// the slots: the taken ones with their module, then the free ones by kind
			std::map<std::string, int> free;
			for (const HouseModules::SlotView& v : slots)
			{
				if (v.Site.empty()) { ++free[v.Slot.Kind]; continue; }
				ImGui::PushID(v.Site.c_str());
				const auto d = m_Buildings.Defs.find(v.Module);
				icon(CategoryIcon(d != m_Buildings.Defs.end() ? d->second.Module.Category : std::string()), lh + 6.0f);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::BeginGroup();
				ImGui::TextUnformatted(buildingName(v.Module).c_str());
				ImGui::TextDisabled("%s", (L.Name("slot", v.Slot.Kind, v.Slot.Kind) + " \xC2\xB7 " + (v.Done ? L.Text("stands") : L.Text("a work under way"))).c_str());
				ImGui::EndGroup();
				ImGui::SameLine(std::max(ImGui::GetCursorPosX(), width - 96.0f));
				if (ImGui::SmallButton((L.Text("Pull down") + "###rm").c_str())) run("game module remove " + under + " " + Under(v.Site));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("It comes down; all it cost goes back to the store").c_str());
				ImGui::PopID();
			}
			std::string freeText;
			for (const auto& [kind, n] : free) freeText += (freeText.empty() ? "" : ", ") + std::to_string(n) + " " + L.Name("slot", kind, kind);
			ImGui::TextDisabled("%s", L.Text("Free slots: {0}", freeText.empty() ? L.Text("none") : freeText).c_str());
			// "Add a module": the list of the modules that fit, with their cost; what the clan does not know yet, greyed with its technology
			const bool open = s_MenuFor == name;
			ImGui::BeginDisabled(count >= max);
			if (IconLabelButton("yardadd", "build", (open ? L.Text("Close the list") : L.Text("Add a module")), open)) ToggleYardMenu(name);
			ImGui::EndDisabled();
			if (count >= max && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("The house has all the modules it takes").c_str());
			if (open && count < max)
			{
				const ImVec2 top = ImGui::GetCursorScreenPos();
				dl->ChannelsSplit(2);
				dl->ChannelsSetCurrent(1);
				ImGui::Indent(8.0f);
				ImGui::Dummy(ImVec2(1.0f, 2.0f));
				for (const HouseModules::Offer& o : mods.Offers(*own, name))
				{
					const auto d = m_Buildings.Defs.find(o.Id);
					if (d == m_Buildings.Defs.end()) continue;
					ImGui::PushID(o.Id.c_str());
					const bool can = o.Known && o.Room;
					const ImVec2 row = ImGui::GetCursorScreenPos();
					icon(CategoryIcon(d->second.Module.Category), lh + 8.0f);
					ImGui::SameLine(0.0f, 6.0f);
					ImGui::BeginGroup();
					if (can) ImGui::TextUnformatted(buildingName(o.Id).c_str());
					else ImGui::TextDisabled("%s", buildingName(o.Id).c_str());
					// its cost as icons and numbers
					bool first = true;
					for (const auto& [item, n] : d->second.TotalNeeds())
					{
						if (!first) ImGui::SameLine(0.0f, 4.0f);
						first = false;
						const ImVec2 at = ImGui::GetCursorScreenPos();
						const std::string num = std::to_string(n);
						ImGui::Dummy(ImVec2(lh + 4.0f + ImGui::CalcTextSize(num.c_str()).x, lh));
						if (!IconAt(dl, item, at.x, at.y, lh, can ? 0xFFFFFFFFu : 0x80FFFFFFu)) dl->AddRect(at, ImVec2(at.x + lh, at.y + lh), T::BoneDim, 2.0f);
						dl->AddText(ImVec2(at.x + lh + 2.0f, at.y), can ? T::Bone : T::BoneDim, num.c_str());
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", (Lang::Cap(itemName(item)) + " x" + num).c_str());
					}
					if (!o.Known)
					{
						std::string techs;
						for (const std::string& t : o.Techs) techs += (techs.empty() ? "" : L.Text(" or ")) + Lang::Cap(L.Name("tech", t, t));
						ImGui::TextDisabled("%s", L.Text("? needs {0}", techs).c_str());
					}
					else if (!o.Room) ImGui::TextDisabled("%s", L.Text("no free slot of its kind").c_str());
					ImGui::EndGroup();
					if (can)
					{
						ImGui::SameLine(std::max(ImGui::GetCursorPosX(), width - 70.0f));
						if (ImGui::SmallButton((L.Text("Add") + "###add").c_str())) { run("game module add " + under + " " + o.Id); s_MenuFor.clear(); }
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The clan brings its materials and builds it in a free slot of the yard").c_str());
					}
					(void)row;
					ImGui::PopID();
				}
				ImGui::Dummy(ImVec2(1.0f, 2.0f));
				ImGui::Unindent(8.0f);
				const ImVec2 end(top.x + width, ImGui::GetCursorScreenPos().y);
				dl->ChannelsSetCurrent(0);
				dl->AddRectFilled(top, end, T::HideRaised, T::Radius);
				dl->AddRect(top, end, T::Faded(T::Ochre, 0.5f), T::Radius);
				dl->ChannelsMerge();
			}
		}

		// ================= the recipes of a building with a station
		if (!def.Station.empty())
		{
			const RecipeChoices& choices = own->Choices();
			std::vector<const Recipe*> known;
			int unknown = 0;
			for (const std::string& id : m_Recipes.Ids())
				if (const Recipe* r = m_Recipes.Find(id); r && r->Station == def.Station) { if (RecipeChoices::Known(*own, *r)) known.push_back(r); else ++unknown; }
			title(L.Text("Recipes"));
			if (known.empty()) ImGui::TextDisabled("%s", L.Text("The clan knows no recipe of this station yet").c_str());
			for (const Recipe* r : known)
			{
				ImGui::PushID(r->Id.c_str());
				const int state = choices.State(name, r->Id);
				icon(r->Output, lh + 8.0f);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::BeginGroup();
				if (state == RecipeChoices::kOff) ImGui::TextDisabled("%s", Lang::Cap(itemName(r->Output)).c_str());
				else ImGui::TextUnformatted(Lang::Cap(itemName(r->Output)).c_str());
				std::string inputs;
				for (const auto& [item, n] : r->Inputs) inputs += (inputs.empty() ? "" : " + ") + std::to_string(n) + " " + itemName(item);
				ImGui::TextDisabled("%s", (inputs + " -> " +std::to_string(std::max(1, r->OutputCount)) + " " + itemName(r->Output)).c_str());
				ImGui::EndGroup();
				// Auto / No / Keep N
				const std::string base = "game recipe " + under + " " + r->Id + " ";
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lh + 14.0f);
				auto choice = [&](const char* id, const std::string& label, bool lit, const std::string& command, const std::string& tip)
				{
					std::optional<T::PrimaryScope> primary;
					if (lit) primary.emplace();
					const bool pressed = ImGui::SmallButton((label + "###" + id).c_str());
					primary.reset();
					if (pressed && !lit) run(command);
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
				};
				choice("auto", L.Text("Auto"), state == 0, base + "on", L.Text("The clan makes it here when it needs it"));
				ImGui::SameLine(0.0f, 4.0f);
				choice("off", L.Text("No"), state == RecipeChoices::kOff, base + "off", L.Text("Never made here"));
				ImGui::SameLine(0.0f, 4.0f);
				choice("keep", state > 0 ? L.Text("Keep {0}", state) : L.Text("Keep some"), state > 0, base + "keep " + std::to_string(state > 0 ? state : 5),
					L.Text("The clan keeps that many in the store and makes more when it has fewer"));
				if (state > 0)
				{
					ImGui::SameLine(0.0f, 4.0f);
					if (ImGui::SmallButton("-###less") && state > 1) run(base + "keep " + std::to_string(state - 1));
					ImGui::SameLine(0.0f, 2.0f);
					if (ImGui::SmallButton("+###more")) run(base + "keep " + std::to_string(state + 1));
				}
				ImGui::PopID();
			}
			if (unknown > 0) ImGui::TextDisabled("%s", L.Text("And {0} more the clan has not discovered yet", unknown).c_str());
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
		// the yard's line (dashed ochre while it has no fence)
		const bool fenced = own->Modules().Count(name) > 0;
		for (size_t i = 0; i + 1 < yard.Outline.size(); ++i)
		{
			if (fenced) break;
			ImVec2 a, b;
			if (i % 2 == 0 && screen(yard.Outline[i], a) && screen(yard.Outline[i + 1], b)) dl->AddLine(a, b, T::Faded(T::Ochre, 0.75f), 2.0f);
		}
		// each slot: a square on the ground (ochre free, pale taken) and the disc of its work point with a tick towards the slot
		for (const HouseModules::SlotView& v : slots)
		{
			const glm::vec2 c = v.Slot.Pos, f = v.Slot.Front, side(-f.y, f.x);
			const float half = v.Slot.Kind == "lean_to" ? 0.7f : 0.55f;
			ImVec2 q[4];
			bool ok = true;
			const glm::vec2 corners[4] = { c + half * (f + side), c + half * (f - side), c - half * (f + side), c - half * (f - side) };
			for (int k = 0; k < 4 && ok; ++k) ok = screen(corners[k], q[k]);
			if (ok)
			{
				const ImU32 ink = v.Site.empty() ? T::Ochre : T::Faded(T::Bone, 0.6f);
				if (v.Site.empty()) dl->AddQuadFilled(q[0], q[1], q[2], q[3], T::Faded(T::Ochre, 0.18f));
				dl->AddQuad(q[0], q[1], q[2], q[3], T::Outline, 3.5f);
				dl->AddQuad(q[0], q[1], q[2], q[3], ink, 1.8f);
			}
			ImVec2 u, s;
			if (screen(v.Slot.Use, u) && screen(c, s))
			{
				dl->AddLine(u, ImVec2(u.x + (s.x - u.x) * 0.45f, u.y + (s.y - u.y) * 0.45f), T::Outline, 3.0f);
				dl->AddLine(u, ImVec2(u.x + (s.x - u.x) * 0.45f, u.y + (s.y - u.y) * 0.45f), T::Ochre, 1.5f);
				dl->AddCircleFilled(u, 5.5f, T::Outline, 16);
				dl->AddCircleFilled(u, 4.0f, IM_COL32(214, 150, 62, 255), 16);
			}
		}
	}
}
