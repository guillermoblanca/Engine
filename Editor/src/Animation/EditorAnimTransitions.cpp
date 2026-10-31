#include "EditorApp.h"
#include "Animation/EditorAnimPreview.h"
#include <algorithm>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		constexpr const char* kTestA = "bind:test_from";
		constexpr const char* kTestB = "bind:test_to";

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
	}

	void EditorApp::DrawAnimBindingTransitions(const std::vector<BindableAction>& actions, AnimBindings& bindings, const AnimManifest& manifest,
		const BindingDefaults& defaults, AnimInstance* anim, const std::string& skeleton, const ClipExists& hasClip)
	{
		AnimBindingsUi& ui = m_AnimBindingsUi;
		ImGui::BeginChild("##transitions", ImVec2(0.0f, 0.0f), true);
		ImGui::Text("Transitions (the blend when one montage gives way to another in a slot)");
		std::vector<std::string> ends = { "*" };
		for (const BindableAction& action : actions)
			if (action.Key.rfind("loco:", 0) != 0 && action.Key != "air") ends.push_back(action.Key);
		for (const MontageDesc& montage : manifest.Montages)
			if (std::find(ends.begin(), ends.end(), montage.Name) == ends.end()) ends.push_back(montage.Name);
		if (ui.Transition >= static_cast<int>(bindings.Transitions.size())) ui.Transition = -1;
		for (size_t i = 0; i < bindings.Transitions.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::RadioButton("##sel", ui.Transition == static_cast<int>(i))) ui.Transition = static_cast<int>(i);
			ImGui::SameLine();
			std::string from = bindings.Transitions[i].From;
			std::string to = bindings.Transitions[i].To;
			StringCombo("from", from, ends, 190.0f);
			ImGui::SameLine();
			StringCombo("to", to, ends, 190.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			float duration = std::clamp(bindings.Transitions[i].Duration, 0.0f, 1.5f);
			ImGui::SliderFloat("s", &duration, 0.0f, 1.5f, "%.2f");
			ImGui::SameLine();
			std::string curve = BlendCurveName(bindings.Transitions[i].Curve);
			StringCombo("curve", curve, { "smooth", "linear", "easeIn", "easeOut" }, 90.0f);
			ImGui::SameLine();
			const bool remove = ImGui::SmallButton("x");
			if (remove)
			{
				bindings.Transitions.erase(bindings.Transitions.begin() + static_cast<std::ptrdiff_t>(i));
				if (ui.Transition == static_cast<int>(i)) ui.Transition = -1;
				else if (ui.Transition > static_cast<int>(i)) --ui.Transition;
				ImGui::PopID();
				break;
			}
			bindings.Transitions[i].From = std::move(from);
			bindings.Transitions[i].To = std::move(to);
			bindings.Transitions[i].Duration = duration;
			bindings.Transitions[i].Curve = BlendCurveFromName(curve);
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ transition"))
		{
			BindingTransition transition;
			transition.From = "*";
			transition.To = "*";
			bindings.Transitions.push_back(transition);
			ui.Transition = static_cast<int>(bindings.Transitions.size()) - 1;
		}
		ImGui::SameLine();
		const bool canTest = anim && ui.Transition >= 0 && ui.Transition < static_cast<int>(bindings.Transitions.size());
		if (canTest)
		{
			const BindingTransition& selected = bindings.Transitions[static_cast<size_t>(ui.Transition)];
			ImGui::TextDisabled("Selected: %s -> %s", selected.From.c_str(), selected.To.c_str());
			ImGui::TextDisabled("Change the animation on the left by selecting an action, then choose its clip in 'source'.");
		}
		ImGui::BeginDisabled(!canTest);
		if (ImGui::Button("Test selected A -> B") && canTest)
		{
			const BindingTransition& transition = bindings.Transitions[static_cast<size_t>(ui.Transition)];
			MontagePlayer& player = anim->Montages();
			if (ResolveTransitionPreviewMontage(player, bindings, manifest, defaults, transition.From, skeleton, hasClip, kTestA) &&
				ResolveTransitionPreviewMontage(player, bindings, manifest, defaults, transition.To, skeleton, hasClip, kTestB))
			{
				AddPreviewTransition(player, kTestA, kTestB, transition.Duration, transition.Curve);
				player.SetPaused(false);
				m_Player.PlayMontage(kTestA);
				ui.TestStage = 1;
				ui.TestTimer = 0.0f;
				ui.Status = "testing " + transition.From + " -> " + transition.To;
			}
			else ui.Status = "cannot test: an end plays nothing the character has";
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat("switch after", &ui.TestDelay, 0.1f, 3.0f, "%.1f s");
		if (ui.TestStage == 1 && anim)
		{
			ui.TestTimer += ImGui::GetIO().DeltaTime;
			if (ui.TestTimer >= ui.TestDelay) { m_Player.PlayMontage(kTestB); ui.TestStage = 0; }
		}
	}
}
