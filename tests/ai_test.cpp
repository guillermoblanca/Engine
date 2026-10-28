// The brain of an agent (H5 + H6), end to end: one agent alone in a world, with a stand-in body that runs the same task text a real actor runs.
// Checked: with food and water within reach it lives through ten simulated days (seasons, nights, winters) and keeps itself fed, watered and rested; with
// no food it starves; a game saved in the middle and loaded into a fresh world goes on exactly as the one that never stopped; two runs end identically;
// it does not dither between jobs; a hundred of them live two days (ten with GB_AI_CROWD_DAYS=10) with their decisions spread over frames and far ones simulated abstractly; a shout is heard, remembered and makes it feel unsafe; what one learns the clan learns at the camp. CPU only.
//
// The test only checks behaviour, so it passes the same on a busy machine. Opt-in, for measuring and for the full acceptance of H6:
//   GB_AI_BENCH=1          prints the cost of the tribe of a hundred and fails when it is over its budget (20 ms per simulated second)
//   GB_AI_CROWD_DAYS=10    the hundred of one tribe live ten days instead of two (about three minutes)
//   GB_AI_ISOLATED=100     that many agents, each alone in a world of its own, live ten days (instead of six; H6 asks for 100: a few minutes)
#include "GB/AI/Needs.h"
#include "GB/Game/SceneIO.h"
#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Save/SaveWorld.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <sstream>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float dt = 0.1f;
	constexpr double kDay = 1200.0;   // NeedsConfig::DaySeconds
}

using namespace GB;

// Runs the text of a task list against an ability system: goto and wait take time, "ability A node" walks to the node, activates it and waits until it ends.
class FakeBody : public ITribeBody
{
public:
	struct Step { std::string Kind, Ability, Node; float X = 0, Z = 0, Seconds = 0; bool Walking = false; };
	struct State
	{
		glm::vec2 Pos{ 0.0f }, Target{ 0.0f };
		std::vector<Step> Steps;
		bool Started = false, Failed = false;
		std::string Bag;
	};

	FakeBody(const std::string& name, GameplayWorld& gameplay, EntityWorld& entities, glm::vec2 start) : m_Name(name), m_Gameplay(gameplay), m_Entities(entities)
	{
		m_S.Pos = start;
	}
	bool Busy() const override { return !m_S.Steps.empty(); }
	bool Failed() const override { return m_S.Failed; }
	glm::vec2 Position() const override { return m_S.Pos; }
	Inventory& Bag() override { return m_Bag; }
	void Teleport(const glm::vec2& p) override { m_S.Pos = p; m_S.Steps.clear(); m_S.Started = false; }
	void SetSuspended(bool on) override { m_Suspended = on; }
	bool Suspended() const { return m_Suspended; }
	bool Do(const std::string& tasks, std::string& error) override
	{
		m_S.Steps.clear();
		m_S.Failed = false;
		std::stringstream ss(tasks);
		std::string part;
		while (std::getline(ss, part, '|'))
		{
			std::stringstream ws(part);
			Step s;
			ws >> s.Kind;
			if (s.Kind == "goto") ws >> s.X >> s.Z;
			else if (s.Kind == "wait") ws >> s.Seconds;
			else if (s.Kind == "ability") { ws >> s.Ability >> s.Node; for (char& c : s.Node) if (c == '_') c = ' '; }
			else { error = "unknown task " + s.Kind; m_S.Steps.clear(); return false; }
			m_S.Steps.push_back(s);
		}
		m_S.Started = false;
		return true;
	}
	void Update(float step)
	{
		if (m_Suspended) return;   // the tribe moves it as an abstract agent
		AbilitySystemComponent* me = m_Gameplay.Find(m_Name);
		ResourceSystem::SyncTools(*me, m_Bag, *m_Items);
		if (m_S.Steps.empty()) return;
		const float speed = 3.0f * std::max(0.3f, me->Attribute(NeedName::Vigor));   // the tired and hungry walk slower
		Step& s = m_S.Steps.front();
		if (!m_S.Started)
		{
			m_S.Started = true;
			if (s.Kind == "goto") { s.Seconds = glm::distance(m_S.Pos, glm::vec2(s.X, s.Z)) / speed; m_S.Target = { s.X, s.Z }; }
			if (s.Kind == "ability")
			{
				const EntityId id = m_Entities.FindByName(s.Node);
				const glm::vec4 p = id == NullEntity ? glm::vec4(0.0f) : m_Entities.WorldMatrix(id)[3];
				s.Seconds = glm::distance(m_S.Pos, glm::vec2(p.x, p.z)) / speed;
				m_S.Target = { p.x, p.z };
				s.Walking = true;
			}
		}
		if (s.Seconds > 0.0f) { s.Seconds -= step; if (s.Seconds > 0.0f) return; m_S.Pos = m_S.Target; }
		if (s.Kind == "ability" && s.Walking)
		{
			s.Walking = false;
			std::string why;
			if (!me->Activate(s.Ability, m_Gameplay.Find(s.Node), &why, s.Node)) { m_S.Failed = true; m_S.Steps.clear(); return; }
			return;
		}
		if (s.Kind == "ability" && me->AnyActive()) return;
		m_S.Steps.erase(m_S.Steps.begin());
		m_S.Started = false;
	}
	State Capture() const { State s = m_S; s.Bag = m_Bag.ToJson(); return s; }
	void Restore(const State& s) { m_S = s; m_Bag.FromJson(s.Bag); }
	static std::string Describe(const State& s)
	{
		std::string out = std::to_string(s.Pos.x) + "," + std::to_string(s.Pos.y) + "|" + s.Bag + "|" + (s.Started ? "S" : "-") + (s.Failed ? "F" : "-");
		for (const Step& st : s.Steps) out += ";" + st.Kind + st.Ability + st.Node + std::to_string(st.Seconds) + (st.Walking ? "W" : "");
		return out;
	}
	const ItemLibrary* m_Items = nullptr;
private:
	std::string m_Name;
	GameplayWorld& m_Gameplay;
	EntityWorld& m_Entities;
	Inventory m_Bag{ 12, 0.0f };
	State m_S;
	bool m_Suspended = false;
};

