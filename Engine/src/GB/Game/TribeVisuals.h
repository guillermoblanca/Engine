#pragma once
#include "GB/Animation/AnimManifest.h"
#include "GB/Game/ActorWorld.h"
#include "GB/Game/Tribe.h"
#include "GB/Visual/PropVisuals.h"
#include "GB/Visual/SceneVisuals.h"
#include <functional>
#include <map>
#include <string>

namespace GB
{
	class ReachLayer;

	// The scene side of a tribe: what its members show and how they move while they work, and the state that only the simulation holds drawn in
	// the world. It reads the tribe (jobs, bags, the store, the fields), the actors and the gameplay world and never writes the simulation: the
	// same tribe run headless decides exactly the same. Call Update and AddToScene once per frame for every tribe (each clan has its own), after
	// the tribe and the actors have been updated.
	//  - Props (PropVisuals.h): the tool of the trade in the hand while working and on the hip or back while walking, the basket on the back, a
	//    load in the arms with the carrying set ("holding", "box", "wheelbarrow") on the way to a store, firewood to the fire, berries to eat.
	//  - Activities: the montage of the manifest's "activities" for the job (kneel to craft, gestures to talk and teach, lie down to sleep...),
	//    played while the member stands at its spot doing it, at its stage's rate (a child faster, an elder slower), and the set it asks for.
	//  - Reach: the hands go to the contact point of what they work on (the trunk, the embers, the well's rope, the door, the workbench) with
	//    IK during the action's window ("visuals.reach").
	//  - Injury: a member whose "Health" attribute is low walks with the "injured" set (a hook: Injured can be replaced, H14).
	//  - AddToScene: the clan's fields (tilled rows, sprouts, the crop by season, frost), the piles of the store and the stockpiles (logs, stones,
	//    baskets of berries, scaled by how much there is) and the workbench at the camp.
	class TribeVisuals
	{
	public:
		struct Context
		{
			ActorWorld* Actors = nullptr;
			GameplayWorld* Gameplay = nullptr;
			const ItemLibrary* Items = nullptr;
			const EntityWorld* Entities = nullptr;      // where the targets of the reach are (nodes, the fire, the well)
			SceneVisuals* Visuals = nullptr;
			std::function<float(float, float)> Ground; // height of the ground (fields and piles sit on it)
			std::string Season;                         // "spring", "summer", "autumn", "winter" (the frost on the fields)
		};

		bool Enabled = true;
		bool Props = true, Activities = true, Reach = true;
		// Whether a member is hurt (walks with the "injured" set): by default its "Health" attribute below InjuredBelow of its maximum, when it has one.
		std::function<bool(const TribeMember&, const AbilitySystemComponent*)> Injured;
		float InjuredBelow = 0.35f;

		void Update(const Tribe& tribe, const Context& context, float dt);
		void AddToScene(const Tribe& tribe, const Context& context, Scene& scene);
		// Forgets what it did to the actors (they were removed or rebuilt).
		void Reset() { m_Members.clear(); }

		// What it decided for a member last frame (panels and tests).
		struct MemberState
		{
			PropChoice Applied;
			std::string Activity, Ability, Montage, Set;
			int Turn = 0;
			std::string Episode;          // the activity and where: a montage that does not loop is played once per episode
			bool Played = false;
			ReachLayer* Reach = nullptr;  // owned by the actor's AnimInstance
			const void* Anim = nullptr;   // the AnimInstance the layer was added to (a rebuilt animator needs a new one)
			std::string PendingLoad;      // the load waits for the carrying set to finish blending in
			float ReachWeight = 0.0f;
		};
		const MemberState* State(const std::string& member) const { const auto it = m_Members.find(member); return it == m_Members.end() ? nullptr : &it->second; }

	private:
		void ApplyProps(Actor& actor, MemberState& st, const PropChoice& choice, const SceneVisuals* visuals);
		std::map<std::string, MemberState> m_Members;
	};
}
