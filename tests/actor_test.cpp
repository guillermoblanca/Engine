// The actor contract (Actor.h, ActorWorld.h): many characters at once without touching each other, the same result on every run, a source of
// orders that can change (keys, AI, task list, script, replay), removing and creating again, actors as obstacles for one another and the
// models they share. Runs on the generated humanoid of tests/data and, with GB_MIXAMO_DIR, GB_INJURED_DIR and GB_FARMING_DIR (the packs), on
// the real characters. With GB_ACTOR_BENCH set it also prints what an actor costs per frame. CPU only.
#include "GB/Game/ActorWorld.h"
#include "GB/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	constexpr float kDt = 1.0f / 60.0f;

	GB::ActorHost FlatHost()
	{
		GB::ActorHost host;
		host.World.GroundHeight = [](float, float) { return 0.0f; };
		return host;
	}

	std::shared_ptr<GB::Model> TestHumanoid()
	{
		const std::string dir = GB_TEST_DATA_DIR;
		auto model = GB::Model::Load(dir + "/test_humanoid.fbx");
		if (!model) return nullptr;
		for (const char* f : { "test_idle.fbx", "test_walk.fbx", "test_run.fbx" }) model->AppendAnimations(dir + "/" + f);
		return model;
	}

	// The three sets the generated actors use, from the clips they have.
	void GiveSets(GB::Actor& a, int which)
	{
		GB::AnimInstance* anim = a.Anim();
		if (!anim) return;
		if (which == 1) { anim->AddSet({ "alt", { "test_idle", "test_walk", "test_run" } }); a.SetOverlay("alt", 0.0f); }
		if (which == 2) { anim->AddSet({ "slow", { "test_idle", "test_walk" } }); a.SetOverlay("slow", 0.0f); }
	}

	GB::MontageDesc Work()
	{
		GB::MontageDesc d;
		d.Name = "work";
		d.Clip = "test_walk";
		d.BlendIn = 0.2f;
		d.BlendOut = 0.2f;
		return d;
	}

	// Positions of every actor after every frame, for comparing runs.
	using Trace = std::vector<glm::vec3>;

	// Three actors in lanes far from one another, each with its own set and task list. `only`: -1 = all of them, else just that one.
	struct Lanes
	{
		GB::ActorWorld World;
		GB::Actor* Who[3] = { nullptr, nullptr, nullptr };
	};
	bool BuildLanes(Lanes& l, const std::shared_ptr<GB::Model>& model, int only)
	{
		l.World.SetHost(FlatHost());
		const char* tasks[3] = { "goto 0 8|wait 0.5|goto 0 2", "goto 40 6 run|wait 1|goto 44 0", "goto 80 9|goto 78 -3 run" };
		for (int i = 0; i < 3; ++i)
		{
			if (only >= 0 && only != i) continue;
			GB::ActorSpawn s;
			s.Shared = model;
			s.Position = { 40.0f * static_cast<float>(i), 0.0f };
			std::string error;
			l.Who[i] = l.World.Spawn(std::string("npc") + static_cast<char>('0' + i), s, error);
			if (!l.Who[i]) { std::fprintf(stderr, "%s\n", error.c_str()); return false; }
			GiveSets(*l.Who[i], i);
			if (i == 1) l.Who[i]->Anim()->Montages().Add(Work());
			if (!l.Who[i]->RunTasks(tasks[i], error)) { std::fprintf(stderr, "%s\n", error.c_str()); return false; }
		}
		return true;
	}
	Trace RunLanes(Lanes& l, int frames)
	{
		Trace t;
		for (int f = 0; f < frames; ++f)
		{
			l.World.Update(kDt);
			for (GB::Actor* a : l.Who)
				if (a) t.push_back(a->GetPosition());
		}
		return t;
	}
	bool Same(const Trace& a, const Trace& b)
	{
		return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0);
	}

	void Lane(const Trace& together, size_t laneCount, size_t lane, Trace& out)
	{
		out.clear();
		for (size_t i = lane; i < together.size(); i += laneCount) out.push_back(together[i]);
	}

	void Tests(const std::shared_ptr<GB::Model>& model, const char* label, bool generated)
	{
		using namespace GB;
		std::printf("-- %s\n", label);
		std::string error;

		// ---- several actors with their own sets and tasks, at once, without touching one another
		{
			Lanes all;
			Check(BuildLanes(all, model, -1), "three actors are built");
			if (!all.Who[0] || !all.Who[1] || !all.Who[2]) return;
			Check(all.World.Count() == 3 && all.Who[0]->GetModel() == all.Who[1]->GetModel() && all.Who[1]->GetModel() == all.Who[2]->GetModel(), "they share one model");
			Check(all.Who[0]->Instance() != all.Who[1]->Instance() && all.Who[1]->Instance() != all.Who[2]->Instance(), "and each has its own instance");
			if (generated)
			{
				Check(all.Who[0]->Anim()->Overlay() != all.Who[1]->Anim()->Overlay() && all.Who[1]->Anim()->Overlay() != all.Who[2]->Anim()->Overlay(), "each is in a different set");
				all.Who[1]->PlayMontage("work");
			}
			const Trace together = RunLanes(all, 60 * 20);
			bool done = true;
			for (GB::Actor* a : all.Who) done = done && !a->TasksRunning() && !a->TasksFailed();
			Check(done, "all of them finish their task lists");
			const glm::vec3 end[3] = { { 0.0f, 0.0f, 2.0f }, { 44.0f, 0.0f, 0.0f }, { 78.0f, 0.0f, -3.0f } };
			for (int i = 0; i < 3; ++i) Check(glm::distance(glm::vec2(all.Who[i]->GetPosition().x, all.Who[i]->GetPosition().z), glm::vec2(end[i].x, end[i].z)) < 1.0f, "each ends where its tasks end");
			if (generated)
			{
				Check(all.Who[0]->Anim()->Overlay() == AnimInstance::kDefaultSet && all.Who[1]->Anim()->Overlay() == "alt" && all.Who[2]->Anim()->Overlay() == "slow", "the sets stayed apart");
				Check(!all.Who[0]->Anim()->Montages().IsPlaying("work") && !all.Who[2]->Anim()->Montages().IsPlaying("work"), "a montage of one is not in the others");
			}

			// each one alone does exactly what it did in company (lanes far apart: nobody is near anybody)
			for (int i = 0; i < 3; ++i)
			{
				Lanes solo;
				if (!BuildLanes(solo, model, i)) break;
				if (generated && i == 1) solo.Who[1]->PlayMontage("work");
				const Trace alone = RunLanes(solo, 60 * 20);
				Trace mine;
				Lane(together, 3, static_cast<size_t>(i), mine);
				Check(Same(alone, mine), "an actor moves the same alone and among others");
			}

			// the same run again, in a new world
			Lanes again;
			Check(BuildLanes(again, model, -1), "the world is built again");
			if (generated) again.Who[1]->PlayMontage("work");
			Check(Same(together, RunLanes(again, 60 * 20)), "two runs give the same positions to the last bit");
		}

		// ---- the source of orders changes: keys, tasks, AI, script, replay
		{
			ActorWorld w;
			w.SetHost(FlatHost());
			ActorSpawn s;
			s.Shared = model;
			Actor* a = w.Spawn("shifter", s, error);
			Check(a != nullptr, "an actor to hand around");
			if (a)
			{
				auto step = [&](int frames, const CharacterInput* key)
				{
					for (int i = 0; i < frames; ++i) { if (key) a->SetInput(*key); w.Update(kDt); }
				};
				CharacterInput forward;
				forward.Move = { 0.0f, 1.0f };
				forward.CameraYaw = 3.14159265f;
				Check(a->Source() == OrderSource::Direct, "it starts under the host's input");
				glm::vec3 p0 = a->GetPosition();
				step(60, &forward);
				const glm::vec3 p1 = a->GetPosition();
				Check(glm::distance(p0, p1) > 0.8f, "keys walk it");
				step(60, nullptr);
				Check(a->GetSpeed() < 0.05f, "and it stands when there is no input");

				Check(a->RunTasks("goto 20 0", error) && a->Source() == OrderSource::Tasks, "a task list takes over");
				step(60, &forward);   // the keys are ignored while the tasks run
				Check(a->TasksRunning() && a->Source() == OrderSource::Tasks, "it follows the tasks, not the keys");
				step(60 * 20, nullptr);
				Check(!a->TasksRunning() && std::fabs(a->GetPosition().x - 20.0f) < 1.0f, "it gets there");

				a->MoveTo({ 20.0f, 10.0f }, false);
				Check(a->Source() == OrderSource::AI, "an AI takes over");
				step(60 * 12, nullptr);
				Check(a->AIArrived() && std::fabs(a->GetPosition().z - 10.0f) < 1.0f, "and takes it to the point");

				const glm::vec3 beforeScript = a->GetPosition();
				a->SetViewYaw(3.14159265f);
				a->SetScript("W:1.5,:0.3");
				Check(a->Source() == OrderSource::Script && a->ScriptRunning(), "a script takes over");
				step(60, nullptr);
				Check(a->ScriptRunning() && glm::distance(beforeScript, a->GetPosition()) > 0.5f, "it plays the script");
				step(60, nullptr);
				Check(!a->ScriptRunning(), "the script ends");

				a->Record(true);
				a->ReleaseOrders();
				const glm::vec3 recStart = a->GetPosition();
				const float recYaw = a->GetYaw();
				step(40, &forward);
				CharacterInput side;
				side.Move = { 1.0f, 0.3f };
				side.Run = true;
				side.CameraYaw = 3.14159265f;
				step(60, &side);
				step(20, nullptr);
				a->Record(false);
				const glm::vec3 recEnd = a->GetPosition();
				Check(a->Source() == OrderSource::Direct && a->Recorded().size() == 120, "back under the keys, and the frames were recorded");

				// a recording plays back to the same place, with the same bits
				a->Teleport(recStart.x, recStart.z, recYaw);
				a->StartReplay(a->Recorded());
				Check(a->Source() == OrderSource::Replay, "a replay takes over");
				step(120, nullptr);
				Check(a->ReplayFinished(), "it plays all the frames");
				Check(glm::distance(a->GetPosition(), recEnd) < 0.05f, "and ends where the recording ended");
			}
		}

		// ---- the player becomes an NPC and an NPC becomes the player: one actor, whoever commands it
		{
			ActorWorld w;
			w.SetHost(FlatHost());
			ActorSpawn s;
			s.Shared = model;
			Actor* a = w.Spawn("twin", s, error);
			s.Position = { 10.0f, 0.0f };
			Actor* b = w.Spawn("other", s, error);
			if (a && b)
			{
				a->RunTasks("goto 0 10", error);
				for (int i = 0; i < 60 * 8; ++i) w.Update(kDt);
				const float aZ = a->GetPosition().z;
				a->ReleaseOrders();
				CharacterInput forward;
				forward.Move = { 0.0f, 1.0f };
				forward.CameraYaw = 3.14159265f;
				for (int i = 0; i < 60; ++i) { a->SetInput(forward); w.Update(kDt); }
				Check(a->GetPosition().z != aZ || a->GetPosition().x != 0.0f, "an actor that an AI walked answers the keys afterwards");
				Check(b->GetSpeed() < 0.05f, "while the other one stood");
			}
			else Check(false, "two actors for the hand over");
		}

		// ---- removing and creating again, with a fall in between; the scene and the abilities follow
		{
			EntityWorld scene;
			GameplayWorld gameplay;
			ActorWorld w;
			w.SetHost(FlatHost());
			w.Bind(&scene, &gameplay);
			ActorSpawn s;
			s.Shared = model;
			s.CreateEntity = true;
			Actor* a = w.Spawn("guest", s, error);
			Check(a != nullptr && scene.FindByName("guest") != NullEntity && gameplay.Find("guest") != nullptr, "an actor comes with its scene entity and its ability system");
			Check(!w.Spawn("guest", s, error) && !error.empty(), "a name cannot be used twice");
			if (a)
			{
				AbilitySystemComponent* asc = gameplay.Find("guest");
				Check(asc && asc->Runtime() != nullptr, "the character is lent to its abilities");
				for (int i = 0; i < 30; ++i) w.Update(kDt);
				Check(a->StartRagdoll(), "it can fall");
				for (int i = 0; i < 90; ++i) w.Update(kDt);
				Check(w.Remove("guest") && w.Count() == 0, "it is removed, ragdoll and all");
				Check(asc->Runtime() == nullptr, "and its abilities no longer point at it");
				Actor* again = w.Spawn("guest", s, error);
				Check(again != nullptr && w.Count() == 1, "the same name can be created again");
				if (again)
				{
					Check(gameplay.Find("guest")->Runtime() != nullptr, "with its abilities back");
					for (int i = 0; i < 60; ++i) w.Update(kDt);
					Check(std::isfinite(again->GetPosition().x), "and it works");
					Check(w.SyncEntities() == 1, "its position goes to its entity");
				}
			}
			w.Clear();
			Check(w.Count() == 0, "clear takes them all");
		}

		// ---- an actor does not go through another
		{
			ActorWorld w;
			w.SetHost(FlatHost());
			ActorSpawn s;
			s.Shared = model;
			s.Position = { -6.0f, 0.0f };
			Actor* a = w.Spawn("west", s, error);
			s.Position = { 6.0f, 0.0f };
			Actor* b = w.Spawn("east", s, error);
			Check(a && b, "two actors to walk into each other");
			if (a && b)
			{
				a->RunTasks("goto 6 0.3", error);
				b->RunTasks("goto -6 0.3", error);
				float closest = 1e9f;
				for (int i = 0; i < 60 * 10; ++i)
				{
					w.Update(kDt);
					closest = std::min(closest, glm::distance(glm::vec2(a->GetPosition().x, a->GetPosition().z), glm::vec2(b->GetPosition().x, b->GetPosition().z)));
				}
				const float need = a->Radius() + b->Radius();
				std::printf("   closest approach %.3f m (radii add up to %.3f m)\n", static_cast<double>(closest), static_cast<double>(need));
				Check(closest >= need - 0.05f, "they never get closer than their radii");
				Check(closest < need + 1.0f, "(and they did meet)");
			}
			// a new actor is not put on top of one that is there
			s.Position = { 0.0f, 0.0f };
			Actor* c = w.Spawn("c1", s, error);
			Actor* d = w.Spawn("c2", s, error);
			Check(c && d && glm::distance(glm::vec2(c->GetPosition().x, c->GetPosition().z), glm::vec2(d->GetPosition().x, d->GetPosition().z)) >= c->Radius() + d->Radius(), "a new actor starts clear of the others");
		}
	}

	// ---- H16: level of detail of the animation. The same actors with and without a viewer (far ones posed every few frames, no IK): positions,
	// montage events and footsteps must be the same frame by frame (only what is seen changes), and the far ones must skip poses.
	struct LodRun
	{
		std::vector<glm::vec3> Positions;
		std::vector<std::string> Events;   // "frame actor montage event" for every montage event
		int Full = 0, Reduced = 0, Far = 0, Posed = 0;
		double Ms = 0.0;
	};
	LodRun RunLod(const std::shared_ptr<GB::Model>& model, bool viewer, int count, int frames, bool record = true, const std::string& quality = "medium")
	{
		using namespace GB;
		LodRun out;
		ActorWorld w;
		w.SetHost(FlatHost());
		if (viewer) { w.SetViewer({ 0.0f, 0.0f, 0.0f }); w.SetAnimLod(AnimLodForQuality(quality)); }
		std::string error;
		for (int i = 0; i < count; ++i)
		{
			ActorSpawn s;
			s.Shared = model;
			// a row going away from the viewer: 8, 20 m (full), 32, 44 m (reduced), farther (far); in rows of 8 for a crowd
			const float x = 8.0f + 12.0f * static_cast<float>(i % 8), z = 4.0f * static_cast<float>(i / 8);
			s.Position = { x, z };
			Actor* a = w.Spawn("lod" + std::to_string(1000 + i), s, error);
			if (!a) { std::fprintf(stderr, "%s\n", error.c_str()); return out; }
			a->Anim()->Montages().Add(Work());
			a->RunTasks("goto " + std::to_string(x + 4.0f) + " " + std::to_string(z + 6.0f) + " run|wait 0.5|goto " + std::to_string(x) + " " + std::to_string(z - 3.0f), error);
		}
		const std::vector<std::string> names = w.Names();
		for (int f = 0; f < frames; ++f)
		{
			if (f == 40 || f == 200)
				for (const std::string& n : names)
				{
					MontagePlayer& m = w.Find(n)->Anim()->Montages();   // ("work" on the generated humanoid; the real character has no test_walk clip: its first montage)
					if (m.Has("work")) m.Play("work");
					else if (!m.Names().empty()) m.Play(m.Names().front());
				}
			w.Update(kDt);
			const ActorWorld::FrameCost& c = w.LastCost();
			out.Full += c.Full; out.Reduced += c.Reduced; out.Far += c.Far; out.Posed += c.Posed; out.Ms += c.Ms;
			if (!record) continue;
			for (const std::string& n : names)
			{
				Actor* a = w.Find(n);
				out.Positions.push_back(a->GetPosition());
				for (const MontageEvent& e : a->Anim()->Montages().Events())
					out.Events.push_back(std::to_string(f) + " " + n + " " + e.Montage + " " + std::to_string(static_cast<int>(e.What)));
			}
		}
		return out;
	}

	void LodChecks(const std::shared_ptr<GB::Model>& model, const char* label)
	{
		const int count = 6, frames = 480;
		const LodRun full = RunLod(model, false, count, frames), lod = RunLod(model, true, count, frames);
		const std::string tag = std::string(label) + ": animation LOD: ";
		Check(!full.Positions.empty() && full.Positions == lod.Positions, (tag + "every actor walks exactly the same path as at full detail").c_str());
		Check(!full.Events.empty() && full.Events == lod.Events, (tag + "the montage events come on the same frames").c_str());
		Check(lod.Full > 0 && lod.Reduced > 0 && lod.Far > 0, (tag + "near, middle and far actors").c_str());
		Check(full.Posed == count * frames && lod.Posed < full.Posed * 3 / 4, (tag + "the far actors are posed on fewer frames").c_str());
		std::printf("   %s: animation LOD over %d frames: %d full, %d reduced, %d far actor-frames; %d poses instead of %d\n", label, frames, lod.Full, lod.Reduced, lod.Far, lod.Posed, full.Posed);
	}
}

