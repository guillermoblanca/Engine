// V5 (docs/VISION_MUNDO_VIVO.md): eras as packs. A later pack adds needs and eras without repeating the first: needs merge by id and refuse a verb
// the engine already uses (or two needs with one verb); a new era goes right after the eras it follows, so the order still says which era a clan
// that knows the keys of two is in; and a building says what it provides to a need. CPU only.
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
	Check(LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "era": "bronze", "threshold": 45 } ] })", book).empty(), "a need is read");
	Check(book.Needs.size() == 1 && book.Find("faith") && book.ByVerb("pray") && book.Find("faith")->Threshold == 45.0f && book.Find("faith")->Era == "bronze", "its fields are kept");
	Check(LoadNeeds(R"({ "needs": [ { "id": "faith", "verb": "pray", "provision": "faith", "threshold": 30 }, { "id": "fun", "verb": "play", "item": "ball" } ] })", book).empty(), "a later pack adds and replaces");
	Check(book.Needs.size() == 2 && book.Find("faith")->Threshold == 30.0f && book.ByVerb("play"), "the same id is replaced, a new one added");
	const NeedBook before = book;
	Check(!LoadNeeds(R"({ "needs": [ { "id": "hunger2", "verb": "eat", "item": "berry" } ] })", book).empty() && book == before, "a verb the engine uses is refused and nothing changes");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "pray", "item": "berry" } ] })", book).empty() && book == before, "two needs cannot share a verb");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "dance" } ] })", book).empty(), "a need with nowhere to be done is refused");
	Check(!LoadNeeds(R"({ "needs": [ { "id": "joy", "verb": "dance", "item": "x", "colour": 1 } ] })", book).empty(), "an unknown field is refused");
	Check(LoadNeeds(R"({ "items": [] })", book).empty() && book == before, "a document without needs changes nothing");

	// eras: a later file inserts its era after the eras it follows
	TechTree tree;
	Check(LoadTechs(R"({ "knowledge": { "eras": [ { "id": "stone", "key": [] }, { "id": "copper", "key": [ "smelting" ], "from": [ "stone" ] }, { "id": "medieval", "key": [ "masonry" ], "from": [ "copper" ] } ] } })", tree).empty(), "the eras of the first pack");
	Check(LoadTechs(R"({ "eras": [ { "id": "bronze", "name": "Bronze Age", "key": [ "alloying" ], "from": [ "copper" ] } ] })", tree).empty(), "a second pack adds an era");
	const std::vector<EraDef>& eras = tree.Rules.Eras;
	Check(eras.size() == 4 && eras[2].Id == "bronze" && eras[3].Id == "medieval", "the new era goes right after the one it follows");
	Check(LoadTechs(R"({ "eras": [ { "id": "bronze", "name": "Age of Bronze", "key": [ "alloying" ], "from": [ "copper" ] } ] })", tree).empty() && tree.Rules.Eras.size() == 4 && tree.Rules.Eras[2].Name == "Age of Bronze", "the same id is replaced in place");
	Check(!LoadTechs(R"({ "eras": [ { "id": "x" }, { "id": "x" } ] })", tree).empty(), "two eras of one id in a file are refused");

	// buildings: what one provides
	std::map<std::string, BuildingDef> defs;
	Check(LoadBuildings(R"({ "buildings": { "temple": { "name": "Temple", "provides": { "faith": 1.5 }, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", defs).empty(), "a building that provides a need");
	Check(defs.count("temple") && defs["temple"].Provides.count("faith") && defs["temple"].Provides["faith"] == 1.5f, "it keeps what it provides");
	Check(!LoadBuildings(R"({ "buildings": { "bad": { "provides": { "faith": 0 }, "phases": [ { "name": "base", "work": 4, "needs": { "stone": 2 } } ] } } })", defs).empty(), "a provision of 0 is refused");

	if (failures == 0) std::printf("era packs: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
