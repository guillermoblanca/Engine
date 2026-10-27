// Interactions: interaction points on entities (world space, scene files, duplicates), motion warping, the runner that takes a
// character to a point and plays the action (a player's and an NPC's character go through the same flow), and the NPC task lists
// (text parsing and running). The generated humanoid and a flat world; CPU only.
#include "GB/Animation/AnimInstance.h"
#include "GB/Game/Interaction.h"
#include "GB/Game/MotionWarp.h"
#include "GB/Game/SceneIO.h"
#include "GB/Game/TaskList.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps) { return std::fabs(a - b) < eps; }
	float AngleDiff(float a, float b)
	{
		float d = a - b;
		while (d > 3.14159265f) d -= 6.2831853f;
		while (d < -3.14159265f) d += 6.2831853f;
		return std::fabs(d);
	}
}

// One character as the game layer sees it: controller, motion tracker, animation.
struct Actor
{
	explicit Actor(GB::Model& model) : Instance(model), Anim(model, Instance)
	{
		Anim.Default().AddClip("test_humanoid");
		GB::MontageDesc work;
		work.Name = "work";
		work.Clip = "test_humanoid";
		work.BlendIn = 0.2f;
		work.BlendOut = 0.2f;
		work.Notifies = { { 0.5f, "touch" } };
		Anim.Montages().Add(work);
	}
	void Step(const GB::CharacterInput& in, const GB::CharacterWorld& world, float dt)
	{
		Controller.Step(in, world, dt);
		const glm::mat4 placement = glm::rotate(glm::translate(glm::mat4(1.0f), Controller.GetPosition()), Controller.GetYaw(), glm::vec3(0, 1, 0));
		Anim.Update(Tracker.Update(Controller, dt), dt, placement);
	}
	GB::ModelInstance Instance;
	GB::AnimInstance Anim;
	GB::CharacterController Controller;
	GB::MotionTracker Tracker;
};

