#include "GB/Telemetry/TelemetrySink.h"
#include <chrono>
#include <ctime>
#include <memory>
#include <sqlite3.h>

namespace GB
{
	namespace
	{
		enum Table { kIndividuals = 0, kSamples, kClanSamples, kEvents, kTasks, kDecisions, kRelations, kClanRelations, kClanDays, kWorldNodes, kTableCount };

		struct Column { const char* Name; const char* Type; };
		struct TableSpec { const char* Name; std::vector<Column> Columns; };

		// (run_id is the first column of every table and is bound by the writer)
		const std::vector<TableSpec>& Specs()
		{
			static const std::vector<TableSpec> specs = {
				{ "individuals", { { "clan_id", "INTEGER" }, { "member", "TEXT" }, { "role", "TEXT" }, { "joined", "REAL" }, { "female", "INTEGER" }, { "mother", "TEXT" }, { "father", "TEXT" },
					{ "generation", "INTEGER" }, { "genes", "TEXT" }, { "traits", "TEXT" } } },
				{ "samples", { { "clan_id", "INTEGER" }, { "member", "TEXT" }, { "t", "REAL" }, { "day", "INTEGER" }, { "hour", "REAL" }, { "role", "TEXT" }, { "job", "TEXT" }, { "detail", "TEXT" },
					{ "x", "REAL" }, { "z", "REAL" }, { "region", "INTEGER" }, { "hunger", "REAL" }, { "thirst", "REAL" }, { "rest", "REAL" }, { "warmth", "REAL" }, { "health", "REAL" },
					{ "age", "REAL" }, { "alive", "INTEGER" }, { "bag", "TEXT" }, { "skills", "TEXT" } } },
				{ "clan_samples", { { "clan_id", "INTEGER" }, { "t", "REAL" }, { "day", "INTEGER" }, { "hour", "REAL" }, { "tier", "TEXT" }, { "food_days", "REAL" }, { "alive", "INTEGER" },
					{ "dead", "INTEGER" }, { "beds", "INTEGER" }, { "buildings", "INTEGER" }, { "fire", "INTEGER" }, { "store", "TEXT" } } },
				{ "events", { { "clan_id", "INTEGER" }, { "t", "REAL" }, { "kind", "TEXT" }, { "who", "TEXT" }, { "other", "TEXT" }, { "result", "TEXT" }, { "text", "TEXT" } } },
					{ "tasks", { { "clan_id", "INTEGER" }, { "member", "TEXT" }, { "kind", "TEXT" }, { "target", "TEXT" }, { "start", "REAL" }, { "end", "REAL" }, { "result", "TEXT" }, { "reason", "TEXT" } } },
					{ "decisions", { { "clan_id", "INTEGER" }, { "t", "REAL" }, { "member", "TEXT" }, { "chosen", "TEXT" }, { "target", "TEXT" }, { "previous", "TEXT" }, { "options", "TEXT" } } },
					{ "relations", { { "clan_id", "INTEGER" }, { "t", "REAL" }, { "a", "TEXT" }, { "b", "TEXT" }, { "kin", "TEXT" }, { "bond", "REAL" }, { "affinity", "REAL" } } },
					{ "clan_relations", { { "clan_a", "INTEGER" }, { "clan_b", "INTEGER" }, { "t", "REAL" }, { "stance", "TEXT" }, { "attitude", "REAL" }, { "hostility", "REAL" }, { "trust", "REAL" },
						{ "grievance_ab", "REAL" }, { "grievance_ba", "REAL" }, { "encounters", "INTEGER" }, { "trades", "INTEGER" }, { "gifts", "INTEGER" }, { "thefts", "INTEGER" }, { "raids", "INTEGER" },
						{ "marriages", "INTEGER" }, { "pacts", "INTEGER" } } },
					{ "clan_days", { { "clan_id", "INTEGER" }, { "t", "REAL" }, { "day", "INTEGER" }, { "members", "INTEGER" }, { "buildings", "INTEGER" }, { "techs", "TEXT" }, { "beliefs", "TEXT" }, { "rites", "TEXT" }, { "mood", "REAL" } } },
					{ "world_nodes", { { "t", "REAL" }, { "kind", "TEXT" }, { "count", "INTEGER" }, { "empty", "INTEGER" }, { "amount", "REAL" } } },
			};
			return specs;
		}

