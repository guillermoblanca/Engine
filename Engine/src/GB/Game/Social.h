#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// The social life of a clan (docs/VIDA_SOCIAL.md): what its members do TOGETHER when they are not working. Conversations at the camp and the
	// fire (chat, stories, gossip, teaching, consoling the grieving, settling a quarrel, greetings, sharing food, grooming, children's play), rites
	// and rites of passage gathered in a circle (naming, coming of age, pairing, funerals, seasonal festivals, the first hunt, healing, the
	// traditions of H10 kept as recurring rites), beliefs (animism, ancestor spirits, the sky and the seasons: taboos, omens read from the world, a
	// sacred place, a shaman) and food variety (what each food sates, a diet remembered meal by meal). Plus the clan's snares and pit traps of the
	// hunt. Plain CPU state kept by member name (like the Society of H9/H10), deterministic (every "die" is a hash of the seed, names and a counter)
	// and nearly all data: the "social" section of a content file (SocialRules, LoadSocialRules; DefaultSocialRules says the same as
	// assets/data/social.json). The hooks into the tribe are in TribeSocial.cpp.

	// What a value in 0..1 does to the options of the brain (as GB/Game/Society.h's OptionEffects): option kind or aptitude family -> strength; a
	// value v gives the factor 1 + strength x (2 v - 1).
	using SocialEffects = std::map<std::string, float>;

	// Mood (0..1) and grief (0..1) of a member.
	struct MoodRules
	{
		float Start = 0.5f;              // a founder's mood (0.5: its effects are neutral)
		float Rest = 0.5f;               // where the mood returns to with the days ...
		float Return = 0.3f;             // ... this share of the way a day ...
		float Grief = 0.5f;              // ... less this x its grief
		SocialEffects Effects;           // what the mood does to the options ("gather" 0.15, "rest" -0.2, "talk" 0.2...)
		float Low = 0.3f, High = 0.75f;  // the log says when someone falls under Low or rises over High
		bool operator==(const MoodRules&) const = default;
	};
	struct GriefRules
	{
		float Kin = 0.7f;                // grief at the death of family (partner, parent, child, sibling) ...
		float Friend = 0.5f;             // ... and of a friend, x the affinity (only a positive one)
		float DecayPerDay = 0.2f;        // grief fades this much a day
		float Mood = -0.15f;             // the mood the clan loses at a death (everyone)
		bool operator==(const GriefRules&) const = default;
	};

	// One kind of conversation: what it does to the two who talk (and to a third, for gossip).
	struct TalkKind
	{
		float Seconds = 8.0f;            // how long they talk
		float Relation = 0.03f;          // added to their bond
		float Mood = 0.02f;              // to both moods
		float Company = 20.0f;           // to both Company needs
		float Grief = 0.0f;              // to the listener's grief (consoling: negative)
		float Teach = 0.0f;              // understanding of a technology the speaker knows and the listener does not (H11: at 1 it knows it)
		float Third = 0.0f;              // gossip: added to both bonds with the one they talk about
		std::string Event;               // a society event between them as well ("food" for sharing)
		std::string Tech;                // a technology the clan must know for this kind ("story": storytelling; "" = none)
		bool operator==(const TalkKind&) const = default;
	};
	// When and how members talk. The kinds are chosen by the situation (in this order): console, settle, share, play, teach, story, greet, groom,
	// gossip, chat; a kind missing from Kinds is skipped (the next one that fits is used; with no "chat" nothing else fits).
	struct TalkRules
	{
		float Radius = 12.0f;            // someone this close, free or idle, can be talked to
		float Score = 0.9f;              // base worth of the option ...
		float Want = 85.0f;              // ... when its Company is under this (it grows as Company falls; x Evening at the camp in the evening) ...
		float Evening = 3.0f;
		float Cooldown = 40.0f;          // ... and not again before this many seconds after its last talk
		float GreetAfter = 600.0f;       // two who have not talked for this long greet
		float GroomAbove = 0.35f;        // affinity for grooming (kin and close friends)
		float SettleBelow = -0.05f;      // a bond this low is a quarrel to settle
		float ConsoleAbove = 0.15f;      // the listener's grief that calls for consoling
		float StoryAge = 45.0f;          // a teller of stories is this old (or an elder) ...
		float StorySociability = 0.75f;  // ... or this sociable
		float GossipSociability = 0.55f; // both this sociable to gossip
		float HungerBelow = 55.0f;       // the listener's Hunger under this to be given food
		std::map<std::string, TalkKind> Kinds;
		bool operator==(const TalkRules&) const = default;
	};

	// A rite: what calls it (an event or a season), where it is held, how long, who must come, what it costs and what it gives those who attend.
	struct RiteDef
	{
		std::string Name;
		// The trigger: Event ("birth", "adult", "pair", "death", "first_kill", "wounded", "harvest", "omen:<id>", "tradition") or a Season ("spring",
		// "summer", "autumn", "winter") at the fraction At of it, once a year; with Every > 0 days it comes back that often (a tradition kept).
		std::string Event, Season;
		float At = 0.5f;
		float Every = 0.0f;
		std::string Place = "fire";      // "fire", "totem", "sacred" or "camp" (a missing place falls back to the camp)
		float Seconds = 30.0f;           // the rite itself ...
		float GatherSeconds = 45.0f;     // ... once the Quorum has come, or after this long anyway
		float Quorum = 0.5f;             // the share of the living that must be in the circle
		float Delay = 5.0f;              // seconds after its trigger
		std::map<std::string, int> Cost; // items from the store ("food": units of any food)
		std::string Tech;                // a technology the clan must know ("" = none)
		bool Shaman = false;             // only with a shaman
		// The effects on those in the circle at the end:
		float Mood = 0.06f;
		float Relation = 0.02f;          // between every pair of attendants (cohesion), and twice that with the subject
		float Grief = 0.0f;              // (a funeral: negative)
		float Heal = 0.0f;               // seconds of care for the subject's wounds (H14), x3 like a carer's
		std::map<std::string, float> Norms;     // the clan's norms nudged (H10)
		std::map<std::string, float> Beliefs;   // what it honours: belief system -> weight (it grows by Drift x weight)
		std::string Gesture;             // the activity of the manifest the members show ("" = "rite.<id>")
		bool operator==(const RiteDef&) const = default;
	};

	struct BeliefSystem
	{
		std::string Name;
		float Start = 0.4f;              // a clan's strength in it: Start +- Spread (a hash of the seed and the clan's name)
		float Spread = 0.25f;
		bool operator==(const BeliefSystem&) const = default;
	};
	// A taboo of a belief system, kept while the system is at HoldAbove or more. Kinds: "sacred_hunt" (no hunting within Radius of the sacred
	// place) and "share_kill" (a hunter's first kill goes whole to the store, and the clan thanks it).
	struct TabooDef
	{
		std::string Name, System, Kind;
		float Radius = 25.0f;
		bool operator==(const TabooDef&) const = default;
	};
	// An omen: an event of the world the clan reads ("storm", "eclipse", "good_harvest", "big_kill", "two_deaths"); "eclipse" comes by itself with
	// Chance a day. It moves every mood, the beliefs, and may call a rite.
	struct OmenDef
	{
		std::string Name, Event;
		float Chance = 0.0f;
		float Mood = 0.0f;
		std::map<std::string, float> Beliefs;
		std::string Rite;
		bool operator==(const OmenDef&) const = default;
	};
	struct BeliefRules
	{
		std::map<std::string, BeliefSystem> Systems;
		float Drift = 0.03f;             // a rite moves the systems it honours by this x its weight
		float Spread = 0.04f;            // a greeting between two clans moves their beliefs this share towards each other (x the openness)
		float Trust = 0.04f;             // ... and adds this x (2 similarity - 1) to their attitude and trust (H13)
		float HoldAbove = 0.45f;         // a system this strong holds its taboos
		float SacredDistance = 24.0f;    // the sacred place a clan adopts: this far from the camp (or its totem)
		std::map<std::string, TabooDef> Taboos;
		std::map<std::string, OmenDef> Omens;
		bool operator==(const BeliefRules&) const = default;
	};
	// The spiritual role, derived like a trade: the living adult with the highest score of Traits (trait -> weight) + Age per year + Rites per
	// rite it presided over. It presides the rites (their effects x 1 + Bonus).
	struct ShamanRules
	{
		std::map<std::string, float> Traits;
		float Age = 0.01f;
		float Rites = 0.03f;
		float MinAge = 20.0f;
		float Bonus = 0.5f;
		bool operator==(const ShamanRules&) const = default;
	};

	// What a food sates and the group it is in (for variety). A food not listed sates NeedsConfig::FoodHunger and is a group of its own.
	struct FoodDef
	{
		std::string Group;
		float Nutrition = 25.0f;
		bool operator==(const FoodDef&) const = default;
	};
	struct DietRules
	{
		int Window = 6;                  // meals a member remembers
		int Varied = 3;                  // different groups among them for a varied diet ...
		float Bonus = 0.03f;             // ... which gives this mood a meal ...
		float Health = 1.0f;             // ... and this health
		float Monotony = -0.02f;         // one group only (with the window full): this mood a meal
		std::map<std::string, FoodDef> Foods;
		bool operator==(const DietRules&) const = default;
	};

	// A trap the clan sets for an animal of Tag: a snare for small game, a pit for large. Setting one costs Cost from the store (Tech known); it
	// is armed after Arm seconds and takes the first animal of its tag within Radius; a member then brings the catch (Yields) home.
	struct TrapDef
	{
		std::string Name;
		std::string Tag;
		std::map<std::string, int> Cost;
		std::string Tech;
		float Arm = 60.0f;
		float Radius = 5.0f;
		float Seconds = 4.0f;            // the work of setting it
		int Max = 2;                     // traps of this kind out at once
		float Priority = 0.6f;           // what setting one is worth (x how short of food the clan is)
		std::map<std::string, int> Yields;
		bool operator==(const TrapDef&) const = default;
	};
	struct HuntingRules
	{
		std::map<std::string, int> Wildlife;   // species -> animals the editor lets loose around a new camp (tribe build)
		std::map<std::string, TrapDef> Traps;
		float Short = 3.0f;              // food per member in hand under which the clan sets traps (as the hunts of H14)
		bool operator==(const HuntingRules&) const = default;
	};

	struct SocialRules
	{
		// A tribe has a social life only with rules from content (the "social" section, which turns it on) or DefaultSocialRules(): the plain
		// TribeConfig has none, so a tribe built in code without them behaves as it did before this lane.
		bool On = false;
		uint32_t Seed = 1;
		float StepSeconds = 2.0f;        // how often the clan looks at its rites, traps and omens
		MoodRules Mood;
		GriefRules Grief;
		TalkRules Talk;
		std::map<std::string, RiteDef> Rites;
		BeliefRules Beliefs;
		ShamanRules Shaman;
		DietRules Diet;
		HuntingRules Hunting;
		bool operator==(const SocialRules&) const = default;
	};
	SocialRules DefaultSocialRules();   // assets/data/social.json says the same in its "social" section
	// { "social": { "seed", "stepSeconds", "mood": {...}, "grief": {...}, "talk": { ..., "kinds": { "<kind>": {...} } }, "rites": { "<id>": {...} },
	//   "beliefs": { ..., "systems": {}, "taboos": {}, "omens": {} }, "shaman": {...}, "diet": { ..., "foods": {} }, "hunting": { "wildlife": {},
	//   "traps": {} } } }. "rites" adds or replaces rites by id; every other part replaces that part whole (what it does not say is the default); a
	// file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadSocialRules(const std::string& json, SocialRules& rules);
	std::string LoadSocialRulesFile(const std::string& path, SocialRules& rules);
	std::string SocialRulesToJson(const SocialRules& rules);   // one JSON object, as the section reads it back

	// ---- the state of a clan's social life (saved with the tribe, key "social")
	struct SocialMember
	{
		float Mood = 0.5f;
		float Grief = 0.0f;
		std::vector<std::string> Diet;           // the groups of its last meals, oldest first
		double LastTalk = -1e9;
		std::map<std::string, double> Talked;    // other -> when they last talked
		int Talks = 0, Rites = 0, Led = 0, Kills = 0;
		bool operator==(const SocialMember&) const = default;
	};
	struct PendingRite
	{
		std::string Rite, Subject;
		double Due = 0.0;
		int Tries = 0;
		bool operator==(const PendingRite&) const = default;
	};
	struct ActiveRite
	{
		std::string Rite, Subject, Leader;
		glm::vec2 Place{ 0.0f };
		double Start = 0.0, Hold = -1.0;         // when it was called; when the rite itself began (-1: still gathering)
		std::vector<std::string> Called;         // in circle order
		bool Humble = false;                     // its cost could not be paid
		bool Active() const { return !Rite.empty(); }
		bool operator==(const ActiveRite&) const = default;
	};
	struct Snare
	{
		std::string Name, Trap, Owner;
		glm::vec2 Pos{ 0.0f };
		double Set = 0.0;
		std::string Caught;                      // the animal it took ("" = none yet)
		bool operator==(const Snare&) const = default;
	};

	class SocialState
	{
	public:
		void Clear() { *this = SocialState(); }
		std::map<std::string, SocialMember> Members;
		std::vector<PendingRite> Agenda;
		ActiveRite Rite;
		std::vector<std::string> History;        // the last rites held, for people ("12.0 funeral for Biel: 5 of 6")
		std::map<std::string, float> Beliefs;    // belief system -> 0..1 (empty until the clan's first step: drawn from the seed and its name)
		glm::vec2 Sacred{ 0.0f };
		bool HasSacred = false;
		std::string Shaman;
		std::vector<Snare> Snares;
		std::map<std::string, int> Counts;       // "talk.<kind>", "rite.<id>", "omen.<id>", "snare.<trap>", "taboo.<id>" ...
		std::map<std::string, int> Years;        // seasonal rite -> the year it was last held
		std::map<std::string, double> Recurring; // recurring rite -> when it was last called
		double DayTimer = 0.0;
		int Day = 0;
		int DeathsToday = 0;
		float StepTimer = 0.0f;
		int Serial = 0;                          // the last number given to a snare
		bool operator==(const SocialState&) const = default;

		SocialMember& Of(const std::string& name) { return Members[name]; }
		const SocialMember* Find(const std::string& name) const { const auto it = Members.find(name); return it == Members.end() ? nullptr : &it->second; }
		// The dominant belief system ("" when there are none) and how alike two clans' beliefs are (1 - the mean difference over both sets; 0.5 when
		// either has none).
		std::string Dominant() const;
		static float Similarity(const std::map<std::string, float>& a, const std::map<std::string, float>& b);
		bool Holds(const SocialRules& rules, const std::string& taboo) const;   // the taboo's system is strong enough
		std::string ToJson() const;
		bool FromJson(const std::string& json);  // false (nothing changed) when it is not readable
	};

	// A deterministic 0..1 from the seed, a name and a counter (FNV-1a), the only "die" of the social life.
	float SocialRoll(uint32_t seed, const std::string& name, int n);
	// The factor of a centred value on an option (1 + strength x (2 v - 1)), 1 when the effects do not name it.
	float SocialFactor(const SocialEffects& effects, const std::string& kind, float value);
}
