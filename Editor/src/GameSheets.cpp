// The sheets of the clan book that look at a clan and the world rather than act for the player (docs/JUGAR.md, "La interfaz"): the village at a
// glance, the job board, its households, the other clans, what it knows and the eras, the map of areas and the story so far. Each takes the clan it
// shows, so the free mode's observer (ObserverView.cpp) draws the same sheets for any clan; `playing` says whether its buttons may queue `game`
// commands (the player's clan in the game) or are left out. Each says what is going on with an icon, a bar and a number, and why when something is
// short. Every word goes through Lang (Lang.h); values sit beside their bars, never over them (dark-on-colour reads, light-on-colour does not).
#include "EditorApp.h"
#include "GameTheme.h"
#include "GB/Gameplay/WorldResources.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <optional>
#include <set>

namespace GB::Editor
{
	namespace
	{
		namespace T = GameTheme;

		// A bar of its own width and the value beside it, after a label column; `note`, when given, goes under it (red when the bar is).
		void Meter(const std::string& label, float v01, const std::string& value, ImU32 colour, const std::string& note = std::string())
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label.c_str());
			if (ImGui::CalcTextSize(label.c_str()).x > 112.0f) ImGui::SetCursorPosX(120.0f);   // (a long name keeps its line; the bar goes under it)
			else ImGui::SameLine(120.0f);
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float width = std::clamp(ImGui::GetContentRegionAvail().x * 0.4f, 70.0f, 150.0f), height = T::BarH;
			const float y = at.y + 3.0f;
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width, y + height), T::HideRaised, 3.0f);
			dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + width * std::clamp(v01, 0.0f, 1.0f), y + height), colour, 3.0f);
			dl->AddRect(ImVec2(at.x, y), ImVec2(at.x + width, y + height), T::HideLine, 3.0f);
			ImGui::Dummy(ImVec2(width, height + 6.0f));
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextWrapped("%s", value.c_str());
			if (!note.empty())
			{
				ImGui::SetCursorPosX(120.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colour == T::Bad ? T::Bad : T::BoneDim));
				ImGui::TextWrapped("%s", note.c_str());
				ImGui::PopStyleColor();
			}
		}

		// A small rounded label in line with the text (an era, a stance, a pact): `fill` behind, `ink` for the letters.
		void Chip(const std::string& text, ImU32 fill, ImU32 ink = T::Ink)
		{
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(at, ImVec2(at.x + size.x + 10.0f, at.y + size.y + 4.0f), fill, 4.0f);
			dl->AddText(ImVec2(at.x + 5.0f, at.y + 2.0f), ink, text.c_str());
			ImGui::Dummy(ImVec2(size.x + 10.0f, size.y + 4.0f));
		}

		// A square of a clan's colour.
		void Swatch(const glm::vec3& c, float size = 14.0f)
		{
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + size, at.y + 2.0f + size), ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 1.0f)), 3.0f);
			ImGui::GetWindowDrawList()->AddRect(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + size, at.y + 2.0f + size), T::Outline, 3.0f);
			ImGui::Dummy(ImVec2(size, size + 2.0f));
		}

		// A heading with a line under it (the parts of a sheet).
		void Heading(const std::string& text)
		{
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		void Dim(const std::string& text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::BoneDim));
			ImGui::TextWrapped("%s", text.c_str());
			ImGui::PopStyleColor();
		}

		ImU32 StanceColour(const std::string& stance)
		{
			if (stance == "allied" || stance == "friendly") return T::Good;
			if (stance == "hostile") return T::Bad;
			if (stance == "wary" || stance == "truce") return T::Warn;
			return T::BoneDim;
		}

		// a number with a printf format; Spanish wants a decimal comma (Lang::Num for the plain ones)
		std::string Number(float v, const char* format, const Lang& L)
		{
			char s[32];
			std::snprintf(s, sizeof s, format, static_cast<double>(v));
			std::string out = s;
			if (L.Code() == "es") for (char& c : out) if (c == '.') c = ',';
			return out;
		}
	}

	// ------------------------------------------------------------------ the village (Tab, U)
	void EditorApp::DrawVillageSheet(const Tribe& t)
	{
		const Lang& L = m_Lang;
		const SocietyRules& life = t.Config.Society;
		int alive = 0, children = 0, elders = 0;
		for (const TribeMember& m : t.Members())
		{
			if (m.Dead) continue;
			++alive;
			const Society::Stage stage = t.People().Find(m.Name) ? t.People().StageOf(life, m.Name) : Society::Stage::Adult;
			children += stage == Society::Stage::Child ? 1 : 0;
			elders += stage == Society::Stage::Elder ? 1 : 0;
		}

		// the era and the ladder of the eras
		const std::string era = t.Era();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(L.Text("Era").c_str());
		ImGui::SameLine(120.0f);
		Chip(L.Name("eraname", t.EraName().empty() ? std::string("Stone Age") : t.EraName(), t.EraName().empty() ? std::string("Stone Age") : t.EraName()), T::Ochre);
		ImGui::SameLine();
		ImGui::TextDisabled("%s", L.Text("{0} households", static_cast<int>(t.Households().size())).c_str());
		if (const TechTree* tree = t.Knowledge().Techs(); tree && !tree->Rules.Eras.empty())
		{
			ImGui::SetCursorPosX(120.0f);
			bool reached = true;
			for (size_t i = 0; i < tree->Rules.Eras.size(); ++i)
			{
				const EraDef& e = tree->Rules.Eras[i];
				if (i) { ImGui::SameLine(0.0f, 2.0f); ImGui::TextDisabled(">"); ImGui::SameLine(0.0f, 2.0f); }
				const bool here = e.Id == era || (era.empty() && i == 0);
				Chip(L.Name("era", e.Id, e.Name), here ? T::Ochre : reached ? T::Good : T::HideRaised, here || reached ? T::Ink : T::BoneDim);
				if (here) reached = false;
				if (ImGui::IsItemHovered())
				{
					std::string keys;
					for (const std::string& k : e.Key) keys += (keys.empty() ? "" : ", ") + L.Name("tech", k, t.Knowledge().Name(k));
					ImGui::SetTooltip("%s", (L.Name("era", e.Id, e.Name) + ": " + (keys.empty() ? L.Text("where every clan starts") : L.Text("opened by {0}", keys))).c_str());
				}
			}
		}

		Heading(L.Text("How the village is"));
		// people
		const int most = std::max(1, life.Births.MaxMembers);
		Meter(L.Text("People"), static_cast<float>(alive) / static_cast<float>(most), L.Text("{0} alive ({1} children, {2} elders)", alive, children, elders), T::Good);
		// beds
		const int beds = t.Beds();
		Meter(L.Text("Beds"), alive ? static_cast<float>(beds) / static_cast<float>(alive) : 1.0f, L.Text("{0} beds for {1}", beds, alive),
			beds >= alive ? T::Good : beds * 4 >= alive * 3 ? T::Warn : T::Bad, beds >= alive ? std::string() : L.Text("{0} sleep in the open: the clan will build more", alive - beds));
		// food
		const float days = t.FoodDays();
		const float wantDays = std::max(0.5f, t.Config.Work.Jobs.SurvivalDays);   // (the days of food the clan itself wants in hand)
		Meter(L.Text("Food"), days / (2.0f * wantDays), L.Text("{0} days", Number(days, "%.1f", L)), days >= wantDays ? T::Good : days >= 0.5f * wantDays ? T::Warn : T::Bad,
			days >= wantDays ? std::string() : L.Text("short of food: fewer children are born and the clan gathers and farms more"));
		// water: the wells of the home ground and how many drink from each
		{
			int wells = 0;
			float water = 0.0f, room = 0.0f;
			const auto def = m_Interactives.Defs.find("well");
			float full = 30.0f;
			if (def != m_Interactives.Defs.end())
				for (const InteractiveAttribute& a : def->second.Attributes) if (a.Name == "Water") full = a.Max;
			for (const auto& w : m_Gameplay.WithTag("Interactive.Well"))
			{
				const EntityId id = m_SceneEditor.World().FindByName(*w.Name);
				if (id == NullEntity) continue;
				const glm::vec3 at = m_SceneEditor.World().WorldMatrix(id)[3];
				if (glm::distance(glm::vec2(at.x, at.z), t.Config.Economy.Camp) > 70.0f) continue;
				++wells;
				if (const AbilitySystemComponent* asc = m_Gameplay.Find(*w.Name)) water += asc->Attribute("Water");
				room += full;
			}
			const int per = t.Config.Economy.MembersPerWell;
			const bool crowded = per > 0 && alive > wells * per;
			Meter(L.Text("Water"), room > 0.0f ? water / room : 0.0f, L.Text(wells == 1 ? "{0} well, {1} people each" : "{0} wells, {1} people each", wells, wells ? (alive + wells - 1) / wells : alive),
				wells == 0 ? T::Bad : crowded ? T::Warn : T::Good,
				wells == 0 ? L.Text("no well near the camp") : crowded ? L.Text("more than {0} drink from each: the clan will dig another", per) : std::string());
		}
		// fields
		{
			const auto& fields = t.Economy().Fields;
			int sown = 0, ripe = 0;
			for (const Field& f : fields)
				for (const FieldPlot& p : f.Plots)
				{
					sown += p.State == "sown" || p.State == "sprout" ? 1 : 0;
					ripe += p.State == "grown" ? 1 : 0;
				}
			const int wanted = t.Config.Econ.Farming.FieldsFor(alive);
			Meter(L.Text("Fields"), wanted ? static_cast<float>(fields.size()) / static_cast<float>(wanted) : 1.0f,
				L.Text("{0} of {1} ({2} growing, {3} ripe)", static_cast<int>(fields.size()), wanted, sown, ripe), fields.empty() ? T::BoneDim : T::Good,
				fields.empty() ? L.Text("no fields yet: the clan needs tillage, seeds and a hoe") : std::string());
		}
		// store
		{
			const int used = t.Store().UsedSlots(), slots = std::max(1, t.Store().Slots());
			Meter(L.Text("Store"), static_cast<float>(used) / static_cast<float>(slots), L.Text("{0} of {1} places, up to {2} of each", used, slots, t.StoreCap()),
				used * 10 >= slots * 9 ? T::Warn : T::Good, used * 10 >= slots * 9 ? L.Text("almost full: the clan will build a store") : std::string());
		}

		Heading(L.Text("What the clan wants to build next"));
		const std::vector<BuildingNeed> needs = t.PublicBuildingNeeds();
		if (needs.empty()) ImGui::TextDisabled("%s", L.Text("nothing for now (or every site it may open is open)").c_str());
		for (size_t i = 0; i < needs.size() && i < 5; ++i)
		{
			const BuildingNeed& n = needs[i];
			const auto def = m_Buildings.Defs.find(n.Building);
			ImGui::Bullet();
			ImGui::SameLine();
			ImGui::TextUnformatted(Lang::Cap(L.Name("building", n.Building, def != m_Buildings.Defs.end() ? def->second.Name : std::string())).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", L.Line(n.Why).c_str());
		}

		Heading(L.Text("Rites and beliefs"));
		const SocialState& social = t.Social();
		if (social.Rite.Active())
		{
			const auto def = t.Config.Social.Rites.find(social.Rite.Rite);
			const std::string name = L.Name("rite", social.Rite.Rite, def != t.Config.Social.Rites.end() ? def->second.Name : std::string());
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "%s", L.Text(social.Rite.Hold < 0.0 ? "Now: {0} (the clan is gathering)" : "Now: {0} (under way)", name).c_str());
		}
		else ImGui::TextDisabled("%s", L.Text("No rite now").c_str());
		// the feasts of the seasons, and how often each comes
		std::string feasts;
		for (const auto& [id, d] : t.Config.Social.Rites)
			if (!d.Season.empty())
				feasts += (feasts.empty() ? "" : ", ") + L.Name("rite", id, d.Name) + " (" + L.Name("season", d.Season)
					+ (d.Every > 1.0f ? ", " + L.Text("every {0} years", static_cast<int>(d.Every)) : std::string()) + ")";
		if (!feasts.empty()) Dim(L.Text("Feasts of the year: {0}", feasts));
		// what the clan believes: a bar for each belief, the strongest marked
		const std::string dominant = social.Dominant();
		for (const auto& [id, v] : social.Beliefs)
		{
			const auto def = t.Config.Social.Beliefs.Systems.find(id);
			std::string name = L.Name("belief", id, def != t.Config.Social.Beliefs.Systems.end() ? def->second.Name : std::string());
			if (const size_t cut = name.find(" ("); cut != std::string::npos) name = name.substr(0, cut);   // ("Animism (the spirits of...)": the name alone)
			Meter(Lang::Cap(name), v, Number(v * 100.0f, "%.0f %%", L) + (id == dominant ? "  " + L.Text("(what the clan holds most)") : std::string()), id == dominant ? T::Ochre : T::BoneDim);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("How much of the clan holds this belief: rites, omens and the shaman move it").c_str());
		}
		if (!social.Shaman.empty()) ImGui::TextDisabled("%s", L.Text("Shaman: {0}", social.Shaman).c_str());
	}

	// What a building does once it stands, a line per effect, in the player's words: its beds and rest, warmth, stores, the workshop and what is made
	// there, defence, healing, a meeting place, a well, the buildings it replaces. Read from its data (generic provisions, docs/PROGRESION.md).
	std::vector<std::string> EditorApp::BuildingDoes(const BuildingDef& def) const
	{
		const Lang& L = m_Lang;
		std::vector<std::string> out;
		auto name = [&](const std::string& id) { const auto d = m_Buildings.Defs.find(id); return L.Name("building", id, d != m_Buildings.Defs.end() ? d->second.Name : std::string()); };
		if (def.Shelter > 0)
		{
			std::string s = L.Text("{0} beds: who sleeps inside rests under a roof", def.Shelter);
			if (def.Rest > 0.0f) s += "; " + L.Text("rests faster");
			if (def.Warmth > 0.0f) s += "; " + L.Text("keeps warm at night");
			out.push_back(s);
		}
		else if (def.Warmth > 0.0f) out.push_back(L.Text("gives warmth to whoever is near ({0} m)", L.Num(def.Reach(), 0)));
		if (!def.Station.empty())
		{
			std::string made;
			for (const std::string& id : m_Recipes.Ids())
				if (const Recipe* r = m_Recipes.Find(id); r && r->Station == def.Station)
				{
					const ItemDef* d = m_Items.Find(r->Output);
					const std::string item = L.Name("item", r->Output, d && !d->Name.empty() ? d->Name : std::string());
					if (made.find(item) == std::string::npos) made += (made.empty() ? "" : ", ") + item;
				}
			out.push_back(made.empty() ? L.Text("a workshop ({0})", L.Name("station", def.Station)) : L.Text("a workshop: here the clan makes {0}", made));
		}
		if (!def.Store.empty()) out.push_back(L.Text("a second store: what is gathered nearby is kept here"));
		if (def.StoreCap > 0) out.push_back(L.Text("the clan's store holds {0} more of each thing", def.StoreCap));
		if (def.Keep < 1.0f) out.push_back(L.Text("the food in the store goes off {0} % slower", static_cast<int>(std::lround((1.0f - def.Keep) * 100.0f))));
		if (def.Defense > 0.0f) out.push_back(L.Text("defence {0}: raiders think twice and the camp's defenders fight harder", L.Num(def.Defense, def.Defense == std::floor(def.Defense) ? 0 : 1)));
		if (def.Healing > 0.0f) out.push_back(L.Text("wounds heal {0} times as fast near it", L.Num(1.0f + def.Healing, 1)));
		if (def.Culture > 0.0f) out.push_back(L.Text("a meeting place: company for whoever is near; rites gather here"));
		if (def.Well) out.push_back(L.Text("a well: water for the clan"));
		if (def.Trade > 0.0f) out.push_back(L.Text("a trade post: the clan meets the other clans more often to trade"));   // V4
		for (const auto& [value, threshold] : def.Values)   // V4: whose culture raises it
			out.push_back(L.Text(threshold > 0 ? "only a clan that values {0} raises it" : "only a clan that scorns {0} raises it", L.Name("value", value)));
		if (!def.Supersedes.empty())
		{
			std::string old;
			for (const std::string& b : def.Supersedes) old += (old.empty() ? "" : ", ") + name(b);
			out.push_back(L.Text("once the clan can raise it, it stops planning {0}", old));
		}
		if (out.empty()) out.push_back(L.Text("no lasting effect"));
		return out;
	}

	// A job of the board in the player's words: "Bayas" (gather), "Hacer hacha de piedra", "Refugio 1 (construir)", "Ciervo", "Avivar el fuego".
	std::string EditorApp::JobName(const Job& j) const
	{
		const Lang& L = m_Lang;
		const ItemDef* d = m_Items.Find(j.Target);
		const std::string item = L.Name("item", j.Target, d && !d->Name.empty() ? d->Name : std::string());
		if (j.Kind == "gather") return L.Text("Gather {0}", item);
		if (j.Kind == "craft" || j.Kind == "make") return L.Text("Make {0}", item);
		if (j.Kind == "build" || j.Kind == "deliver") return L.Entity(j.Target) + " (" + L.Name("kind", j.Kind) + ")";
		if (j.Kind == "hunt") return L.Text("Hunt {0}", L.Name("animal", j.Target.rfind("Animal.", 0) == 0 ? j.Target.substr(7) : j.Target));
		if (j.Kind == "stoke" || j.Kind == "fire") return Lang::Cap(L.Name("kind", j.Kind));
		return j.Id;
	}

	// ------------------------------------------------------------------ the job board (J): by kind, an icon, have/want, who holds it
	void EditorApp::DrawBoardSheet(const Tribe& t, bool playing)
	{
		const Lang& L = m_Lang;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		const std::vector<const Job*> jobs = t.Board().Ordered();
		if (jobs.empty()) { ImGui::TextDisabled("%s", L.Text("the board is empty: the clan has all it wants for now").c_str()); return; }
		Dim(L.Text("What the clan needs, the most pressing first in each group. Hover a job to see why."));
		struct Group { const char* Id; const char* Label; };
		static const Group groups[] = { { "food", "Food" }, { "materials", "Materials" }, { "craft", "To make" }, { "build", "Building" }, { "hunt", "Hunting" }, { "fire", "Fire" }, { "other", "Other" } };
		auto groupOf = [&](const Job& j) -> std::string
		{
			if (j.Kind == "gather") return t.IsFood(j.Target) ? "food" : "materials";
			if (j.Kind == "craft" || j.Kind == "make") return "craft";
			if (j.Kind == "build" || j.Kind == "deliver") return "build";
			if (j.Kind == "hunt") return "hunt";
			if (j.Kind == "stoke" || j.Kind == "fire") return "fire";
			return "other";
		};
		// the columns as shares of the sheet; a narrow sheet (the observer's) drops the little bar and keeps the numbers
		const float width = ImGui::GetContentRegionAvail().x;
		const bool narrow = width < 380.0f;
		const float barX = narrow ? width - 110.0f : std::max(150.0f, width * 0.48f), slotsX = barX + (narrow ? 50.0f : 90.0f);
		std::set<const Job*> urgent;   // the three most pressing of the whole board
		for (size_t i = 0; i < jobs.size() && i < 3; ++i) urgent.insert(jobs[i]);
		for (const Group& g : groups)
		{
			bool header = false;
			for (const Job* j : jobs)
			{
				if (groupOf(*j) != g.Id) continue;
				if (!header)
				{
					header = true;
					std::string title = L.Text(g.Label);
					if (std::string(g.Id) == "food") title += "  " + L.Text("({0} days in hand)", Number(t.FoodDays(), "%.1f", L));
					ImGui::Spacing();
					ImGui::SeparatorText(title.c_str());
				}
				ImGui::PushID(j->Id.c_str());
				// the icon and the name of what it is about
				std::string icon, name;
				const std::string target = j->Target;
				if (j->Kind == "gather" || j->Kind == "craft" || j->Kind == "make")
				{
					const ItemDef* d = m_Items.Find(target);
					icon = target;
					name = Lang::Cap(L.Name("item", target, d && !d->Name.empty() ? d->Name : std::string()));
					if (j->Kind != "gather") name = L.Text("Make {0}", L.Name("item", target, d && !d->Name.empty() ? d->Name : std::string()));
				}
				else if (j->Kind == "build" || j->Kind == "deliver") { icon = "build"; name = L.Entity(target) + " (" + L.Name("kind", j->Kind) + ")"; }
				else if (j->Kind == "hunt") { icon = "hunt"; name = Lang::Cap(L.Name("animal", target.rfind("Animal.", 0) == 0 ? target.substr(7) : target)); }
				else if (j->Kind == "stoke" || j->Kind == "fire") { icon = "fire"; name = Lang::Cap(L.Name("kind", j->Kind)); }
				else name = j->Id;
				if (!Icon(icon, 20.0f)) ImGui::Dummy(ImVec2(20.0f, 20.0f));
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				if (urgent.count(j)) { ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Ochre), "!"); ImGui::SameLine(0.0f, 4.0f); }
				ImGui::TextUnformatted(name.c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Lang::Cap(L.Line(j->Why)).c_str());
				// how far it is: the store against what the clan wants (gather), how many lack the tool (make)
				ImGui::SameLine(barX);
				if (j->Kind == "gather")
				{
					const int have = t.Store().Count(target), want = have + static_cast<int>(std::ceil(j->Amount));
					if (!narrow)
					{
						const ImVec2 at = ImGui::GetCursorScreenPos();
						const float v = want > 0 ? static_cast<float>(have) / static_cast<float>(want) : 1.0f, y = at.y + 5.0f;
						ImDrawList* dl = ImGui::GetWindowDrawList();
						dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + 40.0f, y + 8.0f), T::HideRaised, 2.0f);
						dl->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + 40.0f * std::clamp(v, 0.0f, 1.0f), y + 8.0f), T::Level(v), 2.0f);
						ImGui::Dummy(ImVec2(40.0f, 16.0f));
						ImGui::SameLine(0.0f, 6.0f);
					}
					ImGui::TextDisabled("%d/%d", have, want);
				}
				else if (j->Kind == "craft") ImGui::TextDisabled("%s", L.Text("{0} lack it", static_cast<int>(j->Amount)).c_str());
				else ImGui::Dummy(ImVec2(80.0f, 1.0f));
				// who holds it, and what the player can do
				ImGui::SameLine(slotsX);
				std::string who;
				bool mine = false;
				for (const JobClaim& c : j->Claims) { who += (who.empty() ? "" : ", ") + c.Member; mine = mine || (playing && c.Member == m_Game.Player); }
				const bool full = static_cast<int>(j->Claims.size()) >= j->Slots;
				ImGui::TextDisabled("%d/%d", static_cast<int>(j->Claims.size()), j->Slots);
				if (!who.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", who.c_str());
				if (playing)
				{
					ImGui::SameLine();
					if (mine) { if (ImGui::SmallButton((L.Text("Leave it") + "###release").c_str())) run("game release"); }
					else
					{
						ImGui::BeginDisabled(full);
						{
							std::optional<GameTheme::PrimaryScope> primary;
							if (!full && urgent.count(j)) primary.emplace();
							if (ImGui::SmallButton((L.Text("Take") + "###claim").c_str())) run("game claim " + j->Id);
						}
						ImGui::EndDisabled();
						if (full && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", L.Text("No room: {0} already hold it", who).c_str());
					}
				}
				ImGui::PopID();
			}
		}
	}

	// ------------------------------------------------------------------ the households (Y)
	void EditorApp::DrawHomesSheet(const Tribe& t, bool playing)
	{
		const Lang& L = m_Lang;
		const auto& households = t.Households();
		if (households.empty()) { ImGui::TextDisabled("%s", L.Text("no households yet: they form as couples pair and children are born").c_str()); return; }
		int housed = 0;
		for (const Tribe::Household& h : households) housed += h.Home.empty() ? 0 : 1;
		ImGui::TextUnformatted(L.Text("{0} households, {1} with a house of their own", static_cast<int>(households.size()), housed).c_str());
		ImGui::TextDisabled("%s", L.Text("property: {0}", L.Name("property", t.Config.Economy.Property)).c_str());
		for (const Tribe::Household& h : households)
		{
			ImGui::PushID(h.Id.c_str());
			ImGui::Spacing();
			const ImVec2 top = ImGui::GetCursorScreenPos();
			ImGui::BeginGroup();
			ImGui::Indent(T::Pad);
			// the family, by its first member, and its house
			ImGui::TextUnformatted(L.Text("The {0} family", h.Id).c_str());
			ImGui::SameLine();
			if (h.Home.empty()) Chip(L.Text("no house"), T::Warn);
			else Chip(L.Entity(h.Home), T::Good);
			if (!h.Home.empty() && playing)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton((L.Text("show") + "###show").c_str())) m_UiCommands.push_back("pick " + h.Home);
			}
			// its members: the trade of each, children and elders
			std::string people;
			for (const std::string& name : h.Members)
			{
				const TribeMember* m = t.FindMember(name);
				std::string who = name;
				if (m && t.People().Find(name))
				{
					const Society::Stage stage = t.People().StageOf(t.Config.Society, name);
					who += stage == Society::Stage::Child ? " (" + L.Text("child") + ")" : stage == Society::Stage::Elder ? " (" + L.Text("elder") + ", " + L.Name("role", m->Role) + ")" : " (" + L.Name("role", m->Role) + ")";
				}
				people += (people.empty() ? "" : ", ") + who;
			}
			Dim(people);
			// its pantry: the food it keeps at home against what it may keep
			if (const Inventory* pantry = t.Pantry(h.Id); t.Config.Economy.Property != "communal" || (pantry && pantry->UsedSlots() > 0))
			{
				int food = 0;
				std::string what;
				if (pantry)
					for (const ItemSlot& s : pantry->Stacks())
						if (s.Count > 0 && t.IsFood(s.Id)) { food += s.Count; what += (what.empty() ? "" : ", ") + std::to_string(s.Count) + " " + L.Name("item", s.Id); }
				const int room = std::max(1, t.Config.Economy.PantryPerMember * static_cast<int>(h.Members.size()));
				Meter(L.Text("Pantry"), static_cast<float>(food) / static_cast<float>(room), L.Text("{0} of {1} food", food, room), food * 4 >= room ? T::Good : food > 0 ? T::Warn : T::Bad, what);
			}
			ImGui::Unindent(T::Pad);
			ImGui::EndGroup();
			const ImVec2 bottom = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRect(ImVec2(top.x, top.y - 3.0f), ImVec2(top.x + ImGui::GetContentRegionAvail().x, bottom.y + 4.0f), T::HideLine, T::Radius);
			ImGui::Spacing();
			ImGui::PopID();
		}
	}

	// ------------------------------------------------------------------ the other clans (O), seen from `t`'s clan
	void EditorApp::DrawClansSheet(const Tribe& t)
	{
		const Lang& L = m_Lang;
		ClanId mine = -1;
		for (const auto& c : m_Clans.All()) if (c->Folk == &t) mine = c->Id;
		if (m_Clans.Count() < 2 || mine < 0) { ImGui::TextDisabled("%s", L.Text("the clan is alone in the world").c_str()); return; }
		Dim(L.Text("How the clan stands with each of the others. The bar is the feeling: to the left enmity, to the right friendship."));
		for (const auto& c : m_Clans.All())
		{
			if (!c->Folk || c->Id == mine) continue;
			ImGui::PushID(static_cast<int>(c->Id));
			int alive = 0;
			for (const TribeMember& m : c->Folk->Members()) alive += m.Dead ? 0 : 1;
			ImGui::Spacing();
			const ImVec2 top = ImGui::GetCursorScreenPos();
			ImGui::BeginGroup();
			ImGui::Indent(T::Pad);
			Swatch(m_Clans.ColorOf(c->Id));
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(L.Name("clan", c->Name, c->Name).c_str());
			ImGui::SameLine();
			const std::string eraName = c->Folk->EraName().empty() ? std::string("Stone Age") : c->Folk->EraName();
			ImGui::TextDisabled("%s", L.Text("{0} people, {1}", alive, L.Name("eraname", eraName, eraName)).c_str());
			if (!m_Clans.Met(mine, c->Id)) ImGui::TextDisabled("%s", L.Text("not met yet").c_str());
			else
			{
				const std::string stance = m_Clans.Stance(mine, c->Id);
				Chip(L.Name("stance", stance), StanceColour(stance));
				if (const ClanRelation* r = m_Clans.Relation(mine, c->Id))
					for (const Pact& p : r->Pacts) { ImGui::SameLine(); Chip(L.Name("pact", p.Kind), p.Kind == "alliance" ? T::Good : T::Warn); }
				const float attitude = m_Clans.Attitude(mine, c->Id), trust = m_Clans.Trust(mine, c->Id);
				Meter(L.Text("Feeling"), 0.5f + 0.5f * std::clamp(attitude, -1.0f, 1.0f), L.Text("{0}, trust {1}", Number(attitude, "%+.2f", L), Number(trust * 100.0f, "%.0f %%", L)),
					attitude > 0.15f ? T::Good : attitude < -0.15f ? T::Bad : T::Warn);
				if (const ClanRelation* r = m_Clans.Relation(mine, c->Id))
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(r->Raids > 0 ? T::Bad : T::BoneDim));
					ImGui::TextWrapped("%s", L.Text("met {0} times: {1} trades, {2} gifts, {3} lessons, {4} marriages; {5} thefts, {6} raids", r->Encounters, r->Trades, r->Gifts, r->Lessons,
						r->Marriages, r->Thefts, r->Raids).c_str());
					ImGui::PopStyleColor();
					if (!r->Recent.empty()) Dim(L.Text("lately: {0}", L.Line(r->Recent.back())));
				}
			}
			ImGui::Unindent(T::Pad);
			ImGui::EndGroup();
			const ImVec2 bottom = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRect(ImVec2(top.x, top.y - 3.0f), ImVec2(top.x + ImGui::GetContentRegionAvail().x, bottom.y + 4.0f), T::HideLine, T::Radius);
			ImGui::Spacing();
			ImGui::PopID();
		}
	}

	// ------------------------------------------------------------------ what the clan knows, and the eras (K)
	void EditorApp::DrawTechSheet(const Tribe& t)
	{
		const Lang& L = m_Lang;
		const ClanKnowledge& k = t.Knowledge();
		const TechTree* tree = k.Techs();
		if (!tree) { ImGui::TextDisabled("%s", L.Text("no technologies in this world").c_str()); return; }
		const std::set<std::string>& known = k.Clan();
		{   // the tree of knowledge: the same technologies as a graph, over the window (the player's clan in the game, the observed one in the free mode)
			const GameTheme::PrimaryScope primary;
			const bool mine = m_Game.Active && &t == &PlayTribe();
			const std::string key = m_Controls.Keys.count("tech") ? " [" + L.Key(m_Controls.Keys.at("tech")) + "]" : std::string();
			if (ImGui::Button((L.Text("Open the tree of knowledge") + (mine ? key : std::string()) + "###tree").c_str(), ImVec2(-FLT_MIN, 28.0f)))
				m_UiCommands.push_back(mine ? "game tech" : "observer tree");
		}
		Heading(L.Text("Being worked out"));
		DrawResearch(k);

		// every technology by era: known (green, +), being learnt (ochre, ~), open to find (light: what it needs is known) or locked (dim, with what
		// it still needs); the mark says it as well as the colour; the hover says what it opens
		std::set<std::string> learning;
		for (const ClanKnowledge::ResearchLine& r : k.Research()) learning.insert(r.Tech);
		std::map<int, std::vector<const TechDef*>> byEra;
		for (const auto& [id, def] : tree->Techs) byEra[def.Era].push_back(&def);
		int done = 0;
		for (const auto& [id, def] : tree->Techs) done += known.count(id) ? 1 : 0;
		Heading(L.Text("What the clan knows ({0} of {1})", done, static_cast<int>(tree->Techs.size())));
		Dim(L.Text("+ known, ~ being learnt, light: can be found now, dim: needs others first. Hover one to see what it opens."));
		for (const auto& [era, defs] : byEra)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("%s", L.Text("Era {0}", era).c_str());
			float line = 0.0f;
			const float width = ImGui::GetContentRegionAvail().x;
			for (const TechDef* d : defs)
			{
				const bool have = known.count(d->Id) > 0, now = learning.count(d->Id) > 0;
				std::string missing;
				for (const std::string& r : d->Requires) if (!known.count(r)) missing += (missing.empty() ? "" : ", ") + L.Name("tech", r, k.Name(r));
				const ImU32 fill = have ? T::Good : now ? T::Ochre : missing.empty() ? T::Bone : T::HideRaised;
				const ImU32 ink = have || now || missing.empty() ? T::Ink : T::BoneDim;
				const std::string name = (have ? "+ " : now ? "~ " : "") + Lang::Cap(L.Name("tech", d->Id, d->Name));
				// its icon before the chip (dim while it needs others first), when the atlas has it
				glm::vec2 uv0, uv1;
				const bool icon = IconUv("tech." + d->Id, uv0, uv1);
				const float iconW = icon ? ImGui::GetFrameHeight() + 2.0f : 0.0f;
				const float w = ImGui::CalcTextSize(name.c_str()).x + 10.0f + iconW;
				if (line > 0.0f && line + w + 4.0f < width) ImGui::SameLine(0.0f, 4.0f);
				else line = 0.0f;
				if (icon)
				{
					const float s = ImGui::GetFrameHeight();
					const ImVec2 at = ImGui::GetCursorScreenPos();
					ImGui::Dummy(ImVec2(s, s));
					ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_IconTexture->GetID())), at, ImVec2(at.x + s, at.y + s), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y),
						have || now || missing.empty() ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 110));
					ImGui::SameLine(0.0f, 2.0f);
				}
				Chip(name, fill, ink);
				line += w + 4.0f;
				if (ImGui::IsItemHovered())
				{
					std::string opens;
					for (const std::string& b : d->Buildings) { const auto def = m_Buildings.Defs.find(b); opens += (opens.empty() ? "" : ", ") + L.Name("building", b, def != m_Buildings.Defs.end() ? def->second.Name : std::string()); }
					for (const std::string& r : d->Recipes)
						if (const Recipe* rec = m_Recipes.Find(r))
						{
							const ItemDef* item = m_Items.Find(rec->Output);
							opens += (opens.empty() ? "" : ", ") + L.Name("item", rec->Output, item && !item->Name.empty() ? item->Name : std::string());
						}
					std::string tip = name + ": " + (have ? L.Text("known") : now ? L.Text("being learnt") : missing.empty() ? L.Text("can be found now") : L.Text("needs {0}", missing));
					if (!opens.empty()) tip += "\n" + L.Text("opens: {0}", opens);
					ImGui::SetTooltip("%s", tip.c_str());
				}
			}
		}
	}

	// ------------------------------------------------------------------ the map of areas (M)
	void EditorApp::DrawMapSheet()
	{
		const Lang& L = m_Lang;
		MapAreas& m = m_MapAreas;
		auto run = [&](const std::string& command) { m_UiCommands.push_back(command); };
		Dim(L.Text("While this sheet is open the areas are painted on the ground round what the camera looks at. From above you see more."));
		if (ImGui::Button((L.Text(m_Game.View == "top" ? "Back to your character" : "View from above") + "###view").c_str(), ImVec2(-FLT_MIN, 0.0f)))
			run(m_Game.View == "top" ? "game view follow" : "game view top");

		Heading(L.Text("What is painted"));
		struct Layer { const char* Label; const char* Command; bool On; const char* Tip; };
		const Layer layers[] = {
			{ "Land of each clan", "game map territory", m.Territory, "Every clan's land in its colour: take from another clan's land and it is theft" },
			{ "Home ground", "game map home", m.Home, "The ring round each camp where the clan works and builds" },
			{ "Resource zones", "game map zones", m.Zones, "Where copper, flint, clay, salt and the rest grow: only in these places" },
		};
		for (const Layer& l : layers)
		{
			bool on = l.On;
			if (ImGui::Checkbox((L.Text(l.Label) + "###" + l.Command).c_str(), &on)) run(l.Command);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text(l.Tip).c_str());
		}
		bool productive = m.Productivity;
		if (ImGui::Checkbox((L.Text("Where it pays to gather") + "###productivity").c_str(), &productive)) run("game map productivity");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::BeginCombo("##item", m.Item.empty() ? L.Text("pick an item").c_str() : Lang::Cap(L.Name("item", m.Item)).c_str()))
		{
			std::set<std::string> items;
			for (const auto& [id, d] : m_NodeDefs) { items.insert(d.Item); if (!d.ExtraItem.empty()) items.insert(d.ExtraItem); }
			for (const std::string& it : items) if (ImGui::Selectable((Lang::Cap(L.Name("item", it)) + "###" + it).c_str(), it == m.Item)) run("game map productivity " + it);
			ImGui::EndCombo();
		}

		Heading(L.Text("The clans (their colours on the map)"));
		for (const auto& [tribe, id] : MapClans())
		{
			const Clan* clan = m_Clans.Find(id);
			Swatch(m_Clans.ColorOf(id));
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			const glm::vec2 camp = tribe->Config.Economy.Camp;
			ImGui::TextUnformatted((clan ? L.Name("clan", clan->Name, clan->Name) : L.Text("your clan")).c_str());
			if (tribe == &PlayTribe()) { ImGui::SameLine(); ImGui::TextDisabled("%s", L.Text("(yours)").c_str()); }
			else
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", L.Text("{0} m away", static_cast<int>(glm::distance(camp, PlayTribe().Config.Economy.Camp))).c_str());
			}
		}

		// the resource zones nearest your camp, by kind: where to go (or send an outpost, or trade) for what does not grow at home
		Heading(L.Text("Rare resources (nearest zone of each)"));
		if (!m_World.IsBuilt()) { ImGui::TextDisabled("%s", L.Text("no world").c_str()); return; }
		if (m.ZoneList.empty() || m.ZonesBuild != m_World.BuildCount() + 1)
		{
			m.ZoneList = PlanResourceZones(ResourceGroundOf(m_World.World()), m_NodeDefs, m_World.World().Desc.Seed);
			m.ZonesBuild = m_World.BuildCount() + 1;
		}
		std::map<std::string, float> nearest;
		const glm::vec2 home = PlayTribe().Config.Economy.Camp;
		for (const ResourceZone& z : m.ZoneList)
		{
			const float d = std::max(0.0f, glm::distance(z.Centre, home) - z.Radius);
			const auto it = nearest.find(z.Node);
			if (it == nearest.end() || d < it->second) nearest[z.Node] = d;
		}
		if (nearest.empty()) ImGui::TextDisabled("%s", L.Text("this world has no resource zones").c_str());
		for (const auto& [node, d] : nearest)
		{
			const auto def = m_NodeDefs.find(node);
			const std::string item = def != m_NodeDefs.end() ? def->second.Item : node;
			const ItemDef* it = m_Items.Find(item);
			const std::string name = Lang::Cap(L.Name("item", item, it && !it->Name.empty() ? it->Name : std::string()));
			glm::vec2 uv0, uv1;
			if (IconUv(item, uv0, uv1)) { Icon(item, 18.0f); ImGui::SameLine(); }
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(name.c_str());
			ImGui::SameLine(190.0f);
			if (d <= 0.0f) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(T::Good), "%s", L.Text("at home").c_str());
			else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(d < 150.0f ? T::Good : d < 400.0f ? T::Warn : T::Bad), "%s", L.Text("{0} m from the camp", static_cast<int>(d)).c_str());
		}
	}

	// ------------------------------------------------------------------ the story (N)
	void EditorApp::DrawStorySheet()
	{
		const Lang& L = m_Lang;
		struct Kind { const char* Id; const char* Label; const char* Icon; ImU32 Colour; };
		static const Kind kinds[] = {
			{ "birth", "births", "state.birth", T::Good }, { "death", "deaths", "state.death", T::Bad }, { "build", "buildings", "build", T::Ochre },
			{ "know", "discoveries", "state.discovery", T::News }, { "clans", "between clans", "trade", T::Warn }, { "fight", "fights", "fight", T::Bad },
		};
		static std::set<std::string> hidden;   // (what the player chose not to see; it is the sheet's own view, not the game's state)
		// V1: the story is the clan's part of the world's history (saved with the game), read as log lines; the last 400 of what mattered
		auto SheetKind = [](const std::string& k) -> std::string   // the history's kinds as the sheet groups them ("" = not in the story)
		{
			if (k == "birth" || k == "pair" || k == "marriage") return "birth";
			if (k == "death" || k == "killed") return "death";
			if (k == "built" || k == "camp_moved") return "build";
			if (k == "discovery" || k == "era" || k == "tradition" || k == "shaman") return "know";
			if (k == "raid" || k == "wounded" || k == "theft") return "fight";
			if (k == "contact" || k == "found" || k == "alliance" || k == "truce" || k == "pact" || k == "break" || k == "move") return "clans";
			return {};
		};
		std::deque<GameModeState::Event> story;
		const double daySeconds = std::max(1.0, static_cast<double>(PlayTribe().Config.Needs.DaySeconds));
		for (const HistoryEvent* e : m_Clans.Past().OfClan(PlayTribe().HistoryClan()))
			if (const std::string kind = SheetKind(e->Kind); !kind.empty())
			{
				story.push_back({ e->Text, kind, static_cast<int>(e->At / daySeconds) + 1 });
				if (story.size() > 400) story.pop_front();
			}
		std::map<std::string, int> counts;
		for (const GameModeState::Event& e : story) ++counts[e.Kind];
		for (const Kind& k : kinds)
		{
			bool on = !hidden.count(k.Id);
			ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(k.Colour));
			const std::string label = L.Text(k.Label) + " (" + std::to_string(counts[k.Id]) + ")###" + k.Id;
			if (ImGui::Checkbox(label.c_str(), &on)) { if (on) hidden.erase(k.Id); else hidden.insert(k.Id); }
			ImGui::PopStyleColor();
			if (&k != &kinds[2] && &k != &kinds[5]) ImGui::SameLine();
		}
		ImGui::Separator();
		if (story.empty()) { ImGui::TextDisabled("%s", L.Text("nothing has happened yet").c_str()); return; }
		// newest first, under the day it happened
		int day = -1;
		for (auto it = story.rbegin(); it != story.rend(); ++it)
		{
			if (hidden.count(it->Kind)) continue;
			if (it->Day != day)
			{
				day = it->Day;
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(T::Ochre));
				ImGui::TextUnformatted(L.Text("Day {0}", day).c_str());
				ImGui::PopStyleColor();
			}
			const Kind* kind = nullptr;
			for (const Kind& k : kinds) if (it->Kind == k.Id) kind = &k;
			const ImVec2 at = ImGui::GetCursorScreenPos();
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x, at.y + 2.0f), ImVec2(at.x + 3.0f, at.y + ImGui::GetTextLineHeight() + 2.0f), kind ? kind->Colour : T::BoneDim);
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
			if (kind && Icon(kind->Icon, ImGui::GetTextLineHeight())) ImGui::SameLine();
			ImGui::TextWrapped("%s", Lang::Cap(L.Line(it->Text)).c_str());
		}
	}
}