		std::string Now()
		{
			const std::time_t t = std::time(nullptr);
			std::tm tm{};
#ifdef _WIN32
			gmtime_s(&tm, &t);
#else
			gmtime_r(&t, &tm);
#endif
			char b[32];
			std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
			return b;
		}

		TelemetryValue V(int64_t v) { return v; }
		TelemetryValue V(int v) { return static_cast<int64_t>(v); }
		TelemetryValue V(bool v) { return static_cast<int64_t>(v ? 1 : 0); }
		TelemetryValue V(double v) { return v; }
		TelemetryValue V(const std::string& v) { return v; }
	}

	struct TelemetrySink::Db
	{
		sqlite3* Handle = nullptr;
		sqlite3_stmt* Insert[kTableCount] = {};
	};

	TelemetrySink::~TelemetrySink() { Stop(); }

	std::string TelemetrySink::Start(const std::string& path, const TelemetryRunInfo& info)
	{
		if (m_Active.load() || m_Db) return "the telemetry is already running (" + m_Path + ")";
		auto db = std::make_unique<Db>();
		if (sqlite3_open_v2(path.c_str(), &db->Handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
		{
			const std::string why = db->Handle ? sqlite3_errmsg(db->Handle) : "out of memory";
			sqlite3_close(db->Handle);
			return "cannot open " + path + ": " + why;
		}
		auto fail = [&](const std::string& what)
		{
			const std::string why = what + ": " + sqlite3_errmsg(db->Handle);
			for (sqlite3_stmt* s : db->Insert) sqlite3_finalize(s);
			sqlite3_close(db->Handle);
			return why;
		};
		sqlite3_busy_timeout(db->Handle, 5000);
		if (sqlite3_exec(db->Handle, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr) != SQLITE_OK) return fail("pragma");
		std::string schema = "CREATE TABLE IF NOT EXISTS runs (run_id INTEGER PRIMARY KEY, schema_version INTEGER, started TEXT, seed INTEGER, commit_hash TEXT, profile TEXT, note TEXT, "
			"sim_seconds REAL, samples_written INTEGER, samples_dropped INTEGER, events_written INTEGER, closed INTEGER);";
		for (const TableSpec& t : Specs())
		{
			schema += std::string("CREATE TABLE IF NOT EXISTS ") + t.Name + " (run_id INTEGER";
			for (const Column& c : t.Columns) schema += std::string(", ") + c.Name + " " + c.Type;
			schema += ");";
		}
		if (sqlite3_exec(db->Handle, schema.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) return fail("schema");
		{
			sqlite3_stmt* s = nullptr;
			if (sqlite3_prepare_v2(db->Handle, "INSERT INTO runs (schema_version, started, seed, commit_hash, profile, note, closed) VALUES (4, ?, ?, ?, ?, ?, 0)", -1, &s, nullptr) != SQLITE_OK) return fail("run");
			const std::string now = Now();
			sqlite3_bind_text(s, 1, now.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(s, 2, info.Seed);
			sqlite3_bind_text(s, 3, info.Commit.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(s, 4, info.Profile.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(s, 5, info.Note.c_str(), -1, SQLITE_TRANSIENT);
			const int rc = sqlite3_step(s);
			sqlite3_finalize(s);
			if (rc != SQLITE_DONE) return fail("run");
			m_Run = sqlite3_last_insert_rowid(db->Handle);
		}
		for (int t = 0; t < kTableCount; ++t)
		{
			const TableSpec& spec = Specs()[static_cast<size_t>(t)];
			std::string sql = std::string("INSERT INTO ") + spec.Name + " VALUES (?";
			for (size_t i = 0; i < spec.Columns.size(); ++i) sql += ",?";
			sql += ")";
			if (sqlite3_prepare_v2(db->Handle, sql.c_str(), -1, &db->Insert[t], nullptr) != SQLITE_OK) return fail("prepare");
		}
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Lossy.clear();
			m_Keep.clear();
			m_Stats = {};
			m_Stopping = false;
			m_Hold = false;
		}
		m_Db = db.release();
		m_Path = path;
		m_Active = true;
		m_Thread = std::thread([this] { Run(); });
		return {};
	}

	void TelemetrySink::Stop(double simSeconds)
	{
		if (!m_Db) return;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Active = false;
			m_Stopping = true;
			m_Hold = false;
		}
		m_Wake.notify_all();
		if (m_Thread.joinable()) m_Thread.join();
		const TelemetryStats s = Stats();
		sqlite3_stmt* u = nullptr;
		if (sqlite3_prepare_v2(m_Db->Handle, "UPDATE runs SET sim_seconds = ?, samples_written = ?, samples_dropped = ?, events_written = ?, closed = 1 WHERE run_id = ?", -1, &u, nullptr) == SQLITE_OK)
		{
			if (simSeconds >= 0.0) sqlite3_bind_double(u, 1, simSeconds); else sqlite3_bind_null(u, 1);
			sqlite3_bind_int64(u, 2, static_cast<sqlite3_int64>(s.SamplesWritten));
			sqlite3_bind_int64(u, 3, static_cast<sqlite3_int64>(s.SamplesDropped));
			sqlite3_bind_int64(u, 4, static_cast<sqlite3_int64>(s.EventsWritten));
			sqlite3_bind_int64(u, 5, m_Run);
			sqlite3_step(u);
		}
		sqlite3_finalize(u);
		for (sqlite3_stmt* st : m_Db->Insert) sqlite3_finalize(st);
		sqlite3_exec(m_Db->Handle, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
		sqlite3_close(m_Db->Handle);
		delete m_Db;
		m_Db = nullptr;
	}

	TelemetryStats TelemetrySink::Stats() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Stats;
	}

	void TelemetrySink::HoldWriter(bool hold)
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Hold = hold;
		}
		m_Wake.notify_all();
	}

	void TelemetrySink::Push(Item item, bool lossy)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_Active.load()) return;
		if (!lossy) { m_Keep.push_back(std::move(item)); return; }
		if (m_Lossy.size() >= SampleCapacity) { ++m_Stats.SamplesDropped; return; }
		m_Lossy.push_back(std::move(item));
	}