int main()
{
	using namespace GB;
	Log::Init();
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const std::string dir = GB_TEST_DATA_DIR;

	auto humanoid = TestHumanoid();
	if (!humanoid) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	Tests(humanoid, "generated humanoid", true);
	LodChecks(humanoid, "generated humanoid");

	// ---- models are loaded once however many actors use them
	{
		ActorWorld w;
		w.SetHost(FlatHost());
		w.SetAssetRoot(std::string(GB_TEST_DATA_DIR));
		std::string error;
		ActorSpawn s;
		s.Path = dir + "/test_humanoid.fbx";
		Actor* a = w.Spawn("one", s, error);
		Actor* b = w.Spawn("two", s, error);
		Check(a && b && a->GetModel() == b->GetModel() && w.CachedModels() == 1, "two actors from one file share the Model");
		if (a && b) Check(a->Instance() != b->Instance(), "and have their own ModelInstance");
		Check(a && a->Find<GeometryComponent>() && a->Find<RagdollComponent>() && a->Find<AnimationComponent>() && a->Find<PropsComponent>(), "an actor with a model has the geometry, ragdoll, animation and props components");
	}

	// ---- an actor needs no model: the same life cycle, orders, state and save, with nothing to draw
	{
		ActorWorld w;
		w.SetHost(FlatHost());
		std::string error;
		ActorSpawn s;
		s.Kind = "tribe-member";
		s.Position = { 2.0f, 2.0f };
		Check(w.Spawn("forgot", s, error) == nullptr && !error.empty(), "a spawn with no model and no path is an error unless it says so");
		s.NoModel = true;
		Actor* a = w.Spawn("abstract", s, error);
		Check(a != nullptr && a->Valid() && !a->HasPresentation(), "an actor spawned NoModel has no presentation");
		Check(a && a->Handle() != 0, "and the world gave it a handle");
		if (a)
		{
			Check(a->State() == ObjectState::Active && a->Kind() == "tribe-member" && a->Name() == "abstract", "it goes through the life cycle as any object");
			Check(a->Find<MovementComponent>() && a->Find<OrdersComponent>() && a->Find<AbilityComponent>() && a->Find<InventoryComponent>(), "it is made of the logic components");
			Check(!a->Find<GeometryComponent>() && !a->Find<AnimationComponent>() && !a->Find<PropsComponent>() && !a->Find<RagdollComponent>(), "and has none of the model's");
			const glm::vec3 from = a->GetPosition();
			a->MoveTo({ 8.0f, 2.0f }, true);
			for (int i = 0; i < 240; ++i) w.Update(kDt);
			Check(glm::distance(a->GetPosition(), from) > 2.0f, "its orders move it with nothing to animate");
			Check(a->Transform.Position == a->GetPosition(), "and its object transform follows the body");
			Check(a->Anim() == nullptr && a->Instance() == nullptr && !a->UsesLocomotionBlend() && a->PropNames().empty(), "the presentation queries answer empty");
			Check(!a->AttachProp("box") && !a->StartRagdoll(), "and the presentation actions do nothing");
			w.AddToScene(*std::make_unique<Scene>());   // nothing to draw: no crash
			w.SetSuspended("abstract", true);
			Check(a->State() == ObjectState::Paused && w.Suspended("abstract") && w.AnimLevel("abstract") == 3, "suspending pauses the object");
			w.SetSuspended("abstract", false);
			Check(a->State() == ObjectState::Active && !w.Suspended("abstract"), "and resuming activates it again");
			const auto saved = w.CaptureActors();
			Check(saved.size() == 1 && saved[0].Kind == "tribe-member", "it is saved with its kind");
		}
		Check(w.Remove("abstract") && w.Count() == 0, "and removed");
	}

	// ---- the real packs
	const char* mixamo = std::getenv("GB_MIXAMO_DIR");
	const char* injured = std::getenv("GB_INJURED_DIR");
	const char* farming = std::getenv("GB_FARMING_DIR");
	std::shared_ptr<Model> bot;
	if (mixamo && injured && farming && std::filesystem::exists(std::string(mixamo) + "/idle.fbx"))
	{
		bot = Model::Load(std::string(mixamo) + "/idle.fbx");   // (the character with its idle clip; the other clips are added below)
		if (bot)
		{
			for (const std::string& folder : { std::string(mixamo), std::string(injured), std::string(farming) })
			{
				std::vector<std::string> files;
				for (const auto& e : std::filesystem::directory_iterator(folder))
					if (e.path().extension() == ".fbx" && e.path().filename() != "X Bot.fbx" && e.path().filename() != "idle.fbx") files.push_back(e.path().generic_string());
				std::sort(files.begin(), files.end());
				for (const std::string& f : files) bot->AppendAnimations(f);
			}
			// three characters in the free, box and injured sets, doing different things at once
			ActorWorld w;
			w.SetHost(FlatHost());
			std::string error;
			const char* sets[3] = { "free", "box", "injured" };
			Actor* who[3] = {};
			bool built = true;
			for (int i = 0; i < 3; ++i)
			{
				ActorSpawn s;
				s.Shared = bot;
				s.Position = { 20.0f * static_cast<float>(i), 0.0f };
				who[i] = w.Spawn(std::string("pack") + static_cast<char>('0' + i), s, error);
				built = built && who[i] && who[i]->SetOverlay(sets[i], 0.0f);
			}
			Check(built, "real packs: three actors in the free, box and injured sets");
			if (built)
			{
				who[0]->RunTasks("goto 0 12 run|wait 0.5|goto 0 3", error);
				who[1]->RunTasks("goto 20 9|wait 1|goto 24 2", error);
				who[2]->RunTasks("goto 40 7|goto 38 -2", error);
				for (int i = 0; i < 60 * 25; ++i) w.Update(kDt);
				bool done = true;
				for (Actor* a : who) done = done && !a->TasksRunning() && !a->TasksFailed();
				Check(done, "real packs: they all finish");
				Check(who[0]->Anim()->Overlay() == AnimInstance::kDefaultSet && who[1]->Anim()->Overlay() == "box" && who[2]->Anim()->Overlay() == "injured", "real packs: each keeps its set");
			}
			std::puts("-- real packs: tests");
			Tests(bot, "real packs", false);
			LodChecks(bot, "real packs");
		}
	}
	else
		std::fprintf(stderr, "note: GB_MIXAMO_DIR / GB_INJURED_DIR / GB_FARMING_DIR not set, the real-pack checks were skipped\n");

	// ---- cost of an actor per frame (not asserted: the machine decides), on the real character when there is one
	if (std::getenv("GB_ACTOR_BENCH"))
	{
		std::shared_ptr<Model> model = bot ? bot : humanoid;
		std::printf("-- cost per actor (%s), Release, 60 Hz steps\n", bot ? "real packs" : "generated humanoid");
		for (int count : { 1, 8, 32, 128 })
		{
			ActorWorld w;
			w.SetHost(FlatHost());
			std::string error;
			for (int i = 0; i < count; ++i)
			{
				ActorSpawn s;
				s.Shared = model;
				s.Position = { 3.0f * static_cast<float>(i % 16), 3.0f * static_cast<float>(i / 16) };
				Actor* a = w.Spawn("a" + std::to_string(i), s, error);
				if (!a) { std::fprintf(stderr, "%s\n", error.c_str()); break; }
				a->RunTasks("goto " + std::to_string(3 * (i % 16) + 30) + " " + std::to_string(3 * (i / 16) + 10) + " run|goto 0 0", error);
				a->MeasureCost = false;
			}
			for (int i = 0; i < 120; ++i) w.Update(kDt);   // warm up
			for (const std::string& n : w.Names()) { w.Find(n)->MeasureCost = true; w.Find(n)->ResetCost(); }
			const int frames = 300;
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < frames; ++i) w.Update(kDt);
			const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			double control = 0.0, animation = 0.0;
			for (const std::string& n : w.Names()) { control += w.Find(n)->Cost().Control; animation += w.Find(n)->Cost().Animation; }
			const double perActorFrame = total / static_cast<double>(frames) / static_cast<double>(count);
			std::printf("   %3d actors: %.3f ms per world frame, %.1f us per actor-frame (control %.1f us, animation + IK %.1f us)\n", count, total / frames * 1000.0, perActorFrame * 1e6,
				control / frames / count * 1e6, animation / frames / count * 1e6);
		}
		// H16: the animation LOD (a crowd in rows going away from the viewer, 8 to 92 m; the median of 5 runs, the machine may be busy)
		for (int count : { 32, 128 })
		{
			std::printf("   %3d actors in rows from 8 to 92 m:", count);
			for (const char* quality : { "none", "ultra", "medium", "minimum" })
			{
				std::vector<double> ms;
				int posed = 0;
				for (int run = 0; run < 5; ++run)
				{
					const LodRun r = RunLod(model, std::strcmp(quality, "none") != 0, count, 300, false, quality);
					ms.push_back(r.Ms / 300.0);
					posed = r.Posed;
				}
				std::sort(ms.begin(), ms.end());
				std::printf(" %s %.2f ms (%.0f%% posed);", quality, ms[2], 100.0 * posed / (300.0 * count));
			}
			std::printf("\n");
		}
		// the same with a forest: 3000 obstacles (trees, rocks) that every actor checks and carries a copy of
		for (int count : { 32 })
		{
			ActorWorld w;
			ActorHost host = FlatHost();
			for (int i = 0; i < 3000; ++i) host.World.Obstacles.push_back({ { static_cast<float>((i * 37) % 200), static_cast<float>((i * 91) % 200) }, 0.3f });
			w.SetHost(host);
			std::string error;
			for (int i = 0; i < count; ++i)
			{
				ActorSpawn s;
				s.Shared = model;
				s.Position = { 3.0f * static_cast<float>(i % 16), 3.0f * static_cast<float>(i / 16) };
				Actor* a = w.Spawn("a" + std::to_string(i), s, error);
				if (!a) break;
				a->RunTasks("goto " + std::to_string(3 * (i % 16) + 30) + " " + std::to_string(3 * (i / 16) + 10) + " run|goto 0 0", error);
			}
			for (int i = 0; i < 120; ++i) w.Update(kDt);
			for (const std::string& n : w.Names()) { w.Find(n)->MeasureCost = true; w.Find(n)->ResetCost(); }
			const int frames = 300;
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < frames; ++i) w.Update(kDt);
			const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			double control = 0.0;
			for (const std::string& n : w.Names()) control += w.Find(n)->Cost().Control;
			std::printf("   %3d actors among 3000 obstacles: %.3f ms per world frame (control %.1f us per actor-frame)\n", count, total / frames * 1000.0, control / frames / count * 1e6);
		}
		// a fallen actor: the physics step of its ragdoll
		{
			ActorWorld w;
			w.SetHost(FlatHost());
			std::string error;
			ActorSpawn s;
			s.Shared = model;
			Actor* a = w.Spawn("fall", s, error);
			if (a && a->StartRagdoll())
			{
				a->AutoGetUp = false;
				for (int i = 0; i < 60; ++i) w.Update(kDt);
				a->MeasureCost = true;
				a->ResetCost();
				for (int i = 0; i < 120; ++i) w.Update(kDt);
				std::printf("   ragdoll: %.1f us per frame (physics step + pose)\n", a->Cost().Ragdoll / 120.0 * 1e6);
			}
		}
	}

	if (failures) return 1;
	std::puts("actor tests passed");
	return 0;
}