struct Settings
{
	int Trees = 3, Rocks = 2, Bushes = 2, Grass = 2;
	bool Well = true;
	glm::vec2 Camp{ 0.0f, -8.0f };
	glm::vec3 Origin{ 0.0f };
};

struct Snapshot
{
	std::string Scene, Tribe;
	std::vector<Save::GameplayRuntime> Runtime;
	FakeBody::State Body;
	double Clock = 0.0;
};

// One agent called Hero alone with a patch of resources (or none) and a well.
class World
{
public:
	explicit World(const Settings& settings, const Snapshot* from = nullptr)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		if (!from)
		{
			BuildResources(Entities, settings.Origin, Defs, settings.Trees, settings.Rocks, settings.Bushes, settings.Grass);
			if (settings.Well) BuildInteractives(Entities, settings.Origin);
			Entity& e = *Entities.Find(Entities.Create("Hero"));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "PullFiber", "DrawWater" };
			Gameplay.Load(Entities);
		}
		else
		{
			Check(LoadSceneFromString(Entities, from->Scene).Ok, "the saved scene loads");
			Gameplay.Load(Entities);
		}
		Body = std::make_unique<FakeBody>("Hero", Gameplay, Entities, settings.Camp);
		Body->m_Items = &Items;
		Brain.Bind(Entities, Gameplay, Items, Recipes, Defs);   // (registers the effects of the needs, which the saved clocks may name)
		if (from)
		{
			std::string error;
			Check(Save::RestoreGameplay(Gameplay, Entities, from->Runtime, &error), "the abilities' clocks are restored");
		}
		Resources.Bind(Gameplay, Defs, [this](const std::string&, const std::string& item, int n) { Body->Bag().Add(Items, item, n); });
		Rules.Bind(Gameplay,
			[this](const std::string&, const std::string& item, int n) { return Body->Bag().Remove(item, n); },
			[this](const std::string&, const std::string& item, int n) { return Body->Bag().Add(Items, item, n); });
		Brain.Config.Camp = settings.Camp;
		Brain.AddMember("Hero", "forager", Body.get());
		if (from)
		{
			Body->Restore(from->Body);
			Check(Brain.FromJson(from->Tribe, false), "the tribe loads");
			Clock = from->Clock;
		}
	}

	AbilitySystemComponent& Hero() { return *Gameplay.Find("Hero"); }
	const TribeMember& Member() const { return Brain.Members()[0]; }

	void Frame()
	{
		Clock += static_cast<double>(dt);
		Gameplay.Update(dt);
		Resources.Update(Gameplay, dt, SeasonClock::GrowthRate(Brain.Config.Seasons.At(Clock)));
		Rules.Update(dt, &Entities);
		Body->Update(dt);
		Resources.SyncVisuals(Gameplay, Entities);
		Brain.SetWorldClock(Clock);
		Brain.Update(dt);
		const std::string job = Member().Job;
		if (job != m_LastJob) { ++Switches; if (job == "sleep") ++Sleeps; m_LastJob = job; }
		if (Hero().Attribute(NeedName::Hunger) < m_MinHunger) m_MinHunger = Hero().Attribute(NeedName::Hunger);
	}
	void Run(double seconds)
	{
		const int frames = static_cast<int>(seconds / static_cast<double>(dt));
		for (int i = 0; i < frames; ++i) Frame();
	}
	// Until nothing is in flight (an ability in progress is not saved: it is cancelled).
	void RunUntilIdle()
	{
		for (int i = 0; i < 6000 && Hero().AnyActive(); ++i) Frame();
	}
	Snapshot Capture()
	{
		Snapshot s;
		Gameplay.Store(Entities);
		s.Scene = SaveSceneToString(Entities);
		s.Runtime = Save::CaptureGameplay(Gameplay);
		s.Tribe = Brain.ToJson();
		s.Body = Body->Capture();
		s.Clock = Clock;
		return s;
	}
	std::string Fingerprint()
	{
		Gameplay.Store(Entities);
		return SaveSceneToString(Entities) + "\n" + Brain.ToJson() + "\n" + FakeBody::Describe(Body->Capture());
	}

	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Rules;
	Tribe Brain;
	std::unique_ptr<FakeBody> Body;
	double Clock = 0.0;
	int Switches = 0, Sleeps = 0;
