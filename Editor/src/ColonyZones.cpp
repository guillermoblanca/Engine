// T281 (documentation/01_diseno/AGRICULTURA.md A1, MODO_COLONIA.md §4.4): the colony's tool Field. The player marks out a farm zone on the ground
// point by point: a left click puts a point, the outline follows the terrain with a handle on each point, a click on the first point (or Enter)
// closes it, a right click (or Backspace) takes the last one away and Esc drops it. While it is drawn a small label tells its area and the plots
// that fit, and the outline turns red when its edges cross or FarmZones::Check would refuse it (water, a rock, a building or a field under it).
// Once closed, a crop (or Auto: the clan chooses) and a plan are picked over the god's bar, and [Mark the field] runs `game zone farm poly ...`.
// The zones already painted show their outline while the tab is open; a click inside one picks it, to change its crop or plan or remove it with
// the commands of FarmZones (`game zone <id> crop|plan|remove`).
// The UI decides nothing: every click and key queues a `colony zone ...` command, which a script types the same way (the tests and the captures),
// and those only run `game zone ...`. The button and the tool do not exist while the clan does not know how to till (T327: no name, no grey).
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Game/FarmZones.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>

namespace GB::Editor
{
	namespace
	{
		ImTextureID TextureOf(const Texture2D& t) { return static_cast<ImTextureID>(static_cast<intptr_t>(t.GetID())); }

		// a coordinate in a command: two decimals at most, no trailing zeros
		std::string Coord(float v)
		{
			char s[32];
			std::snprintf(s, sizeof s, "%.2f", static_cast<double>(v));
			std::string out = s;
			while (!out.empty() && out.back() == '0') out.pop_back();
			if (!out.empty() && out.back() == '.') out.pop_back();
			return out == "-0" ? "0" : out;
		}

		// the area of a polygon (x, z), in square metres
		float Area(const std::vector<glm::vec2>& p)
		{
			float twice = 0.0f;
			for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) twice += p[j].x * p[i].y - p[i].x * p[j].y;
			return std::abs(twice) * 0.5f;
		}

		// whether two edges that share no point cross
		bool SegmentsCross(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d)
		{
			auto side = [](glm::vec2 p, glm::vec2 q, glm::vec2 r) { const float v = (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); return v > 1e-6f ? 1 : v < -1e-6f ? -1 : 0; };
			const int d1 = side(a, b, c), d2 = side(a, b, d), d3 = side(c, d, a), d4 = side(c, d, b);
			return d1 * d2 < 0 && d3 * d4 < 0;
		}

