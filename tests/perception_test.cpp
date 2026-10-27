// Perception, memory, needs and the utility chooser (H5 + H6), module by module: a cone with obstacles, sounds that carry a radius, a memory that fades
// and merges and survives a round trip through JSON, needs that fall with the season and kill, and a chooser that does not dither. CPU only.
#include "GB/AI/Memory.h"
#include "GB/AI/Mind.h"
#include "GB/AI/Needs.h"
#include "GB/AI/Perception.h"
#include "GB/AI/Utility.h"
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

static void Vision()
{
	const VisionParams v;                     // 28 m, 60 degrees either side, 3 m all round
	const glm::vec2 eye(0.0f), ahead(1.0f, 0.0f);
	Check(CanSee(v, eye, ahead, { 10.0f, 0.0f }, {}), "sees what is straight ahead");
	Check(!CanSee(v, eye, ahead, { 40.0f, 0.0f }, {}), "does not see beyond its range");
	Check(!CanSee(v, eye, ahead, { -10.0f, 0.0f }, {}), "does not see behind");
	Check(CanSee(v, eye, ahead, { 10.0f, 10.0f }, {}), "sees 45 degrees off (inside the cone)");
	Check(!CanSee(v, eye, ahead, { 3.0f, 10.0f }, {}), "does not see 73 degrees off (outside the cone)");
	Check(CanSee(v, eye, ahead, { -2.0f, 0.0f }, {}), "notices what is within 3 m whatever way it looks");
	Check(CanSee(v, eye, glm::vec2(0.0f), { -10.0f, 0.0f }, {}), "with no facing it looks all round");

	const std::vector<Occluder> wall = { { "Rock", { 5.0f, 0.0f }, 1.0f } };
	Check(!CanSee(v, eye, ahead, { 10.0f, 0.0f }, wall), "a rock between hides what is behind it");
	Check(CanSee(v, eye, ahead, { 10.0f, 4.0f }, wall), "it sees past the rock");
	Check(CanSee(v, eye, ahead, { 5.0f, 0.0f }, wall, "Rock"), "a rock is not hidden behind itself");
	Check(CanSee(v, { 4.2f, 0.0f }, ahead, { 10.0f, 0.0f }, wall), "standing against the rock it does not block its own view");
	Check(SegmentHitsCircle({ 0.0f, 0.0f }, { 10.0f, 0.0f }, { 5.0f, 0.5f }, 1.0f), "segment-circle: hit");
	Check(!SegmentHitsCircle({ 0.0f, 0.0f }, { 10.0f, 0.0f }, { 5.0f, 2.0f }, 1.0f), "segment-circle: miss");
	Check(!SegmentHitsCircle({ 0.0f, 0.0f }, { 3.0f, 0.0f }, { 5.0f, 0.0f }, 1.0f), "segment-circle: stops short");
}

static void Hearing()
{
	SoundBus bus;
	bus.Emit("chop", "Ana", "Oak", { 0.0f, 0.0f }, 20.0f, 1.0);
	bus.Emit("scream", "Biel", "", { 50.0f, 0.0f }, 30.0f, 2.0);
	Check(bus.Heard("Cleo", { 10.0f, 0.0f }, 1.0f, 0).size() == 1, "within 20 m it hears the chop and not the far scream");
	Check(bus.Heard("Cleo", { 40.0f, 0.0f }, 1.0f, 0).size() == 1 && bus.Heard("Cleo", { 40.0f, 0.0f }, 1.0f, 0)[0].Kind == "scream", "at 40 m only the scream reaches");
	Check(bus.Heard("Ana", { 0.0f, 0.0f }, 1.0f, 0).empty(), "nobody hears its own noise");
	Check(bus.Heard("Cleo", { 10.0f, 0.0f }, 0.4f, 0).empty(), "poor hearing shortens the reach");
	Check(bus.Heard("Cleo", { 10.0f, 0.0f }, 1.0f, 1).empty(), "a sound already heard (by sequence) is not heard again");
	bus.Prune(7.5, 6.0);
	Check(bus.Recent().size() == 1, "old sounds are dropped");
}

