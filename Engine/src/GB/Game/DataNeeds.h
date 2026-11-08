#pragma once
#include <string>
#include <vector>

namespace GB
{
	// V5 (docs/VISION_MUNDO_VIVO.md): needs that a content pack adds in data, beside the six the engine knows (AI/Needs.h). A pack says
	//   "needs": [ { "id": "faith", "name": "Faith", "era": "bronze", "decayPerDay": 40, "threshold": 45, "weight": 1.2,
	//                "verb": "pray", "provision": "faith", "item": "", "seconds": 8, "amount": 60 } ]
	// Each is a level of 0..100 on every member (an attribute of its ability system, so it is saved with it) that falls `decayPerDay` a day.
	// Below `threshold` the member wants to do the need's VERB: walk to a finished building that provides it (BuildingDef::Provides[provision]),
	// or take one `item` from the store, and spend `seconds` there; done, the level rises by `amount` (x the building's provision). The verb is
	// an option of the utility AI like any other: the agenda, the mood and the back-off read it by its name. A need with an `era` only counts
	// for a clan that has reached that era or one listed after it (the eras of the knowledge rules). It never kills: the six built in do.
	struct NeedDef
	{
		std::string Id;
		std::string Name;
		std::string Era;             // "" = from the start
		float DecayPerDay = 30.0f;   // level lost per day of game
		float Threshold = 40.0f;     // below it the member wants the verb
		float Weight = 1.0f;         // how much the verb is worth at its most urgent (a meal when starving is about 3)
		std::string Verb;
		std::string Provision;       // a building that provides it (BuildingDef::Provides); "" = none
		std::string Item;            // or an item it uses up from the store; "" = none
		float Seconds = 6.0f;
		float Amount = 50.0f;
		bool operator==(const NeedDef&) const = default;
	};

	struct NeedBook
	{
		std::vector<NeedDef> Needs;
		const NeedDef* Find(const std::string& id) const;
		const NeedDef* ByVerb(const std::string& verb) const;
		bool operator==(const NeedBook&) const = default;
	};

	// Reads the "needs" of a content document into the book: a need of an id it holds is replaced, a new one added (a later pack adds needs
	// without repeating those of the first). "" or what is wrong (then the book is as it was). A verb may not be one the engine already uses.
	std::string LoadNeeds(const std::string& json, NeedBook& book);
	std::string NeedsToJson(const NeedBook& book);
	bool ReservedVerb(const std::string& verb);
}
