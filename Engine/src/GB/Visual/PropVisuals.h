#pragma once
#include "GB/Gameplay/Items.h"
#include "GB/Visual/VisualDefs.h"
#include <string>
#include <vector>

namespace GB
{
	// What a character shows in its hands, on its hips and on its back, decided from what its bag holds and what it is doing (pure, no GL, no
	// actor: the tribe's director applies it, a test checks it). The rules are data (PropRules, "visuals.props"):
	//  - the best tool of each family it carries is shown: in the right hand while the work that uses it runs (Chop takes the axe, tilling the
	//    hoe), else where that family rests (the axe on the right hip, the pickaxe on the left);
	//  - the best container (the basket) rests on the back;
	//  - on the way to a store (a "deposit" or "haul" job) a load of at least a few units is carried in the arms, with the look and the
	//    locomotion set of the first carry rule its main item matches (logs in the arms with "holding", stones in a crate with "box", a big
	//    load in the wheelbarrow);
	//  - some activities hold something in the left hand (firewood on the way to stoke the fire, berries to eat).
	// Two props never share a socket (the first by priority keeps it: hands, then the load, the tools and the container), and at most MaxShown.
	struct PropContext
	{
		const Inventory* Bag = nullptr;
		const ItemLibrary* Items = nullptr;
		std::string Activity;            // the tribe job as an activity key ("gather", "deposit", "farm.till"; see ActivityKey), "" = none
		std::string Ability;             // the ability running now ("Chop"), "" = none
	};

	struct PropPlacement
	{
		std::string Name;                // stable per slot: "tool" (the one in the right hand: the name an ability's EquipProp uses), "tool:Item.Tool.Pickaxe" (resting), "container", "load", "hand"
		std::string Look;                // a PropRules look
		std::string Socket;              // "hand_r", "hip_r", "back", "carry"...
		std::string Item;                // the item it shows ("" for a load or a hand look)
		bool operator==(const PropPlacement&) const = default;
	};

	struct PropChoice
	{
		std::vector<PropPlacement> Props;
		std::string Set;                 // the locomotion set the load asks for ("" = the free one)
		int LoadUnits = 0;               // units in the arms (0 = none)
		bool operator==(const PropChoice&) const = default;
	};

	PropChoice ChooseProps(const PropRules& rules, const PropContext& context);

	// The activity key of a tribe job: the job itself, but "farm" becomes "farm.<what it does>" from its detail ("till 0 - 1 2" -> "farm.till"),
	// and "socialize" by an elder is "teach".
	std::string ActivityKey(const std::string& job, const std::string& detail, const std::string& stage = "");
}
