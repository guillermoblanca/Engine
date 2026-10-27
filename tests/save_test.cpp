// Saved games (H4): the format round trips byte for byte, only what changed is stored, old versions migrate, damaged files are refused
// without touching the world, files are replaced atomically, the autosave timer ticks, and a replay plays to the same state twice.
// CPU only. The golden saves in tests/golden/saves are written by a past version and must keep loading: GB_UPDATE_GOLDEN=1 regenerates the
// one of the CURRENT format (never overwrite an older one: they are the point).
#include "GB/Game/SceneIO.h"
#include "GB/Qa/Qa.h"
#include "GB/Save/GameSession.h"
#include "GB/Save/SaveFiles.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace GB;
using namespace GB::Save;

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	void CheckR(const Result& r, const char* what)
	{
		if (!r.Ok) { std::fprintf(stderr, "FAIL: %s: %s\n", what, r.Error.c_str()); ++failures; }
	}

	std::string Slurp(const std::string& path)
	{
		std::string t;
		ReadTextFile(path, t);
		t.erase(std::remove(t.begin(), t.end(), '\r'), t.end());   // a checkout with CRLF line endings is the same file
		return t;
	}

	WorldParams Seed(uint32_t seed)
	{
		WorldParams w;
		w.Generated = true;
		w.Seed = seed;
		w.TreeBudget = 300;
		w.RockBudget = 40;
		return w;
	}

	struct Order { uint64_t Tick; const char* Command; };

	// The orders of a short game: build the garden, work a plot, cut trees, walk, leave a crate.
	const std::vector<Order>& Script()
	{
		static const std::vector<Order> s = {
			{ 0, "garden build 0 0" },
			{ 1, "spawn Gardener" },
			{ 5, "activate Gardener Dig \"Plot 1\"" },
			{ 200, "activate Gardener Sow \"Plot 1\"" },
			{ 200, "fell \"Tree 3\"" },
			{ 200, "fell \"Tree 10\"" },
			{ 210, "add Crate 4 0 5" },
			{ 240, "input Gardener 0 1" },
			{ 420, "activate Gardener Water \"Plot 1\"" },
			{ 330, "weather rain" },
			{ 340, "time 7.5" },
			{ 400, "input Gardener 0 0" },
		};
		return s;
	}

	// Runs the script on `s` up to tick `until` (orders at a tick go before that tick is simulated).
	void Play(GameSession& s, uint64_t until, ReplayRecorder* rec = nullptr, uint64_t from = 0)
	{
		for (uint64_t t = from; t < until; ++t)
		{
			for (const Order& o : Script())
				if (o.Tick == t)
				{
					std::string msg;
					if (rec) rec->Record(s.Tick(), o.Command);
					s.Command(o.Command, msg);
				}
			s.Step();
		}
	}
}

static void TestRoundTrip()
{
	GameSession a({ Seed(5) });
	Play(a, 600);
	const CaptureResult c1 = CaptureGame(a);
	const std::string text1 = SaveGameToString(c1.Game);

	SaveGame parsed;
	CheckR(ParseSaveGame(text1, parsed), "the saved text parses");
	Check(SaveGameToString(parsed) == text1, "parse + write gives the same text");

	GameSession b({ Seed(99) });   // a different world: loading must replace everything
	CheckR(ApplyGame(b, parsed), "the save loads into another session");
	const std::string text2 = SaveGameToString(CaptureGame(b).Game);
	Check(text1 == text2, "save, load, save: the same file");
	Check(ComputeDigest(a) == ComputeDigest(b), "the loaded session has the state of the saved one");
	Check(b.Tick() == 600 && b.World().Seed == 5, "the clock and the seed come back");
	Check(b.Entities().FindByName("Crate") != NullEntity, "an added object comes back");
	Check(b.Entities().FindByName("Tree 3") == NullEntity && b.Entities().FindByName("Tree 4") != NullEntity, "a felled tree stays felled, its neighbours stay");
	Check(c1.Game.Clock.Hours > 7.0f && c1.Game.Weather.Preset == "rain", "hour and weather are in the header");

	// continuing from the loaded game gives what continuing the original gives
	Play(a, 900, nullptr, 600);
	Play(b, 900, nullptr, 600);
	Check(a.Tick() == 900 && ComputeDigest(a) == ComputeDigest(b), "playing on after a load gives the same game");
	Check(a.Entities().NextId() == b.Entities().NextId(), "the next entity id survives the load");
}

