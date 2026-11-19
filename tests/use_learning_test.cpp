// T181: no eras. Knowledge is won by USING materials (GB/Game/UseLearning.h): a member who works clay gains understanding of pottery, a curious one
// gains it faster, nothing is gained from a material the clan has not found, what a technology requires only speeds or slows it (learning without
// it is possible but slower) and what it needs in the world is a hard gate (smelting without a kiln or a forge: never). CPU only, a tribe without a
// scene: the members' bodies are stand-ins that only hold a bag.
#include "GB/Game/GameContent.h"
#include "GB/Game/Tribe.h"
#include "GB/Game/UseLearning.h"
#include "GB/Gameplay/ShippedContent.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	class StandIn : public GB::ITribeBody
	{
	public:
		bool Busy() const override { return false; }
		bool Failed() const override { return false; }
		glm::vec2 Position() const override { return { 0.0f, 0.0f }; }
		bool Do(const std::string&, std::string&) override { return true; }
		GB::Inventory& Bag() override { return m_Bag; }
	private:
		GB::Inventory m_Bag{ 12, 0.0f };
	};

	float Progress(const GB::Tribe& t, const std::string& name, const std::string& tech)
	{
		const GB::AgentKnowledge* k = t.Knowledge().Of(name);
		if (!k) return -1.0f;
		const auto it = k->Progress.find(tech);
		return it == k->Progress.end() ? 0.0f : it->second;
	}

	const GB::TribeMember& Member(const GB::Tribe& t, const std::string& name)
	{
		return *std::find_if(t.Members().begin(), t.Members().end(), [&](const GB::TribeMember& m) { return m.Name == name; });
	}
}

using namespace GB;