private:
	std::string m_LastJob = "-";
	float m_MinHunger = 100.0f;
};

// A hundred members of one tribe with their own stand-in bodies and a patch of resources scaled to them.
class Crowd
{
public:
	Crowd(int n, bool lod)
	{
		RegisterEraOneItems(Items, Recipes, Defs);
		RegisterResourceContent(Gameplay.Library, Defs);
		RegisterInteractiveContent(Gameplay.Library);
		const ResourceCounts counts = ScaledResources(n);
		BuildResources(Entities, glm::vec3(0.0f), Defs, counts.Trees, counts.Rocks, counts.Bushes, counts.Grass);
		BuildInteractives(Entities, glm::vec3(0.0f));
		for (int i = 1; i <= n / 10; ++i)   // one well gives water to a few: a crowd needs more of them
		{
			const EntityId copy = Entities.Duplicate(Entities.FindByName("Well"));
			Entities.Find(copy)->Transform.Position = { 6.0f + 3.0f * static_cast<float>(i), 0.0f, static_cast<float>(i % 3) * 4.0f - 4.0f };
		}
		for (int i = 0; i < n; ++i)
		{
			Entity& e = *Entities.Find(Entities.Create("Npc " + std::to_string(i)));
			e.Gameplay.emplace();
			e.Gameplay->Abilities = { "Chop", "Mine", "Gather", "DrawWater" };
		}
		Gameplay.Load(Entities);
		const glm::vec2 camp(0.0f, -8.0f);
		static const char* const roles[] = { "forager", "woodcutter", "miner", "forager" };
		Brain.Bind(Entities, Gameplay, Items, Recipes, Defs);
		Brain.Config.Camp = camp;
		Brain.Config.DetailRadius = 25.0f;
		Brain.Config.StockCap = 400;
		Brain.Config.Econ.Logistics.Stores["main"].Cap = 400;   // (H18: the cap of the main store is the economy rules')
		Brain.Store().Add(Items, "wood", 2 + 2 * n);
		Brain.Store().Add(Items, "stone", 3 * n);
		for (int i = 0; i < n; ++i)
		{
			const std::string name = "Npc " + std::to_string(i);
			Bodies.push_back(std::make_unique<FakeBody>(name, Gameplay, Entities, camp + glm::vec2(static_cast<float>(i % 10) * 0.7f, static_cast<float>(i / 10) * 0.7f)));
			Bodies.back()->m_Items = &Items;
			Index[name] = static_cast<size_t>(i);
		}
		Resources.Bind(Gameplay, Defs, [this](const std::string& who, const std::string& item, int count) { Bodies[Index[who]]->Bag().Add(Items, item, count); });
		Rules.Bind(Gameplay,
			[this](const std::string& who, const std::string& item, int count) { return Bodies[Index[who]]->Bag().Remove(item, count); },
			[this](const std::string& who, const std::string& item, int count) { return Bodies[Index[who]]->Bag().Add(Items, item, count); });
		for (int i = 0; i < n; ++i) Brain.AddMember("Npc " + std::to_string(i), roles[i % 4], Bodies[static_cast<size_t>(i)].get());
		if (lod) Brain.SetViewer(camp);
		Brain.Config.Seasons = SeasonClock();
	}
	void Run(double seconds)
	{
		const float step = 0.2f;
		const int frames = static_cast<int>(seconds / static_cast<double>(step));
		for (int i = 0; i < frames; ++i)
		{
			Clock += static_cast<double>(step);
			Gameplay.Update(step);
			Resources.Update(Gameplay, step, SeasonClock::GrowthRate(Brain.Config.Seasons.At(Clock)));
			Rules.Update(step, &Entities);
			for (auto& b : Bodies) b->Update(step);
			Resources.SyncVisuals(Gameplay, Entities);
			Brain.SetWorldClock(Clock);
			const auto t0 = std::chrono::steady_clock::now();
			Brain.Update(step);
			const double ms = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			BrainSeconds += ms;
			WorstMs = std::max(WorstMs, ms * 1000.0);
			++Frames;
		}
	}
	ItemLibrary Items;
	RecipeBook Recipes;
	std::map<std::string, ResourceNodeDef> Defs;
	EntityWorld Entities;
	GameplayWorld Gameplay;
	ResourceSystem Resources;
	InteractiveSystem Rules;
	Tribe Brain;
	std::vector<std::unique_ptr<FakeBody>> Bodies;
	std::map<std::string, size_t> Index;
	double Clock = 0.0, BrainSeconds = 0.0, WorstMs = 0.0;
	int Frames = 0;
};