static void TestDifferential()
{
	GameSession a({ Seed(5) });
	Check(CaptureGame(a).Game.Entities.Size() == 0, "a world nobody touched saves no entities");
	Play(a, 450);
	const SaveGame g = CaptureGame(a).Game;
	Check(g.Entities.Removed.size() == 2, "the two felled trees are listed as removed");
	Check(g.Entities.Size() < 120, "the diff is a small share of the 340 generated objects");

	SaveGame whole = g;   // the same game written without the seed: every entity is in the file
	whole.World.Generated = false;
	whole.Baseline = BaselineIdOf(EntityWorld());
	whole.Entities = DiffEntities(EntityWorld(), a.Entities());
	Check(SaveGameToString(g).size() * 3 < SaveGameToString(whole).size(), "the differential save is much smaller than the whole world");
}

static void TestWorldParamsAndDrift()
{
	GameSession a({ Seed(5) });
	Play(a, 100);
	SaveGame g = CaptureGame(a).Game;

	GameSession fresh({ Seed(5) });
	Play(fresh, 20);
	const Digest before = ComputeDigest(fresh);

	SaveGame drifted = g;
	drifted.Baseline.Hash = "0000000000000000";
	Result r = ApplyGame(fresh, drifted);
	Check(!r.Ok && r.Error.find("seed") != std::string::npos, "a generator that changed is reported");
	Check(ComputeDigest(fresh) == before, "a refused load leaves the world alone");
	LoadOptions lenient;
	lenient.AllowBaselineDrift = true;
	r = ApplyGame(fresh, drifted, lenient);
	Check(r.Ok && !r.Warnings.empty(), "the drift can be accepted with a warning");

	// a diff that does not hold together (the parent of a tree removed, its children kept)
	GameSession s2({ Seed(5) });
	Play(s2, 20);
	const Digest before2 = ComputeDigest(s2);
	SaveGame broken = g;
	broken.Entities.Removed.push_back(s2.Entities().FindByName("Trees"));
	r = ApplyGame(s2, broken);
	Check(!r.Ok, "entities that do not link are refused");
	Check(ComputeDigest(s2) == before2, "...and nothing changed");

	// a state for an ability system the scene does not have, and for an effect nobody defined
	SaveGame ghost = g;
	ghost.Gameplay.push_back({ "Nobody", {} });
	Check(!ApplyGame(s2, ghost).Ok && ComputeDigest(s2) == before2, "an ability system with no entity is refused");
	SaveGame badEffect = g;
	for (GameplayRuntime& rt : badEffect.Gameplay)
		if (rt.Entity == "Gardener") rt.State.Effects.push_back({ "NoSuchEffect", 1.0f, 0.0f });
	r = ApplyGame(s2, badEffect);
	Check(!r.Ok && r.Error.find("NoSuchEffect") != std::string::npos && ComputeDigest(s2) == before2, "an unknown effect is refused");
	SaveGame badActor = g;
	badActor.Actors.push_back({ "Ghost" });
	Check(!ApplyGame(s2, badActor).Ok && ComputeDigest(s2) == before2, "an actor with no entity is refused");
}

