// The selection, in the free mode and in a game (H15): a left click in the viewport picks a character, a resource node or a building (a
// construction site too), as in a city-builder, and a card shows what it is; over a selected character a bubble tells with icons what it is
// doing, the thing it is after and how it feels. The icons are the atlas assets/ui/icons.png with its index icons.json (built from the
// generated sheets by tools/make_icon_atlas.py), by id: an item id ("berry"), a task ("chop", "deliver") or a state ("state.hungry").
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace GB::Editor
{
	namespace
	{
		ImTextureID TextureId(const Texture2D& t) { return static_cast<ImTextureID>(static_cast<intptr_t>(t.GetID())); }

		// The icon of the task a member is on: the ability of the node it works for a gather, the action of a farm trip, the kind otherwise.
		std::string TaskIcon(const TribeMember& m, const std::map<std::string, ResourceNodeDef>& nodes)
		{
			const std::string& job = m.Job;
			if (job == "gather")
			{
				const MemoryEntry* e = m.Mind.Memory.Find(m.Detail);
				const auto def = e ? nodes.find(e->Kind) : nodes.end();
				const std::string ability = def != nodes.end() ? def->second.Ability : std::string();
				return ability == "Chop" ? "chop" : ability == "Mine" ? "mine" : ability == "Collect" ? "collect" : "gather";
			}
			if (job == "farm")
			{
				const std::string action = m.Detail.substr(0, m.Detail.find(' '));
				return action == "till" || action == "sow" || action == "tend" || action == "harvest" ? action : "till";
			}
			if (job == "craft") return "make";
			if (job == "approach") return "goto";
			if (job == "recover" || job == "rest" || job.empty()) return "rest";
			if (job == "socialize" || job == "regroup") return "talk";
			if (job == "envoy" || job == "meeting" || job == "trade") return "trade";
			return job;   // deposit, deliver, build, make, experiment, explore, expedition, hunt, fight, flee, care, eat, drink, sleep, warm, stoke, fire, rite, talk, haul, migrate
		}

		// The thing the task is about: the item of the node it gathers or looks for, what it carries, the meat of a hunt, the output of a recipe.
		std::string TaskItem(const TribeMember& m, const std::map<std::string, ResourceNodeDef>& nodes, const RecipeBook& recipes, const Inventory* bag)
		{
			const std::string& job = m.Job;
			auto most = [&]()
			{
				std::string best;
				int n = 0;
				if (bag) for (const ItemSlot& s : bag->Stacks()) if (s.Count > n) { n = s.Count; best = s.Id; }
				return best;
			};
			if (job == "gather")
			{
				const MemoryEntry* e = m.Mind.Memory.Find(m.Detail);
				const auto def = e ? nodes.find(e->Kind) : nodes.end();
				return def != nodes.end() ? def->second.Item : std::string();
			}
			if (job == "explore") { const auto def = nodes.find(m.Detail); return def != nodes.end() ? def->second.Item : m.Detail == "well" ? "water" : std::string(); }
			if (job == "make" || job == "craft") { const Recipe* r = recipes.Find(m.Detail); return r ? r->Output : m.Detail; }
			if (job == "deposit" || job == "deliver" || job == "haul") return most();
			if (job == "hunt") return "meat";
			if (job == "drink") return "water";
			if (job == "stoke" || job == "fire") return "wood";
			if (job == "farm") return "grain";
			if (job == "experiment") return "tech." + m.Detail;   // the technology it works out (its icon in the atlas)
			return {};
		}

		// The need it feels most, when one is low ("" = none).
		std::string StateIcon(const std::map<std::string, float>& levels, bool fleeing)
		{
			if (fleeing) return "state.afraid";
			static const std::pair<const char*, const char*> kNeeds[] = {
				{ "Health", "state.wounded" }, { "Thirst", "state.thirsty" }, { "Hunger", "state.hungry" }, { "Warmth", "state.cold" }, { "Rest", "state.tired" }, { "Safety", "state.afraid" } };
			std::string out;
			float low = 30.0f;
			for (const auto& [need, icon] : kNeeds)
				if (const auto it = levels.find(need); it != levels.end() && it->second < low) { low = it->second; out = icon; }
			return out;
		}
	}

	bool EditorApp::IconUv(const std::string& id, glm::vec2& uv0, glm::vec2& uv1)
	{
		if (!m_IconsTried)
		{
			m_IconsTried = true;
			const Image image = Image::Load(m_AssetRoot + "/ui/icons.png", false);
			std::ifstream in(m_AssetRoot + "/ui/icons.json");
			nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
			if (image.IsValid() && j.is_object() && j.contains("icons"))
			{
				m_IconTexture = std::make_unique<Texture2D>(image, TextureEncoding::Linear);
				m_IconCell = j.value("cell", 128.0f);
				for (const auto& [name, at] : j["icons"].items())
					if (at.is_array() && at.size() == 2) m_IconCells[name] = { at[0].get<float>(), at[1].get<float>() };
			}
			else GB_CLIENT_WARN("no icon atlas at {}/ui (tools/make_icon_atlas.py builds it): the selection shows text only", m_AssetRoot);
		}
		const auto it = m_IconCells.find(id);
		if (!m_IconTexture || it == m_IconCells.end()) return false;
		const glm::vec2 size(static_cast<float>(m_IconTexture->GetWidth()), static_cast<float>(m_IconTexture->GetHeight()));
		uv0 = (it->second + 0.5f) / size;
		uv1 = (it->second + m_IconCell - 0.5f) / size;
		return true;
	}

	bool EditorApp::Icon(const std::string& id, float size)
	{
		glm::vec2 a, b;
		if (!IconUv(id, a, b)) return false;
		ImGui::Image(TextureId(*m_IconTexture), ImVec2(size, size), ImVec2(a.x, a.y), ImVec2(b.x, b.y));
		return true;
	}

	bool EditorApp::IconButton(const char* id, const std::string& icon, float size)
	{
		glm::vec2 a, b;
		if (!IconUv(icon, a, b)) return false;
		return ImGui::ImageButton(id, TextureId(*m_IconTexture), ImVec2(size, size), ImVec2(a.x, a.y), ImVec2(b.x, b.y));
	}

	const TribeMember* EditorApp::AnyMember(const std::string& name) const
	{
		if (const TribeMember* m = m_Tribe.FindMember(name)) return m;
		for (const auto& c : m_Clans.All())
			if (c->Folk && c->Folk != &m_Tribe)
				if (const TribeMember* m = c->Folk->FindMember(name)) return m;
		return nullptr;
	}

	std::string EditorApp::PickSelectable(const Ray& ray) const
	{
		std::string picked = PickActor(ray);
		if (picked.empty()) picked = PickArea(ray);   // U2
		if (picked.empty()) picked = PickNode(ray);
		if (picked.empty())
			if (const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.Pick(ray)))
			{
				// a part of a building (its walls, its roof) picks the building: the nearest ancestor with abilities
				for (const GB::Entity* at = e; at && picked.empty(); at = at->Parent != NullEntity ? m_SceneEditor.World().Find(at->Parent) : nullptr)
					if (m_Gameplay.Find(at->Name)) picked = at->Name;
			}
		return picked;
	}

	// U2: a building by the box over its footprint (turned with it, 3 m high), an animal by a ball of its size: the nearest along the ray
	std::string EditorApp::PickArea(const Ray& ray) const
	{
		std::string best;
		float bestT = 1e30f;
		const glm::vec3 d = glm::normalize(ray.Dir);
		for (const auto& [id, e] : m_SceneEditor.World().All())
		{
			const AbilitySystemComponent* asc = m_Gameplay.Find(e.Name);
			if (!asc) continue;
			if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs))
			{
				const glm::mat3 back = glm::transpose(glm::mat3_cast(glm::quat(glm::radians(e.Transform.Rotation))));
				const glm::vec3 o = back * (ray.Origin - e.Transform.Position), v = back * d;
				const glm::vec3 lo(-0.5f * def->Footprint.x, -0.2f, -0.5f * def->Footprint.y), hi(0.5f * def->Footprint.x, 3.0f, 0.5f * def->Footprint.y);
				float t0 = 0.0f, t1 = 1e30f;
				for (int i = 0; i < 3; ++i)
				{
					if (std::abs(v[i]) < 1e-6f) { if (o[i] < lo[i] || o[i] > hi[i]) t0 = 2e30f; continue; }
					float a = (lo[i] - o[i]) / v[i], b = (hi[i] - o[i]) / v[i];
					if (a > b) std::swap(a, b);
					t0 = std::max(t0, a);
					t1 = std::min(t1, b);
				}
				if (t0 <= t1 && t0 < bestT) { bestT = t0; best = e.Name; }
				continue;
			}
			for (const auto& [sid, sp] : m_Ecology.Species)
				if (asc->HasTag(sp.Tag))
				{
					const float r = std::max(0.4f, 0.5f * sp.Size);
					const glm::vec3 c = e.Transform.Position + glm::vec3(0.0f, r, 0.0f);
					const float s = std::max(0.0f, glm::dot(c - ray.Origin, d));
					if (glm::length(ray.Origin + d * s - c) < r + 0.15f && s < bestT) { bestT = s; best = e.Name; }
					break;
				}
		}
		return best;
	}

	// U2: the selection's area on the ground: a building's footprint, a ring round an animal
	void EditorApp::DrawArea(float ox, float oy, float w, float h, const std::string& name)
	{
		const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name));
		const AbilitySystemComponent* asc = e ? m_Gameplay.Find(name) : nullptr;
		if (!asc) return;
		std::vector<glm::vec3> ring;
		const glm::vec3 at = e->Transform.Position + glm::vec3(0.0f, 0.08f, 0.0f);
		if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs))
		{
			const glm::mat3 turn = glm::mat3_cast(glm::quat(glm::radians(e->Transform.Rotation)));
			const float x = 0.5f * def->Footprint.x, z = 0.5f * def->Footprint.y;
			for (const glm::vec2 c : { glm::vec2(-x, -z), glm::vec2(x, -z), glm::vec2(x, z), glm::vec2(-x, z) }) ring.push_back(at + turn * glm::vec3(c.x, 0.0f, c.y));
		}
		else
		{
			float r = 0.0f;
			for (const auto& [sid, sp] : m_Ecology.Species) if (asc->HasTag(sp.Tag)) r = std::max(0.5f, 0.6f * sp.Size);
			if (r <= 0.0f) return;
			for (int i = 0; i < 20; ++i) ring.push_back(at + glm::vec3(r * std::cos(0.314159f * static_cast<float>(i)), 0.0f, r * std::sin(0.314159f * static_cast<float>(i))));
		}
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		std::vector<ImVec2> points;
		for (const glm::vec3& p : ring)
		{
			glm::vec2 s;
			if (!ProjectToScreen(viewProj, p, glm::vec2(w, h), s)) return;
			points.push_back(ImVec2(ox + s.x, oy + s.y));
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();   // (the viewport: under the cards)
		dl->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(0, 0, 0, 150), ImDrawFlags_Closed, 4.0f);
		dl->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(255, 220, 90, 255), ImDrawFlags_Closed, 2.0f);
	}

	// a grid of what an inventory holds: an icon and the count, the name on hover; empty slots greyed
	void EditorApp::DrawItems(const Inventory& bag)
	{
		const int cols = 6, slots = std::max(bag.Slots(), static_cast<int>(bag.Stacks().size()));
		for (int i = 0; i < slots; ++i)
		{
			ImGui::PushID(i);
			if (i % cols) ImGui::SameLine();
			if (i < static_cast<int>(bag.Stacks().size()))
			{
				const ItemSlot& s = bag.Stacks()[static_cast<size_t>(i)];
				const ItemDef* d = m_Items.Find(s.Id);
				const std::string label = Lang::Cap(m_Lang.Name("item", s.Id, d && !d->Name.empty() ? d->Name : std::string()));
				const ImVec2 at = ImGui::GetCursorScreenPos();
				if (!Icon(s.Id, 40.0f))
				{
					char text[32];
					std::snprintf(text, sizeof text, "%.3s\n%d", label.c_str(), s.Count);
					ImGui::Button(text, ImVec2(42.0f, 40.0f));
				}
				else ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 26.0f, at.y + 26.0f), IM_COL32(255, 255, 255, 255), std::to_string(s.Count).c_str());
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s x%d", label.c_str(), s.Count);
			}
			else { ImGui::BeginDisabled(); ImGui::Button("-", ImVec2(40.0f, 40.0f)); ImGui::EndDisabled(); }
			ImGui::PopID();
		}
	}

	void EditorApp::DrawSelection(float ox, float oy, float w, float h, std::string& selected)
	{
		if (selected.empty()) return;
		const std::string name = selected;
		const AbilitySystemComponent* asc = m_Gameplay.Find(name);
		if (!asc) { selected.clear(); return; }
		const TribeMember* who = AnyMember(name);
		if (!who) DrawArea(ox, oy, w, h, name);
		bool open = true;
		if (m_Game.Active || m_Observer.On)
		{   // in the game the card goes under the needs, down to the prompt over the action bar; under the observer, under the clan card (the right
			// side is the clan book's in both)
			const float top = std::max(oy + 12.0f, (m_Game.Active ? m_Game.HudBottom : m_Observer.CardBottom) + 12.0f);
			const float bottom = m_Game.Active ? (m_Game.BarTop > 0.0f ? m_Game.BarTop : oy + h - 70.0f) - 54.0f : oy + h - 12.0f;
			ImGui::SetNextWindowPos(ImVec2(ox + 12.0f, top), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 320.0f), std::clamp(bottom - top, 120.0f, 460.0f)), ImGuiCond_Always);
		}
		else
		{
			ImGui::SetNextWindowPos(ImVec2(ox + w - 12.0f, oy + h - 12.0f), ImGuiCond_Appearing, ImVec2(1.0f, 1.0f));
			ImGui::SetNextWindowSize(ImVec2(std::min(w - 40.0f, 340.0f), std::min(h - 80.0f, 460.0f)), ImGuiCond_Appearing);
		}
		const Lang& L = m_Lang;
		auto itemName = [&](const std::string& id) { const ItemDef* d = m_Items.Find(id); return L.Name("item", id, d && !d->Name.empty() ? d->Name : std::string()); };
		if (ImGui::Begin((L.Entity(name) + "###sel").c_str(), &open))
		{
			if (who)
			{
				// a character: who, what it does (icons), its needs, skills and bag
				const Actor* actor = m_Actors.Find(name);
				ImGui::TextUnformatted((who->Name + " - " + L.Name("role", who->Role) + (who->Dead ? " (" + L.Text("dead") + ")" : std::string())).c_str());
				const std::string task = TaskIcon(*who, m_NodeDefs), item = TaskItem(*who, m_NodeDefs, m_Recipes, actor ? &actor->Bag() : nullptr);
				if (Icon(task, 32.0f)) ImGui::SameLine();
				if (!item.empty() && Icon(item, 32.0f)) ImGui::SameLine();
				ImGui::TextWrapped("%s", L.Doing(who->Job, who->Detail).c_str());
				if (m_Game.Active && who->Name != m_Game.Player)
				{
					char bond[16];
					std::snprintf(bond, sizeof bond, "%+.2f", static_cast<double>(PlayTribe().People().Affinity(PlayTribe().Config.Society, who->Name, m_Game.Player)));
					ImGui::TextDisabled("%s", L.Text("bond with you {0}", std::string(bond)).c_str());
				}
				const Tribe* clan = m_Tribe.FindMember(name) ? &m_Tribe : nullptr;
				for (const auto& c : m_Clans.All()) if (!clan && c->Folk && c->Folk->FindMember(name)) clan = c->Folk;
				if (clan)
				{
					const std::map<std::string, float> levels = clan->NeedLevels(*who);
					for (const auto& [n, v] : levels)
					{
						ImGui::ProgressBar(std::clamp(v / 100.0f, 0.0f, 1.0f), ImVec2(150.0f, 10.0f), "");
						ImGui::SameLine();
						ImGui::TextUnformatted(Lang::Cap(L.Name("need", n, n)).c_str());
					}
				}
				if (ImGui::CollapsingHeader((L.Text("Skills") + "###skills").c_str()))
					for (const auto& [family, level] : who->Skills) ImGui::Text("%-14s %.1f", Lang::Cap(L.Name("skill", family)).c_str(), static_cast<double>(level));
				if (actor)
				{
					ImGui::TextUnformatted(L.Text("Bag").c_str());
					DrawItems(actor->Bag());
				}
			}
			else if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs))
			{
				// a building or a site: its phase and what it still lacks (and what may stand in for it)
				const SiteState s = ReadSite(*asc, *def);
				ImGui::TextDisabled("%s", (s.Done ? L.Text("What it does:") : L.Text("When it stands:")).c_str());
				for (const std::string& line : BuildingDoes(*def)) { ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%s", line.c_str()); }
				ImGui::Separator();
				if (s.Done)
				{
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Good), "%s", L.Text("stands").c_str());
					if (def->Shelter > 0)
					{
						ImGui::TextUnformatted(L.Text("{0} beds", def->Shelter).c_str());
						// U3/N1: whose house it is, who lives in it and who is inside now
						const Tribe::Household* home = nullptr;
						const Tribe* owner = nullptr;
						auto find = [&](const Tribe& t) { if (!home) if (const Tribe::Household* h = t.HouseholdAt(name)) { home = h; owner = &t; } };
						find(m_Tribe);
						for (const auto& c : m_Clans.All()) if (c->Folk) find(*c->Folk);
						if (!home) ImGui::TextDisabled("%s", L.Text("nobody's house yet: anyone may sleep in it").c_str());
						else
						{
							ImGui::TextUnformatted(L.Text("the house of the {0} family", home->Id).c_str());
							const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name));
							for (const std::string& dweller : home->Members)
							{
								const TribeMember* m = owner->FindMember(dweller);
								const Actor* actor = m_Actors.Find(dweller);
								const bool inside = actor && e && glm::distance(glm::vec2(actor->GetPosition().x, actor->GetPosition().z), glm::vec2(e->Transform.Position.x, e->Transform.Position.z)) < def->Radius() + 0.8f;
								ImGui::BulletText("%s%s", dweller.c_str(), inside ? ("  (" + L.Text("inside") + ")").c_str() : "");
								if (m)
								{
									ImGui::SameLine();
									ImGui::TextDisabled("%s, %s", L.Name("role", m->Role).c_str(), L.Name("doing", m->Job.empty() ? std::string("idle") : m->Job).c_str());
								}
							}
							ImGui::TextUnformatted(L.Text("pantry ({0}):", L.Name("property", owner->Config.Economy.Property)).c_str());
							if (const Inventory* pantry = owner->Pantry(home->Id)) DrawItems(*pantry);
							else ImGui::TextDisabled("%s", L.Text("empty").c_str());
						}
					}
					if (!def->Station.empty())
					{
						ImGui::TextUnformatted(L.Text("workplace: {0}", L.Name("station", def->Station)).c_str());
						// U3: who works at it now (a member making a recipe of this station)
						std::string workers;
						auto scan = [&](const Tribe& t)
						{
							for (const TribeMember& m : t.Members())
								if (!m.Dead && m.Job == "make")
									if (const Recipe* r = m_Recipes.Find(m.Detail); r && r->Station == def->Station) workers += (workers.empty() ? "" : ", ") + m.Name + " (" + itemName(r->Output) + ")";
						};
						scan(m_Tribe);
						for (const auto& c : m_Clans.All()) if (c->Folk && c->Folk != &m_Tribe) scan(*c->Folk);
						ImGui::TextWrapped("%s", L.Text("working here: {0}", workers.empty() ? L.Text("nobody now") : workers).c_str());
					}
					if (!def->Store.empty())
					{
						// U3: what the store holds (the stockpile of its clan's economy)
						const Stockpile* pile = nullptr;
						auto find = [&](const Tribe& t) { for (const Stockpile& p : t.Economy().Stockpiles) if (p.Name == name) pile = &p; };
						find(m_Tribe);
						for (const auto& c : m_Clans.All()) if (!pile && c->Folk) find(*c->Folk);
						int units = 0;
						if (pile) for (const ItemSlot& it : pile->Store.Stacks()) units += it.Count;
						ImGui::TextUnformatted(L.Text("store: {0} things", units).c_str());
						if (pile) DrawItems(pile->Store);
					}
				}
				else
				{
					ImGui::ProgressBar(s.Progress, ImVec2(-1.0f, 8.0f), "");
					ImGui::TextWrapped("%s", L.Text("{0} done; phase {1} of {2}: {3} ({4} of {5} s of work)", std::to_string(static_cast<int>(s.Progress * 100.0f)) + " %", s.Phase + 1, s.Phases,
						L.Name("phase", s.PhaseName), static_cast<int>(s.Work), static_cast<int>(s.WorkNeeded)).c_str());
					if (s.Missing.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Good), "%s", L.Text("has its materials: it can be built").c_str());
					else ImGui::TextUnformatted(L.Text("lacks:").c_str());
					for (const auto& [item, n] : s.Missing)
					{
						if (Icon(item, 28.0f)) ImGui::SameLine();
						ImGui::Text("%d %s", n, itemName(item).c_str());
						if (const auto sub = s.Substitutes.find(item); sub != s.Substitutes.end())
							for (const std::string& other : sub->second)
							{
								ImGui::SameLine();
								ImGui::TextDisabled(" %s", L.Text("or").c_str());
								ImGui::SameLine();
								if (!Icon(other, 22.0f)) ImGui::TextDisabled("%s", itemName(other).c_str());
								else if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("{0} can stand in for {1}", itemName(other), itemName(item)).c_str());
							}
					}
				}
			}
			else
			{
				// a node (or any other thing with abilities): what it is, what it gives, how much is left
				std::string kind;
				for (const auto& [id, d] : m_NodeDefs) if (asc->HasTag("Resource.Node." + id)) { kind = id; break; }
				// U3: an animal: its species, how fed and how sound it is, what it eats
				for (const auto& [sid, sp] : m_Ecology.Species)
					if (asc->HasTag(sp.Tag))
					{
						ImGui::TextDisabled("%s (%s)", Lang::Cap(L.Name("animal", sp.Name, sp.Name)).c_str(), L.Text(sp.Hunts() ? "a hunter" : "grazes").c_str());
						for (const auto& at : asc->Attributes().All())
							if (at.Max > 0.0f && (at.Name == "Food" || at.Name == "Health"))
							{
								ImGui::ProgressBar(std::clamp(at.Current / at.Max, 0.0f, 1.0f), ImVec2(150.0f, 10.0f), "");
								ImGui::SameLine();
								ImGui::TextUnformatted(L.Text(at.Name == "Food" ? "Fed" : "Health").c_str());
							}
						std::string eats;
						for (const std::string& e : sp.Eats) eats += (eats.empty() ? "" : ", ") + L.Name("item", e);
						ImGui::TextWrapped("%s", L.Text("eats: {0}", eats).c_str());
						break;
					}
				if (!kind.empty())
				{
					const ResourceNodeDef& d = m_NodeDefs.at(kind);
					if (Icon(d.Item, 40.0f)) ImGui::SameLine();
					ImGui::BeginGroup();
					const int left = ResourceSystem::Amount(*asc);
					ImGui::TextUnformatted(L.Text("left: {0} of {1}", left, d.Capacity).c_str());
					ImGui::ProgressBar(d.Capacity > 0 ? static_cast<float>(left) / static_cast<float>(d.Capacity) : 0.0f, ImVec2(150.0f, 8.0f), "");
					ImGui::EndGroup();
					ImGui::TextUnformatted(L.Text("gives {0} x{1} each time", itemName(d.Item), d.PerGather).c_str());
					if (!d.ExtraItem.empty()) ImGui::TextUnformatted(L.Text("and {0} x{1}", itemName(d.ExtraItem), d.ExtraPerGather).c_str());
					// what it asks for: a tool (by its tag: "Tool.Axe" -> an axe) or nothing but hands
					std::string needs = L.Text("bare hands");
					if (!d.RequiresTag.empty())
					{
						const std::string tool = d.RequiresTag.substr(d.RequiresTag.find_last_of('.') + 1);
						needs = L.Text(tool == "Axe" ? "an axe" : tool == "Pickaxe" ? "a pickaxe" : tool == "Hoe" ? "a hoe" : tool == "Sickle" ? "a sickle" : tool == "Spear" ? "a spear" : "a tool");
					}
					ImGui::TextUnformatted(L.Text("needs: {0}", needs).c_str());
					ImGui::TextUnformatted((d.RegenSeconds > 0.0f ? L.Text("regrows {0} every {1} s", d.Regen, static_cast<int>(d.RegenSeconds)) : L.Text("does not regrow")).c_str());
				}
				if (ImGui::CollapsingHeader((L.Text("Details") + "###attributes").c_str()))
					for (const auto& at : asc->Attributes().All()) ImGui::Text("%-14s %.1f / %.1f", at.Name.c_str(), static_cast<double>(at.Current), static_cast<double>(at.Max));
				if (ImGui::CollapsingHeader((L.Text("State (for the curious)") + "###state").c_str())) ImGui::TextWrapped("%s", m_Gameplay.Describe(name).c_str());
			}
		}
		ImGui::End();
		if (!open) selected.clear();
	}

	void EditorApp::DrawBubble(float ox, float oy, float w, float h, const std::string& name)
	{
		if (name.empty()) return;
		const TribeMember* who = AnyMember(name);
		const Actor* actor = m_Actors.Find(name);
		if (!who || !actor || who->Dead) return;
		const glm::mat4 viewProj = m_Camera.GetProjection(w / std::max(h, 1.0f)) * m_Camera.GetView();
		glm::vec2 at;
		if (!ProjectToScreen(viewProj, actor->GetPosition() + glm::vec3(0.0f, 2.1f, 0.0f), glm::vec2(w, h), at)) return;
		const Tribe* clan = m_Tribe.FindMember(name) ? &m_Tribe : nullptr;
		for (const auto& c : m_Clans.All()) if (!clan && c->Folk && c->Folk->FindMember(name)) clan = c->Folk;
		std::vector<std::string> icons{ TaskIcon(*who, m_NodeDefs) };
		if (const std::string item = TaskItem(*who, m_NodeDefs, m_Recipes, &actor->Bag()); !item.empty()) icons.push_back(item);
		if (clan) if (const std::string state = StateIcon(clan->NeedLevels(*who), who->Job == "flee"); !state.empty()) icons.push_back(state);
		std::vector<std::pair<glm::vec2, glm::vec2>> uvs;
		for (const std::string& id : icons) { glm::vec2 a, b; if (IconUv(id, a, b)) uvs.push_back({ a, b }); }
		if (uvs.empty()) return;
		// the bubble: a rounded box over the head with a tail down to it, an icon after another
		const float size = 34.0f, pad = 6.0f;
		const float bw = pad + static_cast<float>(uvs.size()) * (size + pad), bh = size + 2.0f * pad;
		const ImVec2 tip(ox + at.x, oy + at.y), min(tip.x - bw * 0.5f, tip.y - bh - 10.0f), max(min.x + bw, min.y + bh);
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImU32 fill = IM_COL32(250, 244, 228, 235), line = IM_COL32(74, 48, 28, 255);
		dl->AddTriangleFilled(ImVec2(tip.x - 7.0f, max.y - 1.0f), ImVec2(tip.x + 7.0f, max.y - 1.0f), tip, fill);
		dl->AddLine(ImVec2(tip.x - 7.0f, max.y), tip, line, 2.0f);
		dl->AddLine(ImVec2(tip.x + 7.0f, max.y), tip, line, 2.0f);
		dl->AddRectFilled(min, max, fill, 9.0f);
		dl->AddRect(min, max, line, 9.0f, 0, 2.0f);
		for (size_t i = 0; i < uvs.size(); ++i)
		{
			const ImVec2 a(min.x + pad + static_cast<float>(i) * (size + pad), min.y + pad);
			dl->AddImage(TextureId(*m_IconTexture), a, ImVec2(a.x + size, a.y + size), ImVec2(uvs[i].first.x, uvs[i].first.y), ImVec2(uvs[i].second.x, uvs[i].second.y));
		}
	}

	void EditorApp::DrawResearch(const ClanKnowledge& k)
	{
		const std::vector<ClanKnowledge::ResearchLine> lines = k.Research();
		const Lang& L = m_Lang;
		if (lines.empty()) { ImGui::TextDisabled("%s", L.Text("nobody is working anything out now").c_str()); return; }
		const std::vector<std::string>& goals = k.Goals();
		for (const ClanKnowledge::ResearchLine& l : lines)
		{
			std::string who;
			for (const std::string& n : l.Who) who += (who.empty() ? "" : ", ") + n;
			const bool goal = std::find(goals.begin(), goals.end(), l.Tech) != goals.end();
			if (Icon("tech." + l.Tech, 40.0f)) ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextUnformatted((Lang::Cap(L.Name("tech", l.Tech, k.Name(l.Tech))) + "  " + std::to_string(static_cast<int>(l.Progress * 100.0f)) + " %"
				+ (goal ? "  " + L.Text("(the clan's next step)") : std::string())).c_str());
			ImGui::ProgressBar(l.Progress, ImVec2(-FLT_MIN, 8.0f), "");
			ImGui::TextDisabled("%s", L.Text("on it: {0}", who).c_str());
			ImGui::EndGroup();
		}
	}
}