	void TelemetrySink::Individual(const TelemetryIndividual& r)
	{
		if (!m_Active.load()) return;
		Push({ kIndividuals, { V(r.Clan), V(r.Member), V(r.Role), V(r.Joined), V(r.Female), V(r.Mother), V(r.Father), V(r.Generation), V(r.Genes), V(r.Traits) } }, false);
	}

	void TelemetrySink::Sample(const TelemetrySample& r)
	{
		if (!m_Active.load()) return;
		Push({ kSamples, { V(r.Clan), V(r.Member), V(r.Time), V(r.Day), V(r.Hour), V(r.Role), V(r.Job), V(r.Detail), V(r.X), V(r.Z), V(r.Region), V(r.Hunger), V(r.Thirst), V(r.Rest),
			V(r.Warmth), V(r.Health), V(r.Age), V(r.Alive), V(r.Bag), V(r.Skills) } }, true);
	}

	void TelemetrySink::ClanSample(const TelemetryClanSample& r)
	{
		if (!m_Active.load()) return;
		Push({ kClanSamples, { V(r.Clan), V(r.Time), V(r.Day), V(r.Hour), V(r.Tier), V(r.FoodDays), V(r.Alive), V(r.Dead), V(r.Beds), V(r.Buildings), V(r.Fire), V(r.Store) } }, true);
	}

	void TelemetrySink::Event(const TelemetryEvent& r)
	{
		if (!m_Active.load()) return;
		Push({ kEvents, { V(r.Clan), V(r.Time), V(r.Kind), V(r.Who), V(r.Other), V(r.Result), V(r.Text) } }, false);
	}

	void TelemetrySink::Task(const TelemetryTask& r)
	{
		if (!m_Active.load()) return;
		Push({ kTasks, { V(r.Clan), V(r.Member), V(r.Kind), V(r.Target), V(r.Start), V(r.End), V(r.Result), V(r.Reason) } }, false);
	}