static void TestCooldownsAndAttributes()
{
	GameSession a({ Seed(1) });
	std::string m;
	a.Command("garden build 0 0", m);
	a.Command("activate Gardener Dig \"Plot 1\"", m);
	for (int i = 0; i < 400; ++i) a.Step();
	AbilitySystemComponent* g = a.MutableGameplay().Find("Gardener");
	Check(g && GardenSystem::SoilState(*a.MutableGameplay().Find("Plot 1")) == "dug", "the plot was dug");
	const float energy = g->Attribute("Energy");
	// a cooldown still running when saved
	g->ApplyEffect(MakeCooldownEffect("Dig", 30.0f));
	for (int i = 0; i < 60; ++i) a.Step();
	const float left = g->CooldownRemaining("Dig");
	Check(left > 28.0f && left < 30.0f, "the cooldown is running");
	GameSession b({ Seed(1) });
	CheckR(ApplyGame(b, CaptureGame(a).Game), "load with a cooldown");
	AbilitySystemComponent* g2 = b.MutableGameplay().Find("Gardener");
	Check(g2 && std::fabs(g2->CooldownRemaining("Dig") - left) < 1e-3f, "the cooldown continues where it was");
	Check(g2->HasTag("Cooldown.Ability.Dig") && std::fabs(g2->Attribute("Energy") - energy) < 1e-3f, "its tag and the attributes come back");
	for (int i = 0; i < 31 * 60; ++i) b.Step();
	Check(!b.MutableGameplay().Find("Gardener")->HasTag("Cooldown.Ability.Dig"), "and it ends on time");
	// an ability in flight is reported
	GameSession c({ Seed(1) });
	c.Command("garden build 0 0", m);
	c.Command("activate Gardener Dig \"Plot 1\"", m);
	c.Step();
	Check(!CaptureGame(c).Warnings.empty(), "saving mid-ability warns");
}

static void TestMigrations()
{
	// the chain machinery, with a made-up chain: v1 -> v2 -> v3 on plain text
	const std::vector<Migration> chain = {
		{ 1, "rename a", [](std::string& j, std::string&) { j += "+2"; return true; } },
		{ 2, "rename b", [](std::string& j, std::string&) { j += "+3"; return true; } },
	};
	std::string t = "x";
	std::vector<std::string> log;
	Check(Migrate(t, 1, 3, chain, &log).Ok && t == "x+2+3" && log.size() == 2, "migrations run in order");
	t = "x";
	Check(!Migrate(t, 1, 4, chain).Ok, "a missing step is an error");
	t = "x";
	std::vector<Migration> failing = chain;
	failing[1].Apply = [](std::string&, std::string& e) { e = "boom"; return false; };
	const Result f = Migrate(t, 1, 3, failing);
	Check(!f.Ok && f.Error.find("boom") != std::string::npos, "a failing step reports why");
	Check(BuiltinMigrations().size() == static_cast<size_t>(kFormatVersion), "there is one migration per old format");
	for (size_t i = 0; i < BuiltinMigrations().size(); ++i) Check(BuiltinMigrations()[i].From == static_cast<int>(i), "the chain has no gaps");

	const std::string dir = std::string(GB_SAVES_DIR);
	// v0: a bare scene file from before saved games
	{
		if (Qa::Config::UpdateMode() && !fs::exists(dir + "/v0_scene.json"))
		{
			EntityWorld w;
			BuildGarden(w, glm::vec3(0.0f));
			CheckR({ SaveSceneToFile(w, dir + "/v0_scene.json").Ok, "", {} }, "writing the golden v0 scene");
		}
		SaveGame g;
		const std::string text = Slurp(dir + "/v0_scene.json");
		Check(!text.empty() && DetectFormat(text) == 0, "the v0 golden is a bare scene");
		const Result r = ParseSaveGame(text, g);
		CheckR(r, "a v0 scene migrates");
		Check(!r.Warnings.empty(), "the migration is reported");
		GameSession s({ Seed(1) });
		CheckR(ApplyGame(s, g), "a migrated v0 scene loads");
		Check(!s.World().Generated && s.Entities().FindByName("Plot 1") != NullEntity && s.Entities().FindByName("Gardener") != NullEntity, "its entities are there");
		EntityWorld direct;
		LoadSceneFromString(direct, text);
		Check(direct.Count() == s.Entities().Count() && SaveSceneToString(direct) == SaveSceneToString(s.Entities()), "exactly the scene's entities, ids included");
		s.MutableGameplay().Load(s.Entities());
		Check(s.MutableGameplay().Find("Gardener") != nullptr, "the characters' abilities are alive");
		// saved again it is a current-format file that round trips
		const std::string again = SaveGameToString(CaptureGame(s).Game);
		SaveGame back;
		CheckR(ParseSaveGame(again, back), "the migrated game saves in the current format");
		Check(back.Format == kFormatVersion && SaveGameToString(back) == again, "...and that file round trips");
	}
	// v1: the first saved game format
	{
		const std::string path = dir + "/v1_garden.gbsave";
		if (Qa::Config::UpdateMode() && !fs::exists(path))   // a past version is frozen: never rewritten
		{
			GameSession s({ Seed(7) });
			Play(s, 500);
			CheckR(SaveGameToFile(CaptureGame(s).Game, path), "writing the golden v1 save");
		}
		SaveGame g;
		const std::string text = Slurp(path);
		Check(DetectFormat(text) == 1, "the v1 golden is format 1");
		CheckR(ParseSaveGame(text, g), "the v1 save loads");
		GameSession s({ Seed(3) });
		CheckR(ApplyGame(s, g), "the v1 save applies on the generated world");
		Check(s.World().Seed == 7 && s.Tick() == 500 && s.Entities().FindByName("Tree 3") == NullEntity && s.Entities().FindByName("Crate") != NullEntity,
			"the v1 save has its world, clock and edits");
		Check(GardenSystem::SoilState(*s.MutableGameplay().Find("Plot 1")) != "" && s.Character("Gardener") != nullptr, "the v1 garden and walker are alive");
		if (kFormatVersion == 1) Check(SaveGameToString(g) == text, "the v1 golden is exactly what this version writes");
		for (int i = 0; i < 120; ++i) s.Step();   // and it keeps simulating
		Check(s.Tick() == 620, "the v1 game runs on");
	}
}

