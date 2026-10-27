// The animation state graph: conditions and their text, the JSON file, validation, a graph made from the locomotion sets, transitions by
// parameter (with a minimum time and priorities), a trigger that plays a montage and comes back when it ends, the motion's own parameters,
// and a runner that is off. The generated humanoid; CPU only.
#include "GB/Animation/AnimGraph.h"
#include "GB/Animation/AnimInstance.h"
#include "GB/Assets/Model.h"
#include "GB/Log.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	const float kDt = 1.0f / 60.0f;
}

int main()
{
	using namespace GB;
	Log::Init();
	auto model = Model::Load(std::string(GB_TEST_DATA_DIR) + "/test_humanoid.fbx");
	if (!model) { std::fprintf(stderr, "FAIL: test humanoid\n"); return 1; }
	model->AppendAnimations(std::string(GB_TEST_DATA_DIR) + "/test_idle.fbx");

	auto makeAnim = [&](ModelInstance& inst)
	{
		auto anim = std::make_unique<AnimInstance>(*model, inst);
		anim->Default().AddClip("test_humanoid");
		anim->AddSet({ "alt", { "test_idle", "test_humanoid" } });
		MontageDesc wave;
		wave.Name = "wave"; wave.Clip = "test_idle"; wave.BlendIn = 0.05f; wave.BlendOut = 0.05f; wave.PlayRate = 4.0f;   // 0.5 s
		anim->Montages().Add(wave);
		return anim;
	};
	auto run = [&](AnimInstance& a, const MotionState& s, float seconds) { for (float t = 0.0f; t < seconds; t += kDt) a.Update(s, kDt, glm::mat4(1.0f)); };
	MotionState still;

	// ---- conditions as text
	{
		GraphCondition c;
		Check(ParseCondition("Speed>2.5", c) && c.Param == "Speed" && c.What == GraphCondition::Op::Greater && std::fabs(c.Value - 2.5f) < 1e-6f, "a comparison is read from text");
		Check(ParseCondition(" Set == 2 ", c) && c.What == GraphCondition::Op::Equal && c.Value == 2.0f && c.Param == "Set", "with spaces around");
		Check(ParseCondition("Speed<=0.1", c) && c.What == GraphCondition::Op::LessEq && ParseCondition("Grounded!=1", c) && c.What == GraphCondition::Op::NotEqual, "all the operators");
		Check(ParseCondition("Pick", c) && c.Param == "Pick" && c.What == GraphCondition::Op::Equal && c.Value == 1.0f && DescribeCondition(c) == "Pick", "a bare name means it is set");
		Check(ParseCondition("!Pick", c) && c.Value == 0.0f && DescribeCondition(c) == "!Pick", "and a '!' that it is not");
		Check(!ParseCondition("", c) && !ParseCondition("Speed>", c) && !ParseCondition("Speed>fast", c), "nonsense is refused");
		Check(ParseCondition("Speed>=3", c) && DescribeCondition(c) == "Speed>=3", "a condition describes itself");
	}

	// ---- a graph, its file and its validation
	AnimGraph graph;
	graph.Params = { { "Do", GraphParam::Kind::Trigger, 0.0f }, { "Mood", GraphParam::Kind::Float, 0.5f } };
	graph.States = { { "loco", GraphState::Kind::Locomotion, "default", { 20.0f, 30.0f } }, { "fast", GraphState::Kind::Locomotion, "alt", { 200.0f, 30.0f } }, { "act", GraphState::Kind::Montage, "wave", { 100.0f, 120.0f } } };
	graph.Start = "loco";
	GraphTransition up;
	up.From = "loco"; up.To = "fast"; up.Blend = 0.2f; up.MinStateTime = 0.5f;
	up.Conditions = { { "Speed", GraphCondition::Op::Greater, 2.0f } };
	GraphTransition down;
	down.From = "fast"; down.To = "loco"; down.Conditions = { { "Speed", GraphCondition::Op::LessEq, 1.0f } };
	GraphTransition act;
	act.From = "*"; act.To = "act"; act.Priority = 5; act.Conditions = { { "Do", GraphCondition::Op::Equal, 1.0f } };
	GraphTransition back;
	back.From = "act"; back.To = "loco"; back.OnFinish = true;
	graph.Transitions = { up, down, act, back };
	{
		AnimGraph loaded;
		std::string error;
		const std::string text = AnimGraphToJson(graph);
		Check(AnimGraphFromJson(text, loaded, &error), "a graph round-trips through JSON");
		Check(loaded.States.size() == 3 && loaded.Transitions.size() == 4 && loaded.Params.size() == 2 && loaded.Start == "loco", "with its states, transitions and parameters");
		Check(loaded.States[1].Target == "alt" && loaded.States[2].What == GraphState::Kind::Montage && loaded.States[0].Position.y == 30.0f, "the targets, kinds and editor positions");
		Check(loaded.Transitions[0].MinStateTime == 0.5f && std::fabs(loaded.Transitions[0].Blend - 0.2f) < 1e-6f && loaded.Transitions[0].Conditions[0].What == GraphCondition::Op::Greater
			&& loaded.Transitions[2].Priority == 5 && loaded.Transitions[3].OnFinish && loaded.Params[0].What == GraphParam::Kind::Trigger, "and every setting of a transition");
		Check(!AnimGraphFromJson("{", loaded, &error) && !error.empty(), "bad text is refused");
		Check(!AnimGraphFromJson("{\"version\":1,\"states\":[{\"name\":\"a\",\"kind\":\"weird\"}]}", loaded, &error) && error.find("weird") != std::string::npos, "an unknown state kind is refused");
		Check(loaded.States.size() == 3, "and a failed load leaves the output alone");

		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		Check(graph.Validate(anim.get()).empty(), "a sound graph has no problems");
		AnimGraph bad = graph;
		bad.States.push_back({ "loco", GraphState::Kind::Locomotion, "default", {} });
		bad.States.push_back({ "lost", GraphState::Kind::Locomotion, "nope", {} });
		bad.States.push_back({ "ghost", GraphState::Kind::Montage, "nothing", {} });
		bad.Transitions.push_back({ "loco", "missing", {}, 0.3f, 0, false, 0.0f });
		bad.Transitions.push_back({ "nowhere", "loco", {}, 0.3f, 0, false, 0.0f });
		GraphTransition badParam;
		badParam.From = "loco"; badParam.To = "fast"; badParam.Conditions = { { "Weather", GraphCondition::Op::Equal, 1.0f } };
		bad.Transitions.push_back(badParam);
		const std::vector<std::string> problems = bad.Validate(anim.get());
		auto mentions = [&](const char* word) { for (const std::string& p : problems) if (p.find(word) != std::string::npos) return true; return false; };
		Check(mentions("two states are called 'loco'") && mentions("no locomotion set 'nope'") && mentions("no montage 'nothing'") && mentions("no state 'missing'") && mentions("no state 'nowhere'") && mentions("no parameter 'Weather'") && mentions("never entered"),
			"validation names every problem: duplicates, unknown sets, montages, states and parameters, states nothing leads to");
	}

	// ---- a graph from the locomotion sets, driven by the Set parameter
	{
		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		const AnimGraph made = MakeDefaultGraph(*anim);
		Check(made.States.size() == 2 && made.States[0].Name == "free" && made.States[1].Name == "alt" && made.Validate(anim.get()).empty(), "a default graph has a state for each set");
		anim->Graph().SetGraph(made);
		run(*anim, still, 0.2f);
		Check(anim->Graph().Current() == "free" && anim->Overlay() == AnimInstance::kDefaultSet, "it starts in the first one");
		anim->Graph().SetParam("Set", 1.0f);
		run(*anim, still, 0.1f);
		Check(anim->Graph().Current() == "alt" && anim->Overlay() == "alt" && anim->Graph().LastTransition() == "free -> alt", "choosing the second set switches the animation to it");
		anim->Graph().SetParam("Set", 0.0f);
		run(*anim, still, 0.1f);
		Check(anim->Graph().Current() == "free" && anim->Overlay() == AnimInstance::kDefaultSet && anim->Graph().TransitionCount() == 2, "and back");
	}

	// ---- transitions by the motion's own parameters, with a minimum time and no self transitions
	{
		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		anim->Graph().SetGraph(graph);
		MotionState quick, slow;
		quick.Speed = 3.0f; quick.LocalVelocity = { 0.0f, 3.0f }; quick.Velocity = { 0.0f, 0.0f, 3.0f };
		slow.Speed = 0.5f; slow.LocalVelocity = { 0.0f, 0.5f }; slow.Velocity = { 0.0f, 0.0f, 0.5f };
		run(*anim, quick, 0.3f);
		Check(anim->Graph().Current() == "loco", "the transition waits for its minimum time in the state");
		run(*anim, quick, 0.4f);
		Check(anim->Graph().Current() == "fast" && anim->Overlay() == "alt", "then speeding up changes state (Speed > 2)");
		run(*anim, slow, 0.1f);
		Check(anim->Graph().Current() == "loco" && anim->Overlay() == AnimInstance::kDefaultSet, "slowing down comes back");
		const int count = anim->Graph().TransitionCount();
		run(*anim, slow, 1.0f);
		Check(anim->Graph().TransitionCount() == count, "nothing flickers while the conditions do not change");
		// the airborne and grounded parameters come from the motion
		AnimGraph air = graph;
		air.Transitions.clear();
		GraphTransition fall;
		fall.From = "loco"; fall.To = "fast"; fall.Conditions = { { "Airborne", GraphCondition::Op::Equal, 1.0f } };
		air.Transitions.push_back(fall);
		anim->Graph().SetGraph(air);
		run(*anim, still, 0.1f);
		MotionState jumping; jumping.Grounded = false;
		run(*anim, jumping, 0.1f);
		Check(anim->Graph().Current() == "fast", "being in the air is a parameter of its own");
	}

	// ---- a trigger plays a montage and the graph comes back when it ends
	{
		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		anim->Graph().SetGraph(graph);
		run(*anim, still, 0.2f);
		anim->Graph().Trigger("Do");
		Check(anim->Graph().Param("Do") == 1.0f, "a trigger stays set until something reads it");
		run(*anim, still, 0.05f);
		Check(anim->Graph().Current() == "act" && anim->Montages().IsPlaying("wave") && anim->Graph().Param("Do") == 0.0f, "firing it enters the montage state, plays the montage and uses the trigger up");
		run(*anim, still, 0.2f);
		Check(anim->Graph().Current() == "act", "the state lasts while the action does");
		run(*anim, still, 0.6f);
		Check(anim->Graph().Current() == "loco" && !anim->Montages().IsPlaying("wave") && anim->Graph().LastTransition() == "act -> loco", "when the action ends it goes back");
		const int before = anim->Graph().TransitionCount();
		run(*anim, still, 0.5f);
		Check(anim->Graph().TransitionCount() == before, "and the trigger does not fire again");
	}

	// ---- priorities
	{
		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		AnimGraph g = graph;
		g.Transitions.clear();
		GraphTransition low, high;
		low.From = "loco"; low.To = "fast"; low.Priority = 0;
		high.From = "loco"; high.To = "act"; high.Priority = 3;
		g.Transitions = { low, high };   // both always hold: the one with the higher priority wins
		anim->Graph().SetGraph(g);
		run(*anim, still, 0.1f);
		Check(anim->Graph().Current() == "act", "of two transitions that hold, the higher priority goes first");
	}

	// ---- off, and no graph
	{
		ModelInstance inst(*model);
		auto anim = makeAnim(inst);
		run(*anim, still, 0.2f);
		Check(!anim->Graph().HasGraph() && anim->Overlay() == AnimInstance::kDefaultSet, "an instance without a graph is untouched");
		anim->Graph().SetGraph(graph);
		anim->Graph().Enabled = false;
		anim->Graph().Trigger("Do");
		run(*anim, still, 0.3f);
		Check(anim->Graph().Current().empty() && !anim->Montages().Busy(), "a runner that is off does nothing");
		anim->Graph().Enabled = true;
		run(*anim, still, 0.2f);
		Check(anim->Graph().Current() == "act", "and picks up where the parameters are when it is turned on");
	}

	if (failures) return 1;
	std::puts("anim graph tests passed");
	return 0;
}
