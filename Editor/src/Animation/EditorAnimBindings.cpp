// The "Animation Bindings" window and its commands (bindings_*): which clip each game action plays, and how (loop, trim, speed, blends,
// transitions), with a preview on the controlled character. All the data and logic are GB::AnimBindings (Engine/src/GB/Animation/AnimBindings.*:
// resolution, validation, JSON, application to the manifest) held by PlayerMode; this file only draws them and forwards the buttons.
// See docs/EDITOR_ANIMACIONES.md.
#include "EditorApp.h"
#include "Animation/EditorAnimPreview.h"
#include "GB/Animation/AnimBindings.h"
#include "GB/Animation/BlendSpace.h"
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
		if (ImGui::Button("Load"))
		{
			const bool ok = m_Player.LoadBindings(m_AssetRoot, report);
			ui.Status = report;
			if (ok)
			{
				ui.Selected.clear();
				ui.Transition = -1;
				ui.Clip = 0;
				ui.Variant = 0;
				ui.PreviewSignature.clear();
				ui.AppliedSignature.clear();
				ui.LoggedIssues.clear();
			}
		}
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
			// Nothing to preview on yet: one click creates the isolated interaction sandbox.
			if (ImGui::Button("Start isolated sandbox"))
			{
				m_UiCommands.push_back("new_scene");
				m_UiCommands.push_back("add plane 0 0 0");
				m_UiCommands.push_back("player \"packs/full/X Bot.fbx\"");
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replaces the current scene with an isolated flat interaction sandbox: plane, navigation and the X Bot. It does not generate a game world or resource patch.\nThe preview plays on the character in the Viewport: move this window aside to see it.");
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

		const std::vector<BindableAction> actions = ListBindableActions(manifest, defaults);
		DrawAnimBindingActionList(actions, bindings);
		ImGui::SameLine();

		ImGui::BeginChild("##editor", ImVec2(-285.0f, -150.0f), true);
		const BindableAction* action = nullptr;
		for (const BindableAction& a : actions) if (a.Key == ui.Selected) action = &a;
		if (!action) ImGui::TextWrapped("Pick an action on the left. A * marks the ones with a binding; the rest play their default (the manifest, or the C++ list of the combat rules).");
		else
		{
			ImGui::Text("%s  [%s]", action->Key.c_str(), action->Category.c_str());
			ImGui::TextDisabled("default: %s", action->Defaults.empty() ? "(nothing)" : Join(action->Defaults).c_str());
			const int bindingIndex = DrawAnimBindingLifecycle(*action, bindings, manifest, scope);
			if (bindingIndex >= 0 && bindingIndex < static_cast<int>(bindings.Bindings.size()))
			{
				AnimBinding& binding = bindings.Bindings[static_cast<size_t>(bindingIndex)];
				DrawAnimBindingContext(binding, *action);
				const bool isLoco = action->Key.rfind("loco:", 0) == 0, isAir = action->Key == "air";
				std::vector<std::string> sources;
				for (const MontageDesc& m : manifest.Montages) sources.push_back(m.Name);
				for (const std::string& c : clipNames) sources.push_back(c);
				ImGui::SeparatorText(isLoco ? "Clips that replace the set's" : "Animation associated with this action");
				ImGui::TextUnformatted("Change the animation played by this action:");
				for (size_t i = 0; i < binding.Clips.size(); ++i)
				{
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::RadioButton("##sel", ui.Clip == static_cast<int>(i))) ui.Clip = static_cast<int>(i);
					ImGui::SameLine();
					ImGui::TextDisabled("source");
					ImGui::SameLine();
					const bool sourceChanged = StringCombo("Change animation", binding.Clips[i].Source, sources, 220.0f);
					if (sourceChanged)
					{
						ui.PreviewSignature.clear();
						ui.Status = "animation '" + binding.Clips[i].Source + "' associated with " + action->Key;
					}
					if (sources.empty())
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "no montages or model clips available");
					}
					if (isLoco)
					{
						ImGui::SameLine();
						std::string current = binding.Clips[i].Replaces;
						const ManifestSet* set = action->Key == "loco:default" ? &manifest.Free : nullptr;
						for (const ManifestSet& s : manifest.Sets) if ("loco:" + s.Name == action->Key) set = &s;
						if (set && StringCombo("replaces", current, set->Clips, 200.0f)) binding.Clips[i].Replaces = current;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("x") && binding.Clips.size() > 1) { binding.Clips.erase(binding.Clips.begin() + static_cast<std::ptrdiff_t>(i)); ui.Clip = 0; ImGui::PopID(); break; }
					ImGui::PopID();
				}
				if (ImGui::SmallButton("+ clip")) { binding.Clips.push_back({}); ui.Clip = static_cast<int>(binding.Clips.size()) - 1; }

				if (!isLoco && !isAir) DrawAnimBindingClipSettings(*action, binding, manifest, defaults, hasClip);
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
		ImGui::SameLine();
		ImGui::BeginChild("##preview", ImVec2(270.0f, -150.0f), true);
		ImGui::SeparatorText("Preview");
		ImGui::TextDisabled("The selected animation plays on the character in the Viewport.");
		const BindableAction* previewAction = nullptr;
		for (const BindableAction& candidate : actions)
			if (candidate.Key == ui.Selected) { previewAction = &candidate; break; }
		if (!anim) ImGui::TextWrapped("Start a character to preview an animation or a flow.");
		else if (!previewAction) ImGui::TextWrapped("Select an action on the left.");
		else if (previewAction->Key.rfind("loco:", 0) == 0 || previewAction->Key == "air")
			ImGui::TextWrapped("Locomotion is previewed by moving the character in the Viewport. Select an ability or activity for clip playback.");
		else
		{
			std::vector<AnimBinding*> previewVariants;
			for (AnimBinding& candidate : bindings.Bindings)
				if (candidate.Action == previewAction->Key && candidate.Skeleton == scope) previewVariants.push_back(&candidate);
			AnimBinding* previewBinding = previewVariants.empty() ? nullptr : previewVariants[static_cast<size_t>(std::clamp(ui.Variant, 0, static_cast<int>(previewVariants.size()) - 1))];
			AnimBindings previewBindings;
			if (previewBinding)
			{
				AnimBinding solo = *previewBinding;
				solo.Skeleton.clear();
				solo.When.clear();
				previewBindings.Bindings.push_back(std::move(solo));
			}
			const ResolvedClip preview = ResolveClip(previewBinding ? previewBindings : bindings, manifest, defaults, previewAction->Key, {},
				previewBinding ? std::string() : skeleton, ui.Clip, hasClip);
			if (!preview.Found)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Cannot preview: %s", preview.Why.c_str());
			else
			{
				MontagePlayer& mp = anim->Montages();
				const std::string signature = (previewBinding ? AnimBindingsToJson(previewBindings) : previewAction->Key) + std::to_string(ui.Clip);
				if (signature != ui.PreviewSignature || !mp.Has(kPreview))
				{
					MontageDesc d = MakeNamedPreviewMontage(preview, manifest, kPreview);
					mp.Remove(kPreview);
					if (!mp.Add(d))
					{
						ui.PreviewSignature.clear();
						ui.Status = "preview could not be created for '" + previewAction->Key + "'";
					}
					else
					{
						ui.PreviewSignature = signature;
						ui.Status = "preview ready for '" + previewAction->Key + "'";
					}
				}
				if (mp.Has(kPreview))
				{
					const float whole = mp.ClipDuration(kPreview);
					const bool playing = mp.IsPlaying(kPreview);
					ImGui::TextWrapped("%s%s", previewAction->Key.c_str(), previewBinding ? " (binding)" : " (default)");
					if (ImGui::Button(playing ? "Restart animation" : "Play animation"))
					{
						mp.SetPaused(false);
						if (!m_Player.PlayMontage(kPreview))
							ui.Status = "preview could not start '" + previewAction->Key + "'";
					}
					if (ImGui::Button(mp.Paused() ? "Resume" : "Pause")) mp.SetPaused(!mp.Paused());
					ImGui::SameLine();
					if (ImGui::Button("Stop")) { mp.SetPaused(false); m_Player.StopMontage(); }
					if (!previewAction->Defaults.empty() && ImGui::Button("Play default"))
					{
						const size_t defaultClip = static_cast<size_t>(ui.Clip) % previewAction->Defaults.size();
						const ResolvedClip fallback = ResolveClip(bindings, manifest, defaults, previewAction->Key, {}, skeleton, static_cast<int>(defaultClip), hasClip);
						MontageDesc defaultDesc = MakeNamedPreviewMontage(fallback, manifest, "bind:default");
						mp.Remove(defaultDesc.Name);
						if (fallback.Found && mp.Add(defaultDesc))
						{
							mp.SetPaused(false);
							if (!m_Player.PlayMontage(defaultDesc.Name))
								ui.Status = "default animation could not start '" + previewAction->Key + "'";
						}
						else
							ui.Status = "default animation could not be created for '" + previewAction->Key + "'";
					}
					float t = std::max(mp.Time(kPreview), 0.0f);
					if (ImGui::SliderFloat("##preview_scrub", &t, 0.0f, whole, "%.2f s"))
					{
						if (!mp.IsPlaying(kPreview)) m_Player.PlayMontage(kPreview);
						mp.SetPaused(true);
						mp.Seek(kPreview, t);
					}
					ImGui::Text("%.2f s clip, %.2f s played", static_cast<double>(whole), static_cast<double>(mp.PlayDuration(kPreview)));
				}
			}
		}
		ImGui::SeparatorText("Flow preview");
		ImGui::TextWrapped("Select a transition below and use Test selected A -> B to play the complete flow with its blend.");
		ImGui::EndChild();

		DrawAnimBindingTransitions(actions, bindings, manifest, defaults, anim, skeleton, hasClip);

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
			if (ok)
			{
				m_AnimBindingsUi.Selected.clear();
				m_AnimBindingsUi.Transition = -1;
				m_AnimBindingsUi.Clip = 0;
				m_AnimBindingsUi.Variant = 0;
				m_AnimBindingsUi.PreviewSignature.clear();
				m_AnimBindingsUi.AppliedSignature.clear();
				m_AnimBindingsUi.LoggedIssues.clear();
			}
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
