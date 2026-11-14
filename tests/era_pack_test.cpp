// V5 (docs/VISION_MUNDO_VIVO.md) + T181: needs in data. A later file adds needs without repeating the first: needs merge by id and refuse a verb
// the engine already uses (or two needs with one verb); there are no eras (a need says when it counts by the clan's technology and size, and an
// "era" anywhere is refused); and a building says what it provides to a need. CPU only.
#include "GB/Game/DataNeeds.h"
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Techs.h"
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

int GBTest_era_pack(int, char**)
{
	// needs: read, merged by id, checked
	NeedBook book;
	Check(LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "tech": "ritual", "members": 20, "threshold": 45 } ] })", book).empty(), "a need is read");
	Check(book.Needs.size() == 1 && book.Find("faith") && book.ByVerb("pray") && book.Find("faith")->Threshold == 45.0f && book.Find("faith")->Tech == "ritual"
		&& book.Find("faith")->Members == 20, "its fields are kept (when it counts: the rites and 20 living)");
	{
		NeedBook back;
		Check(LoadNeeds(NeedsToJson(book), back).empty() && back == book, "the needs round-trip");
		NeedBook keep = book;
		Check(!LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "era": "bronze" } ] })", keep).empty() && keep == book, "T181: a need with an era is refused");
		Check(!LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "members": -1 } ] })", keep).empty(), "a negative size is refused");
	}
	Check(LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "threshold": 30 }, { "id": "fun", "verb": "play", "item": "ball" } ] })", book).empty(), "a later pack adds and replaces");
	Check(book.Needs.size() == 2 && book.Find("faith")->Threshold == 30.0f && book.ByVerb("play"), "the same id is replaced, a new one added");
	const NeedBook before = book;
	Check(!LoadNeeds(R"({ "needs": [ { "id": "hunger2", "verb": "eat", "item": "berry" } ] })", book).empty() && book == before, "a verb the engine uses is refused and nothing changes");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "pray", "item": "berry" } ] })", book).empty() && book == before, "two needs cannot share a verb");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "dance" } ] })", book).empty(), "a need with nowhere to be done is refused");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "dance", "item": "x", "colour": 1 } ] })", book).empty(), "an unknown field is refused");
	Check(LoadNeeds(R"({ "items": [] })", book).empty() && book == before, "a document without needs changes nothing");

	// T181: there are no eras, in the knowledge rules, at the top of a file or on a technology
	TechTree tree;
	Check(!LoadTechs(R"({ "knowledge": { "eras": [ { "id": "stone", "key": [] } ] } })", tree).empty(), "eras in the knowledge rules are refused");
	Check(!LoadTechs(R"({ "eras": [ { "id": "bronze", "key": [ "alloying" ] } ] })", tree).empty(), "eras at the top of a file are refused");
	Check(!LoadTechs(R"({ "techs": [ { "id": "alloying", "era": 3 } ] })", tree).empty() && tree.Techs.empty(), "a technology with an era is refused");

	// buildings: what one provides
	std::map<std::string, BuildingDef> defs;
	Check(LoadBuildings(R"({ "buildings": { "temple": { "name": "Temple", "provides": { "faith": 1.5 }, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", defs).empty(), "a building that provides a need");
	Check(defs.count("temple") && defs["temple"].Provides.count("faith") && defs["temple"].Provides["faith"] == 1.5f, "it keeps what it provides");
	Check(!LoadBuildings(R"({ "buildings": { "bad": { "provides": { "faith": 0 }, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", defs).empty(), "a provision of 0 is refused");

	if (failures == 0) std::printf("era packs: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
