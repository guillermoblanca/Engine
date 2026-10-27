#include "GB/Animation/AnimGraph.h"
#include "GB/Animation/AnimInstance.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace GB
{
	namespace
	{
		const char* const kAutoParams[] = { "Speed", "Forward", "Strafe", "YawRate", "Grounded", "Airborne", "Wading", "WadeDepth", "Blocked", "Landed", "LandingSpeed", "Finished" };

		bool IsAuto(const std::string& name)
		{
			for (const char* p : kAutoParams) if (name == p) return true;
			return false;
		}

		const char* OpText(GraphCondition::Op op)
		{
			switch (op)
			{
			case GraphCondition::Op::Greater: return ">";
			case GraphCondition::Op::Less: return "<";
			case GraphCondition::Op::GreaterEq: return ">=";
			case GraphCondition::Op::LessEq: return "<=";
			case GraphCondition::Op::Equal: return "==";
			case GraphCondition::Op::NotEqual: return "!=";
			}
			return "==";
		}

		std::string Trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
			while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
			return s.substr(a, b - a);
		}

		bool Holds(const GraphCondition& c, float v)
		{
			const float e = 1e-4f;
			switch (c.What)
			{
			case GraphCondition::Op::Greater: return v > c.Value;
			case GraphCondition::Op::Less: return v < c.Value;
			case GraphCondition::Op::GreaterEq: return v >= c.Value - e;
			case GraphCondition::Op::LessEq: return v <= c.Value + e;
			case GraphCondition::Op::Equal: return std::fabs(v - c.Value) < e;
			case GraphCondition::Op::NotEqual: return std::fabs(v - c.Value) >= e;
			}
			return false;
		}
	}

	int AnimGraph::FindState(const std::string& name) const
	{
		for (size_t i = 0; i < States.size(); ++i) if (States[i].Name == name) return static_cast<int>(i);
		return -1;
	}

	int AnimGraph::FindParam(const std::string& name) const
	{
		for (size_t i = 0; i < Params.size(); ++i) if (Params[i].Name == name) return static_cast<int>(i);
		return -1;
	}

	std::vector<std::string> AnimGraph::Validate(const AnimInstance* anim) const
	{
		std::vector<std::string> problems;
		if (States.empty()) problems.push_back("the graph has no states");
		for (size_t i = 0; i < States.size(); ++i)
		{
			const GraphState& s = States[i];
			if (s.Name.empty()) problems.push_back("a state has no name");
			for (size_t j = 0; j < i; ++j)
				if (States[j].Name == s.Name) problems.push_back("two states are called '" + s.Name + "'");
			if (anim)
			{
				if (s.What == GraphState::Kind::Locomotion && !anim->HasSet(s.Target.empty() ? std::string(AnimInstance::kDefaultSet) : s.Target)) problems.push_back("state '" + s.Name + "': the character has no locomotion set '" + s.Target + "'");
				if (s.What == GraphState::Kind::Montage && !anim->Montages().Has(s.Target)) problems.push_back("state '" + s.Name + "': the character has no montage '" + s.Target + "'");
			}
			else if (s.What == GraphState::Kind::Montage && s.Target.empty()) problems.push_back("state '" + s.Name + "' plays no montage");
		}
		if (!Start.empty() && FindState(Start) < 0) problems.push_back("the start state '" + Start + "' does not exist");
		for (size_t i = 0; i < Transitions.size(); ++i)
		{
			const GraphTransition& t = Transitions[i];
			const std::string label = "transition " + std::to_string(i + 1) + " (" + t.From + " -> " + t.To + ")";
			if (t.From != "*" && !t.From.empty() && FindState(t.From) < 0) problems.push_back(label + ": no state '" + t.From + "'");
			if (FindState(t.To) < 0) problems.push_back(label + ": no state '" + t.To + "'");
			for (const GraphCondition& c : t.Conditions)
				if (!IsAuto(c.Param) && FindParam(c.Param) < 0) problems.push_back(label + ": no parameter '" + c.Param + "'");
		}
		for (size_t i = 0; i < States.size(); ++i)
		{
			const std::string& n = States[i].Name;
			if (n == (Start.empty() && !States.empty() ? States[0].Name : Start)) continue;
			const bool entered = std::any_of(Transitions.begin(), Transitions.end(), [&](const GraphTransition& t) { return t.To == n; });
			if (!entered) problems.push_back("state '" + n + "' is never entered: no transition leads to it");
		}
		return problems;
	}

	AnimGraph MakeDefaultGraph(const AnimInstance& anim)
	{
		AnimGraph g;
		g.Params.push_back({ "Set", GraphParam::Kind::Float, 0.0f });
		const std::vector<std::string> sets = anim.SetNames();
		for (size_t i = 0; i < sets.size(); ++i)
		{
			GraphState s;
			s.Name = sets[i] == AnimInstance::kDefaultSet ? "free" : sets[i];
			s.Target = sets[i];
			s.Position = { 40.0f + 170.0f * static_cast<float>(i % 3), 40.0f + 90.0f * static_cast<float>(i / 3) };
			g.States.push_back(s);
		}
		for (size_t i = 0; i < g.States.size(); ++i)
		{
			GraphTransition t;
			t.From = "*";
			t.To = g.States[i].Name;
			t.Conditions.push_back({ "Set", GraphCondition::Op::Equal, static_cast<float>(i) });
			g.Transitions.push_back(t);
		}
		if (!g.States.empty()) g.Start = g.States[0].Name;
		return g;
	}

	bool ParseCondition(const std::string& textIn, GraphCondition& out)
	{
		const std::string text = Trim(textIn);
		if (text.empty()) return false;
		static const struct { const char* Text; GraphCondition::Op Op; } ops[] = {
			{ ">=", GraphCondition::Op::GreaterEq }, { "<=", GraphCondition::Op::LessEq }, { "==", GraphCondition::Op::Equal }, { "!=", GraphCondition::Op::NotEqual },
			{ ">", GraphCondition::Op::Greater }, { "<", GraphCondition::Op::Less } };
		for (const auto& o : ops)
		{
			const size_t at = text.find(o.Text);
			if (at == std::string::npos) continue;
			const std::string param = Trim(text.substr(0, at)), value = Trim(text.substr(at + std::string(o.Text).size()));
			if (param.empty() || value.empty()) return false;
			char* end = nullptr;
			const float v = std::strtof(value.c_str(), &end);
			if (!end || *end != '\0') return false;
			out = { param, o.Op, v };
			return true;
		}
		if (text[0] == '!' && text.size() > 1) { out = { Trim(text.substr(1)), GraphCondition::Op::Equal, 0.0f }; return true; }
		out = { text, GraphCondition::Op::Equal, 1.0f };
		return true;
	}

	std::string DescribeCondition(const GraphCondition& c)
	{
		char b[96];
		if (c.What == GraphCondition::Op::Equal && c.Value == 1.0f) return c.Param;
		if (c.What == GraphCondition::Op::Equal && c.Value == 0.0f) return "!" + c.Param;
		std::snprintf(b, sizeof b, "%s%s%g", c.Param.c_str(), OpText(c.What), static_cast<double>(c.Value));
		return b;
	}

	std::string AnimGraphToJson(const AnimGraph& g)
	{
		using Json = nlohmann::json;
		Json root;
		root["version"] = 1;
		root["start"] = g.Start;
		Json params = Json::array(), states = Json::array(), transitions = Json::array();
		for (const GraphParam& p : g.Params)
			params.push_back({ { "name", p.Name }, { "kind", p.What == GraphParam::Kind::Float ? "float" : p.What == GraphParam::Kind::Bool ? "bool" : "trigger" }, { "value", p.Value } });
		for (const GraphState& s : g.States)
			states.push_back({ { "name", s.Name }, { "kind", s.What == GraphState::Kind::Locomotion ? "locomotion" : "montage" }, { "target", s.Target }, { "x", s.Position.x }, { "y", s.Position.y } });
		for (const GraphTransition& t : g.Transitions)
		{
			Json conditions = Json::array();
			for (const GraphCondition& c : t.Conditions) conditions.push_back({ { "param", c.Param }, { "op", OpText(c.What) }, { "value", c.Value } });
			Json j{ { "from", t.From }, { "to", t.To }, { "blend", t.Blend }, { "priority", t.Priority }, { "conditions", conditions } };
			if (t.OnFinish) j["onFinish"] = true;
			if (t.MinStateTime > 0.0f) j["minStateTime"] = t.MinStateTime;
			transitions.push_back(std::move(j));
		}
		root["params"] = params;
		root["states"] = states;
		root["transitions"] = transitions;
		return root.dump(2) + "\n";
	}

	bool AnimGraphFromJson(const std::string& text, AnimGraph& out, std::string* error)
	{
		using Json = nlohmann::json;
		auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return fail("invalid JSON");
		if (root.value("version", 0) != 1) return fail("unsupported graph file version");
		AnimGraph g;
		try
		{
			g.Start = root.value("start", "");
			if (root.contains("params"))
				for (const Json& j : root["params"])
				{
					GraphParam p;
					p.Name = j.value("name", "");
					if (p.Name.empty()) return fail("a parameter needs a name");
					const std::string kind = j.value("kind", "float");
					p.What = kind == "bool" ? GraphParam::Kind::Bool : kind == "trigger" ? GraphParam::Kind::Trigger : GraphParam::Kind::Float;
					p.Value = j.value("value", 0.0f);
					g.Params.push_back(p);
				}
			if (root.contains("states"))
				for (const Json& j : root["states"])
				{
					GraphState s;
					s.Name = j.value("name", "");
					if (s.Name.empty()) return fail("a state needs a name");
					const std::string kind = j.value("kind", "locomotion");
					if (kind != "locomotion" && kind != "montage") return fail("state '" + s.Name + "': unknown kind '" + kind + "'");
					s.What = kind == "montage" ? GraphState::Kind::Montage : GraphState::Kind::Locomotion;
					s.Target = j.value("target", "");
					s.Position = { j.value("x", 0.0f), j.value("y", 0.0f) };
					g.States.push_back(s);
				}
			if (root.contains("transitions"))
				for (const Json& j : root["transitions"])
				{
					GraphTransition t;
					t.From = j.value("from", "*");
					t.To = j.value("to", "");
					if (t.To.empty()) return fail("a transition needs a destination");
					t.Blend = j.value("blend", 0.3f);
					t.Priority = j.value("priority", 0);
					t.OnFinish = j.value("onFinish", false);
					t.MinStateTime = j.value("minStateTime", 0.0f);
					if (j.contains("conditions"))
						for (const Json& c : j["conditions"])
						{
							GraphCondition cond;
							cond.Param = c.value("param", "");
							const std::string op = c.value("op", "==");
							GraphCondition parsed;
							if (cond.Param.empty() || !ParseCondition(cond.Param + op + "0", parsed)) return fail("a condition needs a parameter and an operator (> < >= <= == !=)");
							cond.What = parsed.What;
							cond.Value = c.value("value", 0.0f);
							t.Conditions.push_back(cond);
						}
					g.Transitions.push_back(t);
				}
		}
		catch (const Json::exception& ex)
		{
			return fail(std::string("bad value: ") + ex.what());
		}
		out = std::move(g);
		return true;
	}

	void GraphRunner::SetGraph(const AnimGraph& graph)
	{
		m_Graph = graph;
		Restart();
	}

	void GraphRunner::Restart()
	{
		m_Started = false;
		m_Current.clear();
		m_Time = 0.0f;
	}

	void GraphRunner::SetParam(const std::string& name, float value)
	{
		const int i = m_Graph.FindParam(name);
		if (i >= 0) { m_Graph.Params[static_cast<size_t>(i)].Value = value; return; }
		m_Graph.Params.push_back({ name, GraphParam::Kind::Float, value });
	}

	void GraphRunner::Trigger(const std::string& name)
	{
		const int i = m_Graph.FindParam(name);
		if (i >= 0) { m_Graph.Params[static_cast<size_t>(i)].Value = 1.0f; return; }
		m_Graph.Params.push_back({ name, GraphParam::Kind::Trigger, 1.0f });
	}

	float GraphRunner::Param(const std::string& name) const { return Read(name); }

	float GraphRunner::Read(const std::string& name) const
	{
		for (const GraphParam& p : m_Auto) if (p.Name == name) return p.Value;
		const int i = m_Graph.FindParam(name);
		return i >= 0 ? m_Graph.Params[static_cast<size_t>(i)].Value : 0.0f;
	}

	void GraphRunner::Consume(const GraphTransition& t)
	{
		for (const GraphCondition& c : t.Conditions)
		{
			const int i = m_Graph.FindParam(c.Param);
			if (i >= 0 && m_Graph.Params[static_cast<size_t>(i)].What == GraphParam::Kind::Trigger) m_Graph.Params[static_cast<size_t>(i)].Value = 0.0f;
		}
	}

	void GraphRunner::Enter(const std::string& name, float blend, AnimInstance& anim)
	{
		const int i = m_Graph.FindState(name);
		if (i < 0) return;
		const GraphState& s = m_Graph.States[static_cast<size_t>(i)];
		m_Current = s.Name;
		m_Time = 0.0f;
		if (s.What == GraphState::Kind::Locomotion) anim.SetOverlay(s.Target.empty() ? std::string(AnimInstance::kDefaultSet) : s.Target, blend);
		else anim.Montages().Play(s.Target);
	}

	void GraphRunner::Update(const MotionState& state, float dt, AnimInstance& anim)
	{
		if (!Enabled || !HasGraph()) return;
		if (!m_Started)
		{
			m_Started = true;
			Enter(m_Graph.Start.empty() ? m_Graph.States[0].Name : m_Graph.Start, 0.0f, anim);
		}
		m_Time += dt;
		const int cur = m_Graph.FindState(m_Current);
		const bool finished = cur >= 0 && m_Graph.States[static_cast<size_t>(cur)].What == GraphState::Kind::Montage && m_Time > 0.05f && !anim.Montages().IsPlaying(m_Graph.States[static_cast<size_t>(cur)].Target);
		m_Auto.clear();
		const auto put = [&](const char* name, float v) { m_Auto.push_back({ name, GraphParam::Kind::Float, v }); };
		put("Speed", state.Speed);
		put("Forward", state.LocalVelocity.y);
		put("Strafe", state.LocalVelocity.x);
		put("YawRate", state.YawRate);
		put("Grounded", state.Grounded ? 1.0f : 0.0f);
		put("Airborne", state.Grounded ? 0.0f : 1.0f);
		put("Wading", state.WadeDepth > 0.25f ? 1.0f : 0.0f);
		put("WadeDepth", state.WadeDepth);
		put("Blocked", state.Blocked ? 1.0f : 0.0f);
		put("Landed", state.Landed ? 1.0f : 0.0f);
		put("LandingSpeed", state.LandingSpeed);
		put("Finished", finished ? 1.0f : 0.0f);

		std::vector<size_t> order;
		for (size_t i = 0; i < m_Graph.Transitions.size(); ++i)
		{
			const GraphTransition& t = m_Graph.Transitions[i];
			if ((t.From == m_Current || t.From == "*" || t.From.empty()) && t.To != m_Current) order.push_back(i);
		}
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return m_Graph.Transitions[a].Priority > m_Graph.Transitions[b].Priority; });
		for (size_t i : order)
		{
			const GraphTransition t = m_Graph.Transitions[i];
			if (t.MinStateTime > m_Time || (t.OnFinish && !finished)) continue;
			bool ok = true;
			for (const GraphCondition& c : t.Conditions) ok = ok && Holds(c, Read(c.Param));
			if (!ok) continue;
			Consume(t);
			m_Last = m_Current + " -> " + t.To;
			++m_Count;
			Enter(t.To, t.Blend, anim);
			break;
		}
	}
}
