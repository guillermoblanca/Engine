// EntityWorld's structure stamp (T59, T66) and what Clear does to the ids (T53). Headless.
#include "GB/Scene/Entity.h"
#include <cstdio>
#include <utility>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	void Fill(GB::EntityWorld& w)
	{
		const GB::EntityId a = w.Create("A");
		w.Create("B", a);
		w.Create("C");
	}

	// T53: Clear starts the ids again at 1 (a cleared world numbers as a new one: the saves' baselines and user entities count from 1), and the
	// stamp changes, so a cache of Entity pointers knows the entities it holds are gone even when the same ids come back.
	void ClearRestartsIdsAndRestamps()
	{
		GB::EntityWorld w;
		Fill(w);
		const uint64_t before = w.Structure();
		const GB::EntityId next = w.NextId();
		w.Clear();
		Check(w.Count() == 0, "Clear empties the world");
		Check(w.NextId() == 1, "Clear starts the ids again at 1");
		Check(w.Structure() != before, "Clear changes the structure stamp");
		Fill(w);
		Check(w.NextId() == next, "the same entities take the same ids again");
		Check(w.FindByName("A") == 1 && w.FindByName("C") == 3, "ids from 1, as in a new world");
		Check(w.Structure() != before, "a repopulated world does not take its old stamp back");
	}

	// T66: a move hands the nodes to the destination; both sides take a new stamp (the source's cached pointers now point into the destination).
	void MoveRestampsBothSides()
	{
		GB::EntityWorld src;
		Fill(src);
		const uint64_t srcBefore = src.Structure();
		GB::EntityWorld moved(std::move(src));
		Check(moved.Count() == 3 && moved.FindByName("B") == 2, "the move constructor takes the entities");
		Check(src.Structure() != srcBefore, "the moved-from world changes its stamp (move constructor)");
		Check(moved.Structure() != srcBefore, "the moved-to world has a new stamp (move constructor)");
		Check(moved.Structure() != src.Structure(), "the two sides of a move do not share a stamp");

		GB::EntityWorld dst;
		dst.Create("Old");
		const uint64_t dstBefore = dst.Structure(), movedBefore = moved.Structure();
		dst = std::move(moved);
		Check(dst.Count() == 3 && dst.FindByName("Old") == GB::NullEntity, "the move assignment takes the entities");
		Check(moved.Structure() != movedBefore, "the moved-from world changes its stamp (move assignment)");
		Check(dst.Structure() != dstBefore && dst.Structure() != movedBefore, "the moved-to world has a new stamp (move assignment)");
		Check(dst.Structure() != moved.Structure(), "the two sides of a move assignment do not share a stamp");

		// a moved-from world is usable again
		moved.Clear();
		moved.Create("Again");
		Check(moved.Count() == 1, "a moved-from world can be filled again");

		// copies: the source keeps its stamp (it did not change), the copy has its own
		const uint64_t dstStamp = dst.Structure();
		GB::EntityWorld copy(dst);
		Check(dst.Structure() == dstStamp && copy.Structure() != dstStamp && copy.Count() == 3, "a copy takes its own stamp");
		GB::EntityWorld assigned;
		assigned = dst;
		Check(dst.Structure() == dstStamp && assigned.Structure() != dstStamp && assigned.Structure() != copy.Structure(), "a copy assignment takes its own stamp");
	}
}

int main()
{
	ClearRestartsIdsAndRestamps();
	MoveRestampsBothSides();
	if (failures) return 1;
	std::puts("entity world tests passed");
	return 0;
}