int main()
{
	const auto started = std::chrono::steady_clock::now();
	const double tenDays = 10.0 * kDay;

	// ---- alone with food and water for ten days
	Settings rich;
	World lives(rich);
	double lowestHealth = 100.0;
	double lowestHunger = 100.0;
	double lowestThirst = 100.0;
	const int frames = static_cast<int>(tenDays / static_cast<double>(dt));
	for (int i = 0; i < frames; ++i)
	{
		lives.Frame();
		if (i % 100 == 0)
		{
			lowestHealth = std::min(lowestHealth, static_cast<double>(lives.Hero().Attribute(NeedName::Health)));
			lowestHunger = std::min(lowestHunger, static_cast<double>(lives.Hero().Attribute(NeedName::Hunger)));
			lowestThirst = std::min(lowestThirst, static_cast<double>(lives.Hero().Attribute(NeedName::Thirst)));
		}
	}
	const TribeMember& hero = lives.Member();
	std::fprintf(stderr, "  rich world, 10 days: %s | hunger min %.0f thirst min %.0f health min %.0f | %d job changes, %d sleeps, %d meals, %zu things remembered\n",
		lives.Brain.Report().c_str(), lowestHunger, lowestThirst, lowestHealth, lives.Switches, lives.Sleeps, hero.Meals, hero.Mind.Memory.Size());
	Check(!hero.Dead && !IsDead(lives.Hero()), "alone with food and water it lives through ten days");
	Check(lowestHealth > 90.0, "and never gets hurt");
	Check(lowestHunger > 10.0 && lowestThirst > 10.0, "it keeps itself fed and watered (no need ever near empty)");
	Check(hero.Meals >= 30, "it ate regularly");
	Check(lives.Sleeps >= 9, "it slept nearly every night");
	Check(lives.Switches / 10 < 400, "it does not dither: fewer than 400 job changes a day");
	Check(hero.Failures <= 2, "almost no trip failed");
	Check(hero.Mind.Memory.OfKind("berry_bush").size() == 2 && !hero.Mind.Memory.OfKind("well").empty(), "it remembers where the berries and the well are");
	Check(hero.Mind.Memory.Find("Well") != nullptr, "the well it drinks from is in its memory");

	// ---- alone with no food
	Settings poor;
	poor.Bushes = 0;
	poor.Grass = 0;
	World starves(poor);
	double diedAt = -1.0;
	for (int i = 0; i < static_cast<int>(3.0 * kDay / static_cast<double>(dt)) && diedAt < 0.0; ++i)
	{
		starves.Frame();
		if (starves.Member().Dead) diedAt = starves.Clock;
	}
	std::fprintf(stderr, "  poor world: died at %.0f s of %s, explored %d times\n", diedAt, starves.Member().Cause.c_str(), starves.Member().Mind.ExploreCount);
	Check(diedAt > 0.0 && diedAt < 2.0 * kDay, "with nothing to eat it dies within two days");
	Check(starves.Member().Cause == "Hunger" && CauseOfDeath(starves.Hero()) == "Hunger", "and it dies of hunger");
	Check(starves.Member().Mind.ExploreCount >= 2, "after looking for food further and further away");

	// ---- several agents, each alone in a world of its own (different camps and patches), ten days
	{
		const int isolated = std::getenv("GB_AI_ISOLATED") ? std::max(1, std::atoi(std::getenv("GB_AI_ISOLATED"))) : 6;
		int alive = 0, total = 0;
		for (int i = 0; i < isolated; ++i)
		{
			Settings s;
			s.Origin = glm::vec3(static_cast<float>(i) * 37.0f - 150.0f, 0.0f, static_cast<float>((i * 7) % 5) * 11.0f);
			s.Camp = { s.Origin.x + static_cast<float>(i % 3) * 3.0f - 3.0f, s.Origin.z - 8.0f };
			s.Trees = 2 + i % 3;
			s.Bushes = 1 + i % 3;   // from one bush to three
			World w(s);
			w.Run(tenDays);
			++total;
			if (!w.Member().Dead) ++alive;
		}
		std::fprintf(stderr, "  %d isolated agents, 10 days: %d alive\n", total, alive);
		Check(alive == total, "agents alone in different worlds all live through ten days");
	}

	// ---- the same world twice
	Settings small;
	World a(small), b(small);
	a.Run(2.0 * kDay);
	b.Run(2.0 * kDay);
	Check(a.Fingerprint() == b.Fingerprint(), "two runs end identically");

	// ---- save in the middle, load into a fresh world, and go on exactly the same
	World whole(small);
	whole.Run(1.4 * kDay);
	whole.RunUntilIdle();
	const Snapshot snap = whole.Capture();
	Check(!snap.Tribe.empty() && snap.Tribe.find("\"memory\"") != std::string::npos, "the save holds the memory");
	Check(snap.Scene.find("Hunger") != std::string::npos, "the save holds the needs");
	whole.Run(2.0 * kDay);
	World loaded(small, &snap);
	Check(loaded.Fingerprint() == [&]() { World again(small, &snap); return again.Fingerprint(); }(), "loading the same save twice gives the same game");
	loaded.Run(2.0 * kDay);
	const std::string wholeEnd = whole.Fingerprint();
	const std::string loadedEnd = loaded.Fingerprint();
	if (wholeEnd != loadedEnd)
	{
		size_t i = 0;
		while (i < wholeEnd.size() && i < loadedEnd.size() && wholeEnd[i] == loadedEnd[i]) ++i;
		std::fprintf(stderr, "  differ at %zu: ...%s | ...%s\n", i, wholeEnd.substr(i > 40 ? i - 40 : 0, 120).c_str(), loadedEnd.substr(i > 40 ? i - 40 : 0, 120).c_str());
	}
	Check(wholeEnd == loadedEnd, "a game loaded from the middle goes on exactly like the one that never stopped");
	Check(!loaded.Member().Dead && loaded.Member().Mind.Memory.Size() >= 4, "and it still remembers what it knew");

	// ---- a shout is heard, remembered and unsettling
	{
		Settings s;
		World w(s);
		w.Run(30.0);
		const float safe = w.Hero().Attribute(NeedName::Safety);
		w.Brain.Shout("Stranger", w.Body->Position() + glm::vec2(5.0f, 0.0f), 30.0f);
		w.Run(3.0);
		Check(w.Hero().Attribute(NeedName::Safety) < safe - 20.0f, "it hears a shout and feels less safe");
		Check(w.Member().Mind.Memory.Find("Stranger") != nullptr && w.Member().Mind.Memory.Find("Stranger")->Heard, "it remembers where it came from, by ear");
		w.Brain.Shout("Far", { 500.0f, 500.0f }, 30.0f);
		const size_t known = w.Member().Mind.Memory.Size();
		w.Run(3.0);
		Check(w.Member().Mind.Memory.Size() == known, "a shout out of earshot is not heard");
		w.Run(20.0);
		bool wondered = false;
		for (const UtilityOption& o : w.Member().Mind.Options) if (o.Kind == "regroup") wondered = true;
		Check(wondered, "its options include going back to the camp");
	}

	// ---- level of detail: far from the viewer an agent is abstract and decides the same; coming back, the body is put where the agent is
	{
		Settings s;
		World w(s);
		w.Brain.SetViewer({ 1000.0f, 1000.0f });
		w.Run(200.0);
		Check(w.Member().Abstract && w.Body->Suspended() && w.Brain.AbstractCount() == 1, "far from the viewer the member is an abstract agent and its body is suspended");
		Check(glm::distance(w.Member().Agent.Position(), s.Camp) > 0.1f || w.Member().Meals > 0 || !w.Member().Job.empty(), "and it keeps working");
		const glm::vec2 there = w.Member().Agent.Position();
		w.Brain.SetViewer(s.Camp);
		w.Run(3.0);
		Check(!w.Member().Abstract && !w.Body->Suspended(), "when the viewer comes near it is full again");
		Check(glm::distance(w.Body->Position(), there) < 4.0f, "and its body stands where the agent was: no jump");
		World alone(s);
		alone.Brain.SetViewer({ 1000.0f, 1000.0f });
		alone.Run(tenDays);
		Check(!alone.Member().Dead && alone.Member().Abstract, "an agent simulated abstractly for ten days lives as well");
	}

	// ---- the log is a ring, members can be removed, a member with no body does not crash anything
	{
		Settings s;
		World w(s);
		w.Brain.Config.LogLines = 40;
		w.Brain.AddMember("Ghost", "forager", nullptr);
		w.Run(1500.0);
		Check(w.Brain.Log().size() <= 52, "the log keeps only its last lines");
		Check(w.Brain.RemoveMember("Ghost") && !w.Brain.RemoveMember("Ghost") && w.Brain.Members().size() == 1, "a member can be removed once");
		w.Run(50.0);
		Check(!w.Member().Dead, "and the rest goes on");
	}

	// ---- a hundred agents, one tribe with a patch scaled to it: ten days, decisions spread over frames, the far ones abstract
	{
		const int crowd = 100;
		const double crowdDays = std::getenv("GB_AI_CROWD_DAYS") ? std::atof(std::getenv("GB_AI_CROWD_DAYS")) : 2.0;   // ten days take two minutes: GB_AI_CROWD_DAYS=10
		const bool bench = std::getenv("GB_AI_BENCH") != nullptr;   // the budget in milliseconds only means something on a quiet machine: opt-in
		std::fprintf(stderr, "  crowd of %d:", crowd);
		for (const bool lod : { false, true })
		{
			Crowd c(crowd, lod);
			const auto t0 = std::chrono::steady_clock::now();
			c.Run(1200.0 * (lod ? crowdDays : 1.0));
			const double brainMs = c.BrainSeconds * 1000.0;
			int alive = 0;
			std::string causes;
			for (const TribeMember& m : c.Brain.Members()) { if (!m.Dead) ++alive; else causes += m.Cause.substr(0, 1); }
			if (!causes.empty()) std::fprintf(stderr, " (dead of %s)", causes.c_str());
			std::fprintf(stderr, " [%s] %d alive, tribe update %.2f ms per simulated second on average (%.2f ms worst frame), %d abstract at the end, took %.1f s;", lod ? "with level of detail" : "all full", alive,
				brainMs / c.Clock, c.WorstMs, c.Brain.AbstractCount(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
			Check(alive == crowd, "a hundred agents live through the days of the run");
			if (bench) Check(brainMs / c.Clock < 20.0, "the tribe update costs under 20 ms per simulated second (about 0.3 ms a frame at 60 fps) with a hundred members");
			if (lod) Check(c.Brain.AbstractCount() > 0, "some were abstract");
		}
		std::fprintf(stderr, "\n");
	}

	// ---- why: the inspector has something to say
	{
		const std::string why = lives.Brain.Because(lives.Member());
		Check(!why.empty() && why != "no decision yet", "the agent can say why it does what it does");
		Check(!lives.Member().Mind.Options.empty() && lives.Member().Mind.Chosen >= 0, "with the options it weighed");
		bool factors = false;
		for (const UtilityOption& o : lives.Member().Mind.Options) if (!o.Factors.empty()) factors = true;
		Check(factors, "and the factors of each score");
	}

	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	std::fprintf(stderr, "  ai test took %.1f s\n", seconds);
	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("ai: ok");
	return 0;
}
