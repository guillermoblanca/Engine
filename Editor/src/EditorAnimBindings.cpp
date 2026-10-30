// The "Animation Bindings" window and its commands (bindings_*): which clip each game action plays, and how (loop, trim, speed, blends,
// transitions), with a preview on the controlled character. All the data and logic are GB::AnimBindings (Engine/src/GB/Animation/AnimBindings.*:
// resolution, validation, JSON, application to the manifest) held by PlayerMode; this file only draws them and forwards the buttons.
// See docs/EDITOR_ANIMACIONES.md.
#include "EditorApp.h"
#include "GB/Animation/AnimBindings.h"
#include "GB/Animation/BlendSpace.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <set>
#include <sstream>

namespace GB::Editor
{
	namespace
	{
		constexpr const char* kPreview = "bind:preview";
		constexpr const char* kTestA = "bind:test_from";
		constexpr const char* kTestB = "bind:test_to";

		AnimBinding* FindBinding(AnimBindings& b, const std::string& action, const std::string& skeleton)
		{
			for (AnimBinding& x : b.Bindings)
				if (x.Action == action && x.Skeleton == skeleton && x.When.empty()) return &x;
			return nullptr;
		}

		std::string Join(const std::vector<std::string>& v)
		{
			std::string s;
			for (const std::string& e : v) s += (s.empty() ? "" : ", ") + e;
			return s;
		}

