#include "GB/Game/TribeVisuals.h"
#include "GB/Animation/ReachLayer.h"
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

			// a new animator (rebuilt, retargeted): the layer and the drawer go to it
			if (st.Anim != &anim)
			{
				auto layer = std::make_unique<ReachLayer>(*actor->GetModel(), anim.Rig());
				st.Reach = layer.get();
				anim.AddLayer(std::move(layer));
				st.Anim = &anim;
				st.Applied = PropChoice();
				st.Set.clear();
				if (visuals)
					actor->SetPropDrawer([visuals](const std::string& look, const glm::mat4& world, Scene& scene)
					{
						const auto l = visuals->Library.Props.Looks.find(look);
						return l != visuals->Library.Props.Looks.end() && visuals->Cache().AddModel(l->second.Model, 1.0f, 0, world, scene, 80.0f);
					});
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
				ApplyProps(*actor, st, choice, visuals);
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

		// the fields: each plot by its state (and crop), growing through its stage; frost over what is in the ground in winter
		for (const Field& field : tribe.Economy().Fields)
			for (size_t i = 0; i < field.Plots.size(); ++i)
			{
				const FieldPlot& plot = field.Plots[i];
				const glm::mat4 m = at(field.PlotPos(i));
				const float growth = plot.State == "grown" ? 1.0f : std::clamp(plot.Growth, 0.0f, 1.0f);
				const std::string own = "plot_" + plot.State + (plot.Crop.empty() ? "" : "_" + plot.Crop);
				if (!cache.AddModel(own, std::max(growth, 0.05f), static_cast<int>(i), m, scene, 150.0f))
					cache.AddModel("plot_" + plot.State, std::max(growth, 0.05f), static_cast<int>(i), m, scene, 150.0f);
				if (ctx.Season == "winter" && plot.State != "grass") cache.AddModel("plot_frost", 1.0f, static_cast<int>(i), m, scene, 150.0f);
			}

		// the piles of a store, side by side along its front: how much of each kind, as full as the pile model says
		auto piles = [&](const Inventory& store, const glm::vec2& centre)
		{
			if (!ctx.Items) return;
			std::map<std::string, int> units;
			for (const ItemSlot& s : store.Stacks())
			{
				const std::string pile = PileOf(lib, *ctx.Items, s.Id);
				if (!pile.empty()) units[pile] += s.Count;
			}
			const float spacing = 1.1f;
			int i = 0;
			const int n = static_cast<int>(units.size());
			for (const auto& [pile, count] : units)
			{
				const PileVisual& p = lib.Piles.at(pile);
				const glm::vec2 spot = centre + glm::vec2((static_cast<float>(i) - static_cast<float>(n - 1) * 0.5f) * spacing, 0.0f);
				cache.AddModel(p.Model, std::clamp(static_cast<float>(count) / static_cast<float>(p.Full), 0.0f, 1.0f), 0, at(spot), scene, 150.0f);
				++i;
			}
		};
		piles(tribe.Store(), tribe.Config.Economy.Camp + kStore);
		for (const Stockpile& s : tribe.Economy().Stockpiles) piles(s.Store, s.Pos);

		// the camp's workbench (where "make" and "craft" are done); once the clan has built one (H8) that building is the bench, drawn with the
		// buildings, and the camp's is not drawn too
		if (tribe.StationAt("workbench") && !tribe.BuiltStationAt("workbench")) cache.AddBuilding("workbench", 1000, 1.0f, at(tribe.Config.Economy.Camp + kBench, 0.4f), scene);
	}
}
