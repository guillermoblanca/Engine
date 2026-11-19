#pragma once
// T306 (C2 of documentation/01_diseno/MODO_COLONIA.md §4.3): the categories of work and the jobs inside each one, as data ("work.categories" of
// the era1 pack). The table of priorities of each person (WorkPriorities.h) numbers them; this part only says what they are and which work of the
// clan (a job of the board or an option of the brain, by its kind and what it is about) falls under each job.
//
// A job matches by patterns: "kind" (any work of that kind) or "kind:what", where `what` is the work's target, the item it is about or a tag
// of that item ("gather:Item.Food" is every gather of food; "farm:till" the tilling of the field; "make:Item.Tool" the making of a tool). The
// most specific pattern wins (one with a `what` over one without; the longer `what` over the shorter; then the first in the data). Aliases say
// that a kind is another one for the matching ("designate" is a gather at the node the player marked). The free patterns are work no table
// rules (the drink at the well); anything no job matches is free too (eating, sleeping, talking: the needs and the social life).
//
// Plain data and CPU, deterministic (every list in data order).
#include <map>
#include <string>
#include <vector>

namespace GB
{
	struct WorkJobDef
	{
		std::string Id;                       // "hunt", "build", "tools"
		std::vector<std::string> Match;       // "kind" or "kind:what"
		bool operator==(const WorkJobDef&) const = default;
	};

	struct WorkCategoryDef
	{
		std::string Id;                       // "food", "construction", "crafting"
		std::vector<WorkJobDef> Jobs;
		int Default = 3;                      // the number a new person's table gives it (1 first .. 10 last, 0 = "-") ...
		std::map<std::string, int> Suggest;   // ... unless its trade says otherwise: role -> number (C-5: the trade only suggests)
		bool operator==(const WorkCategoryDef&) const = default;
	};

	struct WorkCategories
	{
		std::vector<WorkCategoryDef> List;              // in the order the table shows them
		std::map<std::string, std::string> Aliases;     // kind -> the kind it is matched as
		std::vector<std::string> Free;                  // patterns of work no table rules
		bool operator==(const WorkCategories&) const = default;

		bool Empty() const { return List.empty(); }
		const WorkCategoryDef* Category(const std::string& id) const;
		// The category a job id belongs to (nullptr: no such job).
		const WorkCategoryDef* CategoryOfJob(const std::string& job) const;
		// The job a piece of work falls under ("" = free: no table rules it). `item` is the item it is about ("" = none) and `tags` that item's tags.
		std::string JobOf(const std::string& kind, const std::string& target, const std::string& item, const std::vector<std::string>& tags) const;
	};

	// Reads the value of "work.categories" (its JSON text): { "list": [ { "id", "default", "suggest": { role: n }, "jobs": [ { "id", "match": [ ... ] } ] } ],
	// "aliases": { kind: kind }, "free": [ pattern ] }. Ids are unique across categories and jobs; numbers 0..10. On a mistake it returns why and
	// changes nothing.
	std::string LoadWorkCategories(const std::string& json, WorkCategories& out);
	std::string WorkCategoriesToJson(const WorkCategories& c);   // the same object, as it reads back
}
