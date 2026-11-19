#pragma once
// T335 (documentation/01_diseno/ARBOL_TECNICO.md §11.5): a clan of the AI that knows how to transplant a berry bush and has none by its camp
// moves one there by itself, with the transplant of T325 (Transplant.h: the node dug up, carried and planted; Mandate::Carry).
//
// When. economy.farming.transplantNear > 0 (data; 0 = off, as before), at the clan's posting of jobs (one line in Tribe::PostJobs):
//   - not the player's clan in the colony (its zones and its orders are the player's: FarmZones::Player);
//   - no transplant in course in the clan;
//   - a kind of node whose item is food and whose transplant's verb a technology of the tree names and the clan knows (with no tree, or no
//     such verb in the data, nothing happens);
//   - the clan's map (its shared memory) has no node of such a kind within transplantNear of the camp, and has one farther, on its home ground
//     (HomeRadius + HomeFalloff): the nearest of them, nobody working it.
// Where. The first free spot (Transplant::WhyNot for a member that may go: dry, reachable, no building, site, field or other node there) on
// rings round the camp inside transplantNear; then Transplant::Assign (the nearest member free to do it), and the log says so (a refusal is
// not logged: it is tried again at the next posting).
//
// Stateless (the mandate in course is the order's, saved by ColonyOrders), CPU only, deterministic (maps in name order, fixed bearings).
#include <glm/vec2.hpp>
#include <string>

namespace GB
{
	class Tribe;

	class BushKeeper
	{
	public:
		static constexpr int kBearings = 12;   // spots tried on each ring
		static constexpr int kRings = 3;       // rings at 0.4, 0.6 and 0.8 of transplantNear

		// The node the clan would move now and where to ("" when nothing: the rule off, the verb unknown, a bush near already, none known).
		static std::string Pick(const Tribe& tribe, glm::vec2& to);

		// ---- the tribe's hook (one line in Tribe::PostJobs)
		static void Post(Tribe& tribe);
	};
}
