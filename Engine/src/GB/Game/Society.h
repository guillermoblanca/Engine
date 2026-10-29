#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	// The society of a clan (H9 demography and inheritance + H10 relations, personality and culture): who is born to whom, how old everyone is,
	// what each one inherited and what kind of person it is, who likes whom, and what the clan as a whole has come to value. Plain CPU state kept by
	// member name (like ClanKnowledge), deterministic (every "die" is a hash of the seed, a name and a counter; no global random number generator),
	// and almost everything is data: the "society" section of a content file (SocietyRules, LoadSocietyRules; DefaultSocietyRules says the same as
	// assets/data/era1.json).
	//
	// - LIFE (H9): an age in years (a year is Life.DaysPerYear simulated days), three stages (child, adult, elder) with effects on the brain (a child
	//   leaves heavy work and learns faster, an elder teaches better and works slower), pairing, births when the clan has a surplus of food and a fire
	//   (Births), and death of old age (Life.OldAge, sharper as the age nears Life.MaxAge). Deaths by hunger, thirst or cold are the needs' (GB/AI/Needs.h).
	// - INHERITANCE: a few GENES per member, 0..1 (strength, endurance, perception, dexterity, longevity... and one per personality trait). A founder
	//   draws them around their mean; a child mixes its parents' (a random blend per gene: recombination) plus a small mutation. Skills are not
	//   inherited (GB/Game/Skills.h) but the rate at which they are learnt is (GeneDef::Learn); knowledge passes by teaching (parents first, see Mentors).
	// - PERSONALITY (H10): a few TRAITS (sociability, diligence, curiosity, boldness), each from its gene and the upbringing (the parents' traits and
	//   the clan's norm that leans on it). Genes and traits scale the options of the utility brain through data (OptionEffects).
	// - RELATIONS: a sparse graph of affinities (-1..1) between pairs, raised or lowered by what happens (working side by side, eating what another
	//   brought, sitting together at the camp, a quarrel over a node, a lesson...), fading slowly, with only the strongest kept per member; family
	//   (partner, parents, children, siblings) adds to it. Used to choose a partner, the companion to seek when lonely and who teaches whom.
	// - CULTURE: clan NORMS (0..1) that drift towards the average trait of the living adults and scale the options of everyone (the agenda of the
	//   clan), and TRADITIONS founded once an event has happened often enough ("Fire vigil" after the 30th time the campfire was fed).

	// What a value in 0..1 does to the options of the brain: option key -> strength. The keys are option kinds ("explore", "experiment", "socialize",
	// "gather", "rest", "sleep", "tool", "fire", "stoke"...) or aptitude families ("woodcutting", "mining"...) for the gathers of that family. A
	// centred value (genes, traits, norms) gives the factor 1 + strength x (2 v - 1): an average member is untouched, one at 1 gets 1 + strength,
	// one at 0 gets 1 - strength. A tradition (which is either kept or not) gives 1 + strength.
	using OptionEffects = std::map<std::string, float>;

	struct GeneDef
	{
		float Mean = 0.5f;               // founders draw the gene around this ...
		float Spread = 0.2f;             // ... up to this much either way (a bell made of three draws)
		OptionEffects Effects;           // what it does to the options ("strength": woodcutting 0.25, mining 0.25)
		std::map<std::string, float> Learn;   // aptitude family -> bias of the learning rate: x (1 + b (2 g - 1))
		std::map<std::string, float> Pace;    // aptitude family -> bias of the speed of the work: x (1 + b (2 g - 1))
		bool operator==(const GeneDef&) const = default;
	};

	// A personality trait: its own gene (Mean, Spread) and how much of it is nature (Heritability; the rest is upbringing).
	struct TraitDef
	{
		float Mean = 0.5f;
		float Spread = 0.25f;
		float Heritability = 0.5f;
		OptionEffects Effects;           // "curiosity": experiment 0.8, explore 0.5
		bool operator==(const TraitDef&) const = default;
	};

	struct LifeRules
	{
		float DaySeconds = 0.0f;         // seconds of a simulated day for the society (0 = the day of the needs' clock, NeedsConfig::DaySeconds)
		float DaysPerYear = 4.0f;        // simulated days in a year of age
		float ChildUntil = 14.0f;        // a child until this age ...
		float ElderFrom = 52.0f;         // ... an elder from this one
		float MaxAge = 72.0f;            // nobody lives past this (moved by the "longevity" gene, Longevity years either way)
		float Longevity = 8.0f;
		float OldAge = 0.35f;            // yearly chance of dying at MaxAge, growing as the square of the way from ElderFrom to it
		float FounderMin = 16.0f;        // the ages the founders of a clan are given
		float FounderMax = 40.0f;
		// The stages on the brain: heavy work (Heavy: families and option kinds) is worth ChildWork to a child and ElderWork to an elder; a child learns
		// ChildLearn times as fast, an elder teaches ElderTeach times as well (a child ChildTeach); children and elders work at ChildPace and ElderPace.
		std::vector<std::string> Heavy = { "woodcutting", "mining", "firekeeping", "tool", "fire", "stoke" };
		float ChildWork = 0.15f, ElderWork = 0.6f;
		float ChildLearn = 1.6f, ChildTeach = 0.5f, ElderTeach = 1.5f;
		float ChildPace = 0.7f, ElderPace = 0.75f;
		float ChildHeight = 0.55f;       // a newborn is this much of an adult's height and grows to 1 at ChildUntil (ITribeBody::SetGrowth)
		std::string ChildRole = "forager";   // the trade a child is given (the review of the trades leaves children alone)
		bool operator==(const LifeRules&) const = default;
	};

	struct BirthRules
	{
		float PairAge = 16.0f;           // adults this old look for a partner ...
		float AgeGap = 15.0f;            // ... no more than this many years apart ...
		float PairMin = -0.1f;           // ... and with an affinity of at least this (relations), the best liked first
		float MinAge = 16.0f, MaxAge = 42.0f;   // the ages of a mother
		float Spacing = 2.0f;            // years between two children of one mother
		float Chance = 0.12f;            // chance a day for a couple when all of it holds
		float FoodPerMember = 1.5f;      // food in the store per member the clan needs before it has children ...
		bool NeedsFire = true;           // ... and a campfire (shelter)
		int MaxMembers = 200;            // the clan stops growing here
		bool operator==(const BirthRules&) const = default;
	};

	struct RelationRules
	{
		std::map<std::string, float> Events;   // what each event does to the affinity of a pair ("work" 0.02, "conflict" -0.1, "rescue" 0.4...)
		float DecayPerDay = 0.03f;       // an affinity loses this share of itself a day (towards 0)
		float Keep = 0.02f;              // an affinity this close to 0 is dropped (the graph stays sparse)
		int MaxPerMember = 12;           // the bonds a member keeps (the weakest goes)
		float Kin = 0.3f;                // family adds this to the affinity (partner, parents, children, siblings)
		float CampSeconds = 30.0f;       // how often members sitting together at the camp grow closer ...
		float CampRadius = 6.0f;         // ... those this close ...
		int CampNearest = 2;             // ... each with its nearest few
		float WorkRadius = 10.0f;        // members gathering this close to each other work "together"
		bool operator==(const RelationRules&) const = default;
	};

	// A norm of the clan: where it starts, the trait of the members it drifts towards ("" = it does not drift) and what it does to the options.
	struct NormDef
	{
		float Start = 0.5f;
		std::string Trait;
		OptionEffects Effects;
		bool operator==(const NormDef&) const = default;
	};

	// A tradition: founded the Count-th time Event happens in the clan ("stoke", "fire", "craft", "learn", "birth", "death", "pair"), then kept.
	struct TraditionDef
	{
		std::string Name;
		std::string Event;
		int Count = 10;
		OptionEffects Effects;
		bool operator==(const TraditionDef&) const = default;
	};

	struct CultureRules
	{
		std::map<std::string, NormDef> Norms;
		float Drift = 0.05f;             // a day, a norm moves this share of the way to the members' average trait
		std::map<std::string, TraditionDef> Traditions;
		bool operator==(const CultureRules&) const = default;
	};

	struct SocietyRules
	{
		LifeRules Life;
		BirthRules Births;
		std::map<std::string, GeneDef> Genes;
		float Mutation = 0.06f;          // a child's gene moves up to this much from its parents' blend
		std::map<std::string, TraitDef> Traits;
		RelationRules Relations;
		CultureRules Culture;
		uint32_t Seed = 1;
		bool operator==(const SocietyRules&) const = default;
	};

	SocietyRules DefaultSocietyRules();   // assets/data/era1.json says the same in its "society" section
	// { "society": { "seed", "mutation", "life": { ... }, "births": { ... }, "genes": { "<gene>": { "mean", "spread", "effects": {}, "learn": {}, "pace": {} } },
	//   "traits": { "<trait>": { "mean", "spread", "heritability", "effects": {} } }, "relations": { "events": {}, ... }, "culture": { "drift", "norms": { "<norm>":
	//   { "start", "trait", "effects": {} } }, "traditions": { "<id>": { "name", "event", "count", "effects": {} } } } } }. Each part ("life", "births", "genes",
	// "traits", "relations", "culture") replaces that part whole (what it does not say is the default); a file without the section changes nothing; on a
	// mistake it returns why and changes nothing.
	std::string LoadSocietyRules(const std::string& json, SocietyRules& rules);
	std::string LoadSocietyRulesFile(const std::string& path, SocietyRules& rules);
	std::string SocietyRulesToJson(const SocietyRules& rules);   // one JSON object, as the section reads it back

	struct Person
	{
		float Age = 25.0f;               // years
		bool Female = false;
		std::string Mother, Father, Partner;
		std::map<std::string, float> Genes;    // gene (and personality trait gene) -> 0..1
		std::map<std::string, float> Traits;   // personality -> 0..1 (gene and upbringing)
		int Generation = 0;              // founders 0, their children 1...
		int Children = 0;
		float LastBirth = -100.0f;       // the age at which (as a mother) she last had a child
		bool Dead = false;
		std::string Cause;
		bool operator==(const Person&) const = default;
	};

	// Something worth telling: "pair" (Who, Other), "birth" (Who = mother, Other = father: the caller gives the child a name and calls Bear), "death"
	// (Who, Text = cause), "adult" / "elder" (Who comes of age / grows old), "norm" (Who = norm), "tradition" (Who = id) and the Text for the log.
	struct SocietyEvent
	{
		std::string Kind, Who, Other, Text;
	};

	// The clan on a day: its food, how many live, whether it has a fire (shelter). Births are counted by the caller as members join.
	struct ClanState
	{
		float Food = 0.0f;
		int Alive = 0;
		bool Shelter = false;
	};

	class Society
	{
	public:
		void Clear();

		// ---- members
		// A founder joins (nothing happens when it is there already): its age between FounderMin and FounderMax, its sex alternating with the founders
		// before it (a clan of four is two couples), its genes and traits drawn from the seed and its name.
		const Person& Join(const SocietyRules& rules, const std::string& name);
		// A child of `mother` and `father` (who may be gone): its genes mixed from theirs with a mutation, its traits from its genes and their traits.
		const Person& Bear(const SocietyRules& rules, const std::string& child, const std::string& mother, const std::string& father);
		// Out of the clan for good (`tribe remove`): its bonds go; its children keep its name as a parent.
		void Leave(const std::string& name);
		// It died (of anything): its partner is free, its bonds go, the clan notes a "death".
		std::vector<SocietyEvent> Died(const SocietyRules& rules, const std::string& name, const std::string& cause);
		// H13: a member from another clan joins as it was (age, sex, genes, traits; its bonds stayed behind). Nothing happens when the name is here.
		void Admit(const std::string& name, const Person& person);
		// H13: two members of this society become partners (a marriage across clans, once both are here); old partners are free. False when either
		// is not here, is dead, or they are the same.
		bool Marry(const SocietyRules& rules, const std::string& a, const std::string& b);
		const Person* Find(const std::string& name) const;
		Person* Find(const std::string& name);
		const std::map<std::string, Person>& People() const { return m_People; }
		int Alive() const;
		// For tests and the editor: set an age, a gene or a trait (traits and genes are clamped to 0..1).
		void SetAge(const std::string& name, float years);
		void SetGene(const std::string& name, const std::string& gene, float value);
		void SetTrait(const std::string& name, const std::string& trait, float value);

		// ---- life
		enum class Stage { Child, Adult, Elder };
		Stage StageOf(const SocietyRules& rules, const std::string& name) const;
		static const char* StageName(Stage s) { return s == Stage::Child ? "child" : s == Stage::Elder ? "elder" : "adult"; }
		bool IsChild(const SocietyRules& rules, const std::string& name) const { return Find(name) && StageOf(rules, name) == Stage::Child; }
		// How fast it learns an aptitude family (stage and genes) and how well it teaches (stage); how fast it works at a family (stage and genes);
		// the height it has grown to (0..1 of an adult's). All 1 for a name the society does not know.
		float LearnRate(const SocietyRules& rules, const std::string& name, const std::string& family) const;
		float TeachRate(const SocietyRules& rules, const std::string& name) const;
		float WorkPace(const SocietyRules& rules, const std::string& name, const std::string& family) const;
		float Growth(const SocietyRules& rules, const std::string& name) const;
		// What the society makes of an option of the brain: `kind` is the option ("gather", "explore"...), `family` the aptitude family of its node
		// ("" when it has none). The three parts (nature: genes and traits; life: the stage; culture: norms and traditions) are given apart for the
		// inspector; the product is the factor.
		struct Factors { float Nature = 1.0f, Life = 1.0f, Culture = 1.0f; float Total() const { return Nature * Life * Culture; } };
		Factors OptionFactor(const SocietyRules& rules, const std::string& name, const std::string& kind, const std::string& family) const;

		// ---- the days: call Tick with the time; on every new day it ages everyone, has the old die, pairs who is ready, has the couples who can
		// have a child (a "birth" event: the caller names the child and calls Bear), lets the affinities fade and the norms drift.
		bool Tick(const SocietyRules& rules, float dt, float daySeconds);   // true when a day has passed (then call Day)
		std::vector<SocietyEvent> Day(const SocietyRules& rules, const ClanState& clan);
		int Days() const { return m_Days; }

		// ---- relations
		// The bond of a pair (what has happened between them), its family tie and the affinity both make (bond + Kin when family), in -1..1.
		float Bond(const std::string& a, const std::string& b) const;
		std::string Kin(const std::string& a, const std::string& b) const;   // "partner", "mother", "father", "child", "sibling" (what b is to a) or ""
		float Affinity(const SocietyRules& rules, const std::string& a, const std::string& b) const;
		// Something happened between a and b: the event's weight in Relations.Events (times `scale`) is added to their bond. Unknown events do nothing.
		void Relate(const SocietyRules& rules, const std::string& a, const std::string& b, const std::string& event, float scale = 1.0f);
		void Adjust(const SocietyRules& rules, const std::string& a, const std::string& b, float delta);
		// The bonds of a member, the strongest (by size) first: other -> bond.
		std::vector<std::pair<std::string, float>> BondsOf(const std::string& name) const;
		size_t BondCount() const { return m_Bonds.size(); }
		// Who it would rather learn from: its parents (alive), then those it likes best (positive affinity), at most `count`.
		std::vector<std::string> Mentors(const SocietyRules& rules, const std::string& name, size_t count = 4) const;
		// Of `candidates`, the one it likes best (ties: the first); "" when there are none.
		std::string Favourite(const SocietyRules& rules, const std::string& name, const std::vector<std::string>& candidates) const;

		// ---- culture
		const std::map<std::string, float>& Norms() const { return m_Norms; }
		float Norm(const SocietyRules& rules, const std::string& norm) const;
		const std::vector<std::string>& Traditions() const { return m_Traditions; }   // in the order they were founded
		const std::map<std::string, int>& Counts() const { return m_Counts; }
		// An event of the clan ("stoke", "birth"...): counted, and a tradition is founded when its count is reached.
		std::vector<SocietyEvent> Note(const SocietyRules& rules, const std::string& event);
		int Births() const { return m_Births; }
		int Deaths() const { return m_Deaths; }

		// ---- for people
		std::string Family(const SocietyRules& rules, const std::string& name) const;     // age, stage, parents, partner, children, siblings
		std::string Character(const SocietyRules& rules, const std::string& name) const;  // genes and traits
		std::string Relations(const SocietyRules& rules, const std::string& name) const;  // the bonds, with family marked
		std::string Culture(const SocietyRules& rules) const;                             // norms, traditions, births and deaths

		// ---- saving (the tribe's "ai" section, key "society"); FromJson: false (nothing changed) when it is not readable
		std::string ToJson() const;
		bool FromJson(const std::string& json);
		// (the index of links is left out: it is made from the bonds, and may keep empty entries a loaded society does not have)
		bool operator==(const Society& o) const
		{
			return m_People == o.m_People && m_Bonds == o.m_Bonds && m_Norms == o.m_Norms && m_Counts == o.m_Counts && m_Traditions == o.m_Traditions
				&& m_Founders == o.m_Founders && m_Days == o.m_Days && m_Births == o.m_Births && m_Deaths == o.m_Deaths && m_DayTimer == o.m_DayTimer;
		}

	private:
		double Roll(const SocietyRules& rules, const std::string& name, uint32_t salt, int64_t n) const;   // deterministic 0..1
		double Bell(const SocietyRules& rules, const std::string& name, uint32_t salt, int64_t n) const;   // -1..1, bell-shaped
		void Unlink(const std::string& name);
		void Trim(const std::string& name, int keep);

		std::map<std::string, Person> m_People;
		std::map<std::pair<std::string, std::string>, float> m_Bonds;   // (a, b) with a < b -> bond
		std::map<std::string, std::set<std::string>> m_Links;          // name -> the others it has a bond with
		std::map<std::string, float> m_Norms;                          // the norms that moved from their start (others are at Start)
		std::map<std::string, int> m_Counts;                            // events of the clan
		std::vector<std::string> m_Traditions;
		int m_Founders = 0, m_Days = 0, m_Births = 0, m_Deaths = 0;
		double m_DayTimer = 0.0;
	};
}
