#include "GB/Visual/TribeVisuals.h"
#include "GB/Animation/ReachLayer.h"
#include "GB/Effects/WorkEffects.h"
#include "GB/Game/StoreLayout.h"
#include "GB/World/TilledGround.h"
#include <chrono>
#include <set>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		// Where the camp's things stand, from the camp's centre (x, z): the workbench (the camp's station, Production.CampStations) and the store's
		// piles. The tribe has no entity for them, so they only exist on screen; the fire is the interactive at FirePos (1.5, 0).
		const glm::vec2 kBench{ -1.8f, 1.4f };
		const glm::vec2 kStore{ 0.0f, -2.4f };

		glm::mat4 PropOffset(const PropLook& look)
		{
			glm::mat4 m = glm::translate(glm::mat4(1.0f), look.Offset);
			m = glm::rotate(m, glm::radians(look.Rotation.y), glm::vec3(0, 1, 0));
			m = glm::rotate(m, glm::radians(look.Rotation.x), glm::vec3(1, 0, 0));
			return glm::rotate(m, glm::radians(look.Rotation.z), glm::vec3(0, 0, 1));
		}

		bool TagUnder(const ItemDef& def, const std::string& prefix)
		{
			for (const std::string& t : def.Tags)
				if (t == prefix || (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0 && t[prefix.size()] == '.')) return true;
			return false;
		}

		// The pile an item goes on: the most specific pile tag it carries (or its own id).
		std::string PileOf(const VisualLibrary& lib, const ItemLibrary& items, const std::string& item)
		{
			if (lib.Piles.count(item)) return item;
			const ItemDef* def = items.Find(item);
			std::string best;
			if (def)
				for (const auto& [tag, pile] : lib.Piles)
					if (TagUnder(*def, tag) && tag.size() > best.size()) best = tag;
			return best;
		}
	}

	ActorRenderer::PropDrawer TribeVisuals::PropDrawer(SceneVisuals* visuals)
	{
		if (!visuals) return nullptr;
		return [visuals](const std::string& look, const glm::mat4& world, Scene& scene)
		{
			const auto l = visuals->Library.Props.Looks.find(look);
			return l != visuals->Library.Props.Looks.end() && visuals->Cache().AddModel(l->second.Model, 1.0f, 0, world, scene, 80.0f);
		};
	}

	void TribeVisuals::ApplyProps(Actor& actor, MemberState& st, const PropChoice& choice, const SceneVisuals* visuals)
	{
		const PropRules& rules = visuals->Library.Props;
		// let go of what it no longer shows (only what this director put there: an ability's own props are the ability's)
		for (const PropPlacement& old : st.Applied.Props)
		{
			const bool kept = std::any_of(choice.Props.begin(), choice.Props.end(), [&](const PropPlacement& p) { return p.Name == old.Name; });
			if (!kept) actor.DetachProp(old.Name);
		}
		const bool settled = !actor.Anim() || !actor.Anim()->Transitioning();
		const std::vector<std::string> attached = actor.PropNames();
		for (const PropPlacement& p : choice.Props)
		{
			const auto look = rules.Looks.find(p.Look);
			if (look == rules.Looks.end()) continue;
			if (actor.PropLookOf(p.Name) == p.Look && actor.PropSocketOf(p.Name) == p.Socket && std::find(attached.begin(), attached.end(), p.Name) != attached.end())
				continue;
			// a load in the arms is measured between the palms of the carrying clips: wait for the set to be on screen
			if (p.Socket == "carry" && (!settled || (!choice.Set.empty() && actor.Anim() && actor.Anim()->HasSet(choice.Set) && actor.Anim()->Overlay() != choice.Set)))
			{
				st.PendingLoad = p.Look;
				continue;
			}
			actor.AttachLook(p.Name, p.Socket, p.Look, PropOffset(look->second));
			if (p.Socket == "carry") st.PendingLoad.clear();
		}
		st.Applied = choice;
	}

	void TribeVisuals::Update(const Tribe& tribe, const Context& ctx, float dt)
	{
		(void)dt;
		if (!Enabled || !ctx.Actors) return;
		SceneVisuals* visuals = ctx.Visuals;
		const VisualLibrary* lib = visuals ? &visuals->Library : nullptr;
		for (const TribeMember& m : tribe.Members())
		{
			if (m.Abstract) continue;
			Actor* actor = ctx.Actors->Find(m.Name);
			if (!actor || !actor->Anim()) continue;
			AnimInstance& anim = *actor->Anim();
			MemberState& st = m_Members[m.Name];
			AbilitySystemComponent* asc = ctx.Gameplay ? ctx.Gameplay->Find(m.Name) : nullptr;
			// dead (H14: "State.Dead", the editor drops the body as a ragdoll) or knocked down: nothing in the hands, no activity, no reach
			if (m.Dead || (asc && asc->HasTag("State.Dead")) || actor->Ragdolled())
			{
				if (st.Anim == &anim)
				{
					for (const PropPlacement& p : st.Applied.Props) actor->DetachProp(p.Name);
					actor->DetachProp("tool");
					if (!st.Montage.empty() && anim.Montages().IsPlaying(st.Montage)) anim.Montages().Stop(st.Montage);
					if (st.Reach) st.Reach->Release();
				}
				st.Applied = PropChoice();
				st.Montage.clear();
				st.Activity = "dead";
				continue;
			}
			const AnimManifest& manifest = actor->Manifest();
			const std::string stage = tribe.People().Find(m.Name) ? Society::StageName(tribe.People().StageOf(tribe.Config.Society, m.Name)) : "adult";
			st.Activity = ActivityKey(m.Job, m.Detail, stage);
			st.Ability.clear();
			if (asc)
				for (const ManifestUse& u : manifest.Abilities)
					if (asc->HasTag("Ability." + u.Name)) { st.Ability = u.Name; break; }

			// a new animator (rebuilt, retargeted): the layer goes to it (the looks it attaches are drawn by the ActorRenderer's PropDrawer)
			if (st.Anim != &anim)
			{
				auto layer = std::make_unique<ReachLayer>(*actor->GetModel(), anim.Rig());
				st.Reach = layer.get();
				anim.AddLayer(std::move(layer));
				st.Anim = &anim;
				st.Applied = PropChoice();
				st.Set.clear();
			}

			// ---- props and the set they (or the activity, or an injury) ask for
			PropChoice choice;
			if (Props && lib && ctx.Items)
			{
				PropContext pc;
				pc.Bag = &actor->Bag();
				pc.Items = ctx.Items;
				pc.Activity = st.Activity;
				pc.Ability = st.Ability;
				choice = ChooseProps(lib->Props, pc);
				const bool near = !ctx.Viewer || glm::distance(actor->GetPosition(), *ctx.Viewer) <= OutfitDistance;
				if (const auto outfit = lib->Props.Outfits.find(m.Role); near && outfit != lib->Props.Outfits.end())   // U5: what its trade wears, near the camera
					for (const PropRules::OutfitPiece& pl : outfit->second.Props)
						if (std::none_of(choice.Props.begin(), choice.Props.end(), [&](const PropPlacement& q) { return q.Socket == pl.Socket; }))
						{
							PropPlacement p;
							p.Name = pl.Name;
							p.Socket = pl.Socket;
							p.Look = pl.Look;
							choice.Props.push_back(p);
						}
				ApplyProps(*actor, st, choice, visuals);
			}
			// U4/U5: the clan's colour on the model's brightest material, its trade's on the others (made again only when the role, the clan's
			// colour or the model change)
			if (st.TintRole != m.Role || st.TintClan != ctx.ClanColor || st.TintModel != actor->GetModel().get())
			{
				st.TintRole = m.Role;
				st.TintClan = ctx.ClanColor;
				st.TintModel = actor->GetModel().get();
				const auto outfit = lib ? lib->Props.Outfits.find(m.Role) : std::map<std::string, PropRules::Outfit>::const_iterator();
				const glm::vec3 trade = lib && outfit != lib->Props.Outfits.end() ? outfit->second.Color : glm::vec3(-1.0f);
				std::vector<glm::vec3> tints;
				if (const std::shared_ptr<Model> model = actor->GetModel(); model && (ctx.ClanColor.r >= 0.0f || trade.r >= 0.0f))
				{
					const std::vector<ModelMaterial>& mats = model->GetMaterials();
					size_t brightest = 0;
					auto light = [](const glm::vec3& c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; };
					for (size_t i = 1; i < mats.size(); ++i) if (light(mats[i].Albedo) > light(mats[brightest].Albedo)) brightest = i;
					tints.assign(mats.size(), glm::vec3(1.0f));
					for (size_t i = 0; i < mats.size(); ++i)
					{
						const glm::vec3 want = i == brightest ? ctx.ClanColor : trade;
						if (want.r < 0.0f) continue;
						// the colour over the material's own lightness (a white body takes it fully, a dark joint keeps dark)
						const float l = std::max(light(mats[i].Albedo), 0.05f);
						tints[i] = glm::mix(glm::vec3(1.0f), want * (std::min(1.0f, l * 1.4f) / l), 0.85f);
					}
				}
				actor->SetTints(std::move(tints));
			}
			const ManifestUse* use = FindUse(manifest.Activities, st.Activity);
			bool hurt = false;
			if (Injured) hurt = Injured(m, asc);
			else if (asc)
				// H14: a serious or grave wound ("Combat.Wounded.*", which also slows Vigor) or little health left
				hurt = asc->HasTag("Combat.Wounded.Serious") || asc->HasTag("Combat.Wounded.Grave")
					|| (asc->Attributes().Has("Health") && asc->Attributes().Current("Health") < InjuredBelow * std::max(1e-3f, std::min(asc->Attributes().Max("Health"), 100.0f)));
			std::string set = !choice.Set.empty() ? choice.Set : (use && !use->Set.empty()) ? use->Set : hurt ? "injured" : "";
			if (!set.empty() && !anim.HasSet(set)) set.clear();
			if (set != st.Set)
			{
				anim.SetOverlay(set.empty() ? (anim.HasSet("free") ? "free" : AnimInstance::kDefaultSet) : set, 0.35f);
				st.Set = set;
			}

			// ---- the activity's montage, while it stands at its spot doing it
			const std::string episode = st.Activity + "|" + m.Detail + "|" + m.Mind.Orders;
			const bool busy = asc && asc->HasTag("State.Busy");
			const bool still = actor->GetSpeed() < 0.25f;
			if (episode != st.Episode)
			{
				if (!st.Montage.empty() && anim.Montages().IsPlaying(st.Montage)) anim.Montages().Stop(st.Montage);
				st.Montage.clear();
				st.Played = false;
				st.Episode = episode;
			}
			const bool want = Activities && use && !use->Montages.empty() && still && !busy && actor->TasksRunning();
			if (want)
			{
				if (st.Montage.empty() || !anim.Montages().IsPlaying(st.Montage))
				{
					st.Montage.clear();
					if (use->Loop || !st.Played)
						for (size_t tries = 0; tries < use->Montages.size(); ++tries)
						{
							const std::string& name = use->Montages[static_cast<size_t>(st.Turn++) % use->Montages.size()];
							if (anim.Montages().Play(name, "", StageRate(manifest, stage))) { st.Montage = name; st.Played = true; break; }
						}
				}
			}
			else if (!st.Montage.empty())
			{
				if (anim.Montages().IsPlaying(st.Montage)) anim.Montages().Stop(st.Montage);
				st.Montage.clear();
			}
			// A45: the work's particles (the clods of the hoe, the straw of the harvest) while its montage plays, timed by the montage's run
			if (ctx.Work && !st.Montage.empty() && anim.Montages().IsPlaying(st.Montage))
			{
				const float yaw = actor->GetYaw();
				ctx.Work->Work(m.Name, st.Activity, actor->GetPosition(), glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)), anim.Montages().RunFraction(st.Montage), dt);
			}

			// ---- the hands on what they work
			st.ReachWeight = 0.0f;
			const bool nearView = !ctx.Actors || ctx.Actors->AnimLevel(m.Name) == 0;   // H16: the hands reach only at full animation detail (the IK is skipped farther)
			if (Reach && lib && st.Reach && nearView)
			{
				const std::string key = !st.Ability.empty() ? st.Ability : st.Activity;
				const auto rule = lib->Reach.find(key);
				glm::vec3 point(0.0f);
				bool have = false;
				if (rule != lib->Reach.end())
				{
					glm::vec2 bench;
					if ((key == "make" || key == "craft") && tribe.StationAt("workbench", &bench))
					{
						// the bench that is drawn: a built workbench (H8) or else the camp's own (see AddToScene)
						glm::vec2 at = tribe.Config.Economy.Camp + kBench;
						tribe.BuiltStationAt("workbench", &at);
						point = { at.x, ctx.Ground ? ctx.Ground(at.x, at.y) : tribe.Config.Economy.CampY, at.y };
						have = true;
					}
					else if (ctx.Entities && !m.Detail.empty())
						if (const EntityId id = ctx.Entities->FindByName(m.Detail); id != NullEntity) { point = glm::vec3(ctx.Entities->WorldMatrix(id)[3]); have = true; }
				}
				if (have)
				{
					const ReachRule& r = rule->second;
					float weight = 0.0f;
					if (!st.Ability.empty())
					{
						const AbilityDef* def = ctx.Gameplay ? ctx.Gameplay->Library.Ability(st.Ability) : nullptr;
						if (def)
						{
							// the montages the ability plays: its list, and those the animation bindings made for it (by context)
							const std::string alias = "bind:ability:" + st.Ability;
							for (const std::string& name : anim.Montages().Names())
							{
								if (!anim.Montages().IsPlaying(name)) continue;
								if (std::find(def->Montages.begin(), def->Montages.end(), name) == def->Montages.end() && name.rfind(alias, 0) != 0) continue;
								// the fraction of the part that plays (the trim): the reach window follows what is seen, not the whole clip
								const float f = anim.Montages().RunFraction(name);
								if (f >= r.From && f <= r.To) weight = r.Weight;
							}
						}
					}
					else if (!st.Montage.empty() && anim.Montages().IsPlaying(st.Montage))
						weight = r.Weight;
					const glm::vec3 me = actor->GetPosition();
					glm::vec2 toward(me.x - point.x, me.z - point.z);
					toward = glm::length(toward) > 1e-3f ? glm::normalize(toward) : glm::vec2(0.0f);
					const glm::vec3 world = point + glm::vec3(toward.x * r.Toward, r.Height, toward.y * r.Toward);
					const glm::vec3 local = glm::vec3(glm::inverse(actor->ModelToWorld()) * glm::vec4(world, 1.0f));
					const bool right = r.Hand != "left", left = r.Hand != "right";
					// both hands: a little apart along the actor's own side axis
					const glm::vec3 side = glm::vec3(glm::inverse(actor->ModelToWorld()) * glm::vec4(glm::vec3(std::cos(actor->GetYaw()), 0.0f, -std::sin(actor->GetYaw())) * 0.12f, 0.0f));
					st.Reach->Reach(true, right && left ? local - side : local, right ? weight : 0.0f);
					st.Reach->Reach(false, right && left ? local + side : local, left ? weight : 0.0f);
					// the eyes on what the hands work (and back to the tribe's own looking once the reach ends)
					if (weight > 0.0f) actor->LookAtPoint(world);
					else if (st.ReachWeight > 0.0f) actor->ClearLook();
					st.ReachWeight = weight;
				}
				else
					st.Reach->Release();
			}
		}
	}

	void TribeVisuals::AddToScene(const Tribe& tribe, const Context& ctx, Scene& scene)
	{
		if (!Enabled || !ctx.Visuals) return;
		VisualCache& cache = ctx.Visuals->Cache();
		const VisualLibrary& lib = ctx.Visuals->Library;
		auto at = [&](const glm::vec2& p, float yaw = 0.0f)
		{
			const float y = ctx.Ground ? ctx.Ground(p.x, p.y) : tribe.Config.Economy.CampY;
			return glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(p.x, y, p.y)), yaw, glm::vec3(0, 1, 0));
		};

		// the fields: the tilled ground of each (T332) and its plants by state and crop, growing through their stage
		AddFields(tribe, ctx, scene);

		// how much of each kind of pile a place holds, the fullest first (ties by the pile's name)
		auto kinds = [&](const std::map<std::string, int>& things)
		{
			std::vector<std::pair<std::string, int>> out;
			if (!ctx.Items) return out;
			std::map<std::string, int> units;
			for (const auto& [id, count] : things)
				if (const std::string pile = PileOf(lib, *ctx.Items, id); !pile.empty() && count > 0) units[pile] += count;
			out.assign(units.begin(), units.end());
			std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
			return out;
		};
		// T256: a pile in the open (what lies by the fire, a field's stockpile): a compact heap of its kinds, rows of three round its centre
		auto heap = [&](const std::map<std::string, int>& things, const glm::vec2& centre)
		{
			const auto list = kinds(things);
			const int n = static_cast<int>(list.size()), rows = (n + 2) / 3;
			for (int i = 0; i < n; ++i)
			{
				const PileVisual& p = lib.Piles.at(list[static_cast<size_t>(i)].first);
				const int row = i / 3, inRow = std::min(3, n - row * 3);
				const glm::vec2 spot = centre + glm::vec2((static_cast<float>(i % 3) - 0.5f * static_cast<float>(inRow - 1)) * 0.95f, (static_cast<float>(row) - 0.5f * static_cast<float>(rows - 1)) * 0.95f);
				cache.AddModel(p.Model, std::clamp(static_cast<float>(list[static_cast<size_t>(i)].second) / static_cast<float>(p.Full), 0.0f, 1.0f), 0, at(spot, 0.35f * static_cast<float>(i)), scene, 150.0f);
			}
		};
		// T256: what a store building holds is drawn inside it, at the spots its look gives (visuals.json storeSlots: under the raised floor of a
		// granary or a storage, in the ring of a cache pit), in its own frame. A slot is full at the building's room shared among its slots (an empty
		// building shows nothing, a full one every slot full); each kind takes the slots its units fill, leaving one for each kind after it.
		auto inside = [&](const StoreLayout::Place& p)
		{
			const auto list = kinds(p.Held);
			if (list.empty()) return;
			std::vector<StoreSlot> slots;
			if (const auto look = lib.Buildings.find(p.Kind); look != lib.Buildings.end()) slots = look->second.StoreSlots;
			if (slots.empty()) slots.push_back(StoreSlot{});
			glm::mat4 base = at(p.Pos);
			if (ctx.Entities)
				if (const EntityId id = ctx.Entities->FindByName(p.Name); id != NullEntity) base = ctx.Entities->WorldMatrix(id);
			const int perSlot = std::max(1, (p.FoodRoom + p.Room + static_cast<int>(slots.size()) - 1) / static_cast<int>(slots.size()));
			size_t s = 0;
			for (size_t k = 0; k < list.size() && s < slots.size(); ++k)
			{
				const PileVisual& pile = lib.Piles.at(list[k].first);
				const int full = std::max(pile.Full, perSlot);
				const size_t most = slots.size() - s - std::min(slots.size() - s - 1, list.size() - k - 1);   // (one slot kept for each kind after it)
				int left = list[k].second;
				for (size_t used = 0; left > 0 && used < most; ++used, ++s)
				{
					const int here = used + 1 == most ? left : std::min(left, full);
					const StoreSlot& slot = slots[s];
					glm::mat4 m = glm::scale(glm::rotate(glm::translate(base, slot.Pos), glm::radians(slot.Yaw), glm::vec3(0, 1, 0)), glm::vec3(slot.Scale));
					// a building on a slope stands on its lowest corner (Tribe::StartSite): a pile is set on the ground under it, not buried in it
					if (ctx.Ground) m[3].y = std::max(m[3].y, ctx.Ground(m[3].x, m[3].z) + slot.Pos.y);
					cache.AddModel(pile.Model, std::clamp(static_cast<float>(here) / static_cast<float>(full), 0.2f, 1.0f), 0, m, scene, 150.0f);
					left -= here;
				}
			}
		};
		// T255: by the fire what lies there; T256: inside each store building what it holds (ClanStores' ledger, StoreLayout::Of)
		const StoreLayout layout = StoreLayout::Of(tribe);
		heap(layout.AtCamp(), tribe.Config.Economy.Camp + kStore);
		for (const StoreLayout::Place& p : layout.Places()) inside(p);
		for (const Stockpile& s : tribe.Economy().Stockpiles)
		{
			std::map<std::string, int> things;
			for (const ItemSlot& it : s.Store.Stacks()) things[it.Id] += it.Count;
			heap(things, s.Pos);
		}

		// the camp's workbench (where "make" and "craft" are done); once the clan has built one (H8) that building is the bench, drawn with the
		// buildings, and the camp's is not drawn too
		if (tribe.StationAt("workbench") && !tribe.BuiltStationAt("workbench")) cache.AddBuilding("workbench", 1000, 1.0f, at(tribe.Config.Economy.Camp + kBench, 0.4f), scene);
	}

	// T332: every field of the tribe as one sheet of tilled earth (GB/World/TilledGround.h) tinted by its plots' states, and the plants of each plot
	// as copies of the plot models (plot_<state>_<crop>, else plot_<state>) without their soil parts, moved, turned and scaled by a hash of the
	// field and the plot so the field reads as one sown ground and not as squares. In winter the whole sheet is the frost material.
	void TribeVisuals::AddFields(const Tribe& tribe, const Context& ctx, Scene& scene)
	{
		VisualCache& cache = ctx.Visuals->Cache();
		const VisualLibrary& lib = ctx.Visuals->Library;
		const TilledLook& look = lib.Fields;
		if (!(look == m_Look)) { m_Fields.clear(); m_Look = look; }   // the look was edited (a reload): every sheet is made again
		const bool winter = ctx.Season == "winter";
		const float campY = tribe.Config.Economy.CampY;
		const HeightFn ground = [&](float x, float z) { return ctx.Ground ? ctx.Ground(x, z) : campY; };
		using Clock = std::chrono::steady_clock;
		auto ms = [](Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); };
		auto mixKey = [](uint64_t h, const void* p, size_t n)
		{
			const auto* b = static_cast<const unsigned char*>(p);
			for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
			return h;
		};

		std::set<std::string> seen;
		std::vector<std::string> tints;
		std::vector<uint8_t> wet;
		for (const Field& field : tribe.Economy().Fields)
		{
			if (field.Plots.empty()) continue;
			TilledShape shape;
			shape.Origin = field.Origin;
			shape.Columns = std::max(1, field.Columns);
			shape.Spacing = field.Spacing;
			shape.Plots = static_cast<int>(field.Plots.size());
			shape.Cells = field.Cells;
			const FarmZones::Zone* zone = tribe.Zones().Of(field.Name);
			if (zone) shape.Outline = zone->Outline;
			const uint32_t seed = TilledSeed(field.Name, field.Origin);
			seen.insert(field.Name);
			TilledDraw& d = m_Fields[{ &tribe, field.Name }];

			// the sheet: made again when the cells move or the ground under them changes (a few heights of the bounding grid)
			uint64_t shapeKey = shape.Hash();
			const glm::vec2 far = field.Origin + glm::vec2(static_cast<float>(shape.Columns - 1), static_cast<float>(shape.Rows() - 1)) * field.Spacing;
			for (const glm::vec2& q : { field.Origin, far, glm::vec2(field.Origin.x, far.y), glm::vec2(far.x, field.Origin.y), 0.5f * (field.Origin + far) })
			{
				const float h = ground(q.x, q.y);
				shapeKey = mixKey(shapeKey, &h, sizeof h);
			}
			if (shapeKey != d.ShapeKey)
			{
				const auto t0 = Clock::now();
				size_t rimIndices = 0;
				const MeshData mesh = BuildTilledGround(shape, look, ground, seed, &rimIndices);
				d.RimIndices = static_cast<uint32_t>(rimIndices);
				d.Ground.reset();
				d.Triangles = mesh.Triangles();
				if (!mesh.Empty())
				{
					d.Ground = std::make_unique<Mesh>(mesh.Vertices, mesh.Indices);
					glm::vec3 lo, hi;
					mesh.Bounds(lo, hi);
					d.Centre = 0.5f * (lo + hi);
					d.Radius = 0.5f * glm::length(hi - lo);
				}
				d.Stones = TilledStones(shape, look, ground, seed);
				d.ShapeKey = shapeKey;
				m_FieldStats.LastMeshMs = ms(t0);
				m_FieldStats.MeshMs += m_FieldStats.LastMeshMs;
				++m_FieldStats.MeshBuilds;
			}

			// the tint of each plot: its state ("grass" not yet tilled); a field left fallow (the zone's plan) rests green
			const bool fallow = zone && zone->Plan == "fallow";
			tints.clear();
			wet.clear();
			uint64_t tintKey = mixKey(1469598103934665603ULL, &seed, sizeof seed);
			for (const FieldPlot& plot : field.Plots)
			{
				tints.push_back(fallow && (plot.State == "grass" || plot.State == "dug") ? std::string("fallow") : plot.State);
				wet.push_back(plot.State != "grass" && plot.Moisture > 0.0f ? 1 : 0);   // watered or rained on: darker earth
				tintKey = mixKey(tintKey, tints.back().data(), tints.back().size() + 1);
				tintKey = mixKey(tintKey, &wet.back(), 1);
			}
			if (!winter && (tintKey != d.TintKey || !d.Tints))
			{
				const auto t0 = Clock::now();
				const Image map = BuildTilledTints(shape, tints, wet, look, seed, d.TintRect);
				d.Tints = std::make_unique<Texture2D>(map, TextureEncoding::Linear);
				d.TintKey = tintKey;
				m_FieldStats.TintMs += ms(t0);
				++m_FieldStats.TintBuilds;
			}
			if (ctx.Work && d.Ground) ctx.Work->PushArea(glm::vec3(d.Centre.x, d.Centre.z, d.Radius));   // A45: who walks here pushes the crop
			if (d.Ground)
			{
				// two draws of the one mesh: the rim (its first RimIndices indices) with the rim material, the furrowed rest with the soil
				const uint32_t all = d.Ground->GetIndexCount();
				const uint32_t ranges[2][2] = { { 0, std::min(d.RimIndices, all) }, { std::min(d.RimIndices, all), all - std::min(d.RimIndices, all) } };
				for (int part = 0; part < 2; ++part)
				{
					if (ranges[part][1] == 0) continue;
					RenderObject obj;
					obj.MeshPtr = d.Ground.get();
					obj.IndexFirst = ranges[part][0];
					obj.IndexCount = ranges[part][1];
					obj.Mat = cache.MaterialFor(winter ? look.FrostMaterial : part == 0 ? look.RimMaterial : look.Material);
					if (winter) obj.Mat.UVScale *= 2.0f;   // (the frost's texture over tiles of eight furrows)
					else
					{
						if (part == 1) obj.Mat.UVScale = 1.0f;   // the UVs already put the texture's furrows on the ripple's
						if (d.Tints && d.Tints->IsValid())
						{
							obj.Mat.MacroMap = d.Tints.get();
							obj.Mat.MacroRect = d.TintRect;
							obj.Mat.Albedo *= 2.0f;      // the map holds the tints halved
						}
					}
					obj.BoundingCenter = d.Centre;
					obj.BoundingRadius = std::max(d.Radius, 0.5f);
					scene.Objects.push_back(std::move(obj));
				}
			}

			// the stones of the margin
			if (const auto stone = lib.Models.find(look.StoneModel); stone != lib.Models.end())
				for (const glm::vec4& st : d.Stones)
				{
					const float sc = look.StoneScale.x + (look.StoneScale.y - look.StoneScale.x) * st.w;
					glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(st.x, st.y - 0.02f, st.z));
					m = glm::rotate(m, st.w * 40.0f, glm::vec3(0, 1, 0));
					m = glm::scale(m, glm::vec3(sc));
					cache.AddModel(look.StoneModel, 1.0f, static_cast<int>(st.w * 7.0f), m, scene, look.PlantDistance);
				}

			// the plants: each plot's model without its soil, spread round its middle
			size_t plants = 0;
			for (size_t i = 0; i < field.Plots.size(); ++i)
			{
				const FieldPlot& plot = field.Plots[i];
				std::string id = "plot_" + plot.State + (plot.Crop.empty() ? "" : "_" + plot.Crop);
				auto model = lib.Models.find(id);
				if (model == lib.Models.end()) { id = "plot_" + plot.State; model = lib.Models.find(id); }
				if (model == lib.Models.end()) continue;
				const float growth = plot.State == "grown" ? 1.0f : std::clamp(plot.Growth, 0.0f, 1.0f);
				std::vector<bool> mask = VisibleCopies(model->second, std::max(growth, 0.05f));
				size_t k = 0;
				bool any = false;
				for (const VisualPart& part : model->second.Parts)
				{
					const bool soil = std::find(look.GroundParts.begin(), look.GroundParts.end(), part.Material) != look.GroundParts.end();
					for (int c = 0; c < part.Count; ++c, ++k)
					{
						if (soil) mask[k] = false;
						any = any || mask[k];
					}
				}
				if (!any) continue;
				TilledPlant p = PlantOf(look, field.Spacing, seed, static_cast<int>(i), model->second.Variants);
				// near the painted perimeter the plants keep inside it: no jitter, and narrower as the edge comes closer
				float spread = look.Spread;
				if (shape.Outline.size() >= 3)
				{
					const float reach = 0.4f * field.Spacing * look.Spread * p.Scale;   // (the plot models scatter within ~0.36 of a plot of the middle)
					const float edge = OutlineDistance(shape.Outline, field.PlotPos(i)) + 0.5f * field.Spacing;   // (where the sheet ends)
					if (edge < reach + glm::length(p.Offset))
					{
						p.Offset = glm::vec2(0.0f);
						spread *= std::clamp(edge / std::max(reach, 1e-3f), 0.45f, 1.0f);
					}
				}
				const glm::vec2 at = field.PlotPos(i) + p.Offset;
				glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(at.x, ground(at.x, at.y) + look.Lift, at.y));
				if (p.Tilt > 0.0f) m = glm::rotate(m, p.Tilt, glm::vec3(std::cos(p.TiltAxis), 0.0f, std::sin(p.TiltAxis)));   // a slight lean, its own way
				m = glm::rotate(m, p.Yaw, glm::vec3(0, 1, 0));
				m = glm::scale(m, glm::vec3(p.Scale * spread, p.Scale, p.Scale * spread));
				const std::string key = "t:" + id + "#" + std::to_string(p.Variant) + ":" + MaskKey(mask);
				cache.Add(key, model->second, mask, p.Variant, m, scene, look.PlantDistance);
				++plants;
			}
			d.Plants = plants;
		}
		// the fields of this tribe that are gone (removed zones, a migration) let their meshes go
		for (auto it = m_Fields.begin(); it != m_Fields.end();)
			if (it->first.first == &tribe && !seen.count(it->first.second)) it = m_Fields.erase(it);
			else ++it;
	}

	TribeVisuals::FieldStats TribeVisuals::Fields() const
	{
		FieldStats s = m_FieldStats;
		for (const auto& [key, d] : m_Fields)
		{
			++s.Fields;
			s.Triangles += d.Triangles;
			s.Plants += d.Plants;
		}
		return s;
	}
}
