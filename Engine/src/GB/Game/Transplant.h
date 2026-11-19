#pragma once
// T325 (documentation/01_diseno/ARBOL_TECNICO.md §11.5 and §11.7.2): the generic provision "transplant": a resource node (a berry bush, a clump
// of herbs, a young tree) is dug up as a portable thing (a cutting, a clump, a sapling), carried and planted somewhere else, where it grows back
// as it always did.
//
// What can be moved is data: a node type whose definition has "transplant": { "item", "verb", "work" } (ResourceTransplantDef, the era1 pack). The
// verb ("transplant" for bushes and herbs, plant_care; "transplant_tree" for trees, arboriculture) is gated by TechVerbs: without it the order
// does not exist for that clan ("does not know how to transplant a tree").
//
// The order. `game order <name> transplant <node|x z> to <x> <z>` (ColonyOrders, verb "transplant") or `game transplant <node|x z> to <x> <z>`
// (Assign: the nearest member free to do it). The member walks to the node and works there for `work` seconds (job "uproot"): the node is left
// empty and claimed, and the thing dug up goes in its bag (Mandate::Carry says what it carries). It walks to the place and works there again
// (job "plant"): the thing leaves its bag and the node itself (its name, its ability system, its tags) stands there now, empty, its regrowth
// clock at 0: it gives again after its usual regrowth time. A cut or cancelled order with the thing in the bag puts it back (Abandon): the node
// stays where it was, empty, and no cutting is left lying in a store with nothing to do with it.
//
// Saving. The node keeps its entity: the moved one is a changed entity of the scene's save (its place, Amount and Regrow are its components),
// so an old save, with no node moved, loads as before. An order in course is the mandate's (ColonyOrders: "carry" only when something is
// carried). Nothing of the tribe's own JSON changes.
//
// T333. Someone who dies or leaves the clan with the thing in its bag (ColonyOrders::Drop) does not take it to the grave nor to another clan: it
// goes back in the ground where the node was (Abandon, as a cancelled order: the items of the dead stay with the body and nothing falls to the
// ground in this game, so a cutting left there would be a plant lost for good and a node empty for ever). The order ends with a notice.
// Other clans (TribeOutside::NodeHeld and NodeMoved, set by ClanWorld; none for a clan alone): a clan may not move a node another clan is
// moving (a mandate "transplant" on it) nor one on the land another clan holds (not an ally: what ClanWorld::Gathered calls theft); and when a
// node is planted elsewhere every other clan forgets what it knew of it (its senses' cache of places, its map, its people's memories: they did
// not see it go, and the nodes are listed once on the belief that they never move), so none walks to where it was.
//
// The AI moves berry bushes by its camp (BushKeeper.h, T335). CPU only, deterministic.
#include "GB/Game/ColonyOrders.h"
#include <glm/vec2.hpp>
#include <string>

namespace GB
{
	class Tribe;
	struct TribeMember;
	struct ResourceNodeDef;

	class Transplant
	{
	public:
		static constexpr float kPick = 3.0f;       // "x z" as the node: the movable node nearest the point, within this
		static constexpr float kRoom = 1.5f;       // no other node closer than this to where it is planted
		static constexpr float kStandOff = 1.2f;   // where the member stands to dig and to plant: this far from the spot, on its own side
		static constexpr int kMostFails = 3;

		enum class Outcome { Going, Done, Failed };

		// The definition of a node that can be moved (its type has a transplant), nullptr: none (no node, or not movable).
		static const ResourceNodeDef* DefOf(const Tribe& tribe, const std::string& node);
		// The movable node nearest `at` within kPick ("" = none).
		static std::string NodeNear(const Tribe& tribe, const glm::vec2& at);
		// "<node|x z> to <x> <z>": the node and the place. "" or what is wrong (the usage).
		static std::string Parse(const Tribe& tribe, const std::string& text, std::string& node, glm::vec2& to);
		// "" when `m` may move `node` to `to` now, else why: unknown node, not movable, the verb unknown ("does not know how to transplant"),
		// the place taken (a building, a site, a field, another node), in the water, out of reach.
		static std::string WhyNot(const Tribe& tribe, const TribeMember& m, const std::string& node, const glm::vec2& to);

		// `game transplant`: the order to the nearest member free to take it (alive, not the player's character, no other order, fit).
		static ColonyOrders::Reply Assign(Tribe& tribe, const std::string& text);

		// ---- ColonyOrders' hooks (verb "transplant", one line each)
		// Give: fills the mandate's Target and At from the order's words; "" or why not.
		static std::string Prepare(const Tribe& tribe, const TribeMember& m, const std::string& text, ColonyOrders::Mandate& d);
		// Turn: the next step (dig, carry, plant).
		static Outcome Step(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why);
		// End, Cancel and Drop: an order left with the thing in the bag puts it back (the node stays where it was), also for the dead (T333).
		static void Abandon(Tribe& tribe, TribeMember& m, const ColonyOrders::Mandate& d);
		// "transplant wild berry bush 3 to 12.0, 4.0" (the line of `game status` and the log).
		static std::string Describe(const ColonyOrders::Mandate& d);
		// ---- T333: the other clans
		// True when `tribe` has an order in course to move `node`.
		static bool Moving(const Tribe& tribe, const std::string& node);
		// "" when no other clan holds `node` (at `at`) for itself: it is moving it, or it grows on its land; else why (TribeOutside::NodeHeld).
		static std::string Held(const Tribe& tribe, const std::string& node, const glm::vec2& at);
		// `node` was planted at `at` by another clan: `tribe` forgets what it knew of it (TribeOutside::NodeMoved).
		static void Forget(Tribe& tribe, const std::string& node);

		// The member's own jobs of the order (ColonyOrders::CloseJob clears them; the tribe's Finish knows nothing of them).
		static bool OwnJob(const std::string& job) { return job == "uproot" || job == "plant"; }

	private:
		static glm::vec2 NodePos(const Tribe& tribe, const std::string& node, bool* found = nullptr);
		static bool Uproot(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why);
		static bool Plant(Tribe& tribe, TribeMember& m, ColonyOrders::Mandate& d, std::string& why);
	};
}