	void TelemetrySink::Decision(const TelemetryDecision& r)
	{
		if (!m_Active.load()) return;
		Push({ kDecisions, { V(r.Clan), V(r.Time), V(r.Member), V(r.Chosen), V(r.Target), V(r.Previous), V(r.Options) } }, true);
	}

	void TelemetrySink::Relation(const TelemetryRelation& r)
	{
		if (!m_Active.load()) return;
		Push({ kRelations, { V(r.Clan), V(r.Time), V(r.A), V(r.B), V(r.Kin), V(r.Bond), V(r.Affinity) } }, true);
	}

	void TelemetrySink::ClanRelation(const TelemetryClanRelation& r)
	{
		if (!m_Active.load()) return;
		Push({ kClanRelations, { V(r.A), V(r.B), V(r.Time), V(r.Stance), V(r.Attitude), V(r.Hostility), V(r.Trust), V(r.GrievanceAB), V(r.GrievanceBA), V(r.Encounters), V(r.Trades), V(r.Gifts),
			V(r.Thefts), V(r.Raids), V(r.Marriages), V(r.Pacts) } }, false);
	}

	void TelemetrySink::ClanDay(const TelemetryClanDay& r)
	{
		if (!m_Active.load()) return;
		Push({ kClanDays, { V(r.Clan), V(r.Time), V(r.Day), V(r.Members), V(r.Buildings), V(r.Techs), V(r.Beliefs), V(r.Rites), V(r.Mood) } }, false);
	}

	void TelemetrySink::WorldNodes(const TelemetryWorldNodes& r)
	{
		if (!m_Active.load()) return;
		Push({ kWorldNodes, { V(r.Time), V(r.Kind), V(r.Count), V(r.Empty), V(r.Amount) } }, false);
	}

	void TelemetrySink::Run()
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		for (;;)
		{
			m_Wake.wait_for(lock, std::chrono::duration<double>(FlushSeconds), [this] { return m_Stopping; });
			if (m_Hold && !m_Stopping) continue;
			std::vector<Item> batch;
			batch.reserve(m_Keep.size() + m_Lossy.size());
			for (Item& i : m_Keep) batch.push_back(std::move(i));
			for (Item& i : m_Lossy) batch.push_back(std::move(i));
			m_Keep.clear();
			m_Lossy.clear();
			const bool last = m_Stopping;
			lock.unlock();
			if (!batch.empty()) WriteBatch(batch);
			lock.lock();
			if (last) break;
		}
	}

	void TelemetrySink::WriteBatch(std::vector<Item>& items)
	{
		sqlite3* h = m_Db->Handle;
		uint64_t samples = 0, events = 0, errors = 0;
		sqlite3_exec(h, "BEGIN", nullptr, nullptr, nullptr);
		for (const Item& item : items)
		{
			sqlite3_stmt* s = m_Db->Insert[item.Table];
			sqlite3_reset(s);
			sqlite3_bind_int64(s, 1, m_Run);
			for (size_t i = 0; i < item.Values.size(); ++i)
			{
				const int col = static_cast<int>(i) + 2;
				const TelemetryValue& v = item.Values[i];
				if (const auto* n = std::get_if<int64_t>(&v)) sqlite3_bind_int64(s, col, *n);
				else if (const auto* d = std::get_if<double>(&v)) sqlite3_bind_double(s, col, *d);
				else if (const auto* t = std::get_if<std::string>(&v)) sqlite3_bind_text(s, col, t->c_str(), static_cast<int>(t->size()), SQLITE_TRANSIENT);
				else sqlite3_bind_null(s, col);
			}
			if (sqlite3_step(s) != SQLITE_DONE) { ++errors; continue; }
			if (item.Table == kSamples || item.Table == kClanSamples) ++samples;
			else if (item.Table == kEvents) ++events;
		}
		sqlite3_exec(h, "COMMIT", nullptr, nullptr, nullptr);
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Stats.SamplesWritten += samples;
		m_Stats.EventsWritten += events;
		m_Stats.WriteErrors += errors;
		++m_Stats.Transactions;
	}
}