		// the polygon's edges cross one another (the closing edge counts)
		bool Crosses(const std::vector<glm::vec2>& p)
		{
			const size_t n = p.size();
			if (n < 4) return false;
			for (size_t i = 0; i < n; ++i)
				for (size_t j = i + 2; j < n; ++j)
				{
					if (i == 0 && j == n - 1) continue;   // (they share the first point)
					if (SegmentsCross(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return true;
				}
			return false;
		}

		// a point inside a polygon (even-odd) or on its edge within 1 cm, as FarmZones lays its plots: which zone a click falls in, and the dots
		bool InsidePolygon(const std::vector<glm::vec2>& poly, glm::vec2 p)
		{
			bool in = false;
			for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
			{
				const glm::vec2 a = poly[j], b = poly[i], ab = b - a;
				const float len2 = glm::dot(ab, ab);
				const float t = len2 > 0.0f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
				if (glm::distance(p, a + t * ab) < 0.01f) return true;
				if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
			}
			return in;
		}

		// the plot a refusal names ("... the plot at 20, 14 is on a rock or a trunk"), to mark it on the ground
		bool RefusedPlot(const std::string& why, glm::vec2& at)
		{
			const size_t k = why.find("the plot at ");
			if (k == std::string::npos) return false;
			const char* text = why.c_str() + k + 12;
			char* end = nullptr;
			const float x = std::strtof(text, &end);
			if (end == text || *end != ',') return false;
			const char* next = end + 1;
			const float z = std::strtof(next, &end);
			if (end == next) return false;
			at = { x, z };
			return true;
		}

		// "zone 3" -> "3" (the id `game zone` takes)
		std::string ZoneId(const std::string& name)
		{
			const std::string prefix = FarmZones::kPrefix;
			return name.rfind(prefix, 0) == 0 ? name.substr(prefix.size()) : name;
		}

		bool IsPlan(const std::string& p) { return p == "sow" || p == "harvest" || p == "fallow"; }
	}

	// ------------------------------------------------------------------ state
	bool EditorApp::ZoneTakesClicks() const
	{
		return ColonyScreen() && m_Colony.Tab == "field" && !m_Observer.Tree;
	}

	bool EditorApp::ZonePainting() const
	{
		return ZoneTakesClicks() && m_Colony.Zone.Painting;
	}

	void EditorApp::ZoneEvaluate(const Tribe& t, const std::vector<glm::vec2>& outline)
	{
		ColonyUiState::ZoneTool& z = m_Colony.Zone;
		std::string key;
		for (const glm::vec2& p : outline) key += Coord(p.x) + "," + Coord(p.y) + ";";
		if (key == z.Key) return;
		z.Key = key;
		z.Cells.clear();
		z.Area = outline.size() >= 3 ? Area(outline) : 0.0f;
		z.Crosses = Crosses(outline);
		z.Plots = 0;
		z.Ok = false;
		z.Why.clear();
		if (outline.size() < 3) return;
		const FarmZones::Reply r = FarmZones::Check(t, outline, std::string(), &z.Plots);
		z.Ok = r.Ok && !z.Crosses;
		z.Why = r.Ok ? std::string() : r.Text;
		// the plots' middles, as Check lays them (the grid from the lowest x and z, Spacing apart): only to draw them
		const float sp = std::max(0.5f, t.Config.Econ.Farming.Spacing);
		glm::vec2 lo = outline[0], hi = outline[0];
		for (const glm::vec2& p : outline) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
		const int columns = static_cast<int>(std::floor((hi.x - lo.x) / sp + 0.5f)) + 1, rows = static_cast<int>(std::floor((hi.y - lo.y) / sp + 0.5f)) + 1;
		if (static_cast<long long>(columns) * rows > 4LL * FarmZones::kMostPlots) return;
		for (int c = 0; c < columns * rows; ++c)
		{
			const glm::vec2 p = lo + glm::vec2(static_cast<float>(c % columns), static_cast<float>(c / columns)) * sp;
			if (InsidePolygon(outline, p)) z.Cells.push_back(p);
		}
		if (!r.Ok) z.Plots = static_cast<int>(z.Cells.size());   // (refused: the plots it would have all the same, from the dots)
	}

	// ------------------------------------------------------------------ texts
	std::string EditorApp::ZoneLabel(const std::string& zone) const
	{
		const std::string id = ZoneId(zone);
		return id.find_first_not_of("0123456789") == std::string::npos ? m_Lang.Text("Field {0}", id) : Lang::Cap(m_Lang.Entity(zone));
	}

	std::string EditorApp::CropLabel(const Tribe& t, const std::string& crop) const
	{
		if (crop.empty()) return m_Lang.Text("Auto");
		const ItemDef* def = t.Items() ? t.Items()->Find(crop) : nullptr;
		return Lang::Cap(m_Lang.Name("item", crop, def ? def->Name : crop));
	}

	bool EditorApp::IconLabelButton(const std::string& id, const std::string& icon, const std::string& label, bool lit, bool dim)
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const float side = ImGui::GetFontSize() + 4.0f;
		glm::vec2 uv0(0.0f), uv1(0.0f);
		const bool hasIcon = !icon.empty() && IconUv(icon, uv0, uv1) && m_IconTexture;   // (IconUv loads the atlas the first time)
		const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
		const ImVec2 size(ts.x + 2.0f * style.FramePadding.x + (hasIcon ? side + 4.0f : 0.0f), std::max(ImGui::GetFrameHeight(), side + 4.0f));
		std::optional<GameTheme::PrimaryScope> primary;
		if (lit) primary.emplace();
		const bool pressed = ImGui::Button(("###" + id).c_str(), size);
		const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
		primary.reset();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 min = ImGui::GetItemRectMin();
		float x = min.x + style.FramePadding.x;
		if (hasIcon)
		{
			dl->AddImage(TextureOf(*m_IconTexture), ImVec2(x, min.y + (size.y - side) * 0.5f), ImVec2(x + side, min.y + (size.y + side) * 0.5f), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y),
				dim ? IM_COL32(255, 255, 255, 110) : IM_COL32_WHITE);
			x += side + 4.0f;
		}
		dl->AddText(ImVec2(x, min.y + (size.y - ts.y) * 0.5f), dim && !lit ? GameTheme::BoneDim : ink, label.c_str());
		return pressed;
	}

	// ------------------------------------------------------------------ the keys
	void EditorApp::ZoneKeys()
	{
		if (!ZoneTakesClicks() || ImGui::GetIO().WantTextInput) return;
		const ColonyUiState::ZoneTool& z = m_Colony.Zone;
		if (z.Painting && z.Points.size() >= 3 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) m_UiCommands.push_back("colony zone close");
		if ((z.Painting && !z.Points.empty()) || z.Closed)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) m_UiCommands.push_back("colony zone undo");
			if (!m_Colony.Open && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			{
				m_UiCommands.push_back("colony zone cancel");
				// Esc only drops the perimeter this frame: the tab, the pause menu and the editor's way back do not see it
				ImGui::SetKeyOwner(ImGuiKey_Escape, ImHashStr("colony_zone"), ImGuiInputFlags_LockThisFrame);
			}
		}
	}

	// ------------------------------------------------------------------ on the ground
	void EditorApp::DrawZoneWorld(float ox, float oy, float w, float h)
	{
		ColonyUiState::ZoneTool& z = m_Colony.Zone;
		if (!ZoneTakesClicks())
		{
			if (m_Colony.Tab != "field") z = ColonyUiState::ZoneTool{};   // (the tab closed: the tool lets go of everything)
			return;
		}
		const Tribe& t = *ColonyTribe();
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		ImGuiIO& io = ImGui::GetIO();
		const glm::vec2 size(w, h);
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const bool built = m_World.IsBuilt();
		auto screen = [&](glm::vec2 g, ImVec2& s)
		{
			const float y = built ? m_World.GetTerrain().GetHeight(g.x, g.y) + 0.12f : 0.12f;
			glm::vec2 v;
			if (!ProjectToScreen(viewProj, glm::vec3(g.x, y, g.y), size, v)) return false;
			s = ImVec2(ox + v.x, oy + v.y);
			return true;
		};
		// an edge on the ground: cut every metre so it lies on the slope, a dark line under the colour (the era's outline)
		auto edge = [&](glm::vec2 a, glm::vec2 b, ImU32 colour, float thick)
		{
			const int steps = std::clamp(static_cast<int>(std::ceil(glm::distance(a, b))), 1, 64);
			std::vector<ImVec2> line;
			line.reserve(static_cast<size_t>(steps) + 1);
			for (int i = 0; i <= steps; ++i)
			{
				ImVec2 s;
				if (!screen(glm::mix(a, b, static_cast<float>(i) / static_cast<float>(steps)), s)) { line.clear(); break; }
				line.push_back(s);
			}
			if (line.size() < 2) return;
			dl->AddPolyline(line.data(), static_cast<int>(line.size()), T::Outline, 0, thick + 2.0f);
			dl->AddPolyline(line.data(), static_cast<int>(line.size()), colour, 0, thick);
		};
		auto label = [&](ImVec2 at, const std::vector<std::pair<std::string, ImU32>>& lines, ImU32 accent)
		{
			float tw = 0.0f, th = 0.0f;
			for (const auto& [text, colour] : lines) { const ImVec2 s = ImGui::CalcTextSize(text.c_str()); tw = std::max(tw, s.x); th += s.y + 2.0f; }
			ImVec2 min(at.x, at.y);
			min.x = std::clamp(min.x, ox + 4.0f, std::max(ox + 4.0f, ox + w - tw - 2.0f * T::Pad - 4.0f));
			// (never under the tab over the bar: its box of the last frame)
			const ColonyUiState::Box& tab = m_Colony.TabBox;
			const bool overTab = tab.X1 > tab.X0 && min.x < tab.X1 && min.x + tw + 2.0f * T::Pad > tab.X0;
			const float floor = overTab ? std::min(oy + h, tab.Y0 - 4.0f) : oy + h - 4.0f;
			min.y = std::clamp(min.y, oy + 4.0f, std::max(oy + 4.0f, floor - th - T::Pad));
			const ImVec2 max(min.x + tw + 2.0f * T::Pad, min.y + th + T::Pad);
			dl->AddRectFilled(min, max, T::HidePanel, T::Radius);
			T::Card(dl, min, max, accent);
			float y = min.y + T::Pad * 0.5f;
			for (const auto& [text, colour] : lines) { dl->AddText(ImVec2(min.x + T::Pad + 2.0f, y), colour, text.c_str()); y += ImGui::CalcTextSize(text.c_str()).y + 2.0f; }
		};

		// ---- the zones of the clan: their outline (the one chosen in ochre) and their name and crop on a small card at their middle
		for (const auto& [name, zone] : t.Zones().All())
		{
			if (zone.Outline.size() < 3 || !FarmZones::Find(t, name)) continue;
			const bool chosen = name == z.Selected;
			for (size_t i = 0; i < zone.Outline.size(); ++i)
				edge(zone.Outline[i], zone.Outline[(i + 1) % zone.Outline.size()], chosen ? T::Ochre : T::Faded(T::Bone, 0.75f), chosen ? 3.0f : 1.5f);
			glm::vec2 mid(0.0f);
			for (const glm::vec2& p : zone.Outline) mid += p;
			mid /= static_cast<float>(zone.Outline.size());
			ImVec2 at;
			if (!screen(mid, at)) continue;
			const std::string text = ZoneLabel(name) + " \xC2\xB7 " + (zone.Auto ? L.Text("Auto") : CropLabel(t, zone.Crop));
			const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
			label(ImVec2(at.x - ts.x * 0.5f - T::Pad, at.y - ts.y * 0.5f - T::Pad * 0.5f), { { text, chosen ? T::Ochre : T::Bone } }, chosen ? T::Ochre : 0);
		}

		// ---- where the mouse is on the ground (or the point a script holds there)
		glm::vec2 cursor(0.0f);
		bool hasCursor = false;
		if (z.HeldCursor) { cursor = z.Cursor; hasCursor = true; }
		else if (glm::vec3 g; m_ViewportHovered && GroundUnderCursor(g)) { cursor = { g.x, g.z }; hasCursor = true; }
		const ImVec2 mouse = io.MousePos;
		ImVec2 first;
		bool closing = false;   // the cursor is on the first point: a click closes the perimeter
		if (z.Painting && z.Points.size() >= 3 && screen(z.Points.front(), first))
		{
			ImVec2 c;
			if (z.HeldCursor ? screen(cursor, c) : true)
			{
				if (!z.HeldCursor) c = mouse;
				closing = (c.x - first.x) * (c.x - first.x) + (c.y - first.y) * (c.y - first.y) <= 12.0f * 12.0f;
			}
		}

		// ---- the perimeter being drawn (or closed): its plots as dots, its edges and its handles
		if (z.Painting || z.Closed)
		{
			std::vector<glm::vec2> outline = z.Points;
			const bool rubber = z.Painting && hasCursor && !closing;
			if (rubber) outline.push_back(cursor);
			ZoneEvaluate(t, outline);
			const bool judged = outline.size() >= 3;
			const ImU32 colour = !judged ? T::Ochre : z.Ok ? T::Good : T::Bad;
			for (const glm::vec2& c : z.Cells)
			{
				ImVec2 s;
				if (screen(c, s)) dl->AddCircleFilled(s, 3.0f, T::Faded(colour, 0.85f), 8);
			}
			if (glm::vec2 bad; judged && !z.Ok && RefusedPlot(z.Why, bad))   // the plot that cannot be (water, a rock, a trunk): a red cross on it
			{
				ImVec2 s;
				if (screen(bad, s))
				{
					dl->AddCircleFilled(s, 9.0f, T::Faded(T::Outline, 0.8f), 16);
					dl->AddLine(ImVec2(s.x - 5.0f, s.y - 5.0f), ImVec2(s.x + 5.0f, s.y + 5.0f), T::Bad, 2.5f);
					dl->AddLine(ImVec2(s.x - 5.0f, s.y + 5.0f), ImVec2(s.x + 5.0f, s.y - 5.0f), T::Bad, 2.5f);
				}
			}
			const size_t n = outline.size();
			for (size_t i = 0; i + 1 < n; ++i)
				edge(outline[i], outline[i + 1], rubber && i + 2 == n ? T::Faded(colour, 0.7f) : colour, 3.0f);
			if (n >= 3) edge(outline.back(), outline.front(), z.Closed ? colour : T::Faded(colour, 0.45f), z.Closed ? 2.5f : 1.5f);   // (the edge that would close it)
			for (size_t i = 0; i < z.Points.size(); ++i)
			{
				ImVec2 s;
				if (!screen(z.Points[i], s)) continue;
				const bool head = i == 0;
				const float r = head ? (closing ? 9.0f : 7.0f) : 5.0f;
				dl->AddCircleFilled(s, r + 2.0f, T::Outline, 16);
				dl->AddCircleFilled(s, r, head && closing ? T::Good : T::Parchment, 16);
				dl->AddCircle(s, r, head ? T::Ochre : colour, 16, head ? 2.5f : 1.5f);
			}
			// the label: area and plots, and why not when it cannot be; beside the cursor while drawing, else beside the last point
			ImVec2 at;
			const glm::vec2 near = rubber ? cursor : z.Points.empty() ? cursor : z.Points.back();
			if ((!z.Points.empty() || hasCursor) && screen(near, at))
			{
				std::vector<std::pair<std::string, ImU32>> lines;
				if (!judged) lines.push_back({ z.Points.empty() ? L.Text("Click to put the first point") : L.Text("{0} points: put at least 3", static_cast<int>(z.Points.size())), T::Bone });
				else
				{
					if (z.Crosses) lines.push_back({ L.Text("The edges cross"), T::Bad });   // (no area: a crossed outline has none that means anything)
					else lines.push_back({ L.Text("{0} m\xC2\xB2", L.Num(z.Area, 0)) + " \xC2\xB7 " + L.Text(z.Plots == 1 ? "{0} plot" : "{0} plots", z.Plots), T::Bone });
					if (!z.Crosses && !z.Why.empty())
					{
						const std::string& why = z.Why;
						const std::string shown = why.find("in the water") != std::string::npos ? L.Text("A plot falls in the water")
							: why.find("rock or a trunk") != std::string::npos ? L.Text("A plot falls on a rock or a trunk")
							: why.find("would stand on") != std::string::npos ? L.Text("It takes ground already used: a building or another field")
							: why.find("too big") != std::string::npos || why.find("would have") != std::string::npos ? L.Text("Too big: at most {0} plots", FarmZones::kMostPlots)
							: why.find("no plot falls") != std::string::npos ? L.Text("Too small: no plot fits")
							: why.find("does not know") != std::string::npos ? L.Text("The clan does not know how to till")
							: Lang::Cap(L.Line(why));
						lines.push_back({ shown, T::Bad });
					}
					if (closing) lines.push_back({ L.Text("Click: close the field"), T::Good });
				}
				label(ImVec2(at.x + 16.0f, at.y + 12.0f), lines, colour);
			}
		}

		// ---- the clicks (a click that did not drag the camera; never through a panel: m_ViewportHovered)
		auto press = [&](int button, bool& down, glm::vec2& pressAt)
		{
			if (m_ViewportHovered && ImGui::IsMouseClicked(button)) { down = true; pressAt = { mouse.x, mouse.y }; }
			if (!down || !ImGui::IsMouseReleased(button)) return false;
			down = false;
			return m_ViewportHovered && glm::distance(pressAt, glm::vec2(mouse.x, mouse.y)) < 5.0f;
		};
		const bool left = press(0, z.DownL, z.PressL), right = press(1, z.DownR, z.PressR);
		if (z.Painting)
		{
			if (left && closing) m_UiCommands.push_back("colony zone close");
			else if (left && hasCursor && !z.HeldCursor) m_UiCommands.push_back("colony zone point " + Coord(cursor.x) + " " + Coord(cursor.y));
			if (right) m_UiCommands.push_back(z.Points.empty() ? "colony zone cancel" : "colony zone undo");
		}
		else if (!z.Closed && left)   // no perimeter open: a click on a zone picks it, elsewhere lets it go
		{
			std::string hit;
			if (hasCursor)
				for (const auto& [name, zone] : t.Zones().All())
					if (zone.Outline.size() >= 3 && InsidePolygon(zone.Outline, cursor)) hit = name;
			m_UiCommands.push_back(hit.empty() ? std::string("colony zone select off") : "colony zone select " + ZoneId(hit));
		}
	}

	// ------------------------------------------------------------------ the panel over the bar
	void EditorApp::DrawFieldTab(const Tribe& t, float width)
	{
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		ColonyUiState::ZoneTool& z = m_Colony.Zone;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		auto tip = [&](const std::string& text) { if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text.c_str()); };
		const std::string help = z.Painting ? L.Text("Click on the ground to put the points of its edge")
			: z.Closed ? L.Text("What is sown in it, and what the clan does with it")
			: L.Text("The clan's fields: click on one to change it");
		ColonyTabTitle(L.Text("Fields"), help);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(width, 320.0f));
		const bool sows = TechVerbs::Allows(t, "farm", "sow");

		// the crops as chips (Auto first): the ones the clan cannot sow now are dimmed with why; a crop is never hidden by a technology (the data
		// gives none), but with sowing still unknown no crop is named: one "?" instead (T327)
		auto crops = [&](const std::string& chosen, const std::string& command)
		{
			ImGui::TextDisabled("%s", L.Text("Crop").c_str());
			ImGui::SameLine(0.0f, 10.0f);
			if (IconLabelButton("crop_auto", "sow", L.Text("Auto"), chosen.empty())) run(command + "auto");
			tip(L.Text("The clan chooses each time it sows: what it has least of"));
			if (!sows)
			{
				ImGui::SameLine();
				ImGui::BeginDisabled();
				IconLabelButton("crop_unknown", "", "?", false, true);
				ImGui::EndDisabled();
				tip(L.Text("The clan learns its crops with new knowledge"));
				return;
			}
			const Season season = t.CurrentSeason();
			for (const auto& [id, crop] : t.Config.Econ.Crops)
			{
				const bool seed = t.Store().Count(crop.Seed) > 0, now = crop.SowsIn(season);
				ImGui::SameLine();
				std::string icon = id;
				glm::vec2 a(0.0f), b(0.0f);
				if (!IconUv(icon, a, b)) icon = IconUv(crop.Seed, a, b) ? crop.Seed : std::string("sow");
				if (IconLabelButton("crop_" + id, icon, CropLabel(t, id), chosen == id, !seed || !now)) run(command + id);
				std::string why = L.Text("{0} seeds in the store", t.Store().Count(crop.Seed));
				if (!seed) why = L.Text("No seed in the store: it waits until there is");
				else if (!now) why = L.Text("Not its season to sow: it waits");
				tip(why);
			}
			// the crop chosen cannot be sown now: why, under the chips (not only in the tooltip)
			if (const auto c = t.Config.Econ.Crops.find(chosen); c != t.Config.Econ.Crops.end())
			{
				if (t.Store().Count(c->second.Seed) <= 0) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("No seed in the store: it waits until there is").c_str());
				else if (!c->second.SowsIn(season)) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Not its season to sow: it waits").c_str());
			}
		};
		auto plans = [&](const std::string& chosen, const std::string& command)
		{
			ImGui::TextDisabled("%s", L.Text("Plan").c_str());
			ImGui::SameLine(0.0f, 10.0f);
			static const std::tuple<const char*, const char*, const char*, const char*> kPlans[] = {
				{ "sow", "sow", "Sow and harvest", "The whole cycle: till, sow, water and harvest" },
				{ "harvest", "harvest", "Harvest only", "What grows is watered and harvested; nothing new is sown" },
				{ "fallow", "plot.fallow", "Rest", "The ground rests: only what is ripe is harvested" } };
			bool firstPlan = true;
			for (const auto& [id, icon, text, help2] : kPlans)
			{
				if (!firstPlan) ImGui::SameLine();
				firstPlan = false;
				if (IconLabelButton(std::string("plan_") + id, icon, L.Text(text), chosen == id)) run(command + id);
				tip(L.Text(help2));
			}
		};

		if (z.Painting)
		{
			ImGui::TextUnformatted(z.Points.empty() ? L.Text("No point yet").c_str() : L.Text(z.Points.size() == 1 ? "{0} point" : "{0} points", static_cast<int>(z.Points.size())).c_str());
			if (z.Points.size() >= 3)
			{
				ImGui::SameLine(0.0f, 12.0f);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(z.Ok ? T::Good : T::Bad), "%s",
					(L.Text("{0} m\xC2\xB2", L.Num(z.Area, 0)) + " \xC2\xB7 " + L.Text(z.Plots == 1 ? "{0} plot" : "{0} plots", z.Plots)).c_str());
			}
			ImGui::TextDisabled("%s", L.Text("[Click] a point  [Click on the first] or [Enter] close  [Right click] or [Backspace] take the last away  [Esc] drop it").c_str());
			ImGui::BeginDisabled(z.Points.size() < 3);
			if (ImGui::Button((L.Text("Close the field") + "###zoneclose").c_str())) run("colony zone close");
			ImGui::EndDisabled();
			tip(z.Points.size() < 3 ? L.Text("A field needs 3 points or more") : L.Text("Join the last point to the first"));
			if (!z.Points.empty())
			{
				ImGui::SameLine();
				if (ImGui::Button((L.Text("Drop it") + "###zonecancel").c_str())) run("colony zone cancel");
			}
		}
		else if (z.Closed)
		{
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", (L.Text("New field") + ": " + L.Text("{0} m\xC2\xB2", L.Num(z.Area, 0)) + " \xC2\xB7 "
				+ L.Text(z.Plots == 1 ? "{0} plot" : "{0} plots", z.Plots)).c_str());
			crops(z.Crop, "colony zone crop ");
			plans(z.Plan, "colony zone plan ");
			ImGui::Separator();
			{
				const T::PrimaryScope primary;
				if (ImGui::Button((L.Text("Mark the field") + "###zoneconfirm").c_str())) run("colony zone confirm");
			}
			tip(L.Text("The clan tills it and sows it when it has hands and seed"));
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Change the edge") + "###zoneedit").c_str())) run("colony zone undo");
			ImGui::SameLine();
			if (ImGui::Button((L.Text("Drop it") + "###zonecancel").c_str())) run("colony zone cancel");
		}
		else
		{
			const Field* f = z.Selected.empty() ? nullptr : FarmZones::Find(t, z.Selected);
			const FarmZones::Zone* zone = f ? t.Zones().Of(f->Name) : nullptr;
			if (f && zone)
			{
				const std::string id = ZoneId(f->Name);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", ZoneLabel(f->Name).c_str());
				ImGui::SameLine(0.0f, 12.0f);
				std::string sizeText = L.Text(f->Plots.size() == 1 ? "{0} plot" : "{0} plots", static_cast<int>(f->Plots.size()));
				if (zone->Outline.size() >= 3) sizeText = L.Text("{0} m\xC2\xB2", L.Num(Area(zone->Outline), 0)) + " \xC2\xB7 " + sizeText;
				ImGui::TextUnformatted(sizeText.c_str());
				const int ripe = f->Count("grown"), growing = f->Count("sown") + f->Count("sprout");
				if (ripe + growing > 0)
				{
					ImGui::SameLine(0.0f, 12.0f);
					ImGui::TextDisabled("%s", L.Text("{0} growing, {1} ripe", growing, ripe).c_str());
				}
				crops(zone->Auto ? std::string() : zone->Crop, "game zone " + id + " crop ");
				plans(zone->Plan, "game zone " + id + " plan ");
				ImGui::Separator();
				if (ImGui::Button((L.Text("Remove the field") + "###zoneremove").c_str())) { run("game zone " + id + " remove"); run("colony zone select off"); }
				tip(L.Text("The field goes: what grows in it is lost"));
				ImGui::SameLine();
				if (ImGui::Button((L.Text("Done") + "###zonedone").c_str())) run("colony zone select off");
				ImGui::SameLine();
			}
			else
			{
				int listed = 0;
				for (const auto& [name, zz] : t.Zones().All())
				{
					if (!FarmZones::Find(t, name)) continue;
					if (listed++ > 0) ImGui::SameLine();
					if (IconLabelButton("pick_" + name, zz.Auto ? "sow" : zz.Crop, ZoneLabel(name), false)) run("colony zone select " + ZoneId(name));
					tip(zz.Auto ? L.Text("Auto") : CropLabel(t, zz.Crop));
				}
				if (listed == 0) ImGui::TextDisabled("%s", L.Text("The clan has no field of yours yet").c_str());
			}
			{
				const T::PrimaryScope primary;
				if (ImGui::Button((L.Text("New field") + "###zonenew").c_str())) run("colony zone start");
			}
			tip(L.Text("Mark out a field on the ground, point by point"));
		}
		ImGui::PopTextWrapPos();
	}

	// ------------------------------------------------------------------ `colony zone ...`
	std::string EditorApp::ZoneStatus()
	{
		ColonyUiState::ZoneTool& z = m_Colony.Zone;
		const Tribe* t = ColonyTribe();
		std::string s = "zone tool: ";
		s += !ZoneTakesClicks() ? "off" : z.Painting ? "painting" : z.Closed ? "closed" : "idle";
		std::vector<glm::vec2> outline = z.Points;
		if (z.Painting && z.HeldCursor) outline.push_back(z.Cursor);
		if (t) ZoneEvaluate(*t, outline);
		s += "; " + std::to_string(z.Points.size()) + " points" + (z.Painting && z.HeldCursor ? " (+ cursor " + Coord(z.Cursor.x) + " " + Coord(z.Cursor.y) + ")" : std::string());
		if (outline.size() >= 3)
		{
			char area[32];
			std::snprintf(area, sizeof area, "%.0f", static_cast<double>(z.Area));
			s += std::string("; area ") + area + " m2; " + std::to_string(z.Plots) + " plots; " + (z.Crosses ? std::string("the edges cross") : z.Ok ? std::string("ok") : "refused: " + z.Why);
		}
		s += "; crop " + (z.Crop.empty() ? std::string("auto") : z.Crop) + "; plan " + z.Plan + "; selected " + (z.Selected.empty() ? std::string("none") : z.Selected);
		return s;
	}

	CommandResult EditorApp::ZoneCommand(const std::vector<std::string>& a)
	{
		static const char* kUsage = "usage: colony zone [status] | start | point <x> <z> | undo | close | cancel | crop <crop|auto> | plan <sow|harvest|fallow> | confirm | select <id|off> | cursor <x> <z>|off";
		ColonyUiState::ZoneTool& z = m_Colony.Zone;
		const std::string sub = a.size() > 1 ? a[1] : "status";
		if (sub == "status") return { true, ZoneStatus() };
		Tribe* t = const_cast<Tribe*>(ColonyTribe());
		if (!t) return { false, "no colony: director colony on, and a world with clans" };
		auto open = [&]()
		{
			if (!m_Hand.Menu.empty() || !m_Hand.Building.empty()) Execute("director hand off");   // (one panel over the bar at a time)
			m_Colony.Tab = "field";
			m_Colony.Open = false;
		};
		auto number = [](const std::string& s, float& v) { char* end = nullptr; v = std::strtof(s.c_str(), &end); return end && *end == '\0' && end != s.c_str(); };
		if (sub == "start")
		{
			if (const std::string why = TechVerbs::WhyNot(*t, "farm", "till"); !why.empty()) return { false, "cannot paint a farm zone: the clan " + why };
			open();
			const std::string crop = z.Crop, plan = z.Plan;
			z = ColonyUiState::ZoneTool{};
			z.Crop = crop;
			z.Plan = plan;
			z.Painting = true;
			return { true, "painting a farm zone: " + ZoneStatus() };
		}
		if (sub == "point" && a.size() == 4)
		{
			float x = 0.0f, zz = 0.0f;
			if (!number(a[2], x) || !number(a[3], zz)) return { false, kUsage };
			if (!z.Painting || z.Closed || m_Colony.Tab != "field")
			{
				const CommandResult r = ZoneCommand({ "zone", "start" });
				if (!r.Ok) return r;
			}
			z.Points.push_back({ x, zz });
			return { true, ZoneStatus() };
		}
		if (sub == "undo")
		{
			if (z.Closed) { z.Closed = false; z.Painting = true; return { true, "the perimeter is open again: " + ZoneStatus() }; }
			if (z.Points.empty()) return { false, "no point to take away" };
			z.Points.pop_back();
			return { true, ZoneStatus() };
		}
		if (sub == "close")
		{
			if (!z.Painting || z.Points.size() < 3) return { false, "a field needs 3 points or more" };
			ZoneEvaluate(*t, z.Points);
			if (z.Crosses) return { false, "cannot close it: the edges cross" };
			if (!z.Ok) return { false, "cannot close it: " + z.Why };
			z.Painting = false;
			z.Closed = true;
			return { true, "closed: " + ZoneStatus() };
		}
		if (sub == "cancel")
		{
			if (z.Closed || !z.Points.empty())
			{
				z.Points.clear();
				z.Closed = false;
				z.Painting = true;
				return { true, "the perimeter is dropped: " + ZoneStatus() };
			}
			z.Painting = false;
			return { true, "the tool stops painting: " + ZoneStatus() };
		}
		if (sub == "crop" && a.size() == 3)
		{
			const std::string c = a[2] == "auto" || a[2] == "any" ? std::string() : a[2];
			if (!c.empty() && !t->Config.Econ.Crops.count(c)) return { false, "no crop called " + c + " in the loaded content" };
			z.Crop = c;
			return { true, "crop " + (c.empty() ? std::string("auto") : c) };
		}
		if (sub == "plan" && a.size() == 3)
		{
			if (!IsPlan(a[2])) return { false, "a plan is sow, harvest or fallow" };
			z.Plan = a[2];
			return { true, "plan " + a[2] };
		}
		if (sub == "confirm")
		{
			if (!z.Closed) return { false, "close the perimeter first (colony zone close)" };
			std::string command = "game zone farm poly";
			for (const glm::vec2& p : z.Points) command += " " + Coord(p.x) + " " + Coord(p.y);
			if (!z.Crop.empty()) command += " " + z.Crop;
			const CommandResult r = Execute(command);
			std::string done = command + " -> " + r.Message;
			if (!r.Ok) return { false, done };
			const size_t painted = r.Message.find(" painted");
			const std::string name = painted == std::string::npos ? std::string() : r.Message.substr(0, painted);
			if (z.Plan != "sow" && !name.empty())
			{
				const std::string plan = "game zone " + ZoneId(name) + " plan " + z.Plan;
				const CommandResult p = Execute(plan);
				done += "; " + plan + " -> " + p.Message;
			}
			const std::string crop = z.Crop, plan = z.Plan;
			z = ColonyUiState::ZoneTool{};
			z.Crop = crop;
			z.Plan = plan;
			z.Selected = name;
			return { true, done };
		}
		if (sub == "select" && a.size() == 3)
		{
			if (a[2] == "off" || a[2] == "none") { z.Selected.clear(); return { true, "no field chosen" }; }
			const Field* f = FarmZones::Find(*t, a[2]);
			if (!f || !t->Zones().Of(f->Name)) return { false, "the clan has no zone " + a[2] };
			open();
			z.Painting = z.Closed = false;
			z.Points.clear();
			z.Selected = f->Name;
			return { true, "chosen: " + f->Report() };
		}
		if (sub == "cursor" && a.size() == 3 && a[2] == "off") { z.HeldCursor = false; return { true, "the cursor is the mouse's" }; }
		if (sub == "cursor" && a.size() == 4)
		{
			float x = 0.0f, zz = 0.0f;
			if (!number(a[2], x) || !number(a[3], zz)) return { false, kUsage };
			z.HeldCursor = true;
			z.Cursor = { x, zz };
			return { true, ZoneStatus() };
		}
		return { false, kUsage };
	}
}
