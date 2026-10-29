#pragma once
#include "GB/AI/PriorityLibrary.h"
#include "GB/AI/Utility.h"
#include <vector>

namespace GB
{
	class Tribe;
	struct TribeMember;

	// The bridge between the tribe and the priority graphs (docs/GRAFO_PRIORIDADES.md). Uses only the public API of the tribe.
	//
	// The inputs a graph can read for a character, and (with `option`) for one of its options. The values of the character are in `Values`; the ones that depend on the
	// option or on a name (factor.*, store.*, skill.*, trait.*) come through the resolver, which keeps `tribe`, `m` and `option` by reference: use the result while they live.
	PgInputs PriorityInputsFor(const Tribe& tribe, const TribeMember& m, const UtilityOption* option = nullptr);

	// The hook of Tribe::Decide: rescores `options` with the graph the library assigns to the member (its own, else its role's, else the default). Without a graph, with an
	// invalid one or with the identity graph ("builtin") it does nothing, so a game without graphs is bit for bit the same. An option is touched only when its score changes
	// (then it gets the factors "before graph" and "graph" with the old and the new score). Deterministic.
	void ApplyPriorityGraphs(const Tribe& tribe, TribeMember& m, const PriorityLibrary& library, std::vector<UtilityOption>& options);
}
