// The tree of knowledge (K, `game tech`; `observer tree`, opened from the Knowledge sheet and tab): the technologies of the world as a graph, as
// management games show their research, over the whole window with the world dimmed behind. Columns go left to right by what each requires (T181:
// no eras), so a line always runs from what is needed to what it opens. A click shows its card at the bottom.
// T341 (PROPUESTA_ARBOL_2026-11-19 K-b): each node says the ONE state of GB/Game/TechStatus.h (the same as the queue and the sheet) with a shape and
//   a word, not only a colour: known (a tick, faded), studying (thick ochre frame and its bar), queued (its number in a badge), on their own (a
//   thin bar and "~"), studiable (a "+"), blocked (an amber dashed frame and why: "no flint at hand"), another clan's (a blue frame and who) and
//   unknown: a "?" with no name, only "after X" (D2). In the god's view (the observer of the free mode) every name shows.
// T338 (K-a, D6): only the technologies with a tier are drawn and counted ("5 of 54"); tier 4 (era 2) is one "?" block with its number.
// T339 (K-d, D1): one way to direct research, the queue of T324: the card's buttons queue `game research add|top|remove <tech>`; the old aim
//   (`game research <tech>`, `director research`) is no longer offered here. The god keeps [Vision] (`director vision`).
// Nothing here decides: the buttons queue commands. Every word goes through Lang.
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Game/TechStatus.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <map>
#include <optional>
#include <set>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;
		constexpr float kNodeW = 172.0f, kNodeH = 54.0f, kColGap = 40.0f, kRowGap = 10.0f, kPad = 16.0f, kHeader = 26.0f, kIcon = 42.0f;

		// a dashed rectangle (the shapes of "blocked" and "unknown": a frame that is not whole)
		void DashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float thick)
		{
			const float dash = 6.0f, gap = 4.0f;
			auto line = [&](ImVec2 p, ImVec2 q)
			{
				const float len = std::hypot(q.x - p.x, q.y - p.y);
				if (len <= 0.0f) return;
				const ImVec2 d((q.x - p.x) / len, (q.y - p.y) / len);
				for (float s = 0.0f; s < len; s += dash + gap)
				{
					const float e = std::min(s + dash, len);
					dl->AddLine(ImVec2(p.x + d.x * s, p.y + d.y * s), ImVec2(p.x + d.x * e, p.y + d.y * e), c, thick);
				}
			};
			line(a, ImVec2(b.x, a.y));
			line(ImVec2(b.x, a.y), b);
			line(b, ImVec2(a.x, b.y));
			line(ImVec2(a.x, b.y), a);
		}
	}

	TechStatus EditorApp::TechStatusOf(const Tribe& t) const
	{
		TechStatus::Input in;
		for (const std::unique_ptr<Clan>& c : m_Clans.All())
			if (c && c->Folk == &t) { in.Neighbours = TechStatus::Neighbours(m_Clans, c->Id); break; }
		return TechStatus(t, in);
	}

	std::string EditorApp::TechStateText(const TechStatus& s, const std::string& tech) const
	{
		const Lang& L = m_Lang;
		const TechStatus::Entry& e = s.Of(tech);
		auto techName = [&](const std::string& id) { return Lang::Cap(L.Name("tech", id, id)); };
		switch (e.State)
		{
		case TechState::Known: return L.Text("known");
		case TechState::Studying: return L.Text("being studied, {0} %", L.Num(e.Progress * 100.0f, 0));
		case TechState::Queued: return e.Why == TechWhy::None ? L.Text("number {0} in the queue", e.Queue) : L.Text("number {0} in the queue: {1}", e.Queue, TechWhyText(s, tech));
		case TechState::OwnPace: return L.Text("~{0} % on their own", L.Num(e.Progress * 100.0f, 0));
		case TechState::Studiable: return L.Text("can be studied now");
		case TechState::Blocked: return TechWhyText(s, tech);
		case TechState::OtherClan: return L.Text("the {0} know it", L.Name("clan", e.From, e.From));
		case TechState::Unknown: return e.Hint.empty() ? L.Text("not known yet") : L.Text("after {0}", techName(e.Hint));
		}
		return {};
	}

	std::string EditorApp::TechWhyText(const TechStatus& s, const std::string& tech) const
	{
		const Lang& L = m_Lang;
		const TechStatus::Entry& e = s.Of(tech);
		auto list = [&](const std::string& kind)
		{
			std::string out;
			for (const std::string& m : e.Missing)
			{
				const std::string name = kind != "tech" ? L.Name(kind, m, m) : s.Named(m) ? Lang::Cap(L.Name("tech", m, m)) : std::string("?");
				out += (out.empty() ? "" : ", ") + name;
			}
			return out;
		};
		switch (e.Why)
		{
		case TechWhy::None: return {};
		case TechWhy::Requires: return L.Text("waits for {0}", list("tech"));
		case TechWhy::Tier: return L.Text("tier {0} is not open yet", e.Tier);
		case TechWhy::Material: return L.Text("no {0} at hand", list("item"));
		case TechWhy::Station: return L.Text("needs {0}", list("station"));
		case TechWhy::NotByTrying: return L.Text("only learnt from others");
		}
		return {};
	}

	void EditorApp::DrawTechTree(const Tribe& t, bool playing, float ox, float oy, float w, float h, bool& open, std::string& pick)
	{
		if (!open) return;
		const ClanKnowledge& k = t.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) { open = false; return; }
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const TechStatus status = TechStatusOf(t);
		// T339: the queue is the player's way to direct research: in the game and in the colony, on the player's own clan
		const Clan* hand = m_Clans.Find(HandClan());
		const bool ownQueue = hand && hand->Folk == &t && (playing || m_Director.Colony());
		// the god's view (the observer of the free mode) sees every name; the player only what the clan could name (D2)
		const bool reveal = !playing && !m_Director.Colony();
		auto named = [&](const std::string& id) { return reveal || status.Named(id); };
		auto techName = [&](const std::string& id) { const TechDef* d = tree->Find(id); return Lang::Cap(L.Name("tech", id, d ? d->Name : std::string())); };
		auto block = [&](const std::string& id) { return status.Of(id).Tier >= TechStatus::kEraBlockTier; };   // D6: era 2 as one block
		const std::string aim = t.ResearchAim();

		// ---- the layout: a column per step of what is required (among the technologies drawn); rows ordered by where their requirements stand
		std::map<std::string, int> col;
		std::function<int(const std::string&, int)> column = [&](const std::string& id, int guard) -> int
		{
			if (const auto it = col.find(id); it != col.end()) return it->second;
			const TechDef* d = tree->Find(id);
			if (!d || guard > 32) return 0;
			int c = 0;
			for (const std::string& r : d->Requires) if (tree->Find(r) && status.Shown(r) && !block(r)) c = std::max(c, column(r, guard + 1) + 1);
			col[id] = c;
			return c;
		};
		for (const auto& [id, d] : tree->Techs) if (status.Shown(id) && !block(id)) column(id, 0);   // (T181: no eras: the columns are only what each requires)
		int columns = 0;
		std::map<int, std::vector<std::string>> byCol;
		for (const auto& [id, c] : col) { byCol[c].push_back(id); columns = std::max(columns, c + 1); }
		std::map<std::string, float> row;
		for (auto& [c, ids] : byCol)
		{
			// by the mean row of what they require (the first column by name), so the lines cross as little as they can
			auto key = [&](const std::string& id)
			{
				const TechDef* d = tree->Find(id);
				float sum = 0.0f;
				int n = 0;
				for (const std::string& r : d->Requires) if (row.count(r)) { sum += row[r]; ++n; }
				return n ? sum / static_cast<float>(n) : 1000.0f;
			};
			std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) { const float ka = key(a), kb = key(b); return ka != kb ? ka < kb : a < b; });
			for (size_t i = 0; i < ids.size(); ++i) row[ids[i]] = static_cast<float>(i);
		}
		int rows = 0;
		for (const auto& [c, ids] : byCol) rows = std::max(rows, static_cast<int>(ids.size()));
		int eraBlock = 0;   // D6: what tier 4 (era 2) keeps, as one "?" after the last column
		for (const auto& [id, e] : status.All()) if (e.Shown && block(id) && e.State != TechState::Known) ++eraBlock;
		// the way to the picked one: it and all it requires, lit
		std::set<std::string> pickWay;
		{
			std::function<void(const std::string&, int)> walk = [&](const std::string& id, int guard)
			{
				if (guard > 32 || pickWay.count(id) || !tree->Find(id)) return;
				pickWay.insert(id);
				for (const std::string& r : tree->Find(id)->Requires) walk(r, guard + 1);
			};
			if (!pick.empty()) walk(pick, 0);
		}

		// ---- the window: the world dims, the tree over it with its card at the bottom
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(ox, oy), ImVec2(ox + w, oy + h), IM_COL32(0, 0, 0, 120));
		const float margin = 24.0f;
		ImGui::SetNextWindowPos(ImVec2(ox + margin, oy + margin), ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(w - 2.0f * margin, h - 2.0f * margin), ImGuiCond_Always);
		std::string title = L.Text("Tree of knowledge") + "  -  " + L.Text("{0} of {1} known", status.KnownCount(), status.Total()) + "  \xC2\xB7  " + L.Text("{0} can be studied", status.StudiableCount());
		if (!reveal && status.UnnamedCount() > 0) title += "  \xC2\xB7  " + L.Text("? {0} to discover", status.UnnamedCount());
		if (ImGui::Begin((title + "###techtree").c_str(), &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking))
		{
			// the legend: each state with the shape its nodes have
			ImDrawList* wdl = ImGui::GetWindowDrawList();
			auto legend = [&](const std::string& text, ImU32 frame, bool dashed, float thick, const char* mark)
			{
				const ImVec2 at = ImGui::GetCursorScreenPos(), size = ImGui::CalcTextSize(text.c_str());
				const ImVec2 a(at.x, at.y + 1.0f), b(at.x + 22.0f, at.y + size.y + 1.0f);
				wdl->AddRectFilled(a, b, T::HideRaised, 3.0f);
				if (dashed) DashedRect(wdl, a, b, frame, thick);
				else wdl->AddRect(a, b, frame, 3.0f, 0, thick);
				if (mark) wdl->AddText(ImVec2(a.x + 11.0f - ImGui::CalcTextSize(mark).x * 0.5f, a.y), frame, mark);
				wdl->AddText(ImVec2(b.x + 5.0f, at.y + 1.0f), T::Bone, text.c_str());
				ImGui::Dummy(ImVec2(size.x + 32.0f, size.y + 2.0f));
				ImGui::SameLine(0.0f, 10.0f);
			};
			legend(L.Text("known"), T::Good, false, 1.0f, "v");
			legend(L.Text("being studied"), T::Ochre, false, 3.0f, nullptr);
			legend(L.Text("in the queue"), T::Ochre, false, 1.0f, "1");
			legend(L.Text("on their own"), T::BoneDim, false, 1.0f, "~");
			legend(L.Text("can be studied now"), T::Bone, false, 1.0f, "+");
			legend(L.Text("something is missing"), T::Warn, true, 1.5f, "!");
			legend(L.Text("another clan knows it"), T::News, false, 1.5f, nullptr);
			legend(L.Text("not known yet"), T::HideLine, true, 1.0f, "?");
			ImGui::NewLine();
			// the queue in a line (T339: the tree and the queue say the same), or the aim the god's vision set
			if (ownQueue)
			{
				const std::vector<std::string>& queue = t.Research().All();
				if (queue.empty()) ImGui::TextDisabled("%s", L.Text("The study queue is empty: pick a technology and add it").c_str());
				else
				{
					std::string line = L.Text("Study queue:");
					for (size_t i = 0; i < queue.size() && i < 6; ++i) line += (i == 0 ? " " : ", ") + std::to_string(i + 1) + ". " + (named(queue[i]) ? techName(queue[i]) : std::string("?"));
					if (queue.size() > 6) line += " " + L.Text("and {0} more", static_cast<int>(queue.size() - 6));
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", line.c_str());
				}
			}
			else if (!aim.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("The clan seeks: {0}", techName(aim)).c_str());
			else ImGui::NewLine();

			// the canvas, scrolled when the tree does not fit; drag it with the mouse
			const float cardH = ownQueue ? 118.0f : 146.0f;   // (the god's card has the vision's line too)
			const ImVec2 canvasSize(ImGui::GetContentRegionAvail().x, std::max(120.0f, ImGui::GetContentRegionAvail().y - cardH));
			ImGui::BeginChild("canvas", canvasSize, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
			const ImVec2 o = ImGui::GetCursorScreenPos();
			const int allColumns = columns + (eraBlock > 0 ? 1 : 0);
			const float contentW = kPad * 2.0f + static_cast<float>(allColumns) * (kNodeW + kColGap) - kColGap;
			const float contentH = kHeader + kPad * 2.0f + static_cast<float>(std::max(rows, 2)) * (kNodeH + kRowGap) - kRowGap;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			auto nodeMin = [&](const std::string& id) { return ImVec2(o.x + kPad + static_cast<float>(col[id]) * (kNodeW + kColGap), o.y + kHeader + kPad + row[id] * (kNodeH + kRowGap)); };
			// the lines: from the right of what is required to the left of what it opens; known ways green, the picked way ochre
			for (const auto& [id, cid] : col)
				for (const std::string& r : tree->Find(id)->Requires)
				{
					if (!col.count(r)) continue;
					const ImVec2 a = nodeMin(r), b = nodeMin(id);
					const ImVec2 p0(a.x + kNodeW, a.y + kNodeH * 0.5f), p1(b.x, b.y + kNodeH * 0.5f);
					const bool lit = pickWay.count(id) && pickWay.count(r);
					const bool knownR = status.Of(r).State == TechState::Known, knownId = status.Of(id).State == TechState::Known;
					const ImU32 c = lit ? T::Ochre : knownR && knownId ? T::Good : knownR ? T::BoneDim : T::Faded(T::HideLine, 0.6f);
					const float bend = (p1.x - p0.x) * 0.5f;
					dl->AddBezierCubic(p0, ImVec2(p0.x + bend, p0.y), ImVec2(p1.x - bend, p1.y), p1, c, lit ? 3.0f : 1.4f);
				}
			// the nodes
			const float lineH = ImGui::GetTextLineHeight();
			for (const auto& [id, cid] : col)
			{
				const TechDef& d = *tree->Find(id);
				const TechStatus::Entry& e = status.Of(id);
				const ImVec2 a = nodeMin(id), b(a.x + kNodeW, a.y + kNodeH);
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID(id.c_str());
				if (ImGui::InvisibleButton("node", ImVec2(kNodeW, kNodeH))) pick = pick == id ? std::string() : id;
				const bool hovered = ImGui::IsItemHovered();
				ImGui::PopID();
				// the body and the frame: the shape says the state (thick, dashed, blue...), the colour backs it
				const bool live = e.State != TechState::Unknown && e.State != TechState::Blocked;
				dl->AddRectFilled(a, b, e.State == TechState::Known ? T::Faded(T::Good, 0.35f) : live || reveal ? T::HideRaised : T::Hide, 6.0f);
				switch (e.State)
				{
				case TechState::Known: dl->AddRect(a, b, T::Good, 6.0f, 0, 1.0f); break;
				case TechState::Studying: dl->AddRect(a, b, T::Ochre, 6.0f, 0, 3.0f); break;
				case TechState::Queued: dl->AddRect(a, b, T::Ochre, 6.0f, 0, 1.5f); break;
				case TechState::OwnPace: dl->AddRect(a, b, T::BoneDim, 6.0f, 0, 1.0f); break;
				case TechState::Studiable: dl->AddRect(a, b, T::Bone, 6.0f, 0, 1.5f); break;
				case TechState::Blocked: DashedRect(dl, a, b, T::Warn, 1.5f); break;
				case TechState::OtherClan: dl->AddRect(a, b, T::News, 6.0f, 0, 2.0f); break;
				case TechState::Unknown: DashedRect(dl, a, b, T::HideLine, 1.0f); break;
				}
				if (id == pick || hovered) dl->AddRect(ImVec2(a.x - 3.0f, a.y - 3.0f), ImVec2(b.x + 3.0f, b.y + 3.0f), id == pick ? T::Bone : T::OchreHover, 8.0f, 0, 1.5f);
				if (!named(id))
				{
					// unknown: a "?" and where it comes after; no icon, no name (D2)
					const float big = ImGui::GetFontSize() * 2.0f;
					dl->AddText(ImGui::GetFont(), big, ImVec2(a.x + 12.0f, a.y + (kNodeH - big) * 0.5f), T::BoneDim, "?");
					const std::string hint = e.Hint.empty() ? L.Text("not known yet") : L.Text("after {0}", techName(e.Hint));
					dl->PushClipRect(ImVec2(a.x + 36.0f, a.y), ImVec2(b.x - 4.0f, b.y), true);
					dl->AddText(ImVec2(a.x + 38.0f, a.y + (kNodeH - lineH) * 0.5f), T::BoneDim, hint.c_str());
					dl->PopClipRect();
					if (hovered) ImGui::SetTooltip("%s", (L.Text("Something the clan does not know yet") + (e.Hint.empty() ? std::string() : "\n" + hint)).c_str());
					continue;
				}
				// its own icon (tech.<id>, assets/ui/sheets/techs_*.jpg) when the atlas has it, else what it opens
				glm::vec2 uv0, uv1;
				std::string icon = IconUv("tech." + id, uv0, uv1) ? "tech." + id : std::string();
				if (icon.empty()) for (const std::string& r : d.Recipes) if (const Recipe* rec = m_Recipes.Find(r)) { icon = rec->Output; break; }
				if (icon.empty() && !d.Buildings.empty()) icon = "build";
				float textX = a.x + 8.0f;
				if (!icon.empty() && IconUv(icon, uv0, uv1))
				{
					dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), ImVec2(a.x + 5.0f, a.y + 6.0f), ImVec2(a.x + 5.0f + kIcon, a.y + 6.0f + kIcon), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y),
						live || e.State == TechState::Known ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 120));
					textX = a.x + kIcon + 10.0f;
				}
				// the name, and the state in a word below it (what it waits for, who knows it, how far)
				dl->PushClipRect(ImVec2(textX, a.y), ImVec2(b.x - 16.0f, b.y), true);
				dl->AddText(ImVec2(textX, a.y + 5.0f), live || e.State == TechState::Known ? T::Bone : T::BoneDim, techName(id).c_str());
				dl->PopClipRect();
				const std::string word = TechStateText(status, id);
				const ImU32 wordInk = e.State == TechState::Blocked ? T::Warn : e.State == TechState::OtherClan ? T::News : e.State == TechState::Studying || e.State == TechState::Queued ? T::Ochre : T::BoneDim;
				dl->PushClipRect(ImVec2(textX, a.y), ImVec2(b.x - 4.0f, b.y), true);
				dl->AddText(ImVec2(textX, a.y + 7.0f + lineH), wordInk, word.c_str());
				dl->PopClipRect();
				// the mark: a tick in the corner, the place in the queue in a badge, "+", "!" or "~"
				const ImVec2 corner(b.x - 9.0f, a.y + 9.0f);
				switch (e.State)
				{
				case TechState::Known:
					dl->AddLine(ImVec2(corner.x - 5.0f, corner.y), ImVec2(corner.x - 1.5f, corner.y + 4.0f), T::Good, 2.0f);
					dl->AddLine(ImVec2(corner.x - 1.5f, corner.y + 4.0f), ImVec2(corner.x + 5.0f, corner.y - 4.0f), T::Good, 2.0f);
					break;
				case TechState::Studying:
				case TechState::Queued:
				{
					const std::string n = std::to_string(e.Queue);
					dl->AddCircleFilled(ImVec2(a.x + 2.0f, a.y + 2.0f), 9.0f, T::Ochre, 16);
					const ImVec2 ns = ImGui::CalcTextSize(n.c_str());
					dl->AddText(ImVec2(a.x + 2.0f - ns.x * 0.5f, a.y + 2.0f - ns.y * 0.5f), T::Ink, n.c_str());
					break;
				}
				case TechState::Studiable: dl->AddText(ImVec2(corner.x - 4.0f, corner.y - 7.0f), T::Ochre, "+"); break;
				case TechState::Blocked: dl->AddText(ImVec2(corner.x - 3.0f, corner.y - 7.0f), T::Warn, "!"); break;
				case TechState::OwnPace: dl->AddText(ImVec2(corner.x - 4.0f, corner.y - 7.0f), T::BoneDim, "~"); break;
				default: break;
				}
				// the bar: thick and ochre while studied, thin and pale when they learn it on their own
				if (e.State == TechState::Studying || ((e.State == TechState::OwnPace || e.State == TechState::Queued) && e.Progress > 0.0f))
				{
					const float v = std::clamp(e.Progress, 0.0f, 1.0f), th = e.State == TechState::Studying ? 5.0f : 3.0f;
					const ImU32 fill = e.State == TechState::OwnPace ? T::BoneDim : T::Ochre;
					dl->AddRectFilled(ImVec2(textX, b.y - 4.0f - th), ImVec2(b.x - 6.0f, b.y - 4.0f), T::Hide, 2.0f);
					dl->AddRectFilled(ImVec2(textX, b.y - 4.0f - th), ImVec2(textX + (b.x - 6.0f - textX) * v, b.y - 4.0f), fill, 2.0f);
				}
				if (hovered) ImGui::SetTooltip("%s", (techName(id) + ": " + word).c_str());
			}
			// D6: era 2 as one block, after the last column
			if (eraBlock > 0)
			{
				const ImVec2 a(o.x + kPad + static_cast<float>(columns) * (kNodeW + kColGap), o.y + kHeader + kPad), b(a.x + kNodeW, a.y + kNodeH * 2.0f + kRowGap);
				dl->AddRectFilled(a, b, T::Hide, 6.0f);
				DashedRect(dl, a, b, T::HideLine, 1.0f);
				const float big = ImGui::GetFontSize() * 2.0f;
				const std::string n = "? " + std::to_string(eraBlock);
				dl->AddText(ImGui::GetFont(), big, ImVec2(a.x + 12.0f, a.y + 10.0f), T::BoneDim, n.c_str());
				dl->AddText(ImVec2(a.x + 12.0f, a.y + 14.0f + big), T::Bone, L.Text("Era 2").c_str());
				dl->AddText(ImVec2(a.x + 12.0f, a.y + 16.0f + big + lineH), T::BoneDim, L.Text("signs of a village").c_str());
				ImGui::SetCursorScreenPos(a);
				ImGui::InvisibleButton("era2", ImVec2(b.x - a.x, b.y - a.y));
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} technologies of the next age: they show once the village gets there", eraBlock).c_str());
			}
			// the space the tree takes, so the child scrolls; dragging the background moves it
			ImGui::SetCursorScreenPos(o);
			ImGui::Dummy(ImVec2(contentW, contentH));
			if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !ImGui::IsAnyItemActive())
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				ImGui::SetScrollX(ImGui::GetScrollX() - delta.x);
				ImGui::SetScrollY(ImGui::GetScrollY() - delta.y);
			}
			ImGui::EndChild();

			// ---- the card of the picked technology (or how to use the tree)
			ImGui::BeginChild("card", ImVec2(0.0f, 0.0f));
			const TechDef* d = pick.empty() ? nullptr : tree->Find(pick);
			if (!d) ImGui::TextDisabled("%s", L.Text(ownQueue ? "Click a technology to see what it opens and to add it to the study queue. Drag to move around the tree."
				: "Click a technology to see who is on it, what it needs and what it opens. Drag to move around the tree.").c_str());
			else if (!named(pick))
			{
				// one the clan cannot name: no name, no buttons; where it comes after
				const TechStatus::Entry& e = status.Of(pick);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::BoneDim), "?  %s", L.Text("Something the clan does not know yet").c_str());
				ImGui::TextDisabled("%s", (e.Hint.empty() ? L.Text("The clan must learn more before it can even think of it") : L.Text("It comes after {0}", techName(e.Hint))).c_str());
			}
			else
			{
				const TechStatus::Entry& e = status.Of(pick);
				const bool have = e.State == TechState::Known;
				// its icon large on the left, the rest beside it
				if (Icon("tech." + pick, 64.0f)) ImGui::SameLine(0.0f, 10.0f);
				ImGui::BeginGroup();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
				ImGui::TextUnformatted(techName(pick).c_str());
				ImGui::PopStyleColor();
				ImGui::SameLine();
				std::string state = TechStateText(status, pick);
				if (!e.Who.empty())
				{
					std::string who;
					for (const std::string& n : e.Who) who += (who.empty() ? "" : ", ") + n;
					state += " (" + L.Text("on it: {0}", who) + ")";
				}
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(e.State == TechState::Blocked ? T::Warn : T::BoneDim), "%s", state.c_str());
				// what it opens: the recipes' outputs and the buildings, with their icons
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(L.Text("Opens:").c_str());
				bool any = false;
				for (const std::string& r : d->Recipes)
					if (const Recipe* rec = m_Recipes.Find(r))
					{
						any = true;
						const ItemDef* item = m_Items.Find(rec->Output);
						ImGui::SameLine();
						if (Icon(rec->Output, 20.0f)) ImGui::SameLine(0.0f, 3.0f);
						ImGui::TextUnformatted(L.Name("item", rec->Output, item && !item->Name.empty() ? item->Name : std::string()).c_str());
					}
				for (const std::string& bid : d->Buildings)
				{
					any = true;
					const auto bdef = m_Buildings.Defs.find(bid);
					ImGui::SameLine();
					if (Icon("build", 20.0f)) ImGui::SameLine(0.0f, 3.0f);
					ImGui::TextUnformatted(Lang::Cap(L.Name("building", bid, bdef != m_Buildings.Defs.end() ? bdef->second.Name : std::string())).c_str());
					if (ImGui::IsItemHovered() && bdef != m_Buildings.Defs.end())
					{
						std::string tip;
						for (const std::string& l : BuildingDoes(bdef->second)) tip += (tip.empty() ? "" : "\n") + l;
						ImGui::SetTooltip("%s", tip.c_str());
					}
				}
				if (!any) { ImGui::SameLine(); ImGui::TextDisabled("%s", L.Text("knowledge for the clan (rites, stories, ways of doing)").c_str()); }
				// T339: the player's buttons queue `game research ...` (the queue of T324, the only way to direct research)
				if (ownQueue && !have)
				{
					const std::string name = techName(pick);
					if (e.Queue == 0)
					{
						{
							const GameTheme::PrimaryScope primary;
							if (ImGui::Button((L.Text("Add to the queue ({0}.)", static_cast<int>(t.Research().All().size()) + 1) + "###qadd").c_str())) run("game research add " + pick);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Add {0} to the end of the queue", name).c_str());
						ImGui::SameLine();
						if (ImGui::Button((L.Text("Study it first") + "###qtop").c_str())) run("game research top " + pick);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Study {0} before the rest", name).c_str());
					}
					else
					{
						if (e.Queue > 1)
						{
							if (ImGui::Button((L.Text("Study it first") + "###qtop").c_str())) run("game research top " + pick);
							if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Study {0} before the rest", name).c_str());
							ImGui::SameLine();
						}
						if (ImGui::Button((L.Text("Take it out of the queue") + "###qoff").c_str())) run("game research remove " + pick);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Take {0} out of the queue", name).c_str());
					}
					if (e.Why != TechWhy::None && e.State != TechState::Studying && e.State != TechState::Studiable)
					{
						ImGui::SameLine();
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("In the queue it waits until this changes: {0}", TechWhyText(status, pick)).c_str());
					}
				}
				// T242: for the god (the observer of the free mode): [Vision] sends a dream of it to the clan (`director vision`), with its cost, why
				// not, and the warning of the material or of the inventor the clan lacks; the vision the clan holds says who dreamt it, with [Let it go]
				else if (!playing && !have && !m_Director.Colony())
				{
					const Clan* clan = m_Clans.Find(m_ActiveClan);
					const std::string id = std::to_string(m_ActiveClan);
					const std::string clanName = clan ? L.Name("clan", clan->Name, clan->Name) : id;
					const DirectorVision* held = clan ? m_Director.VisionOf(clan->Id) : nullptr;
					if (held && held->Tech == pick)
					{
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text("{0} dreamt of it: the {1} seek it", held->Dreamer, clanName).c_str());
						ImGui::SameLine();
						if (ImGui::SmallButton((L.Text("Let it go") + "###visionoff").c_str())) run("director vision " + id + " none");
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("The {0} choose what to seek again (what was paid is not given back)", clanName).c_str());
					}
					else if (clan)
					{
						const DirectorVisionPreview p = m_Director.PreviewVision(clan->Id, pick);
						std::string why;
						if (!p.Ok) why = held ? L.Text("A vision already guides the {0} ({1}): let it go first", clanName, techName(held->Tech)) : Lang::Cap(L.Line(p.Why));
						ImGui::BeginDisabled(!p.Ok);
						{
							std::optional<T::PrimaryScope> primary;
							if (p.Ok) primary.emplace();
							if (ImGui::Button((L.Text("Vision ({0})", L.Num(p.Cost, 0)) + "###vision").c_str())) run("director vision " + id + " " + pick);
						}
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
							ImGui::SetTooltip("%s", (!why.empty() ? why : L.Text("{0} dreams of it and tells it at the fire: the {1} seek it and what it needs first ({2} of {3} would agree)", p.Dreamer, clanName, p.Yes, p.Voters)
								+ "\n" + L.Text("It orients, it does not teach: they still have to work it out")).c_str());
						ImGui::SameLine();
						if (!p.Missing.empty())
						{
							std::string lack;
							for (const std::string& m : p.Missing) lack += (lack.empty() ? "" : ", ") + L.Name("item", m, m);
							ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("They lack {0}: they will not learn it until they find it", lack).c_str());
						}
						else if (!p.Dreamer.empty() && !p.Inventor) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Warn), "%s", L.Text("Nobody of the {0} can work it out: only from another clan", clanName).c_str());
						else if (!why.empty()) ImGui::TextDisabled("%s", why.c_str());
						else ImGui::TextDisabled("%s", L.Text("Dreamer: {0}", p.Dreamer).c_str());
					}
				}
				ImGui::EndGroup();
			}
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
