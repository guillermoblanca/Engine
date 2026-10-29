#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace GB
{
	// The telemetry of a run: what happens to every individual, written to one SQLite file by a thread of its own (docs/DATOS_INDIVIDUO.md).
	//  * Samples (a row per member and per clan every so often) go in a bounded queue: when the writer cannot keep up the newest are dropped and counted
	//    (`runs.samples_dropped`). Events and the sheet of each individual are never dropped.
	//  * Writing never touches the simulation: nothing here is read back by it, so a run ends in the same state with the sink on or off.
	//  * Every row carries the run and the clan, even with a single clan (clan 0).
	// The sink is not a singleton: the host (the editor, a headless runner, a test) owns one and hands the pointer to the tribes. Null = off.
	using TelemetryValue = std::variant<std::monostate, int64_t, double, std::string>;

	struct TelemetryRunInfo
	{
		int64_t Seed = 0;
		std::string Commit, Profile, Note;   // free labels of the run (a sweep writes its own)
	};

	struct TelemetryIndividual
	{
		int Clan = 0;
		std::string Member, Role;
		double Joined = 0.0;                  // simulation seconds when it was first seen
		bool Female = false;
		std::string Mother, Father;
		int Generation = 0;
		std::string Genes, Traits;            // JSON objects
	};

	struct TelemetrySample
	{
		int Clan = 0;
		std::string Member, Role, Job, Detail;
		double Time = 0.0;
		int Day = 1;
		double Hour = 0.0;
		double X = 0.0, Z = 0.0;
		int Region = -1;
		double Hunger = -1.0, Thirst = -1.0, Rest = -1.0, Warmth = -1.0, Health = -1.0;   // -1: it has no such need
		double Age = -1.0;
		bool Alive = true;
		std::string Bag, Skills;              // JSON objects
	};

	struct TelemetryClanSample
	{
		int Clan = 0;
		double Time = 0.0;
		int Day = 1;
		double Hour = 0.0;
		std::string Tier;
		double FoodDays = 0.0;
		int Alive = 0, Dead = 0, Beds = 0, Buildings = 0;
		bool Fire = false;
		std::string Store;                    // JSON object
	};

	struct TelemetryEvent
	{
		int Clan = 0;
		double Time = 0.0;
		std::string Kind;                     // "say", "task_end", "birth", "death", "pair", "adult", "elder", "norm", "tradition"...
		std::string Who, Other, Result, Text;
	};

	// One finished task: from the commitment it belonged to until it ended (`Result`: ok, empty, unreachable, failed).
	struct TelemetryTask
	{
		int Clan = 0;
		std::string Member, Kind, Target, Result, Reason;
		double Start = 0.0, End = 0.0;
	};

	// One decision that changed what a member does: what it chose and the best options it weighed (JSON list of {kind, target, score, factors}).
	struct TelemetryDecision
	{
		int Clan = 0;
		double Time = 0.0;
		std::string Member, Chosen, Target, Previous, Options;
	};

	// The bond of two members of a clan (a < b by name), written about once a day for the pairs that matter.
	struct TelemetryRelation
	{
		int Clan = 0;
		double Time = 0.0;
		std::string A, B, Kin;
		double Bond = 0.0, Affinity = 0.0;
	};

	// How two clans stand (a < b): written every day for the pairs that have met.
	struct TelemetryClanRelation
	{
		int A = 0, B = 0;
		double Time = 0.0;
		std::string Stance;
		double Attitude = 0.0, Hostility = 0.0, Trust = 0.0, GrievanceAB = 0.0, GrievanceBA = 0.0;
		int Encounters = 0, Trades = 0, Gifts = 0, Thefts = 0, Raids = 0, Marriages = 0, Pacts = 0;
	};

	struct TelemetryStats
	{
		uint64_t SamplesWritten = 0, SamplesDropped = 0, EventsWritten = 0, Transactions = 0, WriteErrors = 0;
	};

	class TelemetrySink
	{
	public:
		size_t SampleCapacity = 50000;        // samples waiting for the writer; more are dropped
		double FlushSeconds = 1.0;            // the writer commits about this often

		TelemetrySink() = default;
		~TelemetrySink();
		TelemetrySink(const TelemetrySink&) = delete;
		TelemetrySink& operator=(const TelemetrySink&) = delete;

		// Opens (or creates) the file and starts a run in it; a file that already has runs gets one more. Empty string: all went well.
		std::string Start(const std::string& path, const TelemetryRunInfo& info = {});
		// Writes what is waiting, closes the run (`simSeconds` >= 0 is stored as its length) and the file. Safe to call twice.
		void Stop(double simSeconds = -1.0);
		bool Active() const { return m_Active.load(); }
		const std::string& Path() const { return m_Path; }
		int64_t RunId() const { return m_Run; }
		TelemetryStats Stats() const;
		// For the tests: while held the writer writes nothing, so the queue can be filled on purpose.
		void HoldWriter(bool hold);

		void Individual(const TelemetryIndividual& row);
		void Sample(const TelemetrySample& row);
		void ClanSample(const TelemetryClanSample& row);
		void Event(const TelemetryEvent& row);
		void Task(const TelemetryTask& row);
		void Relation(const TelemetryRelation& row);
		void ClanRelation(const TelemetryClanRelation& row);
		void Decision(const TelemetryDecision& row);

	private:
		struct Item
		{
			int Table = 0;
			std::vector<TelemetryValue> Values;
		};
		void Push(Item item, bool lossy);
		void Run();   // the writer thread
		void WriteBatch(std::vector<Item>& items);

		struct Db;    // the SQLite handles, only in the .cpp
		Db* m_Db = nullptr;
		std::string m_Path;
		int64_t m_Run = 0;
		std::atomic<bool> m_Active{ false };
		bool m_Stopping = false;
		bool m_Hold = false;
		mutable std::mutex m_Mutex;
		std::condition_variable m_Wake;
		std::deque<Item> m_Lossy, m_Keep;
		TelemetryStats m_Stats;
		std::thread m_Thread;
	};
}
