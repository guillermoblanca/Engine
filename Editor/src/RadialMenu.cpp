// T271 (documentation/01_diseno/MODO_COLONIA.md §4.2, C1-b): on the god's screen of the colony the player selects several people of its clan (a
// click, Shift+click to add or take one away, a box dragged over the view) and a right click over something opens a radial menu round it: bubbles
// with the icons of the atlas, a short label when the mouse is over one, and the ones that apply but cannot be used now dimmed with why. Each bubble
// only queues the commands a script would type (`game order sel ...` with the selection, `game designate ...` for the clan when nobody is
// selected, `game site ...`, `director ...`, `observer ...`): the UI decides nothing. Over each person with an order its verb's icon, and a news
// card when an order is cut ("Biel dejó la orden: mucha hambre"). `colony select ...` and `colony radial ...` drive the same code headless.
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include "GB/Game/TechVerbs.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Input/InputSource.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>

namespace GB::Editor
{
	namespace
	{
		ImTextureID TextureOf(const Texture2D& t) { return static_cast<ImTextureID>(static_cast<intptr_t>(t.GetID())); }

		// a name inside a command: one word, with _ for its spaces (the game's commands turn them back)
		std::string Word(std::string s)
		{
			for (char& c : s) if (c == ' ') c = '_';
			return s;
		}
		std::string Num(float v)   // (a number inside a command: a point, never the Spanish comma)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.1f", static_cast<double>(v));
			return b;
		}
		std::string Joined(const std::vector<std::string>& names, const char* sep)
		{
			std::string s;
			for (const std::string& n : names) s += (s.empty() ? "" : sep) + n;
			return s;
		}

		// the label and the icon of a node's work, by the ability its data gives it
		void NodeWork(const std::string& ability, std::string& label, std::string& icon)
		{
			if (ability == "Chop") { label = "Fell"; icon = "chop"; }
			else if (ability == "Mine") { label = "Quarry"; icon = "mine"; }
			else if (ability == "Dig") { label = "Dig"; icon = "mine"; }
			else if (ability == "Collect") { label = "Collect"; icon = "collect"; }
			else { label = "Gather"; icon = "gather"; }
		}

		// the icon of an order's verb over the person that holds it (ColonyOrders::Mandate)
		std::string VerbIcon(const std::string& verb) { return verb == "use" ? "gather" : verb == "follow" ? "expedition" : verb; }

		// T327: how many verbs the clan has still to discover, for the radial's "?" bubble: only over what the people act on (the ground, a node,
		// one of the clan); 0 draws none
		int UnknownVerbs(const Tribe* t, RadialTarget::Kind what)
		{
			using K = RadialTarget::Kind;
			return t && (what == K::Ground || what == K::Node || what == K::Own) ? TechVerbs::Unknown(*t) : 0;
		}

		// the camera does not take the left button while a box is drawn (the rest of the input is the window's)
		class WithoutLeft : public IInputSource
		{
		public:
			explicit WithoutLeft(const IInputSource& in) : m_In(in) {}
			bool IsKeyDown(Key key) const override { return m_In.IsKeyDown(key); }
			bool IsMouseDown(MouseButton b) const override { return b != MouseButton::Left && m_In.IsMouseDown(b); }
			std::pair<double, double> GetMousePosition() const override { return m_In.GetMousePosition(); }
			std::pair<double, double> GetMouseDelta() const override { return m_In.GetMouseDelta(); }
			float GetScrollDelta() const override { return m_In.GetScrollDelta(); }
			bool IsGamepadConnected(int pad) const override { return m_In.IsGamepadConnected(pad); }
			bool IsGamepadButtonDown(GamepadButton b, int pad) const override { return m_In.IsGamepadButtonDown(b, pad); }
			float GetGamepadAxis(GamepadAxis a, int pad, float deadzone) const override { return m_In.GetGamepadAxis(a, pad, deadzone); }
		private:
			const IInputSource& m_In;
		};