int main()
{
	using namespace GB;
	Log::Init();
	const std::string dir = GB_TEST_DATA_DIR;
	auto model = Model::Load(dir + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	const float dt = 1.0f / 60.0f;
	const CharacterWorld flat;

	// ---- interaction points on entities
	EntityWorld world;
	const EntityId tree = world.Create("Apple Tree");
	world.Find(tree)->Transform.Position = { 10.0f, 0.0f, 5.0f };
	world.Find(tree)->Transform.Rotation = { 0.0f, 90.0f, 0.0f };
	InteractionPoint pick;
	pick.Name = "pick";
	pick.Position = { 0.0f, 0.0f, -1.5f };   // 1.5 m behind the tree's own +Z
	pick.Yaw = 0.0f;
	pick.Montage = "work";
	pick.Gives = "fruit";
	world.Find(tree)->Interact.emplace();
	world.Find(tree)->Interact->Points.push_back(pick);
	InteractionPoint shake = pick;
	shake.Name = "shake";
	shake.Montage = "";
	world.Find(tree)->Interact->Points.push_back(shake);
	const EntityId cow = world.Create("Cow");
	InteractionPoint milk;
	milk.Name = "milk";
	milk.Position = { 0.0f, 0.0f, 1.0f };
	milk.Requires = "bucket";
	world.Find(cow)->Interact.emplace();
	world.Find(cow)->Interact->Points.push_back(milk);

	InteractionTarget target;
	std::string error;
	Check(FindInteraction(world, "Apple Tree", "pick", target, &error) && target.Point == "pick", "an interaction point is found by entity and point name");
	Check(Near(target.Position.x, 10.0f - 1.5f, 1e-4f) && Near(target.Position.y, 5.0f, 1e-4f), "its spot is in world space (the tree turned 90 degrees)");
	Check(Near(target.Yaw, 3.14159265f / 2.0f, 1e-4f), "and so is its facing");
	Check(target.Montage == "work" && target.Gives == "fruit", "with its action and what it gives");
	Check(FindInteraction(world, "Apple Tree", "", target) && target.Point == "pick", "an empty point name is the first one");
	Check(!FindInteraction(world, "Nothing", "", target, &error) && error.find("Nothing") != std::string::npos, "an unknown entity says so");
	Check(!FindInteraction(world, "Apple Tree", "dig", target, &error) && error.find("dig") != std::string::npos, "an unknown point says so");
	FindInteraction(world, "Apple Tree", "pick", target);
	const auto listed = ListInteractions(world);
	Check(listed.size() == 3 && listed[0] == "Apple Tree/pick", "the interactions of a scene are listed");

	// scene files and copies keep the points
	{
		EntityWorld loaded;
		Check(LoadSceneFromString(loaded, SaveSceneToString(world)).Ok, "the scene with interaction points loads");
		const Entity* t = loaded.Find(loaded.FindByName("Apple Tree"));
		const Entity* c = loaded.Find(loaded.FindByName("Cow"));
		Check(t && t->Interact && t->Interact->Points.size() == 2 && t->Interact->Points[0].Montage == "work" && t->Interact->Points[0].Gives == "fruit" && Near(t->Interact->Points[0].Position.z, -1.5f, 1e-4f), "points survive a save and load");
		Check(c && c->Interact && c->Interact->Points[0].Requires == "bucket", "including what they require");
		const EntityId copy = loaded.Duplicate(loaded.FindByName("Apple Tree"));
		Check(loaded.Find(copy) && loaded.Find(copy)->Interact && loaded.Find(copy)->Interact->Points.size() == 2, "and a duplicate");
	}

	// ---- motion warp
	{
		MotionWarp warp;
		glm::vec2 xz(0.0f);
		float yaw = 0.0f;
		warp.Start({ 0, 0 }, glm::radians(170.0f), { 2, 0 }, glm::radians(-170.0f), 0.5f);
		float lastX = -1.0f, worstYaw = 0.0f;
		bool monotonic = true;
		for (int i = 0; i < 40; ++i)
		{
			warp.Update(dt, xz, yaw);
			monotonic = monotonic && xz.x >= lastX;
			lastX = xz.x;
			worstYaw = std::max(worstYaw, AngleDiff(yaw, glm::radians(170.0f)));
		}
		Check(!warp.Active() && Near(xz.x, 2.0f, 1e-4f) && AngleDiff(yaw, glm::radians(-170.0f)) < 1e-4f, "the warp ends exactly on the spot and the facing");
		Check(monotonic && worstYaw < glm::radians(21.0f), "it moves forward only and turns the short way (20 degrees, not 340)");
	}

	// ---- an actor goes to the tree and picks
	{
		Actor a(*model);
		a.Controller.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		FindInteraction(world, "Apple Tree", "pick", target);
		InteractionRunner run;
		const bool started = run.Start(target, &a.Anim);
		if (!started) std::fprintf(stderr, "start error: %s\n", run.Error().c_str());
		Check(started && run.GetPhase() == InteractionRunner::Phase::Approach, "the runner starts walking to the point");
		bool sawAlign = false, sawAct = false, sawNotify = false, sawGives = false;
		int dones = 0, notifies = 0;
		float worstStep = 0.0f;
		glm::vec3 last = a.Controller.GetPosition();
		for (int i = 0; i < 60 * 30 && run.Active(); ++i)
		{
			const CharacterInput in = run.Update(a.Controller, flat, &a.Anim, dt);
			a.Step(in, flat, dt);
			sawAlign = sawAlign || run.GetPhase() == InteractionRunner::Phase::Align;
			sawAct = sawAct || run.GetPhase() == InteractionRunner::Phase::Act;
			for (const std::string& e : run.Events())
			{
				if (e == "notify:touch") { sawNotify = true; ++notifies; }
				if (e == "gives:fruit") sawGives = true;
				if (e == "done") ++dones;
			}
			worstStep = std::max(worstStep, glm::distance(a.Controller.GetPosition(), last));
			last = a.Controller.GetPosition();
		}
		Check(run.GetPhase() == InteractionRunner::Phase::Done && dones == 1, "the action finishes once");
		Check(sawAlign && sawAct, "it passed through the align and act phases");
		Check(sawNotify && notifies == 1, "the montage's notify arrives once");
		Check(sawGives, "and what the point gives");
		Check(glm::distance(glm::vec2(a.Controller.GetPosition().x, a.Controller.GetPosition().z), target.Position) < 1e-3f && AngleDiff(a.Controller.GetYaw(), target.Yaw) < 0.01f, "the character ends exactly on the spot, facing the tree");
		Check(worstStep < 0.12f, "the warp does not teleport (it settles over a short time)");
		Check(!a.Anim.Montages().Busy(), "the montage is over");
	}

	// ---- a player's character and an NPC's: the same flow, the same result
	{
		Actor player(*model), npc(*model);
		player.Controller.SetPosition(flat, -4.0f, 8.0f, 2.0f);
		npc.Controller.SetPosition(flat, 20.0f, -3.0f, -1.0f);
		InteractionRunner a, b;
		a.Start(target, &player.Anim);
		b.Start(target, &npc.Anim, true);   // the NPC is in a hurry
		for (int i = 0; i < 60 * 40 && (a.Active() || b.Active()); ++i)
		{
			player.Step(a.Update(player.Controller, flat, &player.Anim, dt), flat, dt);
			npc.Step(b.Update(npc.Controller, flat, &npc.Anim, dt), flat, dt);
		}
		const auto spot = [](const CharacterController& c) { return glm::vec2(c.GetPosition().x, c.GetPosition().z); };
		Check(a.GetPhase() == InteractionRunner::Phase::Done && b.GetPhase() == InteractionRunner::Phase::Done, "both finish");
		Check(glm::distance(spot(player.Controller), spot(npc.Controller)) < 1e-3f && AngleDiff(player.Controller.GetYaw(), npc.Controller.GetYaw()) < 0.01f, "both end in exactly the same place and facing");
	}

	// ---- requirements and failures
	{
		Actor a(*model);
		a.Controller.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		InteractionTarget milking;
		FindInteraction(world, "Cow", "milk", milking);
		InteractionRunner run;
		Check(!run.Start(milking, &a.Anim) && run.GetPhase() == InteractionRunner::Phase::Failed && run.Error().find("bucket") != std::string::npos, "without the required prop it does not start, and says what is missing");
		Prop bucket;
		bucket.Name = "bucket";
		bucket.Socket = "hand_r";
		a.Anim.Attach(bucket);
		Check(run.Start(milking, &a.Anim), "carrying it, it starts");
		run.Cancel();
		Check(run.GetPhase() == InteractionRunner::Phase::Idle && !run.Active(), "an interaction can be cancelled");

		InteractionTarget missing = target;
		missing.Montage = "no such action";
		Check(!run.Start(missing, &a.Anim) && run.Error().find("no such action") != std::string::npos, "a montage the character lacks is reported");
		// without an animation it still goes there and waits
		InteractionTarget plain;
		FindInteraction(world, "Apple Tree", "shake", plain);
		Actor b(*model);
		b.Controller.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		InteractionRunner quiet;
		quiet.ActSeconds = 0.3f;
		quiet.Start(plain, nullptr);
		for (int i = 0; i < 60 * 30 && quiet.Active(); ++i) b.Step(quiet.Update(b.Controller, flat, nullptr, dt), flat, dt);
		Check(quiet.GetPhase() == InteractionRunner::Phase::Done, "a point without a montage (and a character without animation) is just a visit");
	}

	// ---- task lists
	{
		std::vector<Task> tasks;
		std::string err;
		Check(ParseTasks("# a day\ngoto 3 0 run; interact Apple_Tree pick; wait 0.5 # rest\nattach box;detach", tasks, &err) && tasks.size() == 5, "a task list parses (';' or lines, comments)");
		Check(tasks.size() == 5 && tasks[0].What == Task::Kind::GoTo && tasks[0].Run && tasks[1].Entity == "Apple Tree" && tasks[1].Point == "pick" && tasks[3].Prop == "box" && tasks[4].Prop.empty(), "with their arguments (underscores are spaces in names)");
		Check(!ParseTasks("goto 3 0; fly away", tasks, &err) && err.find("fly") != std::string::npos && tasks.size() == 1, "an unknown task is reported with its number, keeping the ones before it");
		Check(!ParseTasks("goto 3", tasks, &err) && !ParseTasks("wait soon", tasks, &err), "bad arguments are refused");

		const TaskRunner::Resolver resolver = [&](const std::string& e, const std::string& p, InteractionTarget& out, std::string* er) { return FindInteraction(world, e, p, out, er); };
		std::vector<std::string> props;
		const TaskRunner::PropHandler handler = [&](const std::string& p, bool attach) { props.push_back((attach ? "+" : "-") + p); return true; };

		Actor npc(*model);
		npc.Controller.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		TaskRunner runner;
		ParseTasks("goto 3 1; interact Apple_Tree pick; wait 0.5; detach fruit", tasks, &err);
		runner.Set(tasks);
		Check(runner.Running() && runner.Describe().find("1/4 goto") == 0, "the runner describes the current task");
		bool sawInteract = false;
		for (int i = 0; i < 60 * 60 && !runner.Done(); ++i)
		{
			npc.Step(runner.Update(npc.Controller, flat, &npc.Anim, dt, resolver, handler), flat, dt);
			sawInteract = sawInteract || runner.Describe().find("interact") != std::string::npos;
		}
		Check(runner.Done() && !runner.Failed() && runner.Describe() == "done", "the NPC does its whole list");
		Check(sawInteract && props.size() == 2 && props[0] == "+fruit" && props[1] == "-fruit", "the fruit it was given is picked up and then let go");
		Check(glm::distance(glm::vec2(npc.Controller.GetPosition().x, npc.Controller.GetPosition().z), target.Position) < 0.05f, "and it stands where it did the work");

		Actor other(*model);
		other.Controller.SetPosition(flat, 0.0f, 0.0f, 0.0f);
		ParseTasks("interact Ghost", tasks, &err);
		runner.Set(tasks);
		other.Step(runner.Update(other.Controller, flat, &other.Anim, dt, resolver, handler), flat, dt);
		Check(runner.Failed() && runner.Done() && runner.Error().find("Ghost") != std::string::npos, "an impossible task fails with the reason");
		ParseTasks("detach", tasks, &err);
		runner.Set(tasks);
		const TaskRunner::PropHandler refuse = [](const std::string&, bool) { return false; };
		other.Step(runner.Update(other.Controller, flat, &other.Anim, dt, resolver, refuse), flat, dt);
		Check(runner.Failed(), "a prop the host cannot handle fails the list");
	}

	if (failures) return 1;
	std::puts("interaction tests passed");
	return 0;
}
