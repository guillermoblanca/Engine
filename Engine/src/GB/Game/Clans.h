#pragma once
#include "GB/Game/Diplomacy.h"
#include "GB/Game/Territory.h"
#include "GB/Game/Tribe.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// Several clans in one world (H13): each CLAN is a tribe (its store, camp, members, knowledge, culture and map; GB/Game/Tribe.h) with an id and a
	// name, and the CLAN WORLD keeps what lies between them: who has met whom, what they think of each other (GB/Game/Diplomacy.h), the land each
	// claims (GB/Game/Territory.h) and the meetings their envoys hold. Everything between clans is done by members as decisions of their own brain
	// (the "envoy" option, through Tribe::SetOutside), so needs still come first and an envoy far from the camera walks as an abstract agent.
	//
	// What happens (see docs/CLANES.md):
	//  * ENCOUNTERS: members of two clans within Encounters.Radius greet each other: the clans have met (first contact), their attitude rises a
	//    little (x their openness) and so does the bond of the two members (the attitude follows the mean of those bonds).
	//  * TERRITORY: every Territory.Every seconds each clan's claims move towards what its strength (living members, buildings) can hold of the regions
	//    around its camp; overlapping claims of clans that are not allies cost them a little attitude a day; gathering a node on land another clan
	//    holds is THEFT: the owner keeps a grievance.
	//  * TRADE: every Trade.Every seconds, two clans that have met and are not hostile compare stores: what one lacks (under its stock target) and the
	//    other has to spare (over Surplus x its target) is bartered, balanced by value; a clan fond enough of another gives without asking. Each posts an
	//    ENVOY (its most sociable free adult) who fetches the goods from the store, walks to a meeting point between the camps, waits for the other,
	//    exchanges, and brings home what it got. At the meeting friendly envoys also show each other a technology (H11, Exchange.Teach); allies renew
	//    their pact; trusting clans agree an alliance; hostile but open clans that hold small grievances parley and agree a truce.
	//  * MARRIAGE: every Marriage.Every seconds, two members of fond clans who have met and like each other may wed: one moves to the other's clan
	//    (to the smaller one) with its person, knowledge and aptitudes (Tribe::Detach / Admit).
	//  * HOSTILITY is exposed for the conflict work (H14): Hostility, MayRaid, Contested, Relate("raid"...) and a listener for every event.
	// Deterministic: clans, pairs, members and regions in id and name order; the only chance (a wedding) is a hash of the seed and the names.

	struct Clan
	{
		ClanId Id = -1;
		std::string Name;
		float Buildings = 0.0f;           // what the settlement work (H8) tells of its buildings: they make its claims stronger
		Tribe* Folk = nullptr;            // its tribe: kept here (Own) or by the host (the editor's first tribe)
		std::unique_ptr<Tribe> Own;
		bool Kept() const { return Own != nullptr; }
	};

	// Something worth telling between clans, for the log, the editor and the conflict work (H14): "contact", "stance", "trade", "gift", "visit",
	// "teach", "pact", "break", "theft", "marriage", "move", "refuse" ... with the two clans and a sentence.
	struct DiplomacyEvent
	{
		std::string Kind;
		ClanId A = -1, B = -1;
		std::string Text;
		double At = 0.0;
	};

	class ClanWorld
	{
	public:
		DiplomacyRules Rules = DefaultDiplomacyRules();
		// H16: ONE budget of turns at looking around and deciding for all the clans of a frame (time-sliced thinking across clans). Each clan gets
		// the turns it needs so none of its members falls behind (Tribe::ThinksNeeded) and what is left of ThinkPerFrame is shared by members, the
		// largest remainders first and ties by clan order: no state, so a loaded game shares it as one that never stopped. With a single clan the
		// tribe keeps its own rule (TribeConfig::ThinkPerFrame), exactly as before H16. 0 = every clan its own rule (the sum of them: the old way).
		int ThinkPerFrame = 6;
		// What the last Update cost (measured; nothing in the simulation reads it): the perf line `stats ai` and the scale tests.
		struct FrameCost
		{
			int Thinks = 0, Budget = 0, Due = 0;   // summed over the clans (see Tribe::FrameCost)
			double TribesMs = 0.0, DiplomacyMs = 0.0, ThinkMs = 0.0;   // (ThinkMs: of TribesMs, the turns at deciding)
		};
		const FrameCost& LastCost() const { return m_Cost; }
		// The telemetry of the run (GB/Telemetry/TelemetrySink.h): every clan, present and future, writes to the sink under its own id. Null = off.
		void SetTelemetry(TelemetrySink* sink, double every = 60.0);
		TelemetrySink* Telemetry() const { return m_Telemetry; }

		ClanWorld() = default;
		ClanWorld(const ClanWorld&) = delete;
		ClanWorld& operator=(const ClanWorld&) = delete;
		~ClanWorld();

		// ---- the clans
		// A clan for a tribe the host keeps (`external`: the editor's first tribe) or for a new tribe kept here (its Config copied from `config` when
		// given, with a campfire of its own name). Its id is the next free one, or `id` (a load). The tribe gets the hooks of the clan world.
		Clan& Add(const std::string& name, Tribe* external = nullptr, ClanId id = -1, const TribeConfig* config = nullptr);
		// Its tribe is unhooked (and dropped, when kept here); its relations, claims and meetings go. False when there is none.
		bool Remove(ClanId id);
		// Every clan, relation, claim and meeting (the tribes the host keeps are unhooked and left alone).
		void Clear();
		Clan* Find(ClanId id);
		const Clan* Find(ClanId id) const;
		const Clan* Find(const std::string& idOrName) const;
		Clan* Find(const std::string& idOrName) { return const_cast<Clan*>(static_cast<const ClanWorld*>(this)->Find(idOrName)); }
		// The clan a member belongs to (nullptr: none).
		const Clan* ClanOf(const std::string& member) const;
		const std::vector<std::unique_ptr<Clan>>& All() const { return m_Clans; }
		size_t Count() const { return m_Clans.size(); }
		// Whether a name is used by a member of any clan (alive or dead) or a person of any society: a new member must not reuse it.
		bool NameTaken(const std::string& name) const;

		// ---- the world they share
		// The regions (they must outlive this; the claims need them). Without them there is no territory (the rest works).
		void SetRegions(const RegionMap* regions) { m_Regions = regions && !regions->Empty() ? regions : nullptr; }
		const RegionMap* Regions() const { return m_Regions; }
		// Who chooses where a new clan settles (the settlement work, H8, can give its own); without one, ChooseHome's own choice.
		void SetSiteChooser(CampSiteChooser chooser) { m_SiteChooser = std::move(chooser); }
		// Where a new clan should set its camp: the dry anchor of a region at least `minApart` from every camp, with the most wild nodes and water and
		// the least danger, on land nobody holds (or the site chooser's, when it gives one that far). `usable`: can a camp be there (reachable ground).
		bool ChooseHome(float minApart, const std::function<bool(const glm::vec2&)>& usable, CampSite& out) const;
		// The camera and the clock, for every tribe (members far from the viewer are abstract agents).
		void SetViewer(const glm::vec2& pos);
		void SetWorldClock(double seconds);

		// Updates every tribe, then what lies between them (StepDiplomacy).
		void Update(float dt);
		// Only what lies between the clans: encounters, territory, trade and meetings, weddings, pacts and the days.
		void StepDiplomacy(float dt);
		double Time() const { return m_Time; }
		float DaySeconds() const;

		// ---- relations (any order of a and b)
		const ClanRelation* Relation(ClanId a, ClanId b) const;
		float Attitude(ClanId a, ClanId b) const;
		float Trust(ClanId a, ClanId b) const;
		float Grievance(ClanId holder, ClanId against) const;
		bool Met(ClanId a, ClanId b) const { const ClanRelation* r = Relation(a, b); return r && r->Encounters > 0; }
		// "unknown", "hostile", "wary", "neutral", "friendly", "truce" or "allied" (see Diplomacy.h).
		std::string Stance(ClanId a, ClanId b) const;
		bool HasPact(ClanId a, ClanId b, const std::string& kind) const;
		// The culture of a clan as diplomacy sees it: openness (good events count more, envoys parley) and aggression (H14), 1 = average.
		float Openness(ClanId clan) const;
		float Aggression(ClanId clan) const;
		// The mean bond between members of the two clans who have met (0 when none have), and one pair's bond.
		float MemberBonds(ClanId a, ClanId b) const;
		float MemberBond(const std::string& a, const std::string& b) const;
		// Something happened between two clans: its weight (Rules.Relations.Events, x `scale`; good events x the openness of both) moves their base
		// attitude and trust; an event with a grievance weight leaves `victim` (one of the two) a grievance against the other. Unknown events only log.
		void Relate(ClanId a, ClanId b, const std::string& event, float scale = 1.0f, const std::string& why = {}, ClanId victim = -1);
		// The social lane (docs/VIDA_SOCIAL.md; ClansBeliefs.cpp): two clans whose members meet bring their beliefs a little closer (Beliefs.Spread
		// x their openness), and how alike they are moves their attitude and trust (Beliefs.Trust x (2 similarity - 1)): shared beliefs, trust.
		// Called by every greeting; returns the similarity (0.5 when either has no beliefs yet).
		float ShareBeliefs(ClanId a, ClanId b);

		// ---- for the conflict work (H14)
		// How ready `a` is to fight `b`, 0..: its aggression x (how hostile it feels, -attitude, plus its grievance against b); 0 under a pact.
		float Hostility(ClanId a, ClanId b) const;
		// Whether `a` may raid `b`: they are hostile and have no pact (an alliance or a truce forbids it).
		bool MayRaid(ClanId a, ClanId b) const;
		// The regions claimed past Min by clans that are not allies, with their claimants (strongest first).
		std::vector<std::pair<int, std::vector<ClanId>>> Contested() const;
		const TerritoryMap& Territory() const { return m_Territory; }
		ClanId OwnerOf(int region) const { return m_Territory.Owner(region, Rules.Territory); }
		// Every event between clans as it happens.
		using Listener = std::function<void(const DiplomacyEvent&)>;
		void Listen(Listener listener) { m_Listeners.push_back(std::move(listener)); }

		// ---- actions (the editor and tests; the clans do them on their own too)
		// Posts a meeting of envoys with a barter of what each lacks and the other spares ("" when posted, or why not). `force`: even if they never
		// met, are not fond of each other or met lately.
		std::string ProposeTrade(ClanId a, ClanId b, bool force = false);
		// Posts a visit (envoys meet, show what they know, maybe agree a pact) ("" or why not).
		std::string ProposeVisit(ClanId a, ClanId b, bool force = false);
		// A member leaves its clan for another, with everything that is its own ("" or why not).
		std::string MoveMember(const std::string& name, ClanId to, const std::string& why = {});
		// Two members of different clans marry: the one of the larger clan moves to the smaller ("" or why not).
		std::string Wed(const std::string& a, const std::string& b);
		// H15 (ClansPlayer.cpp): a player of one clan, at the camp of clan `to` (within PlayerRules::VisitRadius), is its clan's envoy there:
		// "trade" barters what its clan lacks for what that one spares (store to store), "visit" only meets; the relation, the bond with the member
		// who receives it and a lesson either way follow the rules of a meeting. "" (and `outcome`) or why not.
		std::string PlayerVisit(const std::string& player, ClanId to, const std::string& purpose, std::string* outcome = nullptr);
		const std::vector<Meeting>& Meetings() const { return m_Meetings; }
		const std::vector<MeetingRecord>& History() const { return m_History; }

		// ---- invariants (the simulation QA of H20, SimInvariants: "clans" and "territory"), as (subject, what is wrong); empty when all holds.
		// Members: every member (and every person of a society) is in exactly one clan, clan ids are unique, relations and meetings name clans
		// that exist and envoys of their own clan. Territory: claims are finite and non-negative, on land regions that exist, of clans that exist,
		// and stepping the same claims twice with the same clans gives the same map (they are deterministic).
		std::vector<std::pair<std::string, std::string>> CheckMembers() const;
		std::vector<std::pair<std::string, std::string>> CheckTerritory() const;

		// ---- for people
		std::string Report() const;              // one line per clan: members, camp, store, regions held, stance to the others
		std::string RelationsReport() const;     // every pair: stance, attitude, trust, grievances, pacts, what moved it lately
		std::string TerritoryReport() const;     // who holds what and the contested regions
		std::string MeetingsReport() const;
		const std::vector<std::string>& Log() const { return m_Log; }

		// ---- saving (the editor's "clans" section): the clans (ids, names, buildings and, for the tribes kept here, their tribe, knowledge and what each
		// member knows), relations, bonds between members of different clans, claims, meetings, history, clocks and the log.
		std::string ToJson() const;
		struct SavedClan
		{
			ClanId Id = -1;
			std::string Name;
			bool Kept = false;
			std::vector<std::pair<std::string, std::string>> Members;   // name, role (to make their bodies before FromJson)
		};
		static std::vector<SavedClan> SavedClans(const std::string& json);
		// Reads it back over the clans there are (made by Add with the saved ids and their members): the kept tribes (`reissue`, see Tribe::FromJson),
		// then everything between the clans. False on malformed text (the diplomacy is then left as it was).
		bool FromJson(const std::string& json, bool reissue = false);

	private:
		struct CrossBond { ClanId A = -1, B = -1; float Value = 0.0f; bool operator==(const CrossBond&) const = default; };
		using PairKey = std::pair<ClanId, ClanId>;
		static PairKey Key(ClanId a, ClanId b) { return a < b ? PairKey(a, b) : PairKey(b, a); }
		ClanRelation& RelationRef(ClanId a, ClanId b);
		void Hook(Clan& clan);
		TelemetrySink* m_Telemetry = nullptr;   // not owned
		double m_TelemetryEvery = 60.0;
		void Unhook(Clan& clan);
		void Emit(const std::string& kind, ClanId a, ClanId b, const std::string& text);
		std::string ClanName(ClanId id) const;
		int Alive(const Clan& c) const;
		TribeMember* Member(Clan& c, const std::string& name);
		// the parts of StepDiplomacy
		void Encounters();
		void Greet(Clan& ca, TribeMember& ma, Clan& cb, TribeMember& mb);
		void StepTerritory(float dt);
		void Trade();
		void Weddings();
		void Day();
		void CheckPacts();
		void CheckStances();
		// meetings
		StoreView View(const Clan& c) const;
		std::string PostMeeting(ClanId a, ClanId b, const std::string& purpose, const Barter& deal);
		std::string ChooseEnvoy(Clan& c) const;
		bool TechToShow(const Clan& from, const std::string& teacher, const Clan& to, const std::string& learner, std::string& tech) const;
		bool IsEnvoy(const std::string& member) const;
		void StepMeetings();
		void Exchange(Meeting& m);
		void Close(size_t index);
		// the hooks of the tribes
		void EnvoyOptions(ClanId clan, Tribe& t, TribeMember& m, std::vector<UtilityOption>& options);
		bool EnvoyExecute(ClanId clan, Tribe& t, TribeMember& m, const UtilityOption& o);
		void EnvoyFinish(ClanId clan, Tribe& t, TribeMember& m);
		void Gathered(ClanId clan, Tribe& t, const TribeMember& m, const std::string& node);

		std::vector<std::unique_ptr<Clan>> m_Clans;
		ClanId m_NextId = 0;
		const RegionMap* m_Regions = nullptr;
		CampSiteChooser m_SiteChooser;
		std::map<PairKey, ClanRelation> m_Relations;
		std::map<std::pair<std::string, std::string>, CrossBond> m_Bonds;   // (a, b) with a < b: members of different clans who met
		std::map<std::pair<std::string, std::string>, double> m_Greeted;    // when two members last greeted each other
		std::map<PairKey, std::string> m_Stances;                            // what the log last said of each pair
		std::map<PairKey, double> m_TheftAt;                                 // the last theft between a pair (one a minute is told)
		TerritoryMap m_Territory;
		std::vector<Meeting> m_Meetings;
		std::vector<MeetingRecord> m_History;
		int m_NextMeeting = 1;
		double m_Time = 0.0;
		float m_EncounterTimer = 0.0f, m_TerritoryTimer = 0.0f, m_TradeTimer = 0.0f, m_WeddingTimer = 0.0f;
		double m_DayTimer = 0.0;
		int m_Days = 0;
		std::vector<std::string> m_Log;
		std::vector<Listener> m_Listeners;
		FrameCost m_Cost;
	};
}