		// where a ray of the view meets the terrain (false: the sky, or the camera under the ground)
		bool GroundHit(const Terrain& terrain, const Ray& ray, glm::vec3& out)
		{
			auto above = [&](float t) { const glm::vec3 p = ray.Origin + ray.Dir * t; return p.y - terrain.GetHeight(p.x, p.z); };
			float previous = 0.0f;
			if (above(previous) < 0.0f) return false;
			for (float t = 0.5f; t < 1500.0f; t += std::max(0.5f, t * 0.02f))
			{
				if (above(t) < 0.0f)
				{
					float lo = previous, hi = t;
					for (int i = 0; i < 12; ++i) { const float mid = 0.5f * (lo + hi); (above(mid) < 0.0f ? hi : lo) = mid; }
					out = ray.Origin + ray.Dir * (0.5f * (lo + hi));
					return true;
				}
				previous = t;
			}
			return false;
		}
	}

	bool EditorApp::ColonyScreen() const
	{
		return m_Observer.On && m_Observer.Screen && !m_Game.Active && m_Director.Colony() && ColonyTribe() != nullptr;
	}

	const Tribe* EditorApp::ColonyTribe() const
	{
		if (!m_Director.Colony()) return nullptr;
		const Clan* c = m_Clans.Find(static_cast<ClanId>(HandClan()));
		return c && c->Folk ? c->Folk : nullptr;
	}

	void EditorApp::PruneColonySelection()
	{
		const Tribe* t = ColonyTribe();
		std::vector<std::string>& sel = m_Colony.Selection;
		auto own = [&](const std::string& name) { const TribeMember* m = t ? t->FindMember(name) : nullptr; return m && !m->Dead && !m->Player; };
		sel.erase(std::remove_if(sel.begin(), sel.end(), [&](const std::string& n) { return !own(n); }), sel.end());
		// a person picked some other way (the members' table, `pick`) is the selection when it is not in it
		if (own(m_FreeSelected) && std::find(sel.begin(), sel.end(), m_FreeSelected) == sel.end()) sel = { m_FreeSelected };
	}

	void EditorApp::ColonyClicked(const std::string& before, bool shift)
	{
		const Tribe* t = ColonyTribe();
		if (!t) return;
		const std::string picked = m_FreeSelected;
		const TribeMember* m = picked.empty() ? nullptr : t->FindMember(picked);
		std::vector<std::string>& sel = m_Colony.Selection;
		if (m && !m->Dead && !m->Player)
		{
			if (!shift) { sel = { picked }; return; }
			if (const auto it = std::find(sel.begin(), sel.end(), picked); it != sel.end())
			{
				sel.erase(it);   // Shift+click on one selected takes it away; the card goes to the last one left
				m_FreeSelected = sel.empty() ? std::string() : sel.back();
			}
			else sel.push_back(picked);
		}
		else if (shift && !sel.empty()) m_FreeSelected = before;   // (Shift+click on something else keeps the people selected)
		else sel.clear();
	}

	void EditorApp::ColonyBoxSelect(glm::vec2 a, glm::vec2 b, bool add)
	{
		const Tribe* t = ColonyTribe();
		if (!t) return;
		const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
		const glm::vec2 size(static_cast<float>(std::max(m_ViewportW, 1u)), static_cast<float>(std::max(m_ViewportH, 1u)));
		const glm::mat4 viewProj = m_Camera.GetProjection(size.x / size.y) * m_Camera.GetView();
		std::vector<std::string> inside;
		for (const TribeMember& m : t->Members())
		{
			if (m.Dead || m.Player) continue;
			glm::vec3 at;
			glm::vec2 s;
			if (!MemberWhere(m.Name, at) || !ProjectToScreen(viewProj, at + glm::vec3(0.0f, 0.9f, 0.0f), size, s)) continue;   // (its middle)
			if (s.x >= lo.x && s.x <= hi.x && s.y >= lo.y && s.y <= hi.y) inside.push_back(m.Name);
		}
		std::vector<std::string>& sel = m_Colony.Selection;
		if (!add) sel.clear();
		for (const std::string& n : inside) if (std::find(sel.begin(), sel.end(), n) == sel.end()) sel.push_back(n);
		if (!sel.empty()) { if (std::find(sel.begin(), sel.end(), m_FreeSelected) == sel.end()) m_FreeSelected = sel.front(); }
		else if (t->FindMember(m_FreeSelected)) m_FreeSelected.clear();
	}

	RadialTarget EditorApp::RadialTargetOf(const std::string& name, const glm::vec3& ground) const
	{
		RadialTarget r;
		r.Name = name;
		r.At = ground;
		if (name.empty()) { r.What = RadialTarget::Kind::Ground; return r; }
		const Tribe* t = ColonyTribe();
		glm::vec3 at;
		if (const TribeMember* m = AnyMember(name))
		{
			if (MemberWhere(name, at)) r.At = at;
			r.What = t && t->FindMember(name) == m && !m->Player ? RadialTarget::Kind::Own : RadialTarget::Kind::Stranger;
			return r;
		}
		const EntityWorld& world = m_SceneEditor.World();
		if (const EntityId id = world.FindByName(name); id != NullEntity) r.At = glm::vec3(world.WorldMatrix(id)[3]);
		const AbilitySystemComponent* asc = m_Gameplay.Find(name);
		if (!asc) { r.What = RadialTarget::Kind::None; return r; }
		if (asc->HasTag("Resource.Node")) r.What = RadialTarget::Kind::Node;
		else if (BuildingOf(*asc, m_Buildings.Defs))
		{
			bool mine = false;
			if (t && asc->HasTag("Building.Site"))
				for (const std::string& s : t->Designations().SiteOrder(*t)) mine = mine || s == name;
			r.What = mine ? RadialTarget::Kind::Site : RadialTarget::Kind::Building;
		}
		else
		{
			for (const auto& [sid, sp] : m_Ecology.Species) if (asc->HasTag(sp.Tag)) r.What = RadialTarget::Kind::Animal;
			if (r.What != RadialTarget::Kind::Animal) r.What = RadialTarget::Kind::None;
		}
		return r;
	}

	std::vector<RadialOption> EditorApp::RadialOptions(const RadialTarget& target) const
	{
		using K = RadialTarget::Kind;
		std::vector<RadialOption> out;
		const Tribe* t = ColonyTribe();
		if (!t || target.What == K::None) return out;
		std::vector<std::string> sel;
		for (const std::string& n : m_Colony.Selection)
			if (const TribeMember* m = t->FindMember(n); m && !m->Dead && !m->Player) sel.push_back(n);
		const bool any = !sel.empty();
		const std::string nobody = "Choose someone first: a click, Shift+click or a box", later = "Later in the game";
		auto add = [&](const char* id, const std::string& icon, const std::string& label, const std::string& tip, std::vector<std::string> commands, bool needsSel)
		{
			RadialOption o;
			o.Id = id;
			o.Icon = icon;
			o.Label = label;
			o.Tip = tip;
			o.Commands = std::move(commands);
			if (needsSel && !any) { o.Off = true; o.Why = nobody; }
			out.push_back(o);
			return out.size() - 1;
		};
		auto off = [&](size_t i, const std::string& why) { out[i].Off = true; out[i].Why = why; };
		// T327 (ARBOL_TECNICO §11.2.5): a work of a verb the clan does not know is not offered at all (no name, no dimmed bubble): the radial draws
		// one "?" bubble instead, with how many verbs are left to discover (DrawColonyWorld)
		auto knows = [&](const std::string& kind, const std::string& what = std::string()) { return TechVerbs::Allows(*t, kind, what); };
		const std::string at = Num(target.At.x) + " " + Num(target.At.z);
		const std::string name = Word(target.Name);
		switch (target.What)
		{
		case K::Ground:
			if (knows("goto")) add("goto", "goto", "Go here", "The people selected walk here", { "game order sel goto " + at }, true);
			if (knows("farm", "till")) add("field_here", "till", "A field here...", "Mark out a field: this is its first point", { "colony zone start", "colony zone point " + at }, false);   // T281
			add("build_here", "build", "Build here...", "The buildings, to place one here", { "director hand build" }, false);
			break;
		case K::Node:
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(target.Name);
			std::string ability, nodeKind;
			for (const auto& [kind, def] : m_NodeDefs) if (asc && asc->HasTag("Resource.Node." + kind)) { ability = def.Ability; nodeKind = kind; }
			std::string label, icon;
			NodeWork(ability, label, icon);
			const ColonyDesignations::Mark* mark = t->Designations().MarkOf(target.Name);
			const bool mayMark = knows("designate", nodeKind);
			if (any ? knows("use", nodeKind) : mayMark)
			{
				const size_t use = add("use", icon, label, any ? "The people selected go and do it now" : "For the clan: whoever can do it takes it",
					{ any ? "game order sel use " + name : "game designate " + name }, false);
				if (asc && ResourceSystem::Amount(*asc) <= 0) off(use, "There is nothing left in it");
			}
			if (mayMark)
				add("mark_all", "explore", ability == "Chop" || ability == "Gather" ? "Mark the whole stand" : "Mark the whole deposit",
					"Every one of its kind around it, for the clan", { "game designate " + name + " all" }, false);
			if (mark)
			{
				const size_t up = add("priority_up", icon, "Priority up", "The clan does it before the rest", { "game designate " + name + " " + std::to_string(std::max(1, mark->Priority - 1)) }, false);
				out[up].Arrow = true;
				if (mark->Priority <= 1) off(up, "It is already the first");
				add("unmark", "state.cannot", "Remove the mark", "The clan stops doing it", { "game designate " + name + " off" }, false);
			}
			if (knows("goto")) add("goto", "goto", "Go", "The people selected walk to it", { "game order sel goto " + at }, true);
			break;
		}
		case K::Own:
		{
			std::vector<std::string> others;
			for (const std::string& n : sel) if (n != target.Name) others.push_back(n);
			const std::string who = Joined(others, ",");
			for (const char* verb : { "talk", "follow" })
			{
				if (!knows(verb)) continue;
				const bool talks = std::string(verb) == "talk";
				const size_t i = add(verb, talks ? "talk" : "expedition", talks ? "Talk" : "Follow", talks ? "The people selected go and talk to this person" : "The people selected follow this person",
					{ "game order " + who + " " + verb + " " + name }, true);
				if (any && others.empty()) off(i, "Choose someone else first");
			}
			add("trade", "make", "Change trade", "Its sheet, to pick its trade", { "observer person " + name, "director hand calling " + name }, false);
			if (t->Colony().Has(target.Name)) add("cancel", "state.cannot", "Cancel the order", "It goes back to its own life", { "game order " + name + " cancel" }, false);
			break;
		}
		case K::Stranger:
			off(add("talk", "talk", "Talk", "Your people go and talk to this person", {}, false), "Only with the people of your clan, for now");
			off(add("barter", "trade", "Trade", "Exchange goods with its clan", {}, false), later);
			off(add("attack", "fight", "Attack", "Your people attack this person", {}, false), later);
			off(add("invite", "migrate", "Invite to join", "Ask this person to join your clan", {}, false), later);
			break;
		case K::Animal:
			off(add("hunt", "hunt", "Hunt", "The people selected hunt it", {}, false), later);
			off(add("follow", "expedition", "Follow", "The people selected follow it", {}, false), "Only with the people of your clan, for now");
			break;
		case K::Site:
		{
			if (knows("build")) add("build", "build", "Build / help", "The people selected bring what it lacks and build it", { "game order sel build " + name }, true);
			if (knows("deliver")) add("deliver", "deliver", "Bring material", "The people selected only carry what it lacks", { "game order sel deliver " + name }, true);
			const std::vector<std::string> order = t->Designations().SiteOrder(*t);
			const auto it = std::find(order.begin(), order.end(), target.Name);
			const size_t up = add("priority_up", "build", "Priority up", "The clan works on it before the others", { "game site " + name + " up" }, false);
			out[up].Arrow = true;
			if (it == order.begin()) off(up, "It is already the first");
			const size_t down = add("priority_down", "build", "Priority down", "The clan works on it after the others", { "game site " + name + " down" }, false);
			out[down].Arrow = out[down].Down = true;
			if (it == order.end() || it + 1 == order.end()) off(down, "It is already the last");
			add("cancel_plan", "state.cannot", "Cancel the plan", "What was brought goes back to the store", { "game site cancel " + name }, false);
			break;
		}
		case K::Building:
			off(add("repair", "build", "Repair", "Mend what is worn", {}, false), later);
			off(add("upgrade", "make", "Improve", "Make it a better building", {}, false), later);
			off(add("move", "haul", "Move", "Take it somewhere else", {}, false), later);
			off(add("demolish", "chop", "Pull down", "Take it down and keep the material", {}, false), later);
			break;
		default:
			break;
		}
		// the orders to the people selected: who of them cannot take one now, and why (what ColonyOrders::Give would refuse); none can: Off
		std::vector<std::pair<std::string, std::string>> unfit;
		for (const std::string& n : sel)
		{
			const TribeMember* m = t->FindMember(n);
			const std::string& job = m->Job;
			std::string why = job == "fight" ? "in a fight" : job == "flee" ? "fleeing" : job == "raid" ? "on a raid" : job == "expedition" || job == "migrate" ? "away from the camp" : std::string();
			if (why.empty()) why = ColonyOrders::Unfit(*t, *m);
			if (!why.empty()) unfit.push_back({ n, why });
		}
		if (!unfit.empty())
			for (RadialOption& o : out)
			{
				if (o.Off || o.Commands.empty() || o.Commands.front().rfind("game order ", 0) != 0 || o.Commands.front().rfind("game order " + name + " ", 0) == 0) continue;
				size_t asked = 0;   // (talk and follow go to the others of the selection: the target itself is not asked)
				for (const std::string& n : sel) if (n != target.Name || target.What != K::Own) ++asked;
				for (const auto& u : unfit) if (u.first != target.Name || target.What != K::Own) o.Unfit.push_back(u);
				if (!o.Unfit.empty() && o.Unfit.size() >= asked) { o.Off = true; o.Why = "Nobody selected can now"; }
			}
		return out;
	}

	void EditorApp::OpenRadial(const RadialTarget& t)
	{
		m_Colony.Open = t.What != RadialTarget::Kind::None && (!RadialOptions(t).empty() || UnknownVerbs(ColonyTribe(), t.What) > 0);
		m_Colony.Target = t;
		m_Colony.Hover.clear();
	}

	CommandResult EditorApp::RunRadial(const std::string& id)
	{
		if (!m_Colony.Open) return { false, "no radial open" };
		for (const RadialOption& o : RadialOptions(m_Colony.Target))
		{
			if (o.Id != id) continue;
			if (o.Off) return { false, id + " cannot now: " + o.Why };
			m_Colony.Open = false;
			// run now (a bubble's click comes here through the console, from the frame's queue of commands): `sel` is written out as the names it
			// stands for, so the log and a replay keep who was ordered
			PruneColonySelection();
			const std::string names = Joined(m_Colony.Selection, ",");
			bool ok = true;
			std::string done;
			for (std::string c : o.Commands)
			{
				if (c.rfind("game order sel ", 0) == 0) c = "game order " + names + c.substr(14);
				const CommandResult r = Execute(c);
				ok = ok && r.Ok;
				done += (done.empty() ? "" : "; ") + c + " -> " + r.Message;
			}
			return { ok, id + ": " + done };
		}
		return { false, "the radial has no " + id };
	}

	bool EditorApp::ColonyMasksLeft() const
	{
		return ColonyScreen() && m_PressedInViewport && !ImGui::GetIO().KeyAlt;
	}

	const IInputSource& EditorApp::ColonyInput() const
	{
		static WithoutLeft masked(GetInput());   // (the window's input lives as long as the editor)
		return masked;
	}

	void EditorApp::ColonyKeys()
	{
		ZoneKeys();   // T281: Enter, Backspace and Esc of the tool Field first (its Esc drops the perimeter before the tab closes)
		const bool tab = !m_Colony.Tab.empty() && ColonyScreen() && !m_Observer.Tree;   // (a tab hidden under the tree of knowledge does not take the Esc)
		if ((!m_Colony.Open && !tab) || ImGui::GetIO().WantTextInput) return;
		if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return;
		if (m_Colony.Open) m_Colony.Open = false;
		else m_Colony.Tab.clear();   // T308: then the tab Work or Stock (only reached when it is drawn)
		// Esc only closes the radial (or the tab) this frame: the shell's pause menu and the god's screen's "back to the editor" do not see it
		ImGui::SetKeyOwner(ImGuiKey_Escape, ImHashStr("colony_radial"), ImGuiInputFlags_LockThisFrame);
	}

	void EditorApp::ColonyNotices()
	{
		const Tribe* t = ColonyTribe();
		if (!t) return;
		const std::vector<ColonyOrders::Notice>& all = t->Colony().Notices();
		auto key = [](const ColonyOrders::Notice& n) { return std::to_string(n.At) + "|" + n.Member + "|" + n.Reason; };
		size_t from = 0;
		if (!m_Colony.NoticeKey.empty())
			for (size_t i = all.size(); i-- > 0;)
				if (key(all[i]) == m_Colony.NoticeKey) { from = i + 1; break; }
		if (from >= all.size()) return;
		const double now = ImGui::GetTime();
		for (size_t i = from; i < all.size(); ++i)
		{
			const TribeMember* m = t->FindMember(all[i].Member);
			// the clan's own line ("Biel (forager) drops the order: very hungry"): es.json's phrase translates it
			m_Colony.Notes.push_back({ all[i].Member + " (" + (m ? m->Role : std::string("-")) + ") drops the order: " + all[i].Reason, now });
		}
		m_Colony.NoticeKey = key(all.back());
		while (m_Colony.Notes.size() > 8) m_Colony.Notes.erase(m_Colony.Notes.begin());
	}

	void EditorApp::DrawColonyWorld(float ox, float oy, float w, float h)
	{
		if (!ColonyScreen()) { m_Colony.Open = m_Colony.Boxing = m_Colony.RightDown = false; return; }
		namespace T = GameTheme;
		const Lang& L = m_Lang;
		ImGuiIO& io = ImGui::GetIO();
		const Tribe& t = *ColonyTribe();
		PruneColonySelection();
		ColonyNotices();
		const glm::vec2 size(w, h), mouse(io.MousePos.x - ox, io.MousePos.y - oy);
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		ImDrawList* dl = ImGui::GetWindowDrawList();   // (the viewport: under the cards)
		auto screen = [&](const glm::vec3& p, ImVec2& s) { glm::vec2 v; if (!ProjectToScreen(viewProj, p, size, v)) return false; s = ImVec2(ox + v.x, oy + v.y); return true; };

		// ---- an ochre ring on the ground round each person selected (the era's selection border: ochre over a dark outline)
		for (const std::string& name : m_Colony.Selection)
		{
			glm::vec3 at;
			if (!MemberWhere(name, at)) continue;
			ImVec2 ring[24];
			bool ok = true;
			for (int i = 0; i < 24 && ok; ++i)
			{
				const float a = 6.2831853f * static_cast<float>(i) / 24.0f, x = at.x + 0.6f * std::cos(a), z = at.z + 0.6f * std::sin(a);
				const float y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) + 0.06f : at.y + 0.06f;
				ok = screen(glm::vec3(x, y, z), ring[i]);
			}
			if (!ok) continue;
			dl->AddConvexPolyFilled(ring, 24, T::Faded(T::Ochre, 0.16f));
			dl->AddPolyline(ring, 24, T::Outline, ImDrawFlags_Closed, 4.5f);
			dl->AddPolyline(ring, 24, T::Ochre, ImDrawFlags_Closed, 2.5f);
		}

		// ---- over each person holding an order, its verb's icon (small, beside the head): the mandate
		for (const auto& [name, d] : t.Colony().All())
		{
			glm::vec3 at;
			ImVec2 head;
			glm::vec2 uv0, uv1;
			std::string icon = VerbIcon(d.Verb);
			if (d.Verb == "use")
				if (const AbilitySystemComponent* node = m_Gameplay.Find(d.Target))
					for (const auto& [kind, def] : m_NodeDefs)
						if (node->HasTag("Resource.Node." + kind)) { std::string label; NodeWork(def.Ability, label, icon); }
			if (!MemberWhere(name, at) || !screen(at + glm::vec3(0.0f, 2.0f, 0.0f), head) || !IconUv(icon, uv0, uv1)) continue;
			const ImVec2 c(head.x + 20.0f, head.y + 6.0f);
			const float r = 13.0f;
			dl->AddCircleFilled(c, r + 2.0f, T::Outline, 20);
			dl->AddCircleFilled(c, r, T::Parchment, 20);
			dl->AddCircle(c, r, T::Ochre, 20, 2.0f);
			dl->AddImage(TextureOf(*m_IconTexture), ImVec2(c.x - r + 3.0f, c.y - r + 3.0f), ImVec2(c.x + r - 3.0f, c.y + r - 3.0f), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
		}

		// ---- the box a left drag draws (EditorApp::DrawGizmo selects on the release; the camera does not turn meanwhile: ColonyMasksLeft)
		if (m_PressedInViewport && ImGui::IsMouseDown(0) && !io.KeyAlt && m_Hand.Building.empty())
		{
			if (!m_Colony.Boxing && glm::distance(mouse, m_PressPos) >= 5.0f) { m_Colony.Boxing = true; m_Colony.BoxFrom = m_PressPos; }
			if (m_Colony.Boxing) m_Colony.BoxTo = mouse;
		}
		if (m_Colony.Boxing)
		{
			const ImVec2 a(ox + std::min(m_Colony.BoxFrom.x, m_Colony.BoxTo.x), oy + std::min(m_Colony.BoxFrom.y, m_Colony.BoxTo.y));
			const ImVec2 b(ox + std::max(m_Colony.BoxFrom.x, m_Colony.BoxTo.x), oy + std::max(m_Colony.BoxFrom.y, m_Colony.BoxTo.y));
			dl->AddRectFilled(a, b, T::Faded(T::Ochre, 0.12f));
			dl->AddRect(a, b, T::Outline, 0.0f, 0, 3.5f);
			dl->AddRect(a, b, T::Ochre, 0.0f, 0, 1.5f);
		}

		// ---- T281: the tool Field: the perimeter being drawn, its label and the outlines of the clan's zones (ColonyZones.cpp)
		DrawZoneWorld(ox, oy, w, h);

		// ---- the right button: a click (not a drag, which pans) opens the radial over what is under it; never while a building is being placed nor
		// while a field is drawn (there it takes the last point away)
		if (m_ViewportHovered && ImGui::IsMouseClicked(1) && m_Hand.Building.empty() && !ZonePainting()) { m_Colony.RightDown = true; m_Colony.RightAt = mouse; }
		if (m_Colony.RightDown && ImGui::IsMouseReleased(1))
		{
			m_Colony.RightDown = false;
			if (m_ViewportHovered && m_Hand.Building.empty() && glm::distance(mouse, m_Colony.RightAt) < 5.0f)
			{
				const Ray ray = ScreenRay(m_Camera, mouse / size, w / std::max(h, 1.0f));
				glm::vec3 ground(0.0f);
				const std::string picked = PickSelectable(ray);
				const bool hit = m_World.IsBuilt() && GroundHit(m_World.GetTerrain(), ray, ground);
				if (!picked.empty() || hit) OpenRadial(RadialTargetOf(picked, ground));
				else m_Colony.Open = false;
			}
		}

		// ---- the radial: bubbles round what it was opened over
		if (!m_Colony.Open) return;
		const std::vector<RadialOption> options = RadialOptions(m_Colony.Target);
		const int unknown = UnknownVerbs(&t, m_Colony.Target.What);   // T327: the "?" bubble, after the real ones
		if ((options.empty() && unknown == 0) || (!m_Colony.Target.Name.empty() && m_Colony.Target.What != RadialTarget::Kind::Ground && !m_Gameplay.Find(m_Colony.Target.Name) && !AnyMember(m_Colony.Target.Name)))
		{
			m_Colony.Open = false;   // (what it was over is gone)
			return;
		}
		RadialTarget& target = m_Colony.Target;
		if (target.What == RadialTarget::Kind::Own || target.What == RadialTarget::Kind::Stranger) MemberWhere(target.Name, target.At);   // (it follows a person that walks)
		ImVec2 centre;
		if (!screen(target.At + glm::vec3(0.0f, target.What == RadialTarget::Kind::Ground ? 0.0f : 1.0f, 0.0f), centre)) { m_Colony.Open = false; return; }
		const int real = static_cast<int>(options.size()), n = real + (unknown > 0 ? 1 : 0);
		// what it is over, in the middle on a card: the bubbles go round it, clear of the card
		std::string what;
		switch (target.What)
		{
		case RadialTarget::Kind::Ground: what = L.Text("Here"); break;
		case RadialTarget::Kind::Own: case RadialTarget::Kind::Stranger: what = target.Name; break;
		default: what = Lang::Cap(L.Entity(target.Name)); break;
		}
		const ImVec2 ts = ImGui::CalcTextSize(what.c_str());
		const float bubble = 25.0f;
		const float ring = std::max({ 66.0f, 2.0f * (bubble + 5.0f) * static_cast<float>(n) / 6.2831853f, ts.x * 0.5f + T::Pad + bubble + 6.0f });
		const float reach = ring + bubble + 4.0f;
		// inside the view and off the god's bar and the open book (their boxes of the last frame), so no bubble hides under a panel
		const ObserverState::Box& book = m_Observer.BookBox;
		const float right = book.X1 > book.X0 && book.X0 > ox + 2.0f * reach ? book.X0 - 4.0f : ox + w;
		const float top = std::max(oy, m_Observer.BarBottom + 4.0f);
		centre.x = std::clamp(centre.x, ox + reach, std::max(ox + reach, right - reach));
		centre.y = std::clamp(centre.y, top + reach, std::max(top + reach, oy + h - reach));
		ImGui::SetNextWindowPos(ImVec2(centre.x - reach, centre.y - reach));
		ImGui::SetNextWindowSize(ImVec2(2.0f * reach, 2.0f * reach));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("Radial##colony", nullptr, flags))
		{
			ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // (over the cards and the book, drawn after it)
			const T::Scope theme;
			ImDrawList* rl = ImGui::GetWindowDrawList();
			ImDrawList* fg = ImGui::GetForegroundDrawList();   // (the label card may stick out of the radial's window)
			T::Card(rl, ImVec2(centre.x - ts.x * 0.5f - T::Pad, centre.y - ts.y * 0.5f - 4.0f), ImVec2(centre.x + ts.x * 0.5f + T::Pad, centre.y + ts.y * 0.5f + 4.0f), T::Ochre);
			rl->AddText(ImVec2(centre.x - ts.x * 0.5f, centre.y - ts.y * 0.5f), T::Bone, what.c_str());
			int hovered = -1;
			for (int i = 0; i < n; ++i)
			{
				const float a = -1.5707963f + 6.2831853f * static_cast<float>(i) / static_cast<float>(n);   // (the first at the top, clockwise)
				const ImVec2 c(centre.x + ring * std::cos(a), centre.y + ring * std::sin(a));
				if (i == real)   // T327: what the clan does not know yet: a "?" and how many, never their names; a click does nothing
				{
					ImGui::SetCursorScreenPos(ImVec2(c.x - bubble, c.y - bubble));
					ImGui::InvisibleButton("unknown", ImVec2(2.0f * bubble, 2.0f * bubble));
					const bool hover = ImGui::IsItemHovered() || m_Colony.Hover == "unknown";
					if (hover) hovered = i;
					rl->AddLine(ImVec2(centre.x + (ts.x * 0.5f + T::Pad) * std::cos(a), centre.y + (ts.y * 0.5f + 4.0f) * std::sin(a)), ImVec2(c.x - bubble * std::cos(a), c.y - bubble * std::sin(a)), T::Faded(T::Outline, 0.6f), 2.0f);
					rl->AddCircleFilled(c, bubble + 2.5f, T::Outline, 28);
					rl->AddCircleFilled(c, bubble, hover ? T::HideHover : T::HideRaised, 28);
					rl->AddCircle(c, bubble, hover ? T::OchreHover : T::Ochre, 28, hover ? 3.0f : 2.0f);
					const float fs = ImGui::GetFontSize() * 2.0f;
					const ImVec2 qs = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, "?");
					rl->AddText(ImGui::GetFont(), fs, ImVec2(c.x - qs.x * 0.5f, c.y - qs.y * 0.5f), T::Ochre, "?");
					const std::string count = std::to_string(unknown);   // (the number on a dark tab at the bubble's lower right)
					const ImVec2 cs = ImGui::CalcTextSize(count.c_str());
					const ImVec2 b0(c.x + bubble * 0.45f, c.y + bubble * 0.35f), b1(b0.x + cs.x + 8.0f, b0.y + cs.y + 4.0f);
					rl->AddRectFilled(b0, b1, T::Outline, 4.0f);
					rl->AddRect(b0, b1, T::Ochre, 4.0f, 0, 1.0f);
					rl->AddText(ImVec2(b0.x + 4.0f, b0.y + 2.0f), T::Bone, count.c_str());
					continue;
				}
				const RadialOption& o = options[static_cast<size_t>(i)];
				ImGui::SetCursorScreenPos(ImVec2(c.x - bubble, c.y - bubble));
				ImGui::PushID(o.Id.c_str());
				const bool pressed = ImGui::InvisibleButton("bubble", ImVec2(2.0f * bubble, 2.0f * bubble));
				ImGui::PopID();
				const bool hover = ImGui::IsItemHovered() || m_Colony.Hover == o.Id;
				if (hover) hovered = i;
				if (pressed && !o.Off) m_UiCommands.push_back("colony radial do " + o.Id);   // (through the console: the replay and the log see it as a script does)
				const float r = hover && !o.Off ? bubble + 3.0f : bubble;
				rl->AddLine(ImVec2(centre.x + (ts.x * 0.5f + T::Pad) * std::cos(a), centre.y + (ts.y * 0.5f + 4.0f) * std::sin(a)), ImVec2(c.x - r * std::cos(a), c.y - r * std::sin(a)), T::Faded(T::Outline, 0.6f), 2.0f);
				rl->AddCircleFilled(c, r + 2.5f, T::Outline, 28);
				rl->AddCircleFilled(c, r, o.Off ? T::HideRaised : hover ? T::Parchment : T::Faded(T::Parchment, 0.92f), 28);
				rl->AddCircle(c, r, o.Off ? T::HideLine : hover ? T::OchreHover : T::Ochre, 28, hover ? 3.0f : 2.0f);
				glm::vec2 uv0, uv1;
				if (IconUv(o.Icon, uv0, uv1))
				{
					const float s = r * 0.72f;
					rl->AddImage(TextureOf(*m_IconTexture), ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y), o.Off ? IM_COL32(255, 255, 255, 90) : IM_COL32_WHITE);
				}
				if (o.Arrow)   // the priorities: an ochre arrow on the bubble's edge, up or down
				{
					const ImVec2 p(c.x + r * 0.62f, c.y - r * 0.55f);
					const float s = 7.0f, d = o.Down ? -1.0f : 1.0f;
					const ImVec2 tri[3] = { ImVec2(p.x, p.y - s * d), ImVec2(p.x - s, p.y + s * 0.8f * d), ImVec2(p.x + s, p.y + s * 0.8f * d) };
					rl->AddTriangleFilled(tri[0], tri[1], tri[2], o.Off ? T::HideLine : T::Ochre);
					rl->AddTriangle(tri[0], tri[1], tri[2], T::Outline, 1.5f);
				}
				if (o.Off)   // (a slash: it cannot be used now; the card says why)
					rl->AddLine(ImVec2(c.x - r * 0.55f, c.y + r * 0.55f), ImVec2(c.x + r * 0.55f, c.y - r * 0.55f), T::Faded(T::Bad, 0.8f), 2.5f);
			}
			// the label of the bubble under the mouse, on a card beside it (on the side away from the middle): what it does and, when it cannot, why
			if (hovered >= 0)
			{
				static const RadialOption kUnknown;
				const bool isUnknown = hovered == real;
				const RadialOption& o = isUnknown ? kUnknown : options[static_cast<size_t>(hovered)];
				const float a = -1.5707963f + 6.2831853f * static_cast<float>(hovered) / static_cast<float>(n);
				const ImVec2 c(centre.x + ring * std::cos(a), centre.y + ring * std::sin(a));
				std::vector<std::pair<std::string, ImU32>> lines;
				if (isUnknown)
					lines = { { L.Text("To discover"), T::Ochre }, { unknown == 1 ? L.Text("1 action to discover") : L.Text("{0} actions to discover", unknown), T::Bone },
						{ L.Text("The clan learns them with new knowledge"), T::BoneDim } };
				else lines = { { L.Text(o.Label), T::Ochre }, { L.Text(o.Tip), T::Bone } };
				if (!o.Commands.empty() && !o.Off && o.Commands.front().rfind("game order sel ", 0) == 0) lines.push_back({ Joined(m_Colony.Selection, ", "), T::BoneDim });
				if (o.Off) lines.push_back({ L.Text(o.Why), T::Warn });
				for (const auto& [who, why] : o.Unfit)   // (who of the selection would not go, and why)
					lines.push_back({ L.Text("{0} will not go: {1}", who, why.rfind("in a ", 0) == 0 || why == "fleeing" || why == "on a raid" || why == "away from the camp" ? L.Text(why) : L.Line(why)), T::Warn });
				float tw = 0.0f, th = 0.0f;
				for (const auto& [text, colour] : lines) { const ImVec2 s = ImGui::CalcTextSize(text.c_str()); tw = std::max(tw, s.x); th += s.y + 2.0f; }
				const float cw = tw + 2.0f * T::Pad;
				bool outward = std::cos(a) >= -0.01f;
				if (outward && c.x + bubble + 8.0f + cw > right) outward = false;   // (never over the book: to the other side)
				ImVec2 min(outward ? c.x + bubble + 8.0f : c.x - bubble - 8.0f - cw, c.y - th * 0.5f - T::Pad * 0.5f);
				min.x = std::clamp(min.x, ox + 4.0f, std::max(ox + 4.0f, right - cw - 4.0f));
				const ImVec2 max(min.x + cw, min.y + th + T::Pad);
				fg->AddRectFilled(min, max, T::HidePanel, T::Radius);   // (solid: the world must not show through the words)
				T::Card(fg, min, max, o.Off ? T::Warn : T::Ochre);
				float y = min.y + T::Pad * 0.5f;
				for (const auto& [text, colour] : lines) { fg->AddText(ImVec2(min.x + T::Pad, y), colour, text.c_str()); y += ImGui::CalcTextSize(text.c_str()).y + 2.0f; }
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
	}

	CommandResult EditorApp::ColonyCommand(const std::vector<std::string>& a)
	{
		const std::string sub = a.empty() ? "status" : a[0];
		const Tribe* t = ColonyTribe();
		auto selection = [&]() { PruneColonySelection(); return "selected " + std::to_string(m_Colony.Selection.size()) + (m_Colony.Selection.empty() ? std::string() : ": " + Joined(m_Colony.Selection, ", ")); };
		auto list = [&]()
		{
			static const char* kKinds[] = { "none", "ground", "node", "own", "stranger", "animal", "site", "building" };
			std::string s = std::string("radial over ") + kKinds[static_cast<int>(m_Colony.Target.What)] + (m_Colony.Target.Name.empty() ? std::string() : " " + m_Colony.Target.Name)
				+ " at " + Num(m_Colony.Target.At.x) + " " + Num(m_Colony.Target.At.z) + ":";
			for (const RadialOption& o : RadialOptions(m_Colony.Target))
			{
				s += "\n  " + o.Id + " [" + o.Icon + "] " + o.Label + (o.Off ? " (off: " + o.Why + ")" : " -> " + Joined(o.Commands, "; "));
				for (const auto& [who, why] : o.Unfit) s += "; " + who + " will not go: " + why;
			}
			if (const int unknown = UnknownVerbs(t, m_Colony.Target.What); unknown > 0) s += "\n  unknown [?] " + std::to_string(unknown) + " to discover (does nothing)";   // T327
			return s;
		};
		if (sub == "status")
			return { true, std::string(ColonyScreen() ? "colony screen" : "not the colony's screen") + "; " + selection() + "; " + (m_Colony.Open ? list() : std::string("no radial open")) };
		if (sub == "tab" || sub == "work" || sub == "stock") return ColonyTabCommand(a);   // T308 (ColonyTabs.cpp)
		if (sub == "zone") return ZoneCommand(a);   // T281 (ColonyZones.cpp)
		if (!t) return { false, "no colony: director colony on, and a world with clans" };
		if (sub == "select")
		{
			if (a.size() == 1) return { true, selection() };
			if (a[1] == "clear" || a[1] == "none") { m_Colony.Selection.clear(); if (t->FindMember(m_FreeSelected)) m_FreeSelected.clear(); return { true, selection() }; }
			if (a[1] == "box" && a.size() >= 6)   // a drag's box, in pixels of the viewport
			{
				const bool add = a.size() > 6 && a[6] == "add";
				ColonyBoxSelect({ std::strtof(a[2].c_str(), nullptr), std::strtof(a[3].c_str(), nullptr) }, { std::strtof(a[4].c_str(), nullptr), std::strtof(a[5].c_str(), nullptr) }, add);
				return { true, selection() };
			}
			const bool add = a[1] == "add";   // add <name>: as Shift+click (a second time takes it away)
			std::vector<std::string> names;
			for (size_t i = add ? 2 : 1; i < a.size(); ++i)
			{
				std::stringstream ss(a[i]);
				for (std::string n; std::getline(ss, n, ',');) if (!n.empty()) names.push_back(n);
			}
			if (!add) m_Colony.Selection.clear();
			for (const std::string& n : names)
			{
				const TribeMember* m = t->FindMember(n);
				if (!m || m->Dead || m->Player) return { false, n + " is nobody of your clan; " + selection() };
				const std::string before = m_FreeSelected;
				m_FreeSelected = n;
				ColonyClicked(before, true);
			}
			return { true, selection() };
		}
		if (sub == "radial")
		{
			if (a.size() == 1) return { m_Colony.Open, m_Colony.Open ? list() : std::string("no radial open") };
			if (a[1] == "off") { m_Colony.Open = false; return { true, "radial closed" }; }
			if (a[1] == "do" && a.size() > 2) return RunRadial(a[2]);
			if (a[1] == "hover") { m_Colony.Hover = a.size() > 2 ? a[2] : std::string(); return { true, m_Colony.Hover.empty() ? "no bubble held" : "bubble " + m_Colony.Hover + " held under the mouse" }; }
			RadialTarget target;
			if (a[1] == "ground" && a.size() >= 4)
			{
				const float x = std::strtof(a[2].c_str(), nullptr), z = std::strtof(a[3].c_str(), nullptr);
				target = RadialTargetOf("", glm::vec3(x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f, z));
			}
			else if (a[1] == "at" && a.size() >= 4)   // a right click at a pixel of the viewport, as the mouse makes it
			{
				const glm::vec2 size(static_cast<float>(std::max(m_ViewportW, 1u)), static_cast<float>(std::max(m_ViewportH, 1u)));
				const Ray ray = ScreenRay(m_Camera, glm::vec2(std::strtof(a[2].c_str(), nullptr), std::strtof(a[3].c_str(), nullptr)) / size, size.x / size.y);
				glm::vec3 ground(0.0f);
				const std::string picked = PickSelectable(ray);
				if (picked.empty() && !(m_World.IsBuilt() && GroundHit(m_World.GetTerrain(), ray, ground))) return { false, "nothing there" };
				target = RadialTargetOf(picked, ground);
			}
			else
			{
				std::string name;
				for (size_t i = 1; i < a.size(); ++i) name += (name.empty() ? "" : " ") + a[i];
				for (char& c : name) if (c == '_') c = ' ';
				target = RadialTargetOf(name, glm::vec3(0.0f));
				if (target.What == RadialTarget::Kind::None) return { false, "nothing called " + name + " has a radial" };
			}
			OpenRadial(target);
			if (!m_Colony.Open) return { false, "nothing to do there" };
			return { true, list() };
		}
		return { false, "usage: colony status | select [<name[,name...]> | add <name> | box <x0> <y0> <x1> <y1> [add] | clear] | radial [<target> | ground <x> <z> | at <px> <py> | do <id> | hover [id] | off] | tab [work|stock|research|field|off] | zone start|point <x> <z>|undo|close|cancel|crop <crop|auto>|plan <plan>|confirm|select <id|off>|cursor <x> <z> | work fold|unfold <category> | work step <person> <what> up|down | work reset <person> | stock pick|set|off ..." };
	}
}
