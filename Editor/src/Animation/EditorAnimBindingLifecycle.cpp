#include "EditorApp.h"
#include <algorithm>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		int FindVariantIndex(const AnimBindings& bindings, const std::string& action, const std::string& scope, int variant)
		{
			int index = 0;
			for (size_t i = 0; i < bindings.Bindings.size(); ++i)
				if (bindings.Bindings[i].Action == action && bindings.Bindings[i].Skeleton == scope && index++ == variant)
					return static_cast<int>(i);
			return -1;
		}
	}

	int EditorApp::DrawAnimBindingLifecycle(const BindableAction& action, AnimBindings& bindings, const AnimManifest& manifest, const std::string& scope)
	{
		AnimBindingsUi& ui = m_AnimBindingsUi;
		const bool isLoco = action.Key.rfind("loco:", 0) == 0;
		const bool isAir = action.Key == "air";
		const bool usesContext = action.Key.rfind("ability:", 0) == 0 || action.Key.rfind("activity:", 0) == 0;
		int variantCount = 0;
		for (const AnimBinding& binding : bindings.Bindings)
			if (binding.Action == action.Key && binding.Skeleton == scope) ++variantCount;
		if (variantCount > 0) ui.Variant = std::clamp(ui.Variant, 0, variantCount - 1);
		else ui.Variant = 0;

		int bindingIndex = FindVariantIndex(bindings, action.Key, scope, ui.Variant);
		if (usesContext && variantCount > 0)
		{
			ImGui::TextUnformatted("variant:");
			int variant = 0;
			for (AnimBinding& candidate : bindings.Bindings)
			{
				if (candidate.Action != action.Key || candidate.Skeleton != scope) continue;
				ImGui::SameLine();
				std::string label;
				for (const auto& [key, value] : candidate.When) label += (label.empty() ? "" : ", ") + key + "=" + value;
				if (label.empty()) label = "always";
				ImGui::PushID(variant);
				if (ImGui::RadioButton(label.c_str(), ui.Variant == variant)) { ui.Variant = variant; ui.Clip = 0; bindingIndex = FindVariantIndex(bindings, action.Key, scope, variant); }
				ImGui::PopID();
				++variant;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("+ context"))
			{
				if (bindingIndex < 0 || bindingIndex >= static_cast<int>(bindings.Bindings.size())) return -1;
				AnimBinding copy = bindings.Bindings[static_cast<size_t>(bindingIndex)];
				copy.When.clear();
				copy.When["node"] = m_NodeDefs.empty() ? std::string("tree") : m_NodeDefs.begin()->first;
				for (const auto& [id, definition] : m_NodeDefs)
					if (definition.Ability == action.Key.substr(8)) { copy.When["node"] = id; break; }
				bindings.Bindings.push_back(std::move(copy));
				ui.Variant = variantCount;
				bindingIndex = static_cast<int>(bindings.Bindings.size()) - 1;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Another clip for when the action happens in a context: on a given node, with a given tool...\nThe most specific variant that matches wins; 'always' is the fallback.");
		}
		if (bindingIndex < 0)
		{
			ImGui::SeparatorText("Animation binding");
			ImGui::TextWrapped("This action currently uses its default animation. Create a binding to choose a different animation for this action.");
			if (ImGui::Button("Create binding and choose animation"))
			{
				AnimBinding created;
				created.Action = action.Key;
				created.Skeleton = scope;
				if (isLoco)
					for (const std::string& clip : action.Defaults) created.Clips.push_back({ clip, clip });
				else if (isAir)
					created.Clips.push_back({ manifest.AirClip });
				else
					for (const std::string& clip : action.Defaults) created.Clips.push_back({ clip });
				if (created.Clips.empty()) created.Clips.push_back({});
				bindings.Bindings.push_back(std::move(created));
				ui.Variant = 0;
				bindingIndex = static_cast<int>(bindings.Bindings.size()) - 1;
			}
			ImGui::TextDisabled("%s", scope.empty() ? "for every model" : ("for the skeleton '" + scope + "'").c_str());
		}
		if (bindingIndex < 0) return -1;

		if (ImGui::Button("Remove binding (back to the default)"))
		{
			bindings.Bindings.erase(bindings.Bindings.begin() + bindingIndex);
			ui.Clip = 0;
			ui.Variant = 0;
			return -1;
		}
		ImGui::SameLine();
		const AnimBinding& binding = bindings.Bindings[static_cast<size_t>(bindingIndex)];
		ImGui::TextDisabled("%s", binding.Skeleton.empty() ? "every model" : ("skeleton " + binding.Skeleton).c_str());
		return bindingIndex;
	}
}