		// A combo over strings; returns true when the choice changed `value`.
		bool StringCombo(const char* label, std::string& value, const std::vector<std::string>& options, float width = 200.0f)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str()))
			{
				for (const std::string& o : options)
					if (ImGui::Selectable(o.c_str(), o == value)) { value = o; changed = true; }
				ImGui::EndCombo();
			}
			return changed;
		}

		// "[ ] label [value]": an override that is either off (the base value shows greyed out) or set.
		bool OptionalSlider(const char* label, std::optional<float>& v, float base, float lo, float hi, const char* format)
		{
			bool changed = false;
			ImGui::PushID(label);
			bool on = v.has_value();
			if (ImGui::Checkbox("##set", &on)) { if (on) v = base; else v.reset(); changed = true; }
			ImGui::SameLine();
			float shown = v.value_or(base);
			ImGui::BeginDisabled(!on);
			ImGui::SetNextItemWidth(170.0f);
			if (ImGui::SliderFloat(label, &shown, lo, hi, format)) { v = shown; changed = true; }
			ImGui::EndDisabled();
			ImGui::PopID();
			return changed;
		}

		bool OptionalCheck(const char* label, std::optional<bool>& v, bool base)
		{
			bool changed = false;
			ImGui::PushID(label);
			bool on = v.has_value();
			if (ImGui::Checkbox("##set", &on)) { if (on) v = base; else v.reset(); changed = true; }
			ImGui::SameLine();
			bool shown = v.value_or(base);
			ImGui::BeginDisabled(!on);
			if (ImGui::Checkbox(label, &shown)) { v = shown; changed = true; }
			ImGui::EndDisabled();
			ImGui::PopID();
			return changed;
		}
	}

	void EditorApp::DrawAnimBindingsPanel()
	{
		AnimBindingsUi& ui = m_AnimBindingsUi;
		if (ui.Focus > 0) { ImGui::SetNextWindowFocus(); --ui.Focus; }
		ImGui::SetNextWindowSize(ImVec2(760.0f, 640.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 780.0f, 60.0f), ImGuiCond_FirstUseEver);   // (to the right: the character in the Viewport stays in sight)
		if (!ImGui::Begin("Animation Bindings", &m_Panels.AnimBindings)) { ImGui::End(); return; }

		std::string report;
		if (!ui.Loaded) { m_Player.LoadBindings(m_AssetRoot, report); ui.Loaded = true; ui.Status = report; }
		AnimBindings& bindings = m_Player.Bindings();
		const AnimManifest& manifest = m_Player.BaseManifest();
		const BindingDefaults defaults = m_Player.Defaults();
		AnimInstance* anim = m_Player.Anim();
		std::vector<std::string> clipNames;
		if (const ModelInstance* inst = m_Player.Instance())
			for (const AnimationClip& c : inst->GetModel().GetClips()) clipNames.push_back(c.Name);
		const std::set<std::string> clipSet(clipNames.begin(), clipNames.end());
		const ClipExists hasClip = clipNames.empty() ? ClipExists() : ClipExists([&clipSet](const std::string& c) { return clipSet.count(c) > 0; });
		const std::string skeleton = m_Player.SkeletonOfControlled();
		const std::string scope = ui.ThisSkeletonOnly ? skeleton : std::string();
		const std::string file = m_Player.BindingsFile(m_AssetRoot);

		// top bar
		if (ImGui::Button("Save")) { std::string why; ui.Status = SaveAnimBindingsFile(file, bindings, &why) ? "saved " + file : "could not save: " + why; }
		ImGui::SameLine();
		if (ImGui::Button("Load")) { m_Player.LoadBindings(m_AssetRoot, report); ui.Status = report; ui.Selected.clear(); }
		ImGui::SameLine();
		if (ImGui::Button("Apply to all"))
		{
			std::string why;
			m_Player.SetOnlySkeleton(ui.ApplyOnlyThisModel ? skeleton : std::string());
			ui.Status = m_Player.ApplyBindingsToAll(why) ? "applied: " + why : "not applied: " + why;
			ui.AppliedSignature = AnimBindingsToJson(bindings) + m_Player.OnlySkeleton();
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every character of the world (the player's, the AI's, the other clans') plays these bindings, and the ones made later too.\nSave writes %s so the game reads it.", file.c_str());
		ImGui::SameLine();
		ImGui::Checkbox("live", &ui.Live);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply to all at once when an edit is finished");
		ImGui::SameLine();
		ImGui::Checkbox("only this model", &ui.ApplyOnlyThisModel);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Apply only to the characters of the controlled one's model (the others play their defaults)");
		ImGui::SameLine();
		ImGui::Checkbox("new bindings only for this model", &ui.ThisSkeletonOnly);
		if (skeleton.empty())
		{
			// nothing to preview on yet: one click makes a world with nodes and the X Bot (the same as `generate temperate 5`, `resources world`, `player "packs/full/X Bot.fbx"`)
			if (ImGui::Button("Start a character"))
			{
				m_UiCommands.push_back("generate temperate 5");
				m_UiCommands.push_back("resources world");
				m_UiCommands.push_back("player \"packs/full/X Bot.fbx\"");
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Makes a world with resource nodes and puts the X Bot in it, so the clips and the tests on a node have a character to play on.\nThe preview plays on the character in the Viewport: move this window aside to see it.");
			ImGui::SameLine();
		}
		ImGui::TextDisabled("(%s)", skeleton.empty() ? "no character yet" : skeleton.c_str());
		if (!ui.Status.empty()) ImGui::TextWrapped("%s", ui.Status.c_str());

		// live: an edit that is finished (no slider being dragged) reaches every character
		{
			const std::string only = ui.ApplyOnlyThisModel ? skeleton : std::string();
			const std::string signature = AnimBindingsToJson(bindings) + only;
			if (ui.AppliedSignature.empty()) ui.AppliedSignature = signature;   // (what was loaded is what the characters already have)
			else if (ui.Live && signature != ui.AppliedSignature && !ImGui::IsAnyItemActive() && m_Player.ActorCount() > 0)
			{
				std::string why;
				m_Player.SetOnlySkeleton(only);
				ui.Status = m_Player.ApplyBindingsToAll(why) ? "applied: " + why : "not applied: " + why;
				ui.AppliedSignature = signature;
			}
		}

		// the validation, always visible; sent to the log when it changes
		const std::vector<BindingIssue> issues = ValidateBindings(bindings, manifest, defaults, hasClip);
		{
			const std::vector<std::string> lines = DescribeIssues(issues);
			if (lines != ui.LoggedIssues)
			{
				for (const std::string& l : lines) GB_CLIENT_WARN("Animation bindings: {}", l);
				ui.LoggedIssues = lines;
			}
		}

		const float listWidth = 270.0f;
		ImGui::BeginChild("##actions", ImVec2(listWidth, -150.0f), true);
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##filter", "filter actions", ui.Filter, sizeof ui.Filter);
		const std::vector<BindableAction> actions = ListBindableActions(manifest, defaults);
		std::vector<std::string> categories;
		for (const BindableAction& a : actions) if (std::find(categories.begin(), categories.end(), a.Category) == categories.end()) categories.push_back(a.Category);
		for (const char* first : { "Locomotion", "Interaction", "Attack", "Defence", "Life", "Work", "Social", "Other" })
		{
			if (std::find(categories.begin(), categories.end(), first) == categories.end()) continue;
			if (!ImGui::TreeNodeEx(first, ImGuiTreeNodeFlags_DefaultOpen)) continue;
			for (const BindableAction& a : actions)
			{
				if (a.Category != first) continue;
				if (ui.Filter[0] && a.Key.find(ui.Filter) == std::string::npos) continue;
				bool bound = false;
				for (const AnimBinding& b : bindings.Bindings) bound |= b.Action == a.Key;
				const std::string label = (bound ? "* " : "  ") + a.Key.substr(a.Key.find(':') == std::string::npos ? 0 : a.Key.find(':') + 1) + "##" + a.Key;
				if (ImGui::Selectable(label.c_str(), ui.Selected == a.Key)) { ui.Selected = a.Key; ui.Clip = 0; }
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nplays by default: %s", a.Key.c_str(), a.Defaults.empty() ? "(nothing)" : Join(a.Defaults).c_str());
			}
			ImGui::TreePop();
		}
		ImGui::EndChild();
		ImGui::SameLine();

		ImGui::BeginChild("##editor", ImVec2(0.0f, -150.0f), true);
		const BindableAction* action = nullptr;
		for (const BindableAction& a : actions) if (a.Key == ui.Selected) action = &a;
		if (!action) ImGui::TextWrapped("Pick an action on the left. A * marks the ones with a binding; the rest play their default (the manifest, or the C++ list of the combat rules).");
		else
		{
			const bool isLoco = action->Key.rfind("loco:", 0) == 0, isAir = action->Key == "air";
			ImGui::Text("%s  [%s]", action->Key.c_str(), action->Category.c_str());
			ImGui::TextDisabled("default: %s", action->Defaults.empty() ? "(nothing)" : Join(action->Defaults).c_str());
			const bool usesContext = action->Key.rfind("ability:", 0) == 0 || action->Key.rfind("activity:", 0) == 0;
			std::vector<AnimBinding*> variants;
			for (AnimBinding& x : bindings.Bindings) if (x.Action == action->Key && x.Skeleton == scope) variants.push_back(&x);
			if (!variants.empty()) ui.Variant = std::clamp(ui.Variant, 0, static_cast<int>(variants.size()) - 1);
			AnimBinding* binding = variants.empty() ? nullptr : variants[static_cast<size_t>(ui.Variant)];
			if (usesContext && !variants.empty())
			{
				ImGui::TextUnformatted("variant:");
				for (size_t v = 0; v < variants.size(); ++v)
				{
					ImGui::SameLine();
					std::string label;
					for (const auto& [k, val] : variants[v]->When) label += (label.empty() ? "" : ", ") + k + "=" + val;
					if (label.empty()) label = "always";
					ImGui::PushID(static_cast<int>(v));
					if (ImGui::RadioButton(label.c_str(), ui.Variant == static_cast<int>(v))) { ui.Variant = static_cast<int>(v); ui.Clip = 0; binding = variants[v]; }
					ImGui::PopID();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("+ context"))
				{
					AnimBinding nb = *binding;
					nb.When.clear();
					nb.When["node"] = m_NodeDefs.empty() ? std::string("tree") : m_NodeDefs.begin()->first;
					for (const auto& [id, d] : m_NodeDefs) if (d.Ability == action->Key.substr(8)) { nb.When["node"] = id; break; }
					bindings.Bindings.push_back(std::move(nb));
					ui.Variant = static_cast<int>(variants.size());
					binding = &bindings.Bindings.back();
				}
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Another clip for when the action happens in a context: on a given node, with a given tool...\nThe most specific variant that matches wins; 'always' is the fallback.");
			}
			if (!binding)
			{
				if (ImGui::Button("Bind this action (start from the default)"))
				{
					AnimBinding nb;
					nb.Action = action->Key;
					nb.Skeleton = scope;
					if (isLoco) for (const std::string& c : action->Defaults) { BindingClip bc; bc.Source = c; bc.Replaces = c; nb.Clips.push_back(bc); }
					else if (isAir) { BindingClip bc; bc.Source = manifest.AirClip; nb.Clips.push_back(bc); }
					else for (const std::string& c : action->Defaults) { BindingClip bc; bc.Source = c; nb.Clips.push_back(bc); }
					if (nb.Clips.empty()) nb.Clips.push_back({});
					bindings.Bindings.push_back(std::move(nb));
					binding = &bindings.Bindings.back();
					ui.Variant = 0;
				}
				ImGui::TextDisabled("%s", scope.empty() ? "for every model" : ("for the skeleton '" + scope + "'").c_str());
			}
			if (binding)
			{
				if (ImGui::Button("Remove binding (back to the default)"))
				{
					bindings.Bindings.erase(bindings.Bindings.begin() + (binding - bindings.Bindings.data()));
					binding = nullptr;
					ui.Clip = 0;
					ui.Variant = 0;
				}
			}
			if (binding)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", binding->Skeleton.empty() ? "every model" : ("skeleton " + binding->Skeleton).c_str());
				if (usesContext && !binding->When.empty())
				{
					ImGui::SeparatorText("When (all must hold)");
					static const char* const keys[] = { "node", "kind", "tool", "variant", "target", "ability" };
					std::vector<std::string> nodes;
					for (const auto& [id, d] : m_NodeDefs) nodes.push_back(id);
					std::string drop;
					for (auto it = binding->When.begin(); it != binding->When.end(); ++it)
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
						if (!options.empty()) StringCombo("##value", it->second, options, 160.0f);
						else { char buf[64]; std::snprintf(buf, sizeof buf, "%s", it->second.c_str()); ImGui::SetNextItemWidth(160.0f); if (ImGui::InputText("##value", buf, sizeof buf)) it->second = buf; }
						ImGui::SameLine();
						if (ImGui::SmallButton("x")) drop = it->first;
						ImGui::PopID();
					}
					if (!drop.empty()) binding->When.erase(drop);
					for (const char* k : keys)
						if (!binding->When.count(k))
						{
							ImGui::SameLine();
							if (ImGui::SmallButton((std::string("+ ") + k).c_str())) binding->When[k] = k == std::string("node") && !nodes.empty() ? nodes.front() : k == std::string("tool") ? "axe" : k == std::string("kind") ? "wood" : k == std::string("variant") ? "basic" : "";
						}
					if (binding->When.empty()) ImGui::TextDisabled("(no conditions: this variant always matches)");
				}
				if (action->Key.rfind("activity:", 0) == 0)
				{
					std::vector<std::string> sets = { "" , "default" };
					for (const ManifestSet& s : manifest.Sets) sets.push_back(s.Name);
					StringCombo("locomotion set while it lasts", binding->Set, sets, 150.0f);
					bool repeat = binding->Repeat.value_or(false);
					ImGui::SameLine();
					if (ImGui::Checkbox("repeat while the job lasts", &repeat)) binding->Repeat = repeat;
				}
				std::vector<std::string> sources;
				for (const MontageDesc& m : manifest.Montages) sources.push_back(m.Name);
				for (const std::string& c : clipNames) sources.push_back(c);
				ImGui::SeparatorText(isLoco ? "Clips that replace the set's" : "Clips (one per use, in turn)");
				for (size_t i = 0; i < binding->Clips.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::RadioButton("##sel", ui.Clip == static_cast<int>(i))) ui.Clip = static_cast<int>(i);
					ImGui::SameLine();
					StringCombo("source", binding->Clips[i].Source, sources, 220.0f);
					if (isLoco)
					{
						ImGui::SameLine();
						std::string current = binding->Clips[i].Replaces;
						const ManifestSet* set = action->Key == "loco:default" ? &manifest.Free : nullptr;
						for (const ManifestSet& s : manifest.Sets) if ("loco:" + s.Name == action->Key) set = &s;
						if (set && StringCombo("replaces", current, set->Clips, 200.0f)) binding->Clips[i].Replaces = current;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("x") && binding->Clips.size() > 1) { binding->Clips.erase(binding->Clips.begin() + static_cast<std::ptrdiff_t>(i)); ui.Clip = 0; ImGui::PopID(); break; }
					ImGui::PopID();
				}
				if (ImGui::SmallButton("+ clip")) { binding->Clips.push_back({}); ui.Clip = static_cast<int>(binding->Clips.size()) - 1; }

				if (!isLoco && !isAir && !binding->Clips.empty())
				{
					ui.Clip = std::clamp(ui.Clip, 0, static_cast<int>(binding->Clips.size()) - 1);
					BindingClip& c = binding->Clips[static_cast<size_t>(ui.Clip)];
					// the values without this clip's overrides: what "not set" means
					AnimBinding plain;
					plain.Action = binding->Action;
					BindingClip bare;
					bare.Source = c.Source;
					plain.Clips.push_back(bare);
					AnimBindings tmp;
					tmp.Bindings.push_back(plain);
					const ResolvedClip base = ResolveClip(tmp, manifest, defaults, plain.Action, {}, "", 0, hasClip);
					ImGui::SeparatorText("Settings of the selected clip (tick to override the montage's own)");
					OptionalCheck("loop", c.Loop, base.Loop);
					OptionalSlider("trim start", c.TrimStart, base.TrimStart, 0.0f, 1.0f, "%.2f of the clip");
					OptionalSlider("trim end", c.TrimEnd, base.TrimEnd, 0.0f, 1.0f, "%.2f of the clip");
					OptionalSlider("speed", c.Speed, base.Speed, 0.1f, 4.0f, "x%.2f");
					OptionalSlider("blend in", c.BlendIn, base.BlendIn, 0.0f, 2.0f, "%.2f s");
					OptionalSlider("blend out", c.BlendOut, base.BlendOut, 0.0f, 2.0f, "%.2f s");
					{
						ImGui::PushID("curve");
						bool on = c.Curve.has_value();
						if (ImGui::Checkbox("##set", &on)) { if (on) c.Curve = base.Curve; else c.Curve.reset(); }
						ImGui::SameLine();
						ImGui::BeginDisabled(!on);
						std::string name = BlendCurveName(c.Curve.value_or(base.Curve));
						if (StringCombo("blend curve", name, { "smooth", "linear", "easeIn", "easeOut" }, 120.0f)) c.Curve = BlendCurveFromName(name);
						ImGui::EndDisabled();
						ImGui::PopID();
					}
					OptionalCheck("keep root motion / feet (do not pin the clip's travel)", c.KeepRootMotion, base.KeepRootMotion);

					// the real thing: the character uses the ability on a node of the world (its props, its tool, the context of the bindings)
					if (action->Key.rfind("ability:", 0) == 0 && !m_NodeDefs.empty())
					{
						ImGui::SeparatorText("Try on a node");
						std::vector<std::string> nodes;
						for (const auto& [id, d] : m_NodeDefs) if (!d.Decor && !d.Ability.empty()) nodes.push_back(id);
						if (ui.TryNode.empty() || std::find(nodes.begin(), nodes.end(), ui.TryNode) == nodes.end())
						{
							ui.TryNode = nodes.empty() ? std::string() : nodes.front();
							for (const std::string& n : nodes) if (m_NodeDefs.at(n).Ability == action->Key.substr(8)) { ui.TryNode = n; break; }
						}
						StringCombo("node type", ui.TryNode, nodes, 180.0f);
						ImGui::SameLine();
						if (ImGui::Button("Try on a node") && !ui.TryNode.empty())
						{
							std::string why;
							ui.Status = TryOnNode(ui.TryNode, why) ? why : "cannot try: " + why;
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts the character in front of the nearest node of that type (with the tool it asks for) and makes it work it:\nthe clip that plays is the one the bindings pick for that node.");
					}

					// the preview on the controlled character
					ImGui::SeparatorText("Preview on the character");
					if (!anim) ImGui::TextDisabled("Start a character (player <fbx>) to preview.");
					else
					{
						MontagePlayer& mp = anim->Montages();
						AnimBindings one;
						AnimBinding solo = *binding;
						solo.Skeleton.clear();
						solo.When.clear();
						one.Bindings.push_back(solo);
						const ResolvedClip r = ResolveClip(one, manifest, defaults, binding->Action, {}, "", ui.Clip, hasClip);
						if (!r.Found || !r.FromBinding) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "cannot preview: %s", r.Why.c_str());
						else
						{
							const std::string signature = AnimBindingsToJson(one) + std::to_string(ui.Clip);
							if (signature != ui.PreviewSignature || !mp.Has(kPreview))
							{
								MontageDesc d = ToMontage(r, manifest);
								d.Name = kPreview;
								mp.Add(d);
								ui.PreviewSignature = signature;
							}
							const float whole = mp.ClipDuration(kPreview);
							const bool playing = mp.IsPlaying(kPreview);
							if (ImGui::Button(playing ? "Restart" : "Play")) { mp.SetPaused(false); m_Player.PlayMontage(kPreview); }
							ImGui::SameLine();
							if (ImGui::Button(mp.Paused() ? "Resume" : "Pause")) mp.SetPaused(!mp.Paused());
							ImGui::SameLine();
							if (ImGui::Button("Stop")) { mp.SetPaused(false); m_Player.StopMontage(); }
							ImGui::SameLine();
							if (ImGui::Button("Play default instead") && !action->Defaults.empty()) { mp.SetPaused(false); m_Player.PlayMontage(action->Defaults[static_cast<size_t>(ui.Clip) % action->Defaults.size()]); }
							float t = std::max(mp.Time(kPreview), 0.0f);
							ImGui::SetNextItemWidth(-130.0f);
							if (ImGui::SliderFloat("##scrub", &t, 0.0f, whole, "%.2f s"))
							{
								if (!mp.IsPlaying(kPreview)) m_Player.PlayMontage(kPreview);
								mp.SetPaused(true);
								mp.Seek(kPreview, t);
							}
							ImGui::SameLine();
							ImGui::Text("clip %.2f s, plays %.2f s", static_cast<double>(whole), static_cast<double>(mp.PlayDuration(kPreview)));
						}
					}
				}
			}
		}
		// the tuning of the locomotion blend space: it is not a per-action binding but the speed axis every character walks on
		if (action && action->Key.rfind("loco:", 0) == 0)
		{
			ImGui::SeparatorText("Locomotion blend space (all the characters)");
			LocomotionTuning* tuning = nullptr;
			for (LocomotionTuning& t : bindings.Locomotion) if (t.Skeleton == scope) tuning = &t;
			LocomotionTuning scratch;
			scratch.Skeleton = scope;
			LocomotionTuning& t = tuning ? *tuning : scratch;
			const LocomotionOptions engine;   // what the engine does without tuning
			bool edited = false;
			edited |= OptionalSlider("slowest speed factor", t.Values.MinSpeedScale, engine.MinSpeedScale, 0.1f, 1.0f, "x%.2f");
			edited |= OptionalSlider("fastest speed factor", t.Values.MaxSpeedScale, engine.MaxSpeedScale, 1.0f, 3.0f, "x%.2f");
			edited |= OptionalSlider("blend time (velocity smoothing)", t.Values.VelocitySmoothing, engine.VelocitySmoothing, 0.0f, 1.0f, "%.2f s");
			edited |= OptionalSlider("gait share for footsteps", t.Values.MinGaitWeight, engine.MinGaitWeight, 0.0f, 1.0f, "%.2f");
			edited |= OptionalSlider("fade between sets", t.Values.SetFade, 0.3f, 0.0f, 2.0f, "%.2f s");
			const std::string setName = action->Key.substr(5) == "default" ? std::string() : action->Key.substr(5);
			const ManifestSet* set = setName.empty() ? &manifest.Free : nullptr;
			for (const ManifestSet& s : manifest.Sets) if (s.Name == setName) set = &s;
			const ModelInstance* inst = m_Player.Instance();
			if (set && inst)
			{
				ImGui::TextDisabled("Reference speed of each clip: where it sits on the speed axis (idle < walk < run) and the ground speed it plays at its natural rate");
				for (const std::string& clip : set->Clips)
				{
					const int index = inst->GetModel().FindClip(clip);
					const float natural = index >= 0 ? glm::length(MakeGaitInfo(inst->GetModel(), index).Velocity) : 0.0f;
					ImGui::PushID(clip.c_str());
					if (natural < 0.05f) { ImGui::TextDisabled("%s: no travel (idle / turn)", clip.c_str()); ImGui::PopID(); continue; }
					ManifestClipSpeed* entry = nullptr;
					for (ManifestClipSpeed& c : t.Values.Clips) if (c.Set == setName && c.Clip == clip) entry = &c;
					std::optional<float> v;
					if (entry) v = entry->Speed;
					if (OptionalSlider(clip.c_str(), v, natural, 0.3f, 10.0f, "%.2f m/s"))
					{
						edited = true;
						if (v && entry) entry->Speed = *v;
						else if (v) t.Values.Clips.push_back({ setName, clip, *v });
						else t.Values.Clips.erase(std::remove_if(t.Values.Clips.begin(), t.Values.Clips.end(), [&](const ManifestClipSpeed& c) { return c.Set == setName && c.Clip == clip; }), t.Values.Clips.end());
					}
					ImGui::PopID();
				}
			}
			if (edited && !tuning) bindings.Locomotion.push_back(scratch);
			if (tuning && tuning->Values.Empty()) bindings.Locomotion.erase(bindings.Locomotion.begin() + (tuning - bindings.Locomotion.data()));
		}
		ImGui::EndChild();

		// transitions
		ImGui::BeginChild("##transitions", ImVec2(0.0f, 0.0f), true);
		ImGui::Text("Transitions (the blend when one montage gives way to another in a slot)");
		std::vector<std::string> ends = { "*" };
		for (const BindableAction& a : actions) if (a.Key.rfind("loco:", 0) != 0 && a.Key != "air") ends.push_back(a.Key);
		for (size_t i = 0; i < bindings.Transitions.size(); ++i)
		{
			BindingTransition& t = bindings.Transitions[i];
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::RadioButton("##sel", ui.Transition == static_cast<int>(i))) ui.Transition = static_cast<int>(i);
			ImGui::SameLine();
			StringCombo("from", t.From, ends, 190.0f);
			ImGui::SameLine();
			StringCombo("to", t.To, ends, 190.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90.0f);
			ImGui::SliderFloat("s", &t.Duration, 0.0f, 1.5f, "%.2f");
			ImGui::SameLine();
			std::string curve = BlendCurveName(t.Curve);
			if (StringCombo("curve", curve, { "smooth", "linear", "easeIn", "easeOut" }, 90.0f)) t.Curve = BlendCurveFromName(curve);
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) { bindings.Transitions.erase(bindings.Transitions.begin() + static_cast<std::ptrdiff_t>(i)); ui.Transition = -1; ImGui::PopID(); break; }
			ImGui::PopID();
		}
		if (ImGui::SmallButton("+ transition")) { BindingTransition t; t.From = "*"; t.To = "*"; bindings.Transitions.push_back(t); ui.Transition = static_cast<int>(bindings.Transitions.size()) - 1; }
		ImGui::SameLine();
		const bool canTest = anim && ui.Transition >= 0 && ui.Transition < static_cast<int>(bindings.Transitions.size());
		ImGui::BeginDisabled(!canTest);
		if (ImGui::Button("Test selected A -> B") && canTest)
		{
			// the first montage each end plays (an action: the first of its binding or default), registered under test names so the real ones are untouched
			const BindingTransition& t = bindings.Transitions[static_cast<size_t>(ui.Transition)];
			MontagePlayer& mp = anim->Montages();
			auto montageFor = [&](const std::string& end, const char* name) -> bool
			{
				ResolvedClip r;
				if (end == "*") { const std::vector<std::string> n = mp.Names(); for (const std::string& m : n) if (const MontageDesc* d = mp.Get(m); d && d->Name.rfind("bind:", 0) != 0 && !d->Loop) { r = FromMontage(*d); break; } }
				else r = ResolveClip(bindings, manifest, defaults, end, {}, skeleton, 0, hasClip);
				if (!r.Found) return false;
				MontageDesc d = ToMontage(r, manifest);
				d.Name = name;
				return mp.Add(d);
			};
			if (montageFor(t.From, kTestA) && montageFor(t.To, kTestB))
			{
				std::vector<MontageTransition> list = mp.Transitions();
				list.push_back({ kTestA, kTestB, t.Duration, t.Curve });
				mp.SetTransitions(list);
				mp.SetPaused(false);
				m_Player.PlayMontage(kTestA);
				ui.TestStage = 1;
				ui.TestTimer = 0.0f;
				ui.Status = "testing " + t.From + " -> " + t.To;
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

		// the validation
		ImGui::Separator();
		int errors = 0, warnings = 0;
		for (const BindingIssue& i : issues) (i.Severity == BindingIssue::Level::Error ? errors : warnings)++;
		if (issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Validation: no problems");
		else ImGui::TextColored(errors ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Validation: %d error(s), %d warning(s)", errors, warnings);
		for (const BindingIssue& i : issues)
			ImGui::TextColored(i.Severity == BindingIssue::Level::Error ? ImVec4(1.0f, 0.5f, 0.45f, 1.0f) : ImVec4(1.0f, 0.82f, 0.4f, 1.0f), "%s", i.Text.c_str());
		ImGui::EndChild();
		ImGui::End();
	}

	bool EditorApp::TryOnNode(const std::string& nodeId, std::string& report)
	{
		Actor* actor = m_Player.Controlled();
		if (!actor) { report = "no controlled character (player <fbx>)"; return false; }
		const auto def = m_NodeDefs.find(nodeId);
		if (def == m_NodeDefs.end()) { report = "no resource node type '" + nodeId + "'"; return false; }
		if (def->second.Ability.empty()) { report = "'" + nodeId + "' is not worked by any ability"; return false; }
		EntityWorld& world = m_SceneEditor.World();
		std::string best;
		glm::vec3 where(0.0f);
		float bestDistance = 1e30f;
		const glm::vec3 me = actor->GetPosition();
		for (const std::string& name : m_Gameplay.Names())
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(name);
			if (!asc || !asc->HasTag("Resource.Node." + nodeId) || asc->HasTag("State.Depleted")) continue;
			const EntityId id = world.FindByName(name);
			if (id == NullEntity) continue;
			const glm::vec3 p = glm::vec3(world.WorldMatrix(id)[3]);
			const float d = (p.x - me.x) * (p.x - me.x) + (p.z - me.z) * (p.z - me.z);
			if (d < bestDistance) { bestDistance = d; best = name; where = p; }
		}
		// the character needs an ability system to work a node (a player made with `player` has none until now)
		if (!m_Gameplay.Find(m_Player.ControlledName()))
		{
			if (world.FindByName(m_Player.ControlledName()) == NullEntity) world.Find(world.Create(m_Player.ControlledName()))->Gameplay.emplace();
			m_Gameplay.Create(m_Player.ControlledName());
			actor->SetAbilities(&m_Gameplay, m_Player.ControlledName());
		}
		if (best.empty()) { report = "there is no '" + nodeId + "' node in the world (generate one with resources: generate temperate 5)"; return false; }
		// the tool the node asks for, in the bag (the abilities read it as a tag of the character)
		if (!def->second.RequiresTag.empty())
			for (const std::string& item : m_Items.Ids())
			{
				const ItemDef* d = m_Items.Find(item);
				bool matches = false;
				if (d) for (const std::string& tag : d->Tags) matches = matches || GameplayTag(def->second.RequiresTag).Matches(tag);
				if (!matches) continue;
				if (actor->Bag().Count(item) == 0) actor->Bag().Add(m_Items, item, 1);
				break;
			}
		if (AbilitySystemComponent* asc = m_Gameplay.Find(m_Player.ControlledName())) ResourceSystem::SyncTools(*asc, actor->Bag(), m_Items);
		// in front of the node, looking at it
		glm::vec2 away(me.x - where.x, me.z - where.z);
		away = glm::length(away) > 1e-3f ? glm::normalize(away) : glm::vec2(0.0f, 1.0f);
		const glm::vec2 stand = glm::vec2(where.x, where.z) + away * 2.2f;
		actor->Teleport(stand.x, stand.y, std::atan2(-away.x, -away.y));
		std::string error;
		if (!m_Player.UseAbility(def->second.Ability, best, error)) { report = def->second.Ability + " on '" + best + "' did not start: " + error; return false; }
		report = "the character uses " + def->second.Ability + " on '" + best + "'";
		return true;
	}

	// ---- commands (the window's buttons, for scripts and agents)

	void EditorApp::RegisterAnimBindingCommands()
	{
		CommandRunner& c = m_Commands;
		auto ensure = [this]() { if (!m_AnimBindingsUi.Loaded) { std::string r; m_Player.LoadBindings(m_AssetRoot, r); m_AnimBindingsUi.Loaded = true; } };
		auto clipExists = [this]() -> ClipExists
		{
			const ModelInstance* inst = m_Player.Instance();
			if (!inst) return nullptr;
			const Model* model = &inst->GetModel();
			return [model](const std::string& n) { return model->FindClip(n) >= 0; };
		};
		c.Register("bindings_validate", "bindings_validate: checks the animation bindings (clips that do not exist, bad trims, loops on single-shot actions, transitions with no way out) and lists the problems", [=, this](const std::vector<std::string>&) -> CommandResult
		{
			ensure();
			const std::vector<BindingIssue> issues = ValidateBindings(m_Player.Bindings(), m_Player.BaseManifest(), m_Player.Defaults(), clipExists());
			const std::vector<std::string> lines = DescribeIssues(issues);
			for (const std::string& l : lines) GB_CLIENT_WARN("Animation bindings: {}", l);
			bool bad = false;
			for (const BindingIssue& i : issues) bad |= i.Severity == BindingIssue::Level::Error;
			std::string text = std::to_string(m_Player.Bindings().Bindings.size()) + " binding(s), " + std::to_string(m_Player.Bindings().Transitions.size()) + " transition(s): " + (lines.empty() ? "no problems" : std::to_string(lines.size()) + " problem(s)");
			for (const std::string& l : lines) text += "\n  " + l;
			return { !bad, text };
		});
		c.Register("bindings_save", "bindings_save [file]: writes the animation bindings (default assets/data/animation_bindings.json)", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			ensure();
			const std::string path = a.empty() ? m_Player.BindingsFile(m_AssetRoot) : a[0];
			std::string why;
			if (!SaveAnimBindingsFile(path, m_Player.Bindings(), &why)) return { false, why };
			return { true, "saved " + path };
		});
		c.Register("bindings_load", "bindings_load [file]: reads the animation bindings (a missing file gives none: everything plays its default)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			std::string report;
			m_AnimBindingsUi.Loaded = true;
			const bool ok = a.empty() ? m_Player.LoadBindings(m_AssetRoot, report) : m_Player.LoadBindings(m_AssetRoot, report, a[0]);
			return { ok, report };
		});
		c.Register("bindings_apply", "bindings_apply [model]: every character of the world (and those made later) plays the current animation bindings; with `model`, only the controlled character's model", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			ensure();
			std::string why;
			m_Player.SetOnlySkeleton(!a.empty() && a[0] == "model" ? m_Player.SkeletonOfControlled() : std::string());
			const bool ok = m_Player.ApplyBindingsToAll(why);
			m_AnimBindingsUi.AppliedSignature = AnimBindingsToJson(m_Player.Bindings()) + m_Player.OnlySkeleton();
			return { ok, why };
		});
		c.Register("bindings_try", "bindings_try <node type> [ability]: the controlled character uses the ability that works the nearest node of that type (tree, rock, berry_bush...) in front of it", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: bindings_try <node type>" };
			ensure();
			std::string report;
			const bool ok = TryOnNode(a[0], report);
			return { ok, report };
		});
		c.Register("bindings_when", "bindings_when <action> <source> key=value[,key=value] [loop=0|1] [trim=a,b] [speed=x] ...: a binding for a context (node=tree, tool=axe, kind=wood, variant=flint, target=name)", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 3) return { false, "usage: bindings_when <action> <source> node=tree[,tool=axe] [loop=0|1] [trim=a,b] [speed=x] [blend=in,out]" };
			ensure();
			AnimBindings& all = m_Player.Bindings();
			const std::string scope = m_AnimBindingsUi.ThisSkeletonOnly ? m_Player.SkeletonOfControlled() : std::string();
			AnimBinding nb;
			nb.Action = a[0];
			nb.Skeleton = scope;
			std::stringstream conds(a[2]);
			std::string part;
			while (std::getline(conds, part, ','))
			{
				const size_t eq = part.find('=');
				if (eq == std::string::npos || eq == 0) return { false, "expected key=value in the context, got '" + part + "'" };
				nb.When[part.substr(0, eq)] = part.substr(eq + 1);
			}
			BindingClip clip;
			clip.Source = a[1];
			for (size_t i = 3; i < a.size(); ++i)
			{
				const size_t eq = a[i].find('=');
				if (eq == std::string::npos) return { false, "expected key=value, got '" + a[i] + "'" };
				const std::string k = a[i].substr(0, eq), v = a[i].substr(eq + 1);
				const size_t comma = v.find(',');
				const float v0 = static_cast<float>(std::atof(v.c_str())), v1 = comma == std::string::npos ? 0.0f : static_cast<float>(std::atof(v.c_str() + comma + 1));
				if (k == "loop") clip.Loop = v != "0";
				else if (k == "trim" && comma != std::string::npos) { clip.TrimStart = v0; clip.TrimEnd = v1; }
				else if (k == "speed") clip.Speed = v0;
				else if (k == "blend" && comma != std::string::npos) { clip.BlendIn = v0; clip.BlendOut = v1; }
				else return { false, "unknown or malformed setting '" + a[i] + "'" };
			}
			nb.Clips.push_back(clip);
			m_AnimBindingsUi.Selected = a[0];
			m_AnimBindingsUi.Clip = 0;
			for (AnimBinding& e : all.Bindings) if (e.Action == nb.Action && e.Skeleton == nb.Skeleton && e.When == nb.When) { e = nb; return { true, "binding for that context updated" }; }
			all.Bindings.push_back(nb);
			int variant = -1;
			for (const AnimBinding& e : all.Bindings) if (e.Action == nb.Action && e.Skeleton == nb.Skeleton) ++variant;
			m_AnimBindingsUi.Variant = std::max(variant, 0);
			return { true, a[0] + " when " + a[2] + " -> " + a[1] };
		});
		c.Register("bindings_loco", "bindings_loco [min=x] [max=x] [smooth=s] [gait=x] [fade=s] [clip=<name>:<m/s>] [set=<name>] [reset]: tunes the locomotion blend space (the limits of the speed factor, the blend time, the reference speed of a clip)", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			ensure();
			AnimBindings& all = m_Player.Bindings();
			const std::string scope = m_AnimBindingsUi.ThisSkeletonOnly ? m_Player.SkeletonOfControlled() : std::string();
			if (!a.empty() && a[0] == "reset")
			{
				all.Locomotion.erase(std::remove_if(all.Locomotion.begin(), all.Locomotion.end(), [&](const LocomotionTuning& t) { return t.Skeleton == scope; }), all.Locomotion.end());
				return { true, "locomotion tuning removed" };
			}
			LocomotionTuning* t = nullptr;
			for (LocomotionTuning& e : all.Locomotion) if (e.Skeleton == scope) t = &e;
			if (!t) { all.Locomotion.push_back({}); t = &all.Locomotion.back(); t->Skeleton = scope; }
			std::string set;
			for (const std::string& arg : a) if (arg.rfind("set=", 0) == 0) set = arg.substr(4);
			for (const std::string& arg : a)
			{
				const size_t eq = arg.find('=');
				if (eq == std::string::npos) return { false, "expected key=value, got '" + arg + "'" };
				const std::string k = arg.substr(0, eq), v = arg.substr(eq + 1);
				const float f = static_cast<float>(std::atof(v.c_str()));
				if (k == "min") t->Values.MinSpeedScale = f;
				else if (k == "max") t->Values.MaxSpeedScale = f;
				else if (k == "smooth") t->Values.VelocitySmoothing = f;
				else if (k == "gait") t->Values.MinGaitWeight = f;
				else if (k == "fade") t->Values.SetFade = f;
				else if (k == "set") continue;
				else if (k == "clip")
				{
					const size_t colon = v.rfind(':');
					if (colon == std::string::npos) return { false, "clip=<name>:<m/s>" };
					const std::string name = v.substr(0, colon);
					const float speed = static_cast<float>(std::atof(v.c_str() + colon + 1));
					bool done = false;
					for (ManifestClipSpeed& c : t->Values.Clips) if (c.Set == set && c.Clip == name) { c.Speed = speed; done = true; }
					if (!done) t->Values.Clips.push_back({ set, name, speed });
				}
				else return { false, "unknown setting '" + arg + "'" };
			}
			m_AnimBindingsUi.Selected = "loco:" + (set.empty() ? std::string("default") : set);
			return { true, "locomotion tuning updated" };
		});
		c.Register("bindings_resolve", "bindings_resolve <action> [turn]: what the action plays now (ability:Chop, activity:sleep, loco:default, air): clip, loop, trim, speed, blends", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: bindings_resolve <action> [turn]" };
			ensure();
			const ResolvedClip r = ResolveClip(m_Player.Bindings(), m_Player.BaseManifest(), m_Player.Defaults(), a[0], {}, m_Player.SkeletonOfControlled(), a.size() > 1 ? std::atoi(a[1].c_str()) : 0, clipExists());
			if (!r.Found) return { false, a[0] + ": " + r.Why };
			char buf[420];
			std::snprintf(buf, sizeof buf, "%s: %s (clip \"%s\") %s, trim %.2f..%.2f, speed x%.2f, blend %.2f / %.2f s %s%s [%s]", a[0].c_str(), r.Montage.c_str(), r.Clip.c_str(), r.Loop ? "loop" : "one-shot",
				static_cast<double>(r.TrimStart), static_cast<double>(r.TrimEnd), static_cast<double>(r.Speed), static_cast<double>(r.BlendIn), static_cast<double>(r.BlendOut), BlendCurveName(r.Curve),
				r.KeepRootMotion ? ", keeps root motion" : "", r.Why.c_str());
			return { true, buf };
		});
		c.Register("bindings_set", "bindings_set <action> <source> [loop=0|1] [trim=a,b] [speed=x] [blend=in,out] [curve=name] [root=0|1] [replaces=clip]: binds an action to a montage or clip (replaces the first clip of its binding)",
			[=, this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2) return { false, "usage: bindings_set <action> <source> [loop=0|1] [trim=a,b] [speed=x] [blend=in,out] [curve=name] [root=0|1] [replaces=clip]" };
			ensure();
			AnimBindings& all = m_Player.Bindings();
			const std::string scope = m_AnimBindingsUi.ThisSkeletonOnly ? m_Player.SkeletonOfControlled() : std::string();
			AnimBinding* b = FindBinding(all, a[0], scope);
			if (!b) { AnimBinding nb; nb.Action = a[0]; nb.Skeleton = scope; all.Bindings.push_back(nb); b = &all.Bindings.back(); }
			BindingClip clip;
			clip.Source = a[1];
			for (size_t i = 2; i < a.size(); ++i)
			{
				const size_t eq = a[i].find('=');
				if (eq == std::string::npos) return { false, "expected key=value, got '" + a[i] + "'" };
				const std::string k = a[i].substr(0, eq), v = a[i].substr(eq + 1);
				const size_t comma = v.find(',');
				const float v0 = static_cast<float>(std::atof(v.c_str())), v1 = comma == std::string::npos ? 0.0f : static_cast<float>(std::atof(v.c_str() + comma + 1));
				if (k == "loop") clip.Loop = v != "0";
				else if (k == "trim" && comma != std::string::npos) { clip.TrimStart = v0; clip.TrimEnd = v1; }
				else if (k == "speed") clip.Speed = v0;
				else if (k == "blend" && comma != std::string::npos) { clip.BlendIn = v0; clip.BlendOut = v1; }
				else if (k == "curve") clip.Curve = BlendCurveFromName(v);
				else if (k == "root") clip.KeepRootMotion = v != "0";
				else if (k == "replaces") clip.Replaces = v;
				else return { false, "unknown or malformed setting '" + a[i] + "'" };
			}
			if (b->Clips.empty()) b->Clips.push_back(clip); else b->Clips[0] = clip;
			m_AnimBindingsUi.Selected = a[0];
			m_AnimBindingsUi.Clip = 0;
			return { true, a[0] + " -> " + a[1] };
		});
		c.Register("bindings_transition", "bindings_transition <from> <to> <seconds> [curve]: adds or edits a transition (from / to: an action, a montage or *)", [=, this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 3) return { false, "usage: bindings_transition <from> <to> <seconds> [curve]" };
			ensure();
			AnimBindings& all = m_Player.Bindings();
			BindingTransition t;
			t.From = a[0]; t.To = a[1]; t.Duration = static_cast<float>(std::atof(a[2].c_str()));
			if (a.size() > 3) t.Curve = BlendCurveFromName(a[3]);
			for (BindingTransition& e : all.Transitions) if (e.From == t.From && e.To == t.To && e.Skeleton == t.Skeleton) { e = t; return { true, "transition updated" }; }
			all.Transitions.push_back(t);
			return { true, "transition added" };
		});
	}
}