static void TestDamagedFiles()
{
	GameSession a({ Seed(5) });
	Play(a, 300);
	const std::string text = SaveGameToString(CaptureGame(a).Game);
	SaveGame out;
	CheckR(ParseSaveGame(text, out), "the intact file loads");
	Check(!ParseSaveGame("", out).Ok, "an empty file is refused");
	Check(!ParseSaveGame("not json at all", out).Ok, "garbage is refused");
	Check(!ParseSaveGame("[1,2,3]", out).Ok, "JSON that is not a save is refused");
	Check(!ParseSaveGame("{\"hello\": 1}", out).Ok, "an object without a format is refused");
	for (size_t cut : { text.size() / 2, text.size() - 3, text.size() / 10, size_t(5) })
		Check(!ParseSaveGame(text.substr(0, cut), out).Ok, "a truncated file is refused");
	// a flipped value
	std::string flipped = text;
	const size_t at = flipped.find("\"tick\": 3");
	Check(at != std::string::npos, "the tick is in the file");
	if (at != std::string::npos)
	{
		flipped[at + 8] = '4';
		const Result r = ParseSaveGame(flipped, out);
		Check(!r.Ok && r.Error.find("checksum") != std::string::npos, "an edited file fails its checksum");
	}
	std::string newer = text;
	const size_t f = newer.find("\"format\": 1");
	if (f != std::string::npos) newer.replace(f, 11, "\"format\": 99");
	const Result rn = ParseSaveGame(newer, out);
	Check(!rn.Ok && rn.Error.find("newer") != std::string::npos, "a save from the future says so");
	std::string noSum = text;
	const size_t c = noSum.find("\"checksum\"");
	if (c != std::string::npos) noSum.replace(c, 10, "\"checksuX\"");
	Check(!ParseSaveGame(noSum, out).Ok, "a file without its checksum is refused");
	Check(out.Clock.Tick == 0 || out.Clock.Tick == 300, "a failed parse leaves the output as it was");
}