static void Memory()
{
	SpatialMemory mem;
	const MemoryConfig cfg;
	mem.Observe({ "Oak", "tree", { 3.0f, 4.0f }, 5, 10.0, false });
	mem.Observe({ "Berry", "berry_bush", { 1.0f, 1.0f }, 6, 12.0, false });
	Check(mem.Size() == 2 && mem.OfKind("tree").size() == 1, "it remembers what it saw, by kind");
	mem.Observe({ "Oak", "tree", { 3.0f, 4.0f }, 2, 20.0, false });
	Check(mem.Find("Oak")->Amount == 2, "a newer sighting replaces the old one");
	mem.Observe({ "Oak", "tree", { 9.0f, 9.0f }, -1, 15.0, true });
	Check(mem.Find("Oak")->Pos.x == 3.0f, "an older report does not replace a newer sighting");
	mem.Observe({ "Oak", "tree", { 3.5f, 4.0f }, -1, 20.0, true });
	Check(!mem.Find("Oak")->Heard, "a sound does not replace a sighting that is as new");

	Check(SpatialMemory::Confidence(*mem.Find("Oak"), 21.0, cfg) > 0.9f, "fresh knowledge is trusted");
	Check(SpatialMemory::Confidence(*mem.Find("Oak"), 20.0 + cfg.Lifetime * 0.9, cfg) < 0.5f, "old knowledge counts for less");
	mem.Observe({ "Stump", "tree", { 0.0f, 0.0f }, 0, 30.0, false });
	Check(SpatialMemory::Confidence(*mem.Find("Stump"), 40.0, cfg) == 0.0f, "a node seen empty is not worth a visit yet");
	Check(SpatialMemory::Confidence(*mem.Find("Stump"), 30.0 + cfg.RecheckSeconds + 1.0, cfg) > 0.0f, "... until it has had time to grow back");

	Check(mem.Expire(12.0 + cfg.Lifetime + 1.0, cfg) == 1 && !mem.Find("Berry") && mem.Find("Oak"), "what was not seen for its lifetime is forgotten");

	SpatialMemory other;
	other.Observe({ "Pine", "tree", { 20.0f, 0.0f }, 5, 50.0, false });
	other.Observe({ "Oak", "tree", { 3.0f, 4.0f }, 0, 5.0, false });
	const int learned = mem.Merge(other);
	Check(learned == 1 && mem.Find("Pine") && mem.Find("Pine")->Heard, "merging learns what the other knows, as told");
	Check(mem.Find("Oak")->Amount == 2, "merging keeps its own newer knowledge");

	SpatialMemory copy;
	Check(copy.FromJson(mem.ToJson()) && copy.ToJson() == mem.ToJson(), "memory survives a round trip through JSON, bit for bit");
	Check(!copy.FromJson("not json") && copy.Size() == mem.Size(), "a damaged memory is refused and changes nothing");

	AgentMind mind;
	mind.Memory = mem;
	mind.Current = { "gather", "Pine", 12.5, 8.0f };
	mind.Facing = { 0.6f, 0.8f };
	mind.ExploreCount = 3;
	mind.Orders = "goto 1 2 | wait 1";
	AgentMind back;
	Check(back.FromJson(mind.ToJson()) && back.ToJson() == mind.ToJson() && back.Current.Kind == "gather" && back.ExploreCount == 3, "a mind survives a round trip through JSON");
}