int GBTest_use_learning(int, char**)
{
	ItemLibrary items;
	RecipeBook recipes;
	std::map<std::string, ResourceNodeDef> nodes;
	RegisterEraOneItems(items, recipes, nodes);
	const TechTree techs = DefaultTechs();
	Check(techs.Find("pottery") && techs.Find("smelting") && !techs.Rules.Use.Materials.empty(), "the data has the tree and the rules of learning by use");
	Check(techs.Rules.Use.Materials.count("clay") && techs.Rules.Use.Materials.at("clay").count("pottery"), "clay leads to pottery");
	Check(techs.Find("pottery")->Physical.Materials == std::vector<std::string>({ "clay" }), "pottery needs clay found");
	Check(techs.Find("smelting")->Physical.Stations == std::vector<std::string>({ "kiln", "forge" }), "smelting needs a kiln or a forge");

	StandIn ana, biel, cleo;
	Tribe t;
	t.Config.Society = DefaultSocietyRules();
	t.AddMember("Ana", "forager", &ana);
	t.AddMember("Biel", "forager", &biel);
	t.AddMember("Cleo", "forager", &cleo);
	t.SetTechs(&techs);
	t.Knowledge().RaiseTier(2);   // T323: pottery is of step 2 (a clan with its longhouse; this one has no plan to open it)
	t.People().SetTrait("Ana", "curiosity", 0.0f);
	t.People().SetTrait("Biel", "curiosity", 1.0f);
	t.People().SetTrait("Cleo", "curiosity", 1.0f);
	for (const char* n : { "Ana", "Biel", "Cleo" }) t.People().SetGene(n, "intellect", 1.0f);   // (the intellect is tried apart, below)
	for (const char* n : { "Ana", "Biel" }) Check(t.Knowledge().Grant(n, "wattle_and_daub").empty(), "wattle and daub is granted");
	Check(t.Knowledge().Knows("Cleo", "fire_making") && !t.Knowledge().Knows("Cleo", "wattle_and_daub"), "Cleo knows fire but not wattle and daub (a requirement of pottery)");

	// curiosity: the society's factor on "learn" (the trait curiosity)
	const float paceAna = UseLearning::Pace(t, Member(t, "Ana")), paceBiel = UseLearning::Pace(t, Member(t, "Biel"));
	Check(paceBiel > paceAna && paceAna > 0.0f, "the curious one learns faster");
	Check(paceAna >= techs.Rules.Use.PaceMin - 1e-5f && paceBiel <= techs.Rules.Use.PaceMax + 1e-5f, "within the pace the data allows (curiosity says how much, not nine times more)");

	// no clay found: nothing is learnt from it
	Check(UseLearning::Have(t, "clay") == 0.0f && !UseLearning::Physical(t, "pottery") && !t.Knowledge().CanLearn("Biel", "pottery"), "no clay found: pottery cannot be learnt");
	Check(UseLearning::Handled(t, Member(t, "Biel"), { "clay" }).empty() && Progress(t, "Biel", "pottery") == 0.0f, "handling clay the clan has not found teaches nothing");

	// clay found (in a bag): working it teaches pottery, faster to the curious, slower without what pottery requires
	ana.Bag().Add(items, "clay", 3);
	UseLearning::Forget();   // (the answers of the physical gate are kept per instant; the clock has not moved)
	Check(UseLearning::Have(t, "clay") == 3.0f && UseLearning::Physical(t, "pottery"), "clay found: the physical gate of pottery is open");
	for (const char* n : { "Ana", "Biel", "Cleo" }) UseLearning::Handled(t, Member(t, n), { "clay" });
	const float pa = Progress(t, "Ana", "pottery"), pb = Progress(t, "Biel", "pottery"), pc = Progress(t, "Cleo", "pottery");
	Check(pa > 0.0f && pb > pa, "working clay gives understanding of pottery, more to the curious one");
	Check(pc > 0.0f && pc < pb, "without wattle and daub (which pottery requires) it is possible but slower");
	std::printf("one use of clay: pottery Ana (curiosity 0) %.4f, Biel (curiosity 1) %.4f, Cleo (curiosity 1, no wattle and daub) %.4f\n", pa, pb, pc);

	// enough use and it is learnt; the line says which technology
	bool learnt = false;
	for (int i = 0; i < 2000 && !learnt; ++i)
		for (const KnowledgeEvent& e : UseLearning::Handled(t, Member(t, "Cleo"), { "clay" }))
			learnt = learnt || (e.Tech == "pottery" && e.Who == "Cleo");
	Check(learnt && t.Knowledge().Knows("Cleo", "pottery"), "Cleo learns pottery by working clay, without wattle and daub");

	// the physical gate: copper ore found, but no kiln and no forge
	biel.Bag().Add(items, "copper_ore", 5);
	UseLearning::Forget();
	Check(UseLearning::Have(t, "copper_ore") == 5.0f && !UseLearning::Physical(t, "smelting"), "copper without a kiln or a forge: the gate of smelting stays shut");
	for (int i = 0; i < 500; ++i) UseLearning::Handled(t, Member(t, "Biel"), { "copper_ore" });
	Check(Progress(t, "Biel", "smelting") == 0.0f && !t.Knowledge().Knows("Biel", "smelting"), "working copper ore 500 times teaches nothing of smelting");
	std::string recipe;
	Inventory store(40, 0.0f), bag(12, 0.0f);
	store.Add(items, "copper_ore", 5);
	Check(t.Knowledge().NextExperiment("Biel", 1e6, store, bag, recipes, &recipe) != "smelting", "nor can it be tried by experimenting");

	// T181 (2): writing needs clay and a kiln (the fired tablet): clay is found, there is no kiln
	Check(techs.Find("writing") && techs.Find("writing")->Physical.Stations == std::vector<std::string>({ "kiln" }) && UseLearning::Have(t, "clay") > 0.0f
		&& !UseLearning::Physical(t, "writing") && !t.Knowledge().CanLearn("Biel", "writing"), "writing without a kiln is impossible, clay or not");
	for (int i = 0; i < 500; ++i) UseLearning::Handled(t, Member(t, "Biel"), { "clay" });
	Check(Progress(t, "Biel", "writing") == 0.0f && !t.Knowledge().Knows("Biel", "writing"), "working clay 500 times teaches no writing without the kiln");

	// T181 (2): the intellect. Storytelling (an idea, nothing physical) is worked out alone only with intellect >= its complexity; learnt from others
	// by anyone, slower the more is lacking
	{
		const float hard = Complexity(techs, "storytelling");
		Check(hard > 0.1f && hard < 0.9f, "storytelling has a complexity of the data");
		StandIn dull, bright, dull2, bright2;
		Tribe u;
		u.Config.Society = DefaultSocietyRules();
		u.AddMember("Dull", "forager", &dull);
		u.AddMember("Bright", "forager", &bright);
		u.AddMember("Dull2", "forager", &dull2);
		u.AddMember("Bright2", "forager", &bright2);
		u.SetTechs(&techs);
		u.Knowledge().RaiseTier(1);   // T323: storytelling is of step 1
		for (const char* n : { "Dull", "Dull2" }) u.People().SetGene(n, "intellect", 0.05f);
		for (const char* n : { "Bright", "Bright2" }) u.People().SetGene(n, "intellect", 0.95f);
		Check(std::abs(UseLearning::Intellect(u, "Dull") - 0.05f) < 1e-4f && std::abs(u.Knowledge().Intellect("Bright") - 0.95f) < 1e-4f, "the intellect is the society's gene");
		Check(!u.Knowledge().CanInvent("Dull", "storytelling") && u.Knowledge().CanLearn("Dull", "storytelling") && u.Knowledge().CanInvent("Bright", "storytelling"),
			"below the complexity it cannot be invented, but it can be learnt; above, it can be invented");
		Inventory store2(40, 0.0f), bag2(12, 0.0f);
		bool dullTried = false;
		for (int i = 0; i < 400; ++i)
		{
			const double now = 1000.0 * i;
			dullTried = dullTried || u.Knowledge().NextExperiment("Dull", now, store2, bag2, recipes) == "storytelling";
			u.Knowledge().Experiment("Dull", now, store2, bag2, recipes);
			if (!u.Knowledge().Knows("Bright", "storytelling")) u.Knowledge().Experiment("Bright", now, store2, bag2, recipes);
		}
		Check(!dullTried && !u.Knowledge().Knows("Dull", "storytelling"), "the dull one never chooses it nor works it out alone");
		Check(u.Knowledge().Knows("Bright", "storytelling"), "the bright one works it out by experimenting");
		u.Knowledge().Instruct("Dull2", "storytelling", 0.2f, "from Bright");
		u.Knowledge().Instruct("Bright2", "storytelling", 0.2f, "from Bright");
		const float slow = Progress(u, "Dull2", "storytelling"), fast = Progress(u, "Bright2", "storytelling");
		Check(slow > 0.0f && slow < fast, "taught the same, the one with less intellect understands less of it (but something)");
		Check(std::abs(slow / fast - GraspFactor(techs, "storytelling", 0.05f)) < 1e-3f && GraspFactor(techs, "storytelling", 0.05f) >= techs.Rules.GraspFloor - 1e-6f,
			"by GraspFactor, never below the floor");
		bool taught = false;
		for (int i = 0; i < 100 && !taught; ++i) taught = !u.Knowledge().Instruct("Dull2", "storytelling", 0.2f, "from Bright").empty() && u.Knowledge().Knows("Dull2", "storytelling");
		Check(taught, "and in the end it learns it from the one who knows");
		std::printf("intellect: storytelling (complexity %.2f) taught 0.2: intellect 0.05 -> %.3f, 0.95 -> %.3f\n", hard, slow, fast);

		// the gene goes with the society's save
		Society back;
		Check(back.FromJson(u.People().ToJson()) && back.Find("Dull") && std::abs(back.Find("Dull")->Genes.at("intellect") - 0.05f) < 1e-4f, "the intellect is saved and loaded with the other genes");
	}

	// survival first is a priority, not a block: without the basics an experiment is worth less, more to the curious one, never nothing
	Check(!UseLearning::Basics(t), "an empty store and no fire: the basics are not covered");
	const float roomAna = UseLearning::Room(t, Member(t, "Ana")), roomBiel = UseLearning::Room(t, Member(t, "Biel"));
	Check(roomBiel > roomAna && roomAna > 0.0f && roomBiel < 1.0f, "without the basics experimenting is damped, less for the curious one");
	std::printf("room to experiment without the basics: Ana %.3f, Biel %.3f\n", roomAna, roomBiel);

	if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
	std::printf("use learning: all checks passed\n");
	return 0;
}