static void TestFilesAndAtomicity()
{
	const fs::path dir = fs::temp_directory_path() / "gb_save_test";
	fs::remove_all(dir);
	const std::string path = SlotPath(dir.generic_string(), "slot");

	Check(IsValidSlotName("quicksave") && IsValidSlotName("a-b_c.1") && !IsValidSlotName("") && !IsValidSlotName("../x") && !IsValidSlotName("a/b") &&
		!IsValidSlotName(".hidden") && !IsValidSlotName("a b"), "slot names cannot be paths");

	GameSession a({ Seed(5) });
	Play(a, 200);
	const SaveGame g1 = CaptureGame(a).Game;
	CheckR(SaveGameToFile(g1, path), "first save");
	Check(!fs::exists(BackupPath(path)) && !fs::exists(TempPath(path)), "the first save leaves no backup and no temporary");
	const std::string first = Slurp(path);

	Play(a, 400, nullptr, 200);
	const SaveGame g2 = CaptureGame(a).Game;
	CheckR(SaveGameToFile(g2, path), "second save");
	Check(Slurp(BackupPath(path)) == first, "the previous save is kept as the backup");
	const std::string second = Slurp(path);
	Check(second != first && !fs::exists(TempPath(path)), "the file was replaced and no temporary is left");

	// a power cut after the temporary was written: the real file is still the complete previous one
	Play(a, 500, nullptr, 400);
	WriteOptions crash;
	crash.SimulateCrashAfterTemp = true;
	Check(!SaveGameToFile(CaptureGame(a).Game, path, crash).Ok, "the simulated crash is reported");
	Check(Slurp(path) == second, "the destination is untouched by an interrupted save");
	Check(fs::exists(TempPath(path)), "the half-done save is left aside");
	SaveGame loaded;
	CheckR(LoadSaveGameFromFile(path, loaded), "the old save still loads");
	Check(loaded.Clock.Tick == 400, "...and it is the old one");
	CheckR(SaveGameToFile(CaptureGame(a).Game, path), "the next save works despite the leftover temporary");
	Check(!fs::exists(TempPath(path)), "which is cleaned up");

	// the file is damaged afterwards: the backup takes over
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		f << Slurp(path).substr(0, 100);
	}
	const Result viaBackup = LoadSaveGameFromFile(path, loaded);
	Check(viaBackup.Ok && !viaBackup.Warnings.empty() && loaded.Clock.Tick == 400, "a damaged file falls back to the backup, with a warning");
	Check(!LoadSaveGameFromFile(path, loaded, false).Ok, "...unless the backup is not wanted");
	Check(!LoadSaveGameFromFile((dir / "missing.gbsave").generic_string(), loaded).Ok, "a missing file is an error");

	// listing
	CheckR(SaveGameToFile(g1, SlotPath(dir.generic_string(), "other")), "another slot");
	{
		std::ofstream f(SlotPath(dir.generic_string(), "broken"), std::ios::binary);
		f << "{\"format\": 1";
	}
	const std::vector<SaveInfo> list = ListSaves(dir.generic_string());
	Check(list.size() == 3, "three slots are listed (backups and temporaries are not)");
	bool sawBroken = false, sawOther = false;
	for (const SaveInfo& i : list)
	{
		if (i.Name == "broken") sawBroken = !i.Ok && !i.Error.empty();
		if (i.Name == "other") sawOther = i.Ok && i.Format == kFormatVersion && i.Tick == 200 && i.Seed == 5 && i.Modified > 0 && i.Bytes > 0;
	}
	Check(sawBroken && sawOther, "the listing shows version, tick and seed, and flags damaged files");
	Check(FormatTime(list.empty() ? 0 : list[0].Modified).size() == 16, "the date prints as yyyy-mm-dd hh:mm");
	fs::remove_all(dir);
}

static void TestAutosave()
{
	AutoSaver s;
	Check(!s.Tick(1000.0), "off: never");
	s.Configure(true, 10.0);
	int fired = 0;
	for (int i = 0; i < 600; ++i) fired += s.Tick(0.1) ? 1 : 0;   // 60 s
	Check(fired == 6, "one save per interval");
	s.Configure(false, 0.0);
	Check(!s.Tick(100.0) && s.Interval() == 10.0, "turning it off keeps the interval");
}

