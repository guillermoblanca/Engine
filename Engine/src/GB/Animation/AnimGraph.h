#pragma once
#include "GB/Game/MotionState.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	class AnimInstance;

	// A value the graph's conditions read. The motion gives some by itself (Speed, Forward, Strafe, YawRate, Grounded, Airborne, Wading, WadeDepth,
	// Blocked, Landed, LandingSpeed, and Finished while a montage state's action has ended); the game sets the rest.
	struct GraphParam
	{
		enum class Kind { Float, Bool, Trigger };   // a trigger stays set until a transition that reads it fires
		std::string Name;
		Kind What = Kind::Float;
		float Value = 0.0f;
	};

	struct GraphCondition
	{
		enum class Op { Greater, Less, GreaterEq, LessEq, Equal, NotEqual };
		std::string Param;
		Op What = Op::Greater;
		float Value = 0.0f;       // a bool or a trigger is read as 1 = set: "Pick" means "Pick == 1"
	};

	// A state of the animation of a character: the locomotion set it moves with (free, carrying a box, injured...) or a montage it plays.
	struct GraphState
	{
		enum class Kind { Locomotion, Montage };
		std::string Name;
		Kind What = Kind::Locomotion;
		std::string Target;        // the set or the montage
		glm::vec2 Position{ 0.0f };   // where the editor draws it
	};

	struct GraphTransition
	{
		std::string From;          // a state name, or "*" for any state
		std::string To;
		std::vector<GraphCondition> Conditions;   // all of them must hold
		float Blend = 0.3f;        // seconds the locomotion sets take to cross-fade
		int Priority = 0;          // the higher one is looked at first
		bool OnFinish = false;     // only once the montage of the state it leaves has ended
		float MinStateTime = 0.0f; // and not before the state has lasted this long
	};

	// The graph itself: pure data. GraphRunner runs it on an AnimInstance.
	struct AnimGraph
	{
		std::vector<GraphParam> Params;
		std::vector<GraphState> States;
		std::vector<GraphTransition> Transitions;
		std::string Start;         // the state it begins in (the first one when empty)

		int FindState(const std::string& name) const;
		int FindParam(const std::string& name) const;
		// Problems that would stop it from working (an unknown state, set, montage or parameter, a state nothing leads to). `anim` is optional:
		// with it the sets and montages are checked as well.
		std::vector<std::string> Validate(const AnimInstance* anim = nullptr) const;
	};

	// A graph with a state for each locomotion set of the character, and the `Set` parameter (0 = the default set, 1 = the next one...) choosing
	// among them: a starting point to edit.
	AnimGraph MakeDefaultGraph(const AnimInstance& anim);

	std::string AnimGraphToJson(const AnimGraph& graph);
	bool AnimGraphFromJson(const std::string& text, AnimGraph& out, std::string* error = nullptr);

	// "Speed>2", "Grounded==1", "Pick" (a bool or trigger that is set): the text form of a condition; false when it cannot be read.
	bool ParseCondition(const std::string& text, GraphCondition& out);
	std::string DescribeCondition(const GraphCondition& condition);

	class GraphRunner
	{
	public:
		bool Enabled = true;

		void SetGraph(const AnimGraph& graph);
		AnimGraph& Graph() { return m_Graph; }
		const AnimGraph& Graph() const { return m_Graph; }
		bool HasGraph() const { return !m_Graph.States.empty(); }
		void Restart();                          // back to the start state

		void SetParam(const std::string& name, float value);   // creates a float parameter when there is none
		void Trigger(const std::string& name);                 // sets a trigger (creates it when there is none)
		float Param(const std::string& name) const;

		// Once per frame, before the animation updates: reads the motion, follows the first transition whose conditions hold and applies the
		// state it enters (a locomotion set with a cross-fade, or a montage).
		void Update(const MotionState& state, float dt, AnimInstance& anim);

		const std::string& Current() const { return m_Current; }
		float StateTime() const { return m_Time; }
		const std::string& LastTransition() const { return m_Last; }   // "From -> To" of the latest one
		int TransitionCount() const { return m_Count; }

	private:
		void Enter(const std::string& state, float blend, AnimInstance& anim);
		float Read(const std::string& name) const;
		void Consume(const GraphTransition& t);

		AnimGraph m_Graph;
		std::string m_Current, m_Last;
		float m_Time = 0.0f;
		int m_Count = 0;
		bool m_Started = false;
		std::vector<GraphParam> m_Auto;   // the parameters the motion gives, rebuilt every frame
	};
}
