#pragma once
// The look of the game's interface (era 1: dark hide for the panels, bone text, ochre for what is pressed, earth colours for the states). The
// editor keeps its own blue style (ImGuiLayer.cpp): the game pushes this one only while it draws (GameTheme::Scope), never the global style.
// The values are the tokens of the gb-game-ui skill (.claude/skills/gb-game-ui/references/tokens.json, where their contrast is measured):
// keep both in step.
#include <imgui.h>
#include <iterator>

namespace GB::Editor::GameTheme
{
	inline constexpr ImU32 Hide = IM_COL32(36, 27, 20, 225), HideRaised = IM_COL32(56, 42, 31, 235), HideLine = IM_COL32(120, 88, 58, 255);
	inline constexpr ImU32 HidePanel = IM_COL32(36, 27, 20, 245);   // the windows: more solid than the cards, the world's labels must not show through a page
	inline constexpr ImU32 HideHover = IM_COL32(92, 66, 44, 245), HideActive = IM_COL32(112, 80, 52, 250);
	inline constexpr ImU32 Bone = IM_COL32(243, 233, 210, 255), BoneDim = IM_COL32(196, 182, 156, 255);
	inline constexpr ImU32 Parchment = IM_COL32(250, 244, 228, 240), Ink = IM_COL32(58, 38, 22, 255), Outline = IM_COL32(74, 48, 28, 255);
	inline constexpr ImU32 Ochre = IM_COL32(232, 160, 64, 255), OchreHover = IM_COL32(246, 186, 96, 255);
	inline constexpr ImU32 Good = IM_COL32(140, 186, 92, 255), Warn = IM_COL32(236, 184, 72, 255), Bad = IM_COL32(228, 92, 72, 255);
	inline constexpr ImU32 News = IM_COL32(214, 226, 250, 255), Shade = IM_COL32(0, 0, 0, 150), Clear = IM_COL32(0, 0, 0, 0);
	inline constexpr float Pad = 8.0f, Gap = 6.0f, Margin = 12.0f, Radius = 6.0f, BarH = 14.0f, Icon = 24.0f;

	// a need's colour: good above a half, a warning above the danger line (a quarter), bad below it (the bar also shows the number and the line)
	inline ImU32 Level(float v01) { return v01 > 0.5f ? Good : v01 > 0.25f ? Warn : Bad; }