static void TestReplay()
{
	// record: the script from a fresh world
	GameSession rec({ Seed(11) });
	ReplayRecorder recorder;
	Check(recorder.Begin(rec).Ok && recorder.Active(), "recording starts");
	Play(rec, 700, &recorder);
	const Replay replay = recorder.End(rec);
	Check(replay.Ticks == 700 && replay.Events.size() == Script().size() && replay.HasFinal, "the replay has the orders and the end state");

	GameSession one({ Seed(2) }), two({ Seed(3) });   // other worlds: the replay brings its own
	const ReplayRun r1 = PlayReplay(one, replay), r2 = PlayReplay(two, replay);
	CheckR(r1.Status, "the replay plays");
	Check(r1.Matches && r2.Matches && r1.Actual == r2.Actual && r1.TicksRun == 700, "two runs end in the recorded state");
	Check(r1.FailedCommands == 0, "no order failed");
	Check(ComputeDigest(one) == ComputeDigest(rec), "...which is the state of the original session");

	// through a file
	const std::string text = ReplayToString(replay);
	Replay back;
	CheckR(ParseReplay(text, back), "the replay file parses");
	Check(ReplayToString(back) == text, "and writes the same text");
	GameSession three({ Seed(4) });
	Check(PlayReplay(three, back).Matches, "a replay read from text plays to the same state");

	// a regression is noticed and located
	Replay changed = back;
	changed.Events[3].Command = "activate Gardener Dig \"Plot 2\"";
	GameSession four({ Seed(4) });
	const ReplayRun bad = PlayReplay(four, changed);
	Check(!bad.Matches && !bad.Differing.empty(), "a different order ends in a different state");
	changed = back;
	changed.Ticks += 200;
	GameSession five({ Seed(4) });
	const ReplayRun longer = PlayReplay(five, changed);
	Check(!longer.Matches && std::find(longer.Differing.begin(), longer.Differing.end(), "clock") != longer.Differing.end(), "the report names the part that differs");
	Check(!ParseReplay("{}", back).Ok && !ParseReplay(text.substr(0, text.size() / 2), back).Ok, "bad replay files are refused");

	// a replay that starts from a loaded game
	GameSession mid({ Seed(11) });
	Play(mid, 450);
	std::string m;
	mid.Command("input Gardener 0 0", m);
	ReplayRecorder second;
	second.Begin(mid);
	second.Record(mid.Tick(), "garden build 30 30");   // a second garden is not possible by name; just checks orders at the start tick replay
	Play(mid, 600, &second, 450);
	mid.Command("garden build 30 30", m);
	const Replay tail = second.End(mid);
	GameSession six({ Seed(1) });
	const ReplayRun tr = PlayReplay(six, tail);
	CheckR(tr.Status, "a replay from the middle of a game plays");
}

static void TestActorSection()
{
	// an actor source (what the Actor / PlayerMode of the editor will provide) replaces the walkers, and sections are carried
	struct Source : IActorStateSource
	{
		std::vector<ActorState> Actors;
		bool Refuse = false;
		std::vector<ActorState> CaptureActors() const override { return Actors; }
		std::string RestoreActors(const std::vector<ActorState>& a, bool check) override
		{
			if (Refuse) return "refused";
			if (!check) Actors = a;
			return {};
		}
	} src;
	ActorState p;
	p.Name = "Gardener";
	p.Position = { 1.5f, 0.0f, -2.25f };
	p.Yaw = 1.25f;
	p.Set = "farming";
	p.Props = { "shovel" };
	p.Inventory = { { "seed", 5 }, { "apple", 2 } };
	p.Ragdoll = true;
	p.Bodies = { { "Hips", { 0, 1, 0 }, { 0, 0, 0, 1 }, { 0, -1, 0 }, { 0, 0, 0.5f } } };
	src.Actors = { p };

	GameSession a({ Seed(5) });
	std::string m;
	a.Command("garden build 0 0", m);
	a.SetActorSource(&src);
	SaveGame g = CaptureGame(a).Game;
	g.Sections.push_back({ "inventory", 2, "{\"slots\":[1,2,3]}" });
	const std::string text = SaveGameToString(g);
	SaveGame back;
	CheckR(ParseSaveGame(text, back), "a save with actors and sections parses");
	Check(SaveGameToString(back) == text, "...and round trips");
	Check(back.Actors.size() == 1 && back.Actors[0].Inventory.size() == 2 && back.Actors[0].Bodies.size() == 1 && back.Actors[0].Props[0] == "shovel" &&
		back.Actors[0].Set == "farming" && back.Sections.size() == 1 && back.Sections[0].Version == 2, "set, props, inventory, ragdoll and sections are all there");

	GameSession b({ Seed(5) });
	Source dst;
	b.SetActorSource(&dst);
	CheckR(ApplyGame(b, back), "the actors go to their source");
	Check(dst.Actors.size() == 1 && dst.Actors[0].Position == p.Position, "...with their state");
	dst.Refuse = true;
	GameSession c({ Seed(5) });
	c.SetActorSource(&dst);
	Check(!ApplyGame(c, back).Ok, "an actor source that refuses stops the load");
}

