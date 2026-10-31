#include "EditorApp.h"
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
	}

	void EditorApp::DrawAnimBindingContext(AnimBinding& binding, const BindableAction& action)
	{
		const bool usesContext = action.Key.rfind("ability:", 0) == 0 || action.Key.rfind("activity:", 0) == 0;
		if (usesContext && !binding.When.empty())
		{
			ImGui::SeparatorText("When (all must hold)");
			static const char* const keys[] = { "node", "kind", "tool", "variant", "target", "ability" };
			std::vector<std::string> nodes;
			for (const auto& [id, definition] : m_NodeDefs) nodes.push_back(id);
			std::string drop;
			for (auto it = binding.When.begin(); it != binding.When.end(); ++it)
			{
				ImGui::PushID(it->first.c_str());
				ImGui::SetNextItemWidth(90.0f);
				ImGui::TextUnformatted(it->first.c_str());
				ImGui::SameLine(100.0f);
				std::vector<std::string> options;
				if (it->first == "node") options = nodes;
				else if (it->first == "kind") options = { "wood", "stone", "plant", "loose" };
				else if (it->first == "tool") options = { "axe", "pickaxe", "hoe", "none" };
				else if (it->first == "variant") options = { "basic", "flint" };
				if (!options.empty())
					StringCombo("##value", it->second, options, 160.0f);
				else
				{
					char value[64];
					std::snprintf(value, sizeof value, "%s", it->second.c_str());
					ImGui::SetNextItemWidth(160.0f);
					if (ImGui::InputText("##value", value, sizeof value)) it->second = value;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("x")) drop = it->first;
				ImGui::PopID();
			}
			if (!drop.empty()) binding.When.erase(drop);
			for (const char* key : keys)
				if (!binding.When.count(key))
				{
					ImGui::SameLine();
					if (ImGui::SmallButton((std::string("+ ") + key).c_str()))
						binding.When[key] = key == std::string("node") && !nodes.empty() ? nodes.front()
							: key == std::string("tool") ? "axe"
							: key == std::string("kind") ? "wood"
							: key == std::string("variant") ? "basic" : "";
				}
			if (binding.When.empty()) ImGui::TextDisabled("(no conditions: this variant always matches)");
		}
		if (action.Key.rfind("activity:", 0) == 0)
		{
			std::vector<std::string> sets = { "", "default" };
			for (const ManifestSet& set : m_Player.BaseManifest().Sets) sets.push_back(set.Name);
			StringCombo("locomotion set while it lasts", binding.Set, sets, 150.0f);
			bool repeat = binding.Repeat.value_or(false);
			ImGui::SameLine();
			if (ImGui::Checkbox("repeat while the job lasts", &repeat)) binding.Repeat = repeat;
		}
	}
}