	// the same colour with another alpha (for what fades out)
	inline ImU32 Faded(ImU32 c, float alpha01)
	{
		const unsigned a = static_cast<unsigned>(static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFF) * (alpha01 < 0.0f ? 0.0f : alpha01 > 1.0f ? 1.0f : alpha01));
		return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
	}

	// the game's panels while it lives: every colour the game's widgets use (so no blue of the editor shows through) and the rounding, pushed on
	// construction and popped on destruction
	class Scope
	{
	public:
		Scope()
		{
			struct Entry { ImGuiCol Col; ImU32 Value; };
			static const Entry entries[] = {
				{ ImGuiCol_WindowBg, HidePanel }, { ImGuiCol_ChildBg, Clear }, { ImGuiCol_PopupBg, HideRaised }, { ImGuiCol_Border, HideLine },
				{ ImGuiCol_Text, Bone }, { ImGuiCol_TextDisabled, BoneDim }, { ImGuiCol_TextSelectedBg, IM_COL32(232, 160, 64, 90) },
				{ ImGuiCol_FrameBg, HideRaised }, { ImGuiCol_FrameBgHovered, HideHover }, { ImGuiCol_FrameBgActive, HideActive },
				{ ImGuiCol_TitleBg, HideRaised }, { ImGuiCol_TitleBgActive, HideRaised }, { ImGuiCol_TitleBgCollapsed, HideRaised },
				{ ImGuiCol_Button, HideRaised }, { ImGuiCol_ButtonHovered, HideHover }, { ImGuiCol_ButtonActive, HideActive },
				{ ImGuiCol_Header, HideRaised }, { ImGuiCol_HeaderHovered, HideHover }, { ImGuiCol_HeaderActive, HideActive },
				{ ImGuiCol_Separator, HideLine }, { ImGuiCol_SeparatorHovered, Ochre }, { ImGuiCol_SeparatorActive, OchreHover },
				{ ImGuiCol_Tab, HideRaised }, { ImGuiCol_TabHovered, HideHover }, { ImGuiCol_TabSelected, HideActive }, { ImGuiCol_TabSelectedOverline, Ochre },
				{ ImGuiCol_TabDimmed, HideRaised }, { ImGuiCol_TabDimmedSelected, HideActive }, { ImGuiCol_TabDimmedSelectedOverline, Ochre },
				{ ImGuiCol_ScrollbarBg, Clear }, { ImGuiCol_ScrollbarGrab, HideLine }, { ImGuiCol_ScrollbarGrabHovered, BoneDim }, { ImGuiCol_ScrollbarGrabActive, Ochre },
				{ ImGuiCol_CheckMark, Ochre }, { ImGuiCol_SliderGrab, Ochre }, { ImGuiCol_SliderGrabActive, OchreHover }, { ImGuiCol_PlotHistogram, Ochre },
				{ ImGuiCol_TableHeaderBg, HideRaised }, { ImGuiCol_TableBorderStrong, HideLine }, { ImGuiCol_TableBorderLight, HideRaised },
				{ ImGuiCol_TableRowBg, Clear }, { ImGuiCol_TableRowBgAlt, IM_COL32(255, 240, 210, 12) },
				{ ImGuiCol_ResizeGrip, HideLine }, { ImGuiCol_ResizeGripHovered, Ochre }, { ImGuiCol_ResizeGripActive, OchreHover }, { ImGuiCol_NavCursor, Ochre },
			};
			for (const Entry& e : entries) ImGui::PushStyleColor(e.Col, e.Value);
			m_Colors = static_cast<int>(std::size(entries));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Radius);
			ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Radius);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Gap, 5.0f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.45f);   // the default 0.6 hardly tells a disabled button from a live one
		}
		~Scope() { ImGui::PopStyleVar(8); ImGui::PopStyleColor(m_Colors); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		int m_Colors = 0;
	};

	// the one button that matters most where it stands (proposing a building the clan knows, resuming the game): ochre with dark ink on it
	class PrimaryScope
	{
	public:
		PrimaryScope()
		{
			ImGui::PushStyleColor(ImGuiCol_Button, Ochre);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, OchreHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, OchreHover);
			ImGui::PushStyleColor(ImGuiCol_Text, Ink);
		}
		~PrimaryScope() { ImGui::PopStyleColor(4); }
		PrimaryScope(const PrimaryScope&) = delete;
		PrimaryScope& operator=(const PrimaryScope&) = delete;
	};

	// a panel drawn straight on a draw list (the parts of the HUD that are not windows): the hide, its line and, if given, an accent on the left
	inline void Card(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 accent = 0, float alpha01 = 1.0f)
	{
		dl->AddRectFilled(min, max, Faded(Hide, alpha01), Radius);
		dl->AddRect(min, max, Faded(HideLine, alpha01), Radius);
		if (accent) dl->AddRectFilled(min, ImVec2(min.x + 4.0f, max.y), Faded(accent, alpha01), Radius, ImDrawFlags_RoundCornersLeft);
	}

	// a key as a key cap: ochre with dark ink, its width from the text; returns the width it took
	inline float KeyCap(ImDrawList* dl, ImVec2 at, const char* key, float height)
	{
		const ImVec2 size = ImGui::CalcTextSize(key);
		const float w = size.x + 10.0f;
		dl->AddRectFilled(at, ImVec2(at.x + w, at.y + height), Ochre, 4.0f);
		dl->AddRect(at, ImVec2(at.x + w, at.y + height), Outline, 4.0f);
		dl->AddText(ImVec2(at.x + 5.0f, at.y + (height - size.y) * 0.5f), Ink, key);
		return w;
	}
}
