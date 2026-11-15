#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

// T195 (M1 of the god mode, documentation/01_diseno/MODO_DIOS.md §3.4; the faith of MODO_DIOS_DECISIONES.md §4): the director of the world.
// It keeps the god's influence, the waits of each intervention, the pressure on each clan and the director's book, and runs the `director ...`
// orders both hosts register (the editor and GB.exe through EditorApp, GBSim through --director=<day>:<order>).
//
// The influence comes from the faith of the people, not from a clock: each rite held adds its attendees x the clan's faith, each omen a clan
// reads adds a little, every day adds the living x their mean devotion, and a sign that comes true adds what its caller says (Fulfilled). Only
// `director sandbox on` refills it on a fixed clock (and waives the waits), for tests and labs. The director only READS the clans while no
// order is given (no die, no state of a Tribe changes): without orders the simulation is the same as without the director.
//
// The hand of the god (the decisions of MODO_DIOS_DECISIONES: vote, sign, focus...) reaches only a clan whose faith is at least MinFaith; the
// events of the world (the omen) are natural phenomena and do not need it. Every intervention goes through Spend, which says why it cannot be.
namespace GB
{
	class ClanWorld;   // (no forward declaration of Clan here: tests that include GameContent.h have a Clan of their own)

	// assets/data/director.json, the "director" section.
	struct DirectorRules
	{
		float Start = 50.0f;               // the influence of a new world
		float Max = 100.0f;
		float MinFaith = 0.2f;             // the hand reaches only a clan whose faith is at least this
		float RiteGain = 0.5f;             // influence per attendee of a rite x the clan's faith
		float OmenGain = 1.0f;             // influence per omen a clan reads x (0.5 + its faith)
		float DailyGain = 0.5f;            // influence a day per living member x its devotion
		float ShamanDevotion = 0.2f;       // what being the clan's shaman adds to a person's devotion
		float SandboxPerDay = 20.0f;       // `director sandbox on`: the fixed clock that refills the influence
		std::map<std::string, float> Costs = { { "omen", 10.0f } };        // action -> influence it costs
		std::map<std::string, float> Waits = { { "omen", 1.0f } };         // action -> days before it can fall on the same clan again
		// the pressure on a clan (§4.4): only counted for now; G4-6 gives it its consequences
		float PressureDecision = 1.0f;     // a decision of the hand on the clan
		float PressureInsist = 2.0f;       // the same decision asked again within InsistDays after a refusal (the caller says it)
		float PressureContradict = 2.0f;   // an order against the last one within a day (the caller says it)
		float PressureDecay = 1.0f;        // a day
		float PressureTired = 6.0f;
		float PressureHeresy = 12.0f;
		float InsistDays = 3.0f;
		float DaySeconds = 1200.0f;
		int LogMax = 200;
		bool operator==(const DirectorRules&) const = default;
	};
	DirectorRules DefaultDirectorRules();   // assets/data/director.json says the same
	// { "director": { "start", "max", "minFaith", "faith": { "rite", "omen", "daily", "shaman" }, "sandboxPerDay", "costs": {}, "waits": {},
	//   "pressure": { "decision", "insist", "contradict", "decay", "tired", "heresy", "insistDays" }, "logMax" } }. What it does not say keeps its
	// value; a file without the section changes nothing; on a mistake it returns why and changes nothing.
	std::string LoadDirectorRules(const std::string& json, DirectorRules& rules);

	// Where the director acts: the hosts fill it the same way (SimRun, EditorApp), as ContentTargets. The clans and the clock of the world.
	struct DirectorTargets
	{
		ClanWorld* Clans = nullptr;
		std::function<double()> Now;       // the world's clock in seconds (the simulation's, never the wall's)
	};

	struct DirectorEntry                   // a line of the director's book
	{
		double At = 0.0;
		std::string Action, Target, Text;
		float Cost = 0.0f;
		bool operator==(const DirectorEntry&) const = default;
	};

	struct DirectorCheck                   // what Spend / CanSpend answer
	{
		bool Ok = false;
		std::string Why;                   // why not ("" when it can)
		float Cost = 0.0f;
	};

	struct DirectorReply { bool Ok = false; std::string Text; };

	class WorldDirector
	{
	public:
		DirectorRules Rules = DefaultDirectorRules();

		void SetTargets(const DirectorTargets& to) { m_To = to; }
		const DirectorTargets& Targets() const { return m_To; }
		// After the clans' step: reads the rites and omens held since the last look and the day that turned (no state of a clan changes).
		void Update();
		// `director <args...>` (status, sandbox, faith, omen, log); the reply is what the order prints (a contract of the tests: English).
		DirectorReply Command(const std::vector<std::string>& args);

		// ---- what the clans believe (read only)
		float FaithOf(int clan) const;                                     // the clan's faith 0..1: its dominant belief (or the sandbox's value)
		float DevotionOf(int clan, const std::string& person) const;       // a person's 0..1 (MODO_DIOS_DECISIONES §4.1)
		float MeanDevotion(int clan) const;                                // of its living members (0 with none)

		// ---- the generic way to intervene (G4-1 vote, G4-2 sign, G4-3 focus ...): `action` names its cost and wait in the rules; `hand` is a
		// decision of the hand (it needs the clan's faith >= MinFaith and adds PressureDecision to the clan); clan -1 = the world (no faith).
		DirectorCheck CanSpend(const std::string& action, int clan, bool hand) const;
		// Pays it (and starts its wait, adds the pressure, writes the book and a telemetry row "director"); when it cannot, changes nothing.
		DirectorCheck Spend(const std::string& action, int clan, bool hand, const std::string& text);
		void Fulfilled(int clan, float influence, const std::string& text);   // a sign or a vision came true: the god gains (G4-2)
		void AddPressure(int clan, float amount, const std::string& why);     // an insistence, a contradiction, a murmur (the caller decides)
		float PressureOf(int clan) const;
		std::string PressureLevel(int clan) const;                        // "low", "tired", "heresy" (only named, no consequence yet)
		double WaitLeft(const std::string& action, int clan) const;        // seconds until `action` can fall on the clan again (0 = now)

		float Influence() const { return m_Influence; }
		bool Sandbox() const { return m_Sandbox; }
		const std::vector<DirectorEntry>& Book() const { return m_Book; }
		std::string Status() const;

		void Clear();                                                      // a new world: the start influence, nothing else
		std::string ToJson() const;
		bool FromJson(const std::string& json);                            // false (nothing changed) when it is not readable

	private:
		double Now() const { return m_To.Now ? m_To.Now() : 0.0; }
		void Gain(float amount);
		void Write(const std::string& action, const std::string& target, float cost, const std::string& text, int clan);
		void Look(bool count);                                             // reads the clans' counters (count: their deltas become influence)
		DirectorReply Omen(const std::vector<std::string>& args);

		DirectorTargets m_To;
		float m_Influence = 50.0f;
		bool m_Sandbox = false;
		bool m_Looked = false;                                             // the first look only takes the counters
		int m_Day = 0;                                                     // the last day the daily gain was given
		double m_SandboxAt = 0.0;                                          // the sandbox clock's last refill
		std::map<int, float> m_Pressure;
		std::map<int, float> m_FaithSet;                                   // `director faith <clan> <v>` (sandbox only)
		std::map<std::string, double> m_Waits;                             // "action:clan" -> when it can be again
		std::map<int, std::pair<int, int>> m_Seen;                         // clan -> (attendances, omens) last seen
		std::vector<DirectorEntry> m_Book;
		double m_Gained = 0.0;                                             // influence gained from faith over the game (for the status)
	};
}