static void Needs()
{
	GameplayLibrary lib;
	RegisterNeedContent(lib);
	AbilitySystemComponent a("Ana", lib);
	DefineNeeds(a);
	const NeedsConfig cfg;
	NeedContext ctx;
	ctx.Hour = 12.0f;
	for (int i = 0; i < 300; ++i) UpdateNeeds(a, cfg, ctx, 1.0f);
	const float hungerSpring = a.Attribute(NeedName::Hunger);
	Check(hungerSpring < 70.0f && hungerSpring > 60.0f, "hunger falls about a third in 300 s");

	AbilitySystemComponent b("Biel", lib);
	DefineNeeds(b);
	NeedContext winter = ctx;
	winter.Time = Season::Winter;
	for (int i = 0; i < 300; ++i) UpdateNeeds(b, cfg, winter, 1.0f);
	Check(b.Attribute(NeedName::Hunger) < hungerSpring, "winter makes it hungrier");
	Check(b.Attribute(NeedName::Warmth) < a.Attribute(NeedName::Warmth), "winter makes it colder");
	NeedContext fire = winter;
	fire.NearFire = true;
	for (int i = 0; i < 100; ++i) UpdateNeeds(b, cfg, fire, 1.0f);
	Check(b.Attribute(NeedName::Warmth) > 90.0f, "a fire warms it up");

	AbilitySystemComponent c("Cleo", lib);
	DefineNeeds(c);
	for (int i = 0; i < 1000; ++i) UpdateNeeds(c, cfg, ctx, 1.0f);
	Check(c.EffectActive("Need.Hungry") && c.HasTag("Need.Hungry") && c.Attribute(NeedName::Vigor) < 1.0f, "a hungry one is slower (effect, tag and Vigor)");
	NeedContext asleep = ctx;
	asleep.Sleeping = true;
	const float rest = c.Attribute(NeedName::Rest);
	for (int i = 0; i < 100; ++i) UpdateNeeds(c, cfg, asleep, 1.0f);
	Check(c.Attribute(NeedName::Rest) > rest + 30.0f, "sleep restores rest");
	SatisfyNeed(c, NeedName::Hunger, 80.0f);
	Check(!c.EffectActive("Need.Hungry") && c.Attribute(NeedName::Vigor) > 0.99f, "eating takes the slowdown away");

	AbilitySystemComponent d("Dan", lib);
	DefineNeeds(d);
	int died = -1;
	for (int i = 0; i < 3000 && died < 0; ++i)
	{
		UpdateNeeds(d, cfg, ctx, 1.0f);
		if (IsDead(d)) died = i;
	}
	Check(died > 1000 && died < 1600 && CauseOfDeath(d) == "Hunger", "without food it starves in about 20 minutes of simulation");
	const float health = d.Attribute(NeedName::Health);
	UpdateNeeds(d, cfg, ctx, 100.0f);
	Check(d.Attribute(NeedName::Health) == health, "the dead no longer change");
	Check(HourOfDay(cfg, 0.0) == 8.0f && HourOfDay(cfg, 600.0) == 20.0f && IsNight(cfg, 23.0f) && IsNight(cfg, 3.0f) && !IsNight(cfg, 12.0f), "the hour of the day follows the clock");
}

static void Chooser()
{
	auto option = [](const char* kind, const char* target, float score) { UtilityOption o; o.Kind = kind; o.Target = target; o.Score = score; return o; };
	std::vector<UtilityOption> opts = { option("gather", "A", 0.80f), option("craft", "", 0.84f) };
	Commitment held{ "gather", "A", 100.0, 0.0f };
	Check(ChooseOption(opts, held, 103.0, ChooserConfig()) == 0, "a rival that is only a little better does not take over (it dithers no more)");
	Check(ChooseOption(opts, Commitment(), 103.0, ChooserConfig()) == 1, "with nothing in hand the best wins");
	opts[1].Score = 1.4f;
	Check(ChooseOption(opts, held, 103.0, ChooserConfig()) == 0, "even a clearly better rival waits out the minimum commitment");
	Check(ChooseOption(opts, held, 120.0, ChooserConfig()) == 1, "... and takes over once it is over");
	opts[1].Score = 1.8f;
	Check(ChooseOption(opts, held, 101.0, ChooserConfig()) == 1, "an emergency takes over at once");
	Check(ChooseOption({}, held, 0.0, ChooserConfig()) == -1, "no options, no choice");
	opts = { option("a", "", 0.5f), option("b", "", 0.5f) };
	Check(ChooseOption(opts, Commitment(), 0.0, ChooserConfig()) == 0, "ties go to the first");
	Check(Ramp(5.0f, 0.0f, 10.0f) == 0.5f && Urgency(30.0f, 60.0f) == 0.5f && Urgency(80.0f, 60.0f) == 0.0f && Falloff(30.0f, 30.0f) == 0.5f, "the curves");
}

int main()
{
	Vision();
	Hearing();
	Memory();
	Needs();
	Chooser();
	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::puts("perception: ok");
	return 0;
}
