#include "EditorApp.h"
#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		bool StringCombo(const char* label, std::string& value, const std::vector<std::string>& options, float width)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
			{
				for (const std::string& option : options)
					if (ImGui::Selectable(option.c_str(), option == value)) { value = option; changed = true; }
				ImGui::EndCombo();
			}
			return changed;
		}

		bool OptionalSlider(const char* label, std::optional<float>& value, float base, float lo, float hi, const char* format)
		{
			bool changed = false;
			ImGui::PushID(label);
			bool enabled = value.has_value();
			if (ImGui::Checkbox("##set", &enabled)) { if (enabled) value = base; else value.reset(); changed = true; }
			ImGui::SameLine();
			float shown = value.value_or(base);
			ImGui::BeginDisabled(!enabled);
			ImGui::SetNextItemWidth(170.0f);
			if (ImGui::SliderFloat(label, &shown, lo, hi, format)) { value = shown; changed = true; }
			ImGui::EndDisabled();
			ImGui::PopID();
			return changed;
		}

		bool OptionalCheck(const char* label, std::optional<bool>& value, bool base)
		{
			bool changed = false;
			ImGui::PushID(label);
			bool enabled = value.has_value();
			if (ImGui::Checkbox("##set", &enabled)) { if (enabled) value = base; else value.reset(); changed = true; }
			ImGui::SameLine();
			bool shown = value.value_or(base);
			ImGui::BeginDisabled(!enabled);
			if (ImGui::Checkbox(label, &shown)) { value = shown; changed = true; }
			ImGui::EndDisabled();
			ImGui::PopID();
			return changed;
		}
	}

	void EditorApp::DrawAnimBindingClipSettings(const BindableAction& action, AnimBinding& binding, const AnimManifest& manifest,
		const BindingDefaults& defaults, const ClipExists& hasClip)
	{
		AnimBindingsUi& ui = m_AnimBindingsUi;
		if (binding.Clips.empty()) return;
		ui.Clip = std::clamp(ui.Clip, 0, static_cast<int>(binding.Clips.size()) - 1);
		BindingClip& clip = binding.Clips[static_cast<size_t>(ui.Clip)];
		AnimBinding plain;
		plain.Action = binding.Action;
		plain.Clips.push_back({ clip.Source });
		AnimBindings temporary;
		temporary.Bindings.push_back(plain);
		const ResolvedClip base = ResolveClip(temporary, manifest, defaults, plain.Action, {}, "", 0, hasClip);

		ImGui::SeparatorText("Settings of the selected clip (tick to override the montage's own)");
		OptionalCheck("loop", clip.Loop, base.Loop);
		OptionalSlider("trim start", clip.TrimStart, base.TrimStart, 0.0f, 1.0f, "%.2f of the clip");
		OptionalSlider("trim end", clip.TrimEnd, base.TrimEnd, 0.0f, 1.0f, "%.2f of the clip");
		OptionalSlider("speed", clip.Speed, base.Speed, 0.1f, 4.0f, "x%.2f");
		OptionalSlider("blend in", clip.BlendIn, base.BlendIn, 0.0f, 2.0f, "%.2f s");
		OptionalSlider("blend out", clip.BlendOut, base.BlendOut, 0.0f, 2.0f, "%.2f s");
		ImGui::PushID("curve");
		bool curveEnabled = clip.Curve.has_value();
		if (ImGui::Checkbox("##set", &curveEnabled)) { if (curveEnabled) clip.Curve = base.Curve; else clip.Curve.reset(); }
		ImGui::SameLine();
		ImGui::BeginDisabled(!curveEnabled);
		std::string curve = BlendCurveName(clip.Curve.value_or(base.Curve));
		if (StringCombo("blend curve", curve, { "smooth", "linear", "easeIn", "easeOut" }, 120.0f)) clip.Curve = BlendCurveFromName(curve);
		ImGui::EndDisabled();
		ImGui::PopID();
		OptionalCheck("keep root motion / feet (do not pin the clip's travel)", clip.KeepRootMotion, base.KeepRootMotion);

		if (action.Key.rfind("ability:", 0) == 0 && !m_NodeDefs.empty())
		{
			ImGui::SeparatorText("Try on a node");
			std::vector<std::string> nodes;
			for (const auto& [id, definition] : m_NodeDefs)
				if (!definition.Decor && !definition.Ability.empty()) nodes.push_back(id);
			if (ui.TryNode.empty() || std::find(nodes.begin(), nodes.end(), ui.TryNode) == nodes.end())
			{
				ui.TryNode = nodes.empty() ? std::string() : nodes.front();
				for (const std::string& node : nodes)
					if (m_NodeDefs.at(node).Ability == action.Key.substr(8)) { ui.TryNode = node; break; }
			}
			StringCombo("node type", ui.TryNode, nodes, 180.0f);
			ImGui::SameLine();
			if (ImGui::Button("Try on a node") && !ui.TryNode.empty())
			{
				std::string why;
				ui.Status = TryOnNode(ui.TryNode, why) ? why : "cannot try: " + why;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Puts the character in front of the nearest node of that type (with the tool it asks for) and makes it work it:\nthe clip that plays is the one the bindings pick for that node.");
		}
	}
}
