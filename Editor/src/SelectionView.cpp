// The selection, in the free mode and in a game (H15): a left click in the viewport picks a character, a resource node or a building (a
// construction site too), as in a city-builder, and a card shows what it is; over a selected character a bubble tells with icons what it is
// doing, the thing it is after and how it feels. The icons are the atlas assets/ui/icons.png with its index icons.json (built from the
// generated sheets by tools/make_icon_atlas.py), by id: an item id ("berry"), a task ("chop", "deliver") or a state ("state.hungry").
#include "EditorApp.h"
#include "GameTheme.h"
#include "SceneEditor.h"
#include "GB/Game/StoreLayout.h"
#include "GB/Assets/AssetPath.h"
#include "GB/Assets/Model.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Log.h"
#include "GB/Visual/VisualDefs.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
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
		if (picked.empty())
		{
			// U2, T261: the nearest along the ray of a building's box, an animal and a node's look: a node in front of a building is the node (a
			// building's box is 3 m high over its whole footprint, more than a pit or a low shelter shows)
			float areaAt = 1e30f, nodeAt = 1e30f;
			const std::string area = PickArea(ray, &areaAt), node = PickNode(ray, &nodeAt);
			picked = !node.empty() && (area.empty() || nodeAt <= areaAt) ? node : area;
		}
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
	std::string EditorApp::PickArea(const Ray& ray, float* hitAt) const
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
		if (hitAt) *hitAt = bestT;
		return best;
	}

	// ------------------------------------------------------------------ T261: what a click on a resource node hits
	namespace
	{
		// T261: where a ray (o + t v, in the box's frame) enters and leaves a box
		bool RayBox(const glm::vec3& o, const glm::vec3& v, const glm::vec3& lo, const glm::vec3& hi, float& t, float& tOut)
		{
			float t0 = 0.0f, t1 = 1e30f;
			for (int i = 0; i < 3; ++i)
			{
				if (std::abs(v[i]) < 1e-7f) { if (o[i] < lo[i] || o[i] > hi[i]) return false; continue; }
				float a = (lo[i] - o[i]) / v[i], b = (hi[i] - o[i]) / v[i];
				if (a > b) std::swap(a, b);
				t0 = std::max(t0, a);
				t1 = std::min(t1, b);
				if (t0 > t1) return false;
			}
			t = t0;
			tOut = t1;
			return true;
		}
	}

	bool EditorApp::KitBounds(const std::string& path, glm::vec3& lo, glm::vec3& hi) const
	{
		auto it = m_KitBoxes.find(path);
		if (it == m_KitBoxes.end())
		{
			std::pair<glm::vec3, glm::vec3> box(glm::vec3(1.0f), glm::vec3(-1.0f));   // (min > max: not loaded, not tried again)
			if (const std::unique_ptr<Model> model = Model::Load(ResolveAssetPath(path, m_AssetRoot))) box = { model->GetBoundsMin(), model->GetBoundsMax() };
			it = m_KitBoxes.emplace(path, box).first;
		}
		lo = it->second.first;
		hi = it->second.second;
		return lo.x <= hi.x;
	}

	bool EditorApp::NodeBox(EntityId id, glm::mat4& world, glm::vec3& lo, glm::vec3& hi) const
	{
		const GB::Entity* e = m_SceneEditor.World().Find(id);
		const AbilitySystemComponent* asc = e ? m_Gameplay.Find(e->Name) : nullptr;
		if (!asc) return false;
		bool node = false;
		for (const auto& [kind, def] : m_NodeDefs) if (asc->HasTag("Resource.Node." + kind)) { node = true; break; }
		if (!node) return false;
		world = m_SceneEditor.World().WorldMatrix(id);   // (the frame EntityRenderer hands the visual hook: the same walk up the parents)
		lo = glm::vec3(-0.4f, 0.0f, -0.4f);
		hi = glm::vec3(0.4f, 1.6f, 0.4f);
		SceneVisuals::Look look;
		if (!m_Visuals.Enabled || !m_Visuals.Describe(*e, look) || look.Kind != "node") return true;
		const auto model = m_Visuals.Library.Models.find(look.Model);
		if (model == m_Visuals.Library.Models.end()) return true;
		// what SceneVisuals draws: the copies its Amount shows, of its variant (the same cache key as VisualCache, the bounds instead of the meshes)
		const std::vector<bool> visible = VisibleCopies(model->second, look.Fraction);
		const std::string key = look.Model + "#" + std::to_string(look.Variant) + "#" + MaskKey(visible);
		auto it = m_LookBoxes.find(key);
		if (it == m_LookBoxes.end())
		{
			std::vector<std::pair<glm::vec3, glm::vec3>> boxes(1, { glm::vec3(1e9f), glm::vec3(-1e9f) });   // [0]: the whole
			auto add = [&](const glm::vec3& a, const glm::vec3& b)
			{
				boxes.emplace_back(a, b);
				boxes[0].first = glm::min(boxes[0].first, a);
				boxes[0].second = glm::max(boxes[0].second, b);
			};
			for (const VisualPiece& piece : BuildVisual(model->second, visible, look.Variant))   // (a mesh per material: the bark, the leaves, the berries)
				if (!piece.Mesh.Vertices.empty())
				{
					glm::vec3 pa, pb;
					piece.Mesh.Bounds(pa, pb);
					add(pa, pb);
				}
			for (const auto& [part, at] : ModelPieces(model->second, visible, look.Variant))   // the glTF kit pieces (the rocks)
			{
				glm::vec3 ka, kb, a(1e9f), b(-1e9f);
				if (!KitBounds(KitModel(model->second.Parts[part], look.Variant), ka, kb)) continue;
				for (int c = 0; c < 8; ++c)
				{
					const glm::vec3 p(at * glm::vec4((c & 1) ? kb.x : ka.x, (c & 2) ? kb.y : ka.y, (c & 4) ? kb.z : ka.z, 1.0f));
					a = glm::min(a, p);
					b = glm::max(b, p);
				}
				add(a, b);
			}
			if (boxes.size() == 1) boxes = { { lo, hi }, { lo, hi } };   // (nothing shows: an emptied node keeps a box to click)
			it = m_LookBoxes.emplace(key, std::move(boxes)).first;
		}
		lo = it->second[0].first;
		hi = it->second[0].second;
		return true;
	}

	bool EditorApp::NodeRay(EntityId id, const Ray& ray, bool padded, float& tIn, float& tOut, float& volume) const
	{
		constexpr float kSlack = 0.12f, kLeastHalf = 0.35f, kLeastHeight = 0.6f;
		glm::mat4 world;
		glm::vec3 lo, hi;
		if (!NodeBox(id, world, lo, hi)) return false;   // (it fills m_LookBoxes for the look it has now)
		const GB::Entity* e = m_SceneEditor.World().Find(id);
		SceneVisuals::Look look;
		std::vector<std::pair<glm::vec3, glm::vec3>> fallback;
		const std::vector<std::pair<glm::vec3, glm::vec3>>* boxes = nullptr;
		if (m_Visuals.Enabled && m_Visuals.Describe(*e, look) && look.Kind == "node")
			if (const auto model = m_Visuals.Library.Models.find(look.Model); model != m_Visuals.Library.Models.end())
			{
				const auto it = m_LookBoxes.find(look.Model + "#" + std::to_string(look.Variant) + "#" + MaskKey(VisibleCopies(model->second, look.Fraction)));
				if (it != m_LookBoxes.end()) boxes = &it->second;
			}
		if (!boxes) { fallback = { { lo, hi }, { lo, hi } }; boxes = &fallback; }
		// the ray in the node's frame (affine: t means the same in both)
		const glm::mat4 back = glm::inverse(world);
		const glm::vec3 o(back * glm::vec4(ray.Origin, 1.0f)), v(back * glm::vec4(glm::normalize(ray.Dir), 0.0f));
		bool any = false;
		auto test = [&](const glm::vec3& a, const glm::vec3& b)
		{
			float t0 = 0.0f, t1 = 0.0f;
			if (!RayBox(o, v, a, b, t0, t1) || (any && t0 >= tIn)) return;
			any = true;
			tIn = t0;
			tOut = t1;
			const glm::vec3 s(world * glm::vec4(b - a, 0.0f));
			volume = std::abs(s.x * s.y * s.z);
		};
		const glm::vec3 slack(padded ? kSlack : 0.0f);
		for (size_t i = 1; i < boxes->size(); ++i) test((*boxes)[i].first - slack, (*boxes)[i].second + slack);
		if (padded)
		{
			// a least box round the foot of a small look (a tuft of herbs, a fallen branch): the whole, grown to a size a cursor finds
			const glm::vec3 c = (lo + hi) * 0.5f;
			const glm::vec3 half = glm::max((hi - lo) * 0.5f, glm::vec3(kLeastHalf, 0.0f, kLeastHalf));
			const float floor = std::min(lo.y, 0.0f) - kSlack;
			const glm::vec3 a(c.x - half.x, floor, c.z - half.z), b(c.x + half.x, std::max(hi.y, floor + kLeastHeight), c.z + half.z);
			if (a.x < lo.x - kSlack || a.z < lo.z - kSlack || b.y > hi.y + kSlack) test(a, b);   // (only when it is bigger than the look)
		}
		return any;
	}

	std::string EditorApp::PickNode(const Ray& ray, float* hitAt) const
	{
		// T261: the boxes of what is drawn (NodeRay), with a hand's width of slack and a least size (a tuft of herbs is still a target), in the
		// frame the renderer draws them in. (It was a point 0.75 m up with 1.1 m round it: a tree's crown, a wide clay bank or a boulder were
		// missed from most sides.) What the ray meets as drawn wins over a slack hit; then the nearest, except that a much smaller node the ray
		// meets while still inside a bigger one's box (a branch or a tuft under a crown, a bush against a boulder) is the one: the bigger box is
		// the looser fit.
		constexpr float kWidest = 12.0f;
		struct Hit { std::string Name; float In = 0.0f, Out = 0.0f, Volume = 0.0f; bool Exact = false; };
		std::vector<Hit> hits;
		const glm::vec3 d = glm::normalize(ray.Dir);
		const EntityWorld& w = m_SceneEditor.World();
		for (const auto& [id, e] : w.All())
		{
			if (!e.Gameplay) continue;   // (a node of the scene carries its gameplay component: the cheap test first)
			// no look is wider than kWidest: a node whose foot is farther from the ray is not tried (thousands of them on a map)
			const glm::vec3 foot = e.Parent == NullEntity ? e.Transform.Position : glm::vec3(w.WorldMatrix(id)[3]);
			const float s = glm::max(0.0f, glm::dot(foot - ray.Origin, d));
			if (glm::length(ray.Origin + d * s - foot) > kWidest || !w.IsActive(id)) continue;
			Hit h{ e.Name };
			if (NodeRay(id, ray, false, h.In, h.Out, h.Volume)) { h.Exact = true; hits.push_back(h); }
			else if (NodeRay(id, ray, true, h.In, h.Out, h.Volume)) hits.push_back(h);
		}
		if (hits.empty()) { if (hitAt) *hitAt = 1e30f; return {}; }
		// what the ray meets as drawn before what it only meets in the slack round a look; then the nearest
		std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.Exact != b.Exact ? a.Exact : a.In < b.In; });
		const Hit* best = &hits[0];
		for (const Hit& h : hits)
			if (h.Exact == hits[0].Exact && h.In < best->Out && h.Volume < 0.25f * best->Volume) best = &h;
		if (hitAt) *hitAt = best->In;
		return best->Name;
	}

	void EditorApp::ClickSelect(const Ray& ray)
	{
		m_FreeSelected = PickSelectable(ray);   // the card and the bubble of the free mode (SelectionView.cpp)
		const std::string actor = PickActor(ray);   // a character under the cursor wins over the scene behind it
		if (!actor.empty()) SelectActor(actor, false);
		else if (m_Observer.On) m_SceneEditor.Select(NullEntity);   // T261: the god's screen and the observer pick as the game does: what has a card, with its ring (DrawArea); the editor's glow (and Delete) stays off the scenery's rocks and trees
		else
		{
			m_SceneEditor.Select(m_SceneEditor.Pick(ray));
			if (const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.Selected()); e && m_Gameplay.Find(e->Name)) m_AbilityActor = e->Name;   // a plot, a tree or a bush shows its abilities too
		}
	}

	CommandResult EditorApp::PickCommand(const std::vector<std::string>& a)
	{
		std::string& selected = m_Game.Active ? m_Game.Selected : m_FreeSelected;
		if (a.empty() || a[0] == "off") { selected.clear(); return { true, "nothing selected" }; }
		const glm::vec2 size(static_cast<float>(std::max(m_ViewportW, 1u)), static_cast<float>(std::max(m_ViewportH, 1u)));
		const float aspect = size.x / size.y;
		auto highlighted = [this]() { const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.Selected()); return e ? e->Name : std::string("nothing"); };
		if (a[0] == "at" && a.size() >= 3)   // a left click at a pixel of the viewport, as the mouse makes it
		{
			const glm::vec2 px(static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())));
			const Ray ray = ScreenRay(m_Camera, px / size, aspect);
			if (m_Game.Active) m_Game.Selected = PickSelectable(ray);
			else ClickSelect(ray);
			return { true, "picked " + (selected.empty() ? std::string("nothing") : selected) + "; highlighted " + highlighted() };
		}
		if (a[0] == "probe")
		{
			// A click on the middle of the box of every node near what the camera looks at, whose middle is on the screen and not behind the ground,
			// and one on every rock and tree of the scenery there: what each click picks (and, outside a game, what the editor lights up)
			const float reach = a.size() >= 2 ? static_cast<float>(std::atof(a[1].c_str())) : 40.0f;
			const glm::mat4 viewProj = m_Camera.GetProjection(aspect) * m_Camera.GetView();
			const bool ground = m_World.IsBuilt();
			auto underground = [&](const glm::vec3& from, const glm::vec3& to)   // the ground cuts the line of sight (the last half metre is the thing's)
			{
				if (!ground) return false;
				const float len = glm::distance(from, to);
				for (float t = 0.5f; t < len - 0.5f; t += 0.5f)
				{
					const glm::vec3 p = from + (to - from) * (t / len);
					if (p.y < m_World.GetTerrain().GetHeight(p.x, p.z) - 0.05f) return true;
				}
				return false;
			};
			glm::vec3 focus = m_Orbit.Target;
			{
				const Ray mid = ScreenRay(m_Camera, glm::vec2(0.5f), aspect);
				for (float t = 1.0f; ground && t < 2000.0f; t += 1.0f)
				{
					const glm::vec3 p = mid.Origin + mid.Dir * t;
					if (p.y < m_World.GetTerrain().GetHeight(p.x, p.z)) { focus = p; break; }
				}
			}
			auto onScreen = [&](const glm::vec3& p, glm::vec2& px)
			{
				return ProjectToScreen(viewProj, p, size, px) && px.x > 2.0f && px.y > 2.0f && px.x < size.x - 2.0f && px.y < size.y - 2.0f;
			};
			const std::string keepSelected = m_FreeSelected;
			const EntityId keepLit = m_SceneEditor.Selected();
			const std::string keepActor = m_AbilityActor;
			// per sample: the middle of the box, then four points halfway to its sides and one above the middle (all inside what a blob shows)
			struct Tally { int Clicks = 0, Hit = 0, Covered = 0, Person = 0, Wrong = 0, Nothing = 0; };
			Tally middle, all;
			int nodes = 0, hidden = 0, props = 0, propPicked = 0, propLit = 0;
			std::map<std::string, std::pair<int, int>> kinds;   // kind -> middles hit, nodes
			std::map<std::string, std::string> sample;            // kind -> "name (x, y)"
			std::vector<std::string> misses, scenery;
			for (const auto& [id, e] : m_SceneEditor.World().All())
			{
				glm::mat4 world;
				glm::vec3 lo, hi;
				bool part = false;   // (a node's own primitives are props too: its Crown, its Rock; they are the node's, not scenery)
				for (const GB::Entity* up = &e; up && !part; up = up->Parent != NullEntity ? m_SceneEditor.World().Find(up->Parent) : nullptr) part = m_Gameplay.Find(up->Name) != nullptr;
				if (e.Prop && !part)
				{
					// the scenery: aim at a little above its foot
					const glm::vec3 at = glm::vec3(m_SceneEditor.World().WorldMatrix(id) * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f));
					glm::vec2 px;
					if (!m_SceneEditor.World().IsActive(id) || glm::distance(glm::vec2(at.x, at.z), glm::vec2(focus.x, focus.z)) > reach || !onScreen(at, px) || underground(m_Camera.Position, at)) continue;
					const Ray ray = ScreenRay(m_Camera, px / size, aspect);
					++props;
					std::string picked;
					bool lit = false;
					if (m_Game.Active) picked = PickSelectable(ray);
					else
					{
						ClickSelect(ray);
						picked = m_FreeSelected;
						lit = m_SceneEditor.Selected() == id;
					}
					propPicked += picked.empty() ? 0 : 1;
					propLit += lit ? 1 : 0;
					char where[64];
					std::snprintf(where, sizeof where, " (%.0f, %.0f) at %.1f %.1f", static_cast<double>(px.x), static_cast<double>(px.y), static_cast<double>(at.x), static_cast<double>(at.z));
					if (scenery.size() < 8) scenery.push_back(e.Name + where + " -> " + (picked.empty() ? std::string("nothing") : picked) + (lit ? " (lit)" : ""));
					continue;
				}
				if (!NodeBox(id, world, lo, hi)) continue;
				const glm::vec3 c = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f;
				const glm::vec3 centre = glm::vec3(world * glm::vec4(c, 1.0f));
				glm::vec2 px;
				if (glm::distance(glm::vec2(centre.x, centre.z), glm::vec2(focus.x, focus.z)) > reach || !m_SceneEditor.World().IsActive(id) || !onScreen(centre, px)) continue;
				if (underground(m_Camera.Position, centre)) { ++hidden; continue; }
				std::string kind;
				for (const auto& [k, def] : m_NodeDefs) if (m_Gameplay.Find(e.Name)->HasTag("Resource.Node." + k)) { kind = k; break; }
				++nodes;
				++kinds[kind].second;
				const glm::vec3 offsets[] = { glm::vec3(0.0f), { 0.5f * half.x, 0.0f, 0.0f }, { -0.5f * half.x, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.5f * half.z }, { 0.0f, 0.0f, -0.5f * half.z }, { 0.0f, 0.4f * half.y, 0.0f }, { -c.x, 0.3f - c.y, -c.z } };   // (the last: 0.3 m over the node's own origin, the foot of a trunk or a blob)
				for (int k = 0; k < 7; ++k)
				{
					const glm::vec3 p = glm::vec3(world * glm::vec4(c + offsets[k], 1.0f));
					if (!onScreen(p, px) || underground(m_Camera.Position, p)) continue;
					const Ray ray = ScreenRay(m_Camera, px / size, aspect);
					const std::string picked = PickSelectable(ray);
					char where[48];
					std::snprintf(where, sizeof where, " (%.0f, %.0f)", static_cast<double>(px.x), static_cast<double>(px.y));
					if (k == 0 && !sample.count(kind))
					{
						char spot[48];
						std::snprintf(spot, sizeof spot, " at %.1f %.1f", static_cast<double>(centre.x), static_cast<double>(centre.z));
						sample[kind] = e.Name + where + spot;
					}
					Tally* tallies[] = { &all, k == 0 ? &middle : nullptr };
					// what it picked instead: another node whose box the ray meets first covers this one (a click there is right to take it), as
					// a person in front does; anything else is a miss
					std::string verdict = "hit";
					if (picked != e.Name)
					{
						float mine = 0.0f, theirs = 0.0f, out = 0.0f, volume = 0.0f;
						const EntityId oid = picked.empty() ? NullEntity : m_SceneEditor.World().FindByName(picked);
						if (picked.empty()) verdict = "nothing";
						else if (AnyMember(picked) || m_Actors.Find(picked)) verdict = "person";
						else if (oid != NullEntity && NodeRay(oid, ray, false, theirs, out, volume) && (!NodeRay(id, ray, false, mine, out, volume) || theirs < mine)) verdict = "covered";
						else verdict = "wrong";
					}
					for (Tally* t : tallies)
						if (t)
						{
							++t->Clicks;
							(verdict == "hit" ? t->Hit : verdict == "covered" ? t->Covered : verdict == "person" ? t->Person : verdict == "nothing" ? t->Nothing : t->Wrong) += 1;
						}
					if (k == 0 && verdict == "hit") ++kinds[kind].first;
					if ((verdict == "wrong" || verdict == "nothing") && misses.size() < 12) misses.push_back(e.Name + where + (k == 0 ? " middle" : " side") + " -> " + (picked.empty() ? std::string("nothing") : picked));
				}
			}
			m_FreeSelected = keepSelected;
			m_SceneEditor.Select(keepLit);
			m_AbilityActor = keepActor;
			auto line = [](const char* what, const Tally& t)
			{
				const int fair = t.Clicks - t.Covered - t.Person;   // the clicks whose point no other thing hides
				std::ostringstream o;
				o << what << " " << t.Clicks << " clicks: " << t.Hit << " hit (" << (fair > 0 ? 100 * t.Hit / fair : 0) << " % of the " << fair << " not covered), "
					<< t.Covered << " covered by another node, " << t.Person << " by a person, " << t.Wrong << " wrong, " << t.Nothing << " nothing";
				return o.str();
			};
			std::ostringstream out;
			out << "pick probe: " << nodes << " nodes on screen within " << reach << " m (" << hidden << " behind the ground)\n  " << line("middles:", middle) << "\n  " << line("all points:", all)
				<< "\n  scenery: " << props << " rocks and trees clicked, " << propPicked << " selected, " << propLit << " highlighted";
			out << "\n  middles by kind:";
			for (const auto& [k, n] : kinds) out << " " << k << " " << n.first << "/" << n.second;
			out << "\n  one of each:";
			for (const auto& [k, s] : sample) out << " " << s << ";";
			for (const std::string& m : misses) out << "\n  miss: " << m;
			for (const std::string& s : scenery) out << "\n  scenery: " << s;
			return { true, out.str() };
		}
		std::string name;
		for (const std::string& w : a) name += (name.empty() ? "" : " ") + w;
		if (!m_Gameplay.Find(name)) return { false, "nothing called '" + name + "' has abilities (a character, a node, a building)" };
		selected = name;
		return { true, "selected " + name };
	}

	// U2: the selection's area on the ground: a building's footprint, a ring round an animal
	void EditorApp::DrawArea(float ox, float oy, float w, float h, const std::string& name)
	{
		const GB::Entity* e = m_SceneEditor.World().Find(m_SceneEditor.World().FindByName(name));
		const AbilitySystemComponent* asc = e ? m_Gameplay.Find(name) : nullptr;
		if (!asc) return;
		std::vector<glm::vec3> ring;
		const glm::vec3 at = e->Transform.Position + glm::vec3(0.0f, 0.08f, 0.0f);
		glm::mat4 nodeWorld;
		glm::vec3 lo, hi;
		bool node = false;
		if (const BuildingDef* def = BuildingOf(*asc, m_Buildings.Defs))
		{
			const glm::mat3 turn = glm::mat3_cast(glm::quat(glm::radians(e->Transform.Rotation)));
			const float x = 0.5f * def->Footprint.x, z = 0.5f * def->Footprint.y;
			for (const glm::vec2 c : { glm::vec2(-x, -z), glm::vec2(x, -z), glm::vec2(x, z), glm::vec2(-x, z) }) ring.push_back(at + turn * glm::vec3(c.x, 0.0f, c.y));
		}
		else if (NodeBox(e->Id, nodeWorld, lo, hi))
		{
			// T261: a resource node: a ring on the ground round what is drawn (its box's widest half and a hand's width more), following the
			// ground, so the node selected (and how far it spreads) shows from any side
			node = true;
			const glm::vec3 c(nodeWorld * glm::vec4((lo + hi) * 0.5f, 1.0f));
			const glm::vec3 ax(nodeWorld * glm::vec4(0.5f * (hi.x - lo.x), 0.0f, 0.0f, 0.0f)), az(nodeWorld * glm::vec4(0.0f, 0.0f, 0.5f * (hi.z - lo.z), 0.0f));
			const float r = std::max(0.6f, std::max(glm::length(glm::vec2(ax.x, ax.z)), glm::length(glm::vec2(az.x, az.z)))) + 0.25f;
			const bool ground = m_World.IsBuilt();
			for (int i = 0; i < 40; ++i)
			{
				const float a = 6.2831853f * static_cast<float>(i) / 40.0f;
				const float x = c.x + r * std::cos(a), z = c.z + r * std::sin(a);
				ring.push_back(glm::vec3(x, (ground ? m_World.GetTerrain().GetHeight(x, z) : at.y - 0.08f) + 0.08f, z));
			}
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
		if (node)
		{
			// the era's selection border (estilo_era1.md: ochre over a dark outline), a faint wash of it inside
			dl->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), GameTheme::Faded(GameTheme::Ochre, 0.14f));
			dl->AddPolyline(points.data(), static_cast<int>(points.size()), GameTheme::Outline, ImDrawFlags_Closed, 5.0f);
			dl->AddPolyline(points.data(), static_cast<int>(points.size()), GameTheme::Ochre, ImDrawFlags_Closed, 2.5f);
			return;
		}
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
		const bool shown = ImGui::Begin((L.Entity(name) + "###sel").c_str(), &open);
		if (m_Observer.Screen && !m_Game.Active)   // (the god's screen counts it in `observer status`'s layout: line)
			m_Observer.SelectionBox = { ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y };
		if (shown)
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
					// every need on its 0..100 scale (NeedLevels, the data needs too): its number and its colour (good, warning, bad) besides the bar, and
					// the danger mark at 25 as on the player's HUD; the HUD's needs first and in its order (H8). Vigor is the one apart: a factor of
					// the body's pace (1 = whole, the moods lower it: GB/Gameplay/Vitals.h), shown as a percentage
					const std::map<std::string, float> levels = clan->NeedLevels(*who);
					std::vector<std::string> order;
					for (const std::string& n : m_Controls.Hud.Needs) if (levels.count(n)) order.push_back(n);
					for (const auto& [n, v] : levels) if (std::find(order.begin(), order.end(), n) == order.end()) order.push_back(n);
					for (const std::string& n : order)
					{
						const float v = levels.at(n) * (n == "Vigor" ? 100.0f : 1.0f), v01 = std::clamp(v / 100.0f, 0.0f, 1.0f);
						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GameTheme::Level(v01));
						ImGui::ProgressBar(v01, ImVec2(150.0f, 10.0f), "");
						ImGui::PopStyleColor();
						const ImVec2 b0 = ImGui::GetItemRectMin(), b1 = ImGui::GetItemRectMax();
						const float mark = b0.x + (b1.x - b0.x) * 0.25f;
						ImGui::GetWindowDrawList()->AddLine(ImVec2(mark, b0.y - 1.0f), ImVec2(mark, b1.y + 1.0f), GameTheme::Outline, 2.0f);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L.Text("Below the mark (25) it is a danger.").c_str());
						ImGui::SameLine();
						std::string number = L.Num(v, 0);
						if (number.size() < 3) number.insert(0, 3 - number.size(), ' ');   // (the font is monospaced: the numbers line up)
						ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GameTheme::Level(v01)), "%s", number.c_str());
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
					if (!def->Store.empty() || def->StoreCap > 0 || def->FoodCap > 0)
					{
						// U3: what the store holds (the stockpile of its clan's economy), and T248: the share of the clan's main store kept in it
						const Stockpile* pile = nullptr;
						const Tribe* clan = nullptr;
						auto find = [&](const Tribe& t)
						{
							for (const Stockpile& p : t.Economy().Stockpiles) if (p.Name == name) pile = &p;
							for (const auto& site : t.SettlementSites()) if (!clan && site.Name == name) clan = &t;
						};
						find(m_Tribe);
						for (const auto& c : m_Clans.All()) if (!clan && c->Folk) find(*c->Folk);
						Inventory held(64, 0.0f);
						if (pile) for (const ItemSlot& it : pile->Store.Stacks()) held.Add(m_Items, it.Id, it.Count);
						if (clan)
							if (const StoreLayout layout = StoreLayout::Of(*clan); const StoreLayout::Place* place = layout.Find(name))
								for (const auto& [id, count] : place->Held) held.Add(m_Items, id, count);
						int units = 0;
						for (const ItemSlot& it : held.Stacks()) units += it.Count;
						ImGui::TextUnformatted(L.Text("store: {0} things", units).c_str());
						if (units > 0) DrawItems(held);
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
