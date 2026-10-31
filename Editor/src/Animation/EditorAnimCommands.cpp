// The editor's animation authoring commands: montage_* (notifies, sections, settings, preview, files) and graph_* (states, transitions,
// parameters, files). The panels that edit the same data live in EditorAnimPanels.cpp.
#include "EditorApp.h"
#include "GB/Animation/AnimGraph.h"
#include "GB/Animation/Montage.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <sstream>

namespace GB::Editor
{
	void EditorApp::RegisterAnimationCommands()
	{
		RegisterAnimBindingCommands();   // bindings_*: the Animation Bindings window as commands (Animation/EditorAnimBindings.cpp)
		CommandRunner& c = m_Commands;
		c.Register("montage_info", "montage_info [montage]: describes a montage (clip, slot, blends, rate, notifies, sections); no name lists them", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			MontagePlayer& mp = anim->Montages();
			if (a.empty())
			{
				std::string names;
				for (const std::string& n : mp.Names()) names += (names.empty() ? "" : ", ") + n;
				return { true, "montages: " + names };
			}
			const MontageDesc* d = mp.Get(a[0]);
			if (!d) return { false, "no such montage '" + a[0] + "'" };
			m_TimelineMontage = a[0];
			const float clip = mp.Duration(a[0]) * std::max(d->PlayRate, 0.01f);
			char head[200];
			std::snprintf(head, sizeof head, "%s: clip \"%s\" (%.2f s) in %s, blend %.2f / %.2f s, rate x%.2f%s%s", d->Name.c_str(), d->Clip.c_str(), static_cast<double>(clip), d->Slot.c_str(),
				static_cast<double>(d->BlendIn), static_cast<double>(d->BlendOut), static_cast<double>(d->PlayRate), d->Loop ? ", loop" : "", d->StripTravel ? ", strip travel" : "");
			std::string text = head;
			for (const MontageNotify& n : d->Notifies) { char b[96]; std::snprintf(b, sizeof b, "\n  notify %s at %.2f (%.2f s)", n.Name.c_str(), static_cast<double>(n.Time), static_cast<double>(n.Time * clip)); text += b; }
			for (const MontageSection& s : d->Sections) { char b[96]; std::snprintf(b, sizeof b, "\n  section %s from %.2f (%.2f s)", s.Name.c_str(), static_cast<double>(s.Time), static_cast<double>(s.Time * clip)); text += b; }
			return { true, text };
		});
		c.Register("montage_notify", "montage_notify <montage> add <time 0..1> <name> | remove <name> | move <name> <time 0..1>: edits a montage's notifies (time as a fraction of its clip)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.size() < 3) return { false, "usage: montage_notify <montage> add <time> <name> | remove <name> | move <name> <time>" };
			const MontageDesc* found = anim->Montages().Get(a[0]);
			if (!found) return { false, "no such montage '" + a[0] + "'" };
			m_TimelineMontage = a[0];   // the Timeline panel follows the montage the commands work on
			MontageDesc d = *found;
			auto find = [&](const std::string& n) { for (size_t i = 0; i < d.Notifies.size(); ++i) if (d.Notifies[i].Name == n) return static_cast<int>(i); return -1; };
			if (a[1] == "add" && a.size() >= 4) d.Notifies.push_back({ std::clamp(static_cast<float>(std::atof(a[2].c_str())), 0.0f, 1.0f), a[3] });
			else if (a[1] == "remove") { const int i = find(a[2]); if (i < 0) return { false, "no notify '" + a[2] + "'" }; d.Notifies.erase(d.Notifies.begin() + i); }
			else if (a[1] == "move" && a.size() >= 4) { const int i = find(a[2]); if (i < 0) return { false, "no notify '" + a[2] + "'" }; d.Notifies[static_cast<size_t>(i)].Time = std::clamp(static_cast<float>(std::atof(a[3].c_str())), 0.0f, 1.0f); }
			else return { false, "usage: montage_notify <montage> add <time> <name> | remove <name> | move <name> <time>" };
			anim->Montages().Add(d);
			return { true, std::to_string(d.Notifies.size()) + " notify(ies) on " + d.Name };
		});
		c.Register("montage_section", "montage_section <montage> add <time 0..1> <name> | remove <name>: edits a montage's sections", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.size() < 3) return { false, "usage: montage_section <montage> add <time> <name> | remove <name>" };
			const MontageDesc* found = anim->Montages().Get(a[0]);
			if (!found) return { false, "no such montage '" + a[0] + "'" };
			m_TimelineMontage = a[0];   // the Timeline panel follows the montage the commands work on
			MontageDesc d = *found;
			if (a[1] == "add" && a.size() >= 4) d.Sections.push_back({ std::clamp(static_cast<float>(std::atof(a[2].c_str())), 0.0f, 1.0f), a[3] });
			else if (a[1] == "remove")
			{
				const size_t before = d.Sections.size();
				d.Sections.erase(std::remove_if(d.Sections.begin(), d.Sections.end(), [&](const MontageSection& s) { return s.Name == a[2]; }), d.Sections.end());
				if (d.Sections.size() == before) return { false, "no section '" + a[2] + "'" };
			}
			else return { false, "usage: montage_section <montage> add <time> <name> | remove <name>" };
			anim->Montages().Add(d);
			return { true, std::to_string(d.Sections.size()) + " section(s) on " + d.Name };
		});
		c.Register("montage_set", "montage_set <montage> blendin|blendout|rate|loop|striptravel|slot <value>: edits a montage's settings", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.size() < 3) return { false, "usage: montage_set <montage> blendin|blendout|rate|loop|striptravel|slot <value>" };
			const MontageDesc* found = anim->Montages().Get(a[0]);
			if (!found) return { false, "no such montage '" + a[0] + "'" };
			m_TimelineMontage = a[0];   // the Timeline panel follows the montage the commands work on
			MontageDesc d = *found;
			const float v = static_cast<float>(std::atof(a[2].c_str()));
			const bool on = a[2] != "off" && a[2] != "0" && a[2] != "false";
			if (a[1] == "blendin") d.BlendIn = std::max(v, 0.0f);
			else if (a[1] == "blendout") d.BlendOut = std::max(v, 0.0f);
			else if (a[1] == "rate") d.PlayRate = std::max(v, 0.1f);
			else if (a[1] == "loop") d.Loop = on;
			else if (a[1] == "striptravel") d.StripTravel = on;
			else if (a[1] == "slot") d.Slot = a[2];
			else return { false, "unknown setting '" + a[1] + "'" };
			if (!anim->Montages().Add(d)) return { false, "that value is not valid (does the slot exist?)" };
			return { true, d.Name + " updated" };
		});
		c.Register("montage_seek", "montage_seek <montage> <seconds>|off: previews a montage paused at that moment of its clip; off releases it", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: montage_seek <montage> <seconds>|off" };
			MontagePlayer& mp = anim->Montages();
			if (!mp.Has(a[0])) return { false, "no such montage '" + a[0] + "'" };
			m_TimelineMontage = a[0];
			if (a.size() > 1 && a[1] == "off") { mp.SetPaused(false); m_Player.StopMontage(); return { true, "released" }; }
			if (a.size() < 2) return { false, "usage: montage_seek <montage> <seconds>|off" };
			if (!mp.IsPlaying(a[0])) m_Player.PlayMontage(a[0]);
			mp.SetPaused(true);
			mp.Seek(a[0], static_cast<float>(std::atof(a[1].c_str())));
			return { true, "paused at " + a[1] + " s" };
		});
		c.Register("montage_save", "montage_save <file.json>: writes every montage of the player (notifies and sections included)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: montage_save <file.json>" };
			std::ofstream out(a[0]);
			if (!out) return { false, "could not write " + a[0] };
			out << MontagesToJson(anim->Montages().Descs());
			return { true, "saved " + a[0] };
		});
		c.Register("montage_load", "montage_load <file.json>: adds (or replaces, by name) the montages of a file that the character has clips for", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: montage_load <file.json>" };
			std::ifstream in(a[0]);
			if (!in) return { false, "could not read " + a[0] };
			std::stringstream text;
			text << in.rdbuf();
			std::vector<MontageDesc> loaded;
			std::string error;
			if (!MontagesFromJson(text.str(), loaded, &error)) return { false, error };
			int added = 0;
			for (const MontageDesc& m : loaded) added += anim->Montages().Add(m) ? 1 : 0;
			return { true, std::to_string(added) + " of " + std::to_string(loaded.size()) + " montage(s) loaded (the others need clips this character lacks)" };
		});
		// --- animation state graph ---
		auto editGraph = [this](const std::function<CommandResult(AnimGraph&)>& fn) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			AnimGraph g = anim->Graph().Graph();
			const CommandResult r = fn(g);
			if (r.Ok) anim->Graph().SetGraph(g);
			return r;
		};
		c.Register("graph", "graph: lists the states, transitions and parameters of the animation graph and where it is now", [this](const std::vector<std::string>&) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			const GraphRunner& r = anim->Graph();
			const AnimGraph& g = r.Graph();
			std::string s = std::string(r.Enabled ? "running" : "OFF") + ", now in '" + r.Current() + "', start '" + g.Start + "'\n";
			for (const GraphState& st : g.States) s += "state " + st.Name + " (" + (st.What == GraphState::Kind::Montage ? "montage " : "locomotion ") + st.Target + ")\n";
			for (size_t i = 0; i < g.Transitions.size(); ++i)
			{
				const GraphTransition& t = g.Transitions[i];
				s += "[" + std::to_string(i) + "] " + t.From + " -> " + t.To;
				for (const GraphCondition& cd : t.Conditions) s += "  " + DescribeCondition(cd);
				if (t.OnFinish) s += "  finish";
				s += "\n";
			}
			for (const GraphParam& p : g.Params) s += std::string("param ") + p.Name + (p.What == GraphParam::Kind::Trigger ? " (trigger)" : p.What == GraphParam::Kind::Bool ? " (bool)" : "") + "\n";
			return { true, s };
		});
		c.Register("graph_default", "graph_default: replaces the graph with one state per locomotion set of the character", [this, editGraph](const std::vector<std::string>&) -> CommandResult
		{
			return editGraph([this](AnimGraph& g) -> CommandResult { g = MakeDefaultGraph(*m_Player.Anim()); return { true, std::to_string(g.States.size()) + " state(s)" }; });
		});
		c.Register("graph_state", "graph_state <name> locomotion|montage <target> [x y]: adds a state, or changes it when the name exists", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 3) return { false, "usage: graph_state <name> locomotion|montage <set-or-montage> [x y]" };
			if (a[1] != "locomotion" && a[1] != "montage") return { false, "the kind is locomotion or montage" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				int i = g.FindState(a[0]);
				if (i < 0)
				{
					GraphState st;
					st.Name = a[0];
					st.Position = glm::vec2(30.0f + 190.0f * static_cast<float>(g.States.size() % 4), 30.0f + 110.0f * static_cast<float>(g.States.size() / 4));
					g.States.push_back(st);
					i = static_cast<int>(g.States.size()) - 1;
				}
				GraphState& st = g.States[static_cast<size_t>(i)];
				st.What = a[1] == "montage" ? GraphState::Kind::Montage : GraphState::Kind::Locomotion;
				st.Target = a[2];
				if (a.size() >= 5) st.Position = glm::vec2(static_cast<float>(std::atof(a[3].c_str())), static_cast<float>(std::atof(a[4].c_str())));
				return { true, "state " + st.Name };
			});
		});
		c.Register("graph_state_remove", "graph_state_remove <name>: removes a state and the transitions that touch it", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: graph_state_remove <name>" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				const int i = g.FindState(a[0]);
				if (i < 0) return { false, "no such state '" + a[0] + "'" };
				g.States.erase(g.States.begin() + i);
				g.Transitions.erase(std::remove_if(g.Transitions.begin(), g.Transitions.end(), [&](const GraphTransition& t) { return t.From == a[0] || t.To == a[0]; }), g.Transitions.end());
				if (g.Start == a[0]) g.Start.clear();
				return { true, "removed " + a[0] };
			});
		});
		c.Register("graph_start", "graph_start <state>: the state the graph begins in", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: graph_start <state>" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				if (g.FindState(a[0]) < 0) return { false, "no such state '" + a[0] + "'" };
				g.Start = a[0];
				return { true, "start = " + a[0] };
			});
		});
		c.Register("graph_transition", "graph_transition <from|*> <to> [Speed>2 ...] [blend=0.3] [priority=1] [min=0.5] [finish]: adds a transition; every condition must hold", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2) return { false, "usage: graph_transition <from|*> <to> [conditions...] [blend=..] [priority=..] [min=..] [finish]" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				GraphTransition t;
				t.From = a[0];
				t.To = a[1];
				if (t.From != "*" && g.FindState(t.From) < 0) return { false, "no such state '" + t.From + "'" };
				if (g.FindState(t.To) < 0) return { false, "no such state '" + t.To + "'" };
				for (size_t i = 2; i < a.size(); ++i)
				{
					const std::string& w = a[i];
					if (w == "finish") t.OnFinish = true;
					else if (w.rfind("blend=", 0) == 0) t.Blend = static_cast<float>(std::atof(w.c_str() + 6));
					else if (w.rfind("priority=", 0) == 0) t.Priority = std::atoi(w.c_str() + 9);
					else if (w.rfind("min=", 0) == 0) t.MinStateTime = static_cast<float>(std::atof(w.c_str() + 4));
					else
					{
						GraphCondition cd;
						if (!ParseCondition(w, cd)) return { false, "cannot read the condition '" + w + "' (try Speed>2, Grounded==1, Pick or !Pick)" };
						t.Conditions.push_back(cd);
						// an automatic parameter (Speed, Grounded...) needs no declaration; anything else is declared as a trigger
						if (g.FindParam(cd.Param) < 0 && !IsAutoParam(cd.Param)) { GraphParam p; p.Name = cd.Param; p.What = GraphParam::Kind::Trigger; g.Params.push_back(p); }
					}
				}
				g.Transitions.push_back(t);
				return { true, "transition [" + std::to_string(g.Transitions.size() - 1) + "] " + t.From + " -> " + t.To };
			});
		});
		c.Register("graph_transition_remove", "graph_transition_remove <index>: removes a transition (the numbers are listed by graph)", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: graph_transition_remove <index>" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				const int i = std::atoi(a[0].c_str());
				if (i < 0 || i >= static_cast<int>(g.Transitions.size())) return { false, "no such transition" };
				g.Transitions.erase(g.Transitions.begin() + i);
				return { true, "removed" };
			});
		});
		c.Register("graph_declare", "graph_declare <name> float|bool|trigger: declares a parameter of the graph", [editGraph](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.size() < 2) return { false, "usage: graph_declare <name> float|bool|trigger" };
			return editGraph([&](AnimGraph& g) -> CommandResult
			{
				if (a[1] != "float" && a[1] != "bool" && a[1] != "trigger") return { false, "the kind is float, bool or trigger" };
				const int i = g.FindParam(a[0]);
				GraphParam p;
				if (i >= 0) p = g.Params[static_cast<size_t>(i)];
				p.Name = a[0];
				p.What = a[1] == "trigger" ? GraphParam::Kind::Trigger : a[1] == "bool" ? GraphParam::Kind::Bool : GraphParam::Kind::Float;
				if (i >= 0) g.Params[static_cast<size_t>(i)] = p; else g.Params.push_back(p);
				return { true, "param " + a[0] };
			});
		});
		c.Register("graph_param", "graph_param <name> <value>: sets a parameter the conditions read", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.size() < 2) return { false, "usage: graph_param <name> <value>" };
			anim->Graph().SetParam(a[0], static_cast<float>(std::atof(a[1].c_str())));
			return { true, a[0] + " = " + a[1] };
		});
		c.Register("graph_trigger", "graph_trigger <name>: fires a trigger parameter", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: graph_trigger <name>" };
			anim->Graph().Trigger(a[0]);
			return { true, "fired " + a[0] };
		});
		c.Register("graph_enable", "graph_enable on|off: lets the graph drive the animation, or not", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: graph_enable on|off" };
			anim->Graph().Enabled = a[0] == "on" || a[0] == "1" || a[0] == "true";
			if (anim->Graph().Enabled) anim->Graph().Restart();
			return { true, anim->Graph().Enabled ? "on" : "off" };
		});
		c.Register("graph_validate", "graph_validate: lists what would stop the graph from working (unknown states, sets, montages, parameters)", [this](const std::vector<std::string>&) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			const auto problems = anim->Graph().Graph().Validate(anim);
			if (problems.empty()) return { true, "the graph is fine" };
			std::string s;
			for (const std::string& p : problems) s += p + "\n";
			return { false, s };
		});
		c.Register("graph_save", "graph_save <file.json>: writes the animation graph", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: graph_save <file.json>" };
			std::ofstream out(a[0]);
			if (!out) return { false, "could not write " + a[0] };
			out << AnimGraphToJson(anim->Graph().Graph());
			return { true, "saved " + a[0] };
		});
		c.Register("graph_load", "graph_load <file.json>: replaces the animation graph with the one in a file", [this](const std::vector<std::string>& a) -> CommandResult
		{
			AnimInstance* anim = m_Player.Anim();
			if (!anim) return { false, kNeedsFbxPlayer };
			if (a.empty()) return { false, "usage: graph_load <file.json>" };
			std::ifstream in(a[0]);
			if (!in) return { false, "could not read " + a[0] };
			std::stringstream text;
			text << in.rdbuf();
			AnimGraph g;
			std::string error;
			if (!AnimGraphFromJson(text.str(), g, &error)) return { false, error };
			anim->Graph().SetGraph(g);
			return { true, "loaded " + std::to_string(g.States.size()) + " state(s)" };
		});
	}
}
