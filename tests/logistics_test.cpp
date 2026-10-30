// The claims on nodes alone (GB/Game/Logistics.h): a claim keeps a second worker away, replaces another, is released by node or by worker, and the
// holder itself is never kept away. CPU only, no Tribe.
#include "GB/Game/Logistics.h"
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

int main()
{
	GB::NodeClaims claims;
	Check(claims.Empty() && !claims.HeldByOther("berry 1", "Ana"), "nothing is claimed at first");
	claims.Claim("berry 1", "Ana");
	Check(claims.HeldByOther("berry 1", "Bob"), "a second worker is kept away");
	Check(!claims.HeldByOther("berry 1", "Ana"), "the holder is not");
	Check(!claims.HeldByOther("berry 2", "Bob"), "another node is free");
	claims.Claim("berry 1", "Bob");
	Check(claims.All().at("berry 1") == "Bob", "a claim replaces the one before");
	claims.Claim("tree 4", "Bob");
	claims.Claim("rock 2", "Ana");
	claims.ReleaseBy("Bob");
	Check(claims.All().size() == 1 && claims.All().count("rock 2") == 1, "a worker that leaves frees all its nodes");
	claims.Release("rock 2");
	Check(claims.Empty(), "a node is released by name");
	claims.Claim("a", "x");
	claims.Clear();
	Check(claims.Empty(), "clear empties it");
	if (failures == 0) std::printf("logistics: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
