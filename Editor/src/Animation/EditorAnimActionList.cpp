#include "EditorApp.h"
#include <algorithm>
#include <imgui.h>

namespace GB::Editor
{
	namespace
	{
		std::string JoinActions(const std::vector<std::string>& values)
		{
			std::string result;
			for (const std::string& value : values) result += (result.empty() ? "" : ", ") + value;
			return result;
		}
	}

	void EditorApp::DrawAnimBindingActionList(const std::vector<BindableAction>& actions, AnimBindings& bindings)
	{
		AnimBindingsUi& ui = m_AnimBindingsUi;
		ImGui::BeginChild("##actions", ImVec2(270.0f, -150.0f), true);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##filter", "filter actions", ui.Filter, sizeof ui.Filter);

		if (ui.Selected.empty() && !actions.empty())
		{
			for (const BindableAction& action : actions)
				if (action.Key.rfind("loco:", 0) != 0 && action.Key != "air")
				{
					ui.Selected = action.Key;
					break;
				}
			if (ui.Selected.empty()) ui.Selected = actions.front().Key;
		}

		std::vector<std::string> categories;
		for (const BindableAction& action : actions)
			if (std::find(categories.begin(), categories.end(), action.Category) == categories.end()) categories.push_back(action.Category);
		for (const char* category : { "Locomotion", "Interaction", "Attack", "Defence", "Life", "Work", "Social", "Other" })
		{
			if (std::find(categories.begin(), categories.end(), category) == categories.end()) continue;
			if (!ImGui::TreeNodeEx(category, ImGuiTreeNodeFlags_DefaultOpen)) continue;
			for (const BindableAction& action : actions)
			{
				if (action.Category != category) continue;
				if (ui.Filter[0] && action.Key.find(ui.Filter) == std::string::npos) continue;
				bool bound = false;
				for (const AnimBinding& binding : bindings.Bindings) bound |= binding.Action == action.Key;
				const size_t separator = action.Key.find(':');
				const std::string label = (bound ? "* " : "  ") + action.Key.substr(separator == std::string::npos ? 0 : separator + 1) + "##" + action.Key;
				if (ImGui::Selectable(label.c_str(), ui.Selected == action.Key))
				{
					ui.Selected = action.Key;
					ui.Clip = 0;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s\nplays by default: %s", action.Key.c_str(), action.Defaults.empty() ? "(nothing)" : JoinActions(action.Defaults).c_str());
			}
			ImGui::TreePop();
		}
		ImGui::EndChild();
	}
}