// Slots the format keeps ready for later (docs/GUARDADO.md, "Huecos reservados"): how long a stack has existed (goods that spoil), what an agent knows, what an
// actor was made from. They are optional: written only when set, so every save made without them is the same bytes as before, and a golden save with them loads.
static void TestReservedSlots()
{
	GameSession s({ Seed(11) });
	Play(s, 60);
	SaveGame plain = CaptureGame(s).Game;
	plain.Actors.clear();
	ActorState bare;
	bare.Name = "Ana";
	bare.Inventory.push_back({ "berry", 3 });
	plain.Actors.push_back(bare);
	const std::string without = SaveGameToString(plain);
	Check(without.find("\"age\"") == std::string::npos && without.find("\"knowledge\"") == std::string::npos && without.find("\"kind\"") == std::string::npos,
		"a save that sets none of the reserved slots does not mention them");

	SaveGame full = plain;
	full.Actors[0].Kind = "fox";
	full.Actors[0].Knowledge = { "recipe.stone_axe", "place.camp" };
	full.Actors[0].Inventory = { { "berry", 3, 1250.5f }, { "wood", 2, 0.0f } };
	full.Sections.push_back({ "knowledge-clan", 1, "{\"known\":[\"recipe.stone_axe\"]}" });
	const std::string with = SaveGameToString(full);
	SaveGame back;
	CheckR(ParseSaveGame(with, back), "a save with the reserved slots loads");
	Check(back.Actors.size() == 1 && back.Actors[0].Kind == "fox" && back.Actors[0].Knowledge == full.Actors[0].Knowledge, "the kind and the knowledge come back");
	Check(back.Actors[0].Inventory.size() == 2 && back.Actors[0].Inventory[0].Age == 1250.5f && back.Actors[0].Inventory[1].Age == 0.0f, "the age of a stack comes back, and a stack without one stays at 0");
	Check(SaveGameToString(back) == with, "and written again it is the same file");
	Check(back.Sections.size() == full.Sections.size() && back.Sections.back().Name == "knowledge-clan", "a section nobody reads yet is kept");

	// the golden: a past version wrote it and this one must keep loading it
	const std::string path = std::string(GB_SAVES_DIR) + "/v1_slots.gbsave";
	if (Qa::Config::UpdateMode() && !fs::exists(path)) CheckR(SaveGameToFile(full, path), "writing the golden save with the reserved slots");
	SaveGame g;
	const std::string text = Slurp(path);
	CheckR(ParseSaveGame(text, g), "the golden save with the reserved slots loads");
	Check(g.Format == 1 && g.Actors.size() == 1 && g.Actors[0].Kind == "fox" && g.Actors[0].Inventory[0].Age == 1250.5f, "with what it was written with");
	if (kFormatVersion == 1) Check(SaveGameToString(g) == text, "and this version writes it back exactly");
}

int main()
try
{
	TestRoundTrip();
	TestDifferential();
	TestWorldParamsAndDrift();
	TestCooldownsAndAttributes();
	TestMigrations();
	TestDamagedFiles();
	TestFilesAndAtomicity();
	TestAutosave();
	TestReplay();
	TestActorSection();
	TestReservedSlots();
	if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
	std::puts("save tests passed");
	return 0;
}
catch (const std::exception& e)
{
	std::fprintf(stderr, "exception: %s\n", e.what());
	return 2;
}
