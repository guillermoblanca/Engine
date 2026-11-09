#include "GB/Input/InputActions.h"
#include "GB/Input/InputSource.h"
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <utility>
#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		template <typename E>
		struct Named { const char* Name; E Value; };

		constexpr Named<Key> kKeys[] = {
			{ "Space", Key::Space }, { "Apostrophe", Key::Apostrophe }, { "Comma", Key::Comma }, { "Minus", Key::Minus }, { "Period", Key::Period },
			{ "Slash", Key::Slash },
			{ "D0", Key::D0 }, { "D1", Key::D1 }, { "D2", Key::D2 }, { "D3", Key::D3 }, { "D4", Key::D4 },
			{ "D5", Key::D5 }, { "D6", Key::D6 }, { "D7", Key::D7 }, { "D8", Key::D8 }, { "D9", Key::D9 },
			{ "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D }, { "E", Key::E }, { "F", Key::F }, { "G", Key::G },
			{ "H", Key::H }, { "I", Key::I }, { "J", Key::J }, { "K", Key::K }, { "L", Key::L }, { "M", Key::M }, { "N", Key::N },
			{ "O", Key::O }, { "P", Key::P }, { "Q", Key::Q }, { "R", Key::R }, { "S", Key::S }, { "T", Key::T }, { "U", Key::U },
			{ "V", Key::V }, { "W", Key::W }, { "X", Key::X }, { "Y", Key::Y }, { "Z", Key::Z },
			{ "Escape", Key::Escape }, { "Enter", Key::Enter }, { "Tab", Key::Tab }, { "Backspace", Key::Backspace }, { "Insert", Key::Insert },
			{ "Delete", Key::Delete }, { "Right", Key::Right }, { "Left", Key::Left }, { "Down", Key::Down }, { "Up", Key::Up },
			{ "PageUp", Key::PageUp }, { "PageDown", Key::PageDown }, { "Home", Key::Home }, { "End", Key::End },
			{ "F1", Key::F1 }, { "F2", Key::F2 }, { "F3", Key::F3 }, { "F4", Key::F4 }, { "F5", Key::F5 }, { "F6", Key::F6 },
			{ "F7", Key::F7 }, { "F8", Key::F8 }, { "F9", Key::F9 }, { "F10", Key::F10 }, { "F11", Key::F11 }, { "F12", Key::F12 },
			{ "LeftShift", Key::LeftShift }, { "LeftControl", Key::LeftControl }, { "LeftAlt", Key::LeftAlt }, { "LeftSuper", Key::LeftSuper },
			{ "RightShift", Key::RightShift }, { "RightControl", Key::RightControl }, { "RightAlt", Key::RightAlt }, { "RightSuper", Key::RightSuper },
		};
		constexpr Named<MouseButton> kMouse[] = { { "Left", MouseButton::Left }, { "Right", MouseButton::Right }, { "Middle", MouseButton::Middle } };
		constexpr Named<GamepadButton> kPadButtons[] = {
			{ "A", GamepadButton::A }, { "B", GamepadButton::B }, { "X", GamepadButton::X }, { "Y", GamepadButton::Y },
			{ "LeftBumper", GamepadButton::LeftBumper }, { "RightBumper", GamepadButton::RightBumper }, { "Back", GamepadButton::Back },
			{ "Start", GamepadButton::Start }, { "Guide", GamepadButton::Guide }, { "LeftStick", GamepadButton::LeftStick },
			{ "RightStick", GamepadButton::RightStick }, { "DPadUp", GamepadButton::DPadUp }, { "DPadRight", GamepadButton::DPadRight },
			{ "DPadDown", GamepadButton::DPadDown }, { "DPadLeft", GamepadButton::DPadLeft },
		};
		constexpr Named<GamepadAxis> kPadAxes[] = {
			{ "LeftX", GamepadAxis::LeftX }, { "LeftY", GamepadAxis::LeftY }, { "RightX", GamepadAxis::RightX }, { "RightY", GamepadAxis::RightY },
			{ "LeftTrigger", GamepadAxis::LeftTrigger }, { "RightTrigger", GamepadAxis::RightTrigger },
		};

		template <typename E, std::size_t N>
		bool Find(const Named<E> (&table)[N], const std::string& name, int& code)
		{
			for (const Named<E>& n : table)
				if (name == n.Name) { code = static_cast<int>(n.Value); return true; }
			return false;
		}

		bool TriggerDown(const IInputSource& input, const InputTrigger& t)
		{
			switch (t.Kind)
			{
			case InputTriggerKind::Key: return input.IsKeyDown(static_cast<Key>(t.Code));
			case InputTriggerKind::Mouse: return input.IsMouseDown(static_cast<MouseButton>(t.Code));
			case InputTriggerKind::PadButton: return input.IsGamepadButtonDown(static_cast<GamepadButton>(t.Code));
			case InputTriggerKind::StickAbove:
			{
				const GamepadAxis x = t.Code == 0 ? GamepadAxis::LeftX : GamepadAxis::RightX;
				const GamepadAxis y = t.Code == 0 ? GamepadAxis::LeftY : GamepadAxis::RightY;
				const glm::vec2 stick(input.GetGamepadAxis(x, 0, t.DeadZone), input.GetGamepadAxis(y, 0, t.DeadZone));
				return glm::length(stick) > t.Above;
			}
			}
			return false;
		}

		// The contribution of one binding and whether it is active.
		glm::vec2 Contribution(const IInputSource& input, const InputBinding& b, bool& active)
		{
			active = false;
			switch (b.Analog)
			{
			case InputAnalog::None:
				for (const InputTrigger& t : b.Any)
					if (TriggerDown(input, t)) { active = true; return b.Value; }
				return glm::vec2(0.0f);
			case InputAnalog::PadAxis:
			{
				const float raw = input.GetGamepadAxis(static_cast<GamepadAxis>(b.Axis), 0, b.DeadZone);
				const glm::vec2 v = (raw + b.Offset) * b.Value;
				active = v.x != 0.0f || v.y != 0.0f;
				return v;
			}
			case InputAnalog::MouseDelta:
			{
				const auto [dx, dy] = input.GetMouseDelta();
				const glm::vec2 v((static_cast<float>(dx) + b.Offset) * b.Value.x, (static_cast<float>(dy) + b.Offset) * b.Value.y);
				active = v.x != 0.0f || v.y != 0.0f;
				return v;
			}
			case InputAnalog::Scroll:
			{
				const glm::vec2 v = (input.GetScrollDelta() + b.Offset) * b.Value;
				active = v.x != 0.0f || v.y != 0.0f;
				return v;
			}
			}
			return glm::vec2(0.0f);
		}

		// ---- the defaults (assets/data/input.json says the same)
		InputTrigger KeyT(Key k) { return { InputTriggerKind::Key, static_cast<int>(k) }; }
		InputTrigger MouseT(MouseButton m) { return { InputTriggerKind::Mouse, static_cast<int>(m) }; }
		InputTrigger PadT(GamepadButton b) { return { InputTriggerKind::PadButton, static_cast<int>(b) }; }
		InputBinding Digital(glm::vec2 value, std::vector<InputTrigger> any)
		{
			InputBinding b;
			b.Value = value;
			b.Any = std::move(any);
			return b;
		}
		InputBinding PadAxisBinding(GamepadAxis axis, glm::vec2 value, float deadzone = 0.15f, float offset = 0.0f)
		{
			InputBinding b;
			b.Analog = InputAnalog::PadAxis;
			b.Axis = static_cast<int>(axis);
			b.Value = value;
			b.DeadZone = deadzone;
			b.Offset = offset;
			return b;
		}

		InputActions MakeDefaults()
		{
			const glm::vec2 up(0.0f, 1.0f), down(0.0f, -1.0f), right(1.0f, 0.0f), left(-1.0f, 0.0f);
			InputActions a;
			auto& m = a.Actions;
			m[std::string(InputAction::Move)] = {
				Digital(up, { KeyT(Key::W) }), Digital(down, { KeyT(Key::S) }), Digital(right, { KeyT(Key::D) }), Digital(left, { KeyT(Key::A) }),
				PadAxisBinding(GamepadAxis::LeftX, right), PadAxisBinding(GamepadAxis::LeftY, down) };
			m[std::string(InputAction::Jump)] = { Digital(right, { KeyT(Key::Space), PadT(GamepadButton::A) }) };
			InputTrigger stick{ InputTriggerKind::StickAbove, 0, 0.9f, 0.15f };
			m[std::string(InputAction::Run)] = { Digital(right, { KeyT(Key::LeftShift), PadT(GamepadButton::LeftStick), stick }) };
			m[std::string(InputAction::LookLeft)] = { Digital(right, { KeyT(Key::Left) }) };
			m[std::string(InputAction::LookRight)] = { Digital(right, { KeyT(Key::Right) }) };
			m[std::string(InputAction::LookUp)] = { Digital(right, { KeyT(Key::Up) }) };
			m[std::string(InputAction::LookDown)] = { Digital(right, { KeyT(Key::Down) }) };
			m[std::string(InputAction::TurnLeft)] = { Digital(right, { KeyT(Key::Q) }) };
			m[std::string(InputAction::TurnRight)] = { Digital(right, { KeyT(Key::E) }) };
			m[std::string(InputAction::LookStick)] = { PadAxisBinding(GamepadAxis::RightX, right), PadAxisBinding(GamepadAxis::RightY, down) };
			m[std::string(InputAction::LookDrag)] = { Digital(right, { MouseT(MouseButton::Left), MouseT(MouseButton::Right) }) };
			m[std::string(InputAction::CameraDrag)] = { Digital(right, { MouseT(MouseButton::Right) }) };
			InputBinding pointer;
			pointer.Analog = InputAnalog::MouseDelta;
			pointer.Value = glm::vec2(1.0f);
			m[std::string(InputAction::Pointer)] = { pointer };
			InputBinding wheel;
			wheel.Analog = InputAnalog::Scroll;
			m[std::string(InputAction::ZoomWheel)] = { wheel };
			m[std::string(InputAction::Pan)] = {
				Digital(up, { KeyT(Key::W), KeyT(Key::Up) }), Digital(down, { KeyT(Key::S), KeyT(Key::Down) }),
				Digital(right, { KeyT(Key::D), KeyT(Key::Right) }), Digital(left, { KeyT(Key::A), KeyT(Key::Left) }),
				PadAxisBinding(GamepadAxis::LeftX, right), PadAxisBinding(GamepadAxis::LeftY, down) };
			// Triggers report -1 (released) .. 1 (pressed): (v + 1) * 0.5 is 0 .. 1, without a dead zone.
			m[std::string(InputAction::Zoom)] = {
				Digital(left, { KeyT(Key::Z) }), Digital(right, { KeyT(Key::X) }),
				PadAxisBinding(GamepadAxis::RightTrigger, glm::vec2(0.5f, 0.0f), 0.0f, 1.0f), PadAxisBinding(GamepadAxis::LeftTrigger, glm::vec2(-0.5f, 0.0f), 0.0f, 1.0f) };
			return a;
		}

		// ---- loading
		bool ReadFloat(const Json& j, float& out) { if (!j.is_number()) return false; out = j.get<float>(); return true; }

		std::string ReadValue(const Json& j, glm::vec2& out)
		{
			if (j.is_number()) { out = glm::vec2(j.get<float>(), 0.0f); return ""; }
			if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) { out = glm::vec2(j[0].get<float>(), j[1].get<float>()); return ""; }
			return "\"value\" must be a number or [x, y]";
		}

		template <typename E, std::size_t N>
		std::string ReadNames(const Json& j, const char* field, const Named<E> (&table)[N], InputTriggerKind kind, std::vector<InputTrigger>& out)
		{
			if (!j.is_array()) return std::string("\"") + field + "\" must be a list of names";
			for (const Json& n : j)
			{
				int code = 0;
				if (!n.is_string() || !Find(table, n.get<std::string>(), code))
					return std::string("unknown name in \"") + field + "\": " + n.dump();
				out.push_back({ kind, code });
			}
			return "";
		}

		std::string ReadBinding(const Json& j, InputBinding& b)
		{
			if (!j.is_object()) return "a binding must be an object";
			const bool analog = j.contains("padAxis") || j.contains("mouseDelta") || j.contains("scroll");
			if (analog && (j.contains("keys") || j.contains("mouse") || j.contains("pad") || j.contains("stickAbove")))
				return "a binding is either digital (keys, mouse, pad, stickAbove) or analog (padAxis, mouseDelta, scroll)";
			if (j.contains("mouseDelta")) b.Value = glm::vec2(1.0f);
			for (auto it = j.begin(); it != j.end(); ++it)
			{
				const std::string& k = it.key();
				const Json& v = it.value();
				std::string error;
				if (k == "value") error = ReadValue(v, b.Value);
				else if (k == "keys") error = ReadNames(v, "keys", kKeys, InputTriggerKind::Key, b.Any);
				else if (k == "mouse") error = ReadNames(v, "mouse", kMouse, InputTriggerKind::Mouse, b.Any);
				else if (k == "pad") error = ReadNames(v, "pad", kPadButtons, InputTriggerKind::PadButton, b.Any);
				else if (k == "stickAbove")
				{
					InputTrigger t{ InputTriggerKind::StickAbove, 0, 0.9f, 0.15f };
					if (!v.is_object()) error = "\"stickAbove\" must be an object";
					for (auto s = v.begin(); error.empty() && s != v.end(); ++s)
					{
						if (s.key() == "stick")
						{
							if (s.value() == "Left") t.Code = 0;
							else if (s.value() == "Right") t.Code = 1;
							else error = "\"stick\" must be \"Left\" or \"Right\"";
						}
						else if (s.key() == "above") { if (!ReadFloat(s.value(), t.Above)) error = "\"above\" must be a number"; }
						else if (s.key() == "deadzone") { if (!ReadFloat(s.value(), t.DeadZone)) error = "\"deadzone\" must be a number"; }
						else error = "unknown field in \"stickAbove\": " + s.key();
					}
					if (error.empty()) b.Any.push_back(t);
				}
				else if (k == "padAxis")
				{
					b.Analog = InputAnalog::PadAxis;
					if (!v.is_string() || !Find(kPadAxes, v.get<std::string>(), b.Axis)) error = "unknown \"padAxis\": " + v.dump();
				}
				else if (k == "mouseDelta") { b.Analog = InputAnalog::MouseDelta; if (v != true) error = "\"mouseDelta\" must be true"; }
				else if (k == "scroll") { b.Analog = InputAnalog::Scroll; if (v != true) error = "\"scroll\" must be true"; }
				else if (k == "deadzone") { if (!ReadFloat(v, b.DeadZone)) error = "\"deadzone\" must be a number"; }
				else if (k == "offset") { if (!ReadFloat(v, b.Offset)) error = "\"offset\" must be a number"; }
				else error = "unknown field in a binding: " + k;
				if (!error.empty()) return error;
			}
			if (b.Analog == InputAnalog::None && b.Any.empty())
				return "a binding needs keys, mouse, pad, stickAbove, padAxis, mouseDelta or scroll";
			return "";
		}
	}

	glm::vec2 InputActions::Value(const IInputSource& input, std::string_view action) const
	{
		glm::vec2 sum(0.0f);
		const auto it = Actions.find(action);
		if (it == Actions.end()) return sum;
		bool active = false;
		for (const InputBinding& b : it->second)
			sum += Contribution(input, b, active);
		return sum;
	}

	bool InputActions::Pressed(const IInputSource& input, std::string_view action) const
	{
		const auto it = Actions.find(action);
		if (it == Actions.end()) return false;
		for (const InputBinding& b : it->second)
		{
			bool active = false;
			Contribution(input, b, active);
			if (active) return true;
		}
		return false;
	}

	const InputActions& DefaultInputActions()
	{
		static const InputActions defaults = MakeDefaults();
		return defaults;
	}

	std::string LoadInputActions(const std::string& json, InputActions& actions)
	{
		Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded()) return "input: not valid JSON";
		if (!doc.is_object() || !doc.contains("input")) return "";
		const Json& section = doc["input"];
		if (!section.is_object()) return "input: the section must be an object";
		InputActions staged = actions;
		for (auto it = section.begin(); it != section.end(); ++it)
		{
			if (it.key() != "actions") return "input: unknown field " + it.key();
			if (!it.value().is_object()) return "input: \"actions\" must be an object";
			for (auto a = it.value().begin(); a != it.value().end(); ++a)
			{
				if (!a.value().is_array()) return "input: action " + a.key() + ": a list of bindings";
				std::vector<InputBinding> bindings;
				for (const Json& j : a.value())
				{
					InputBinding b;
					if (const std::string error = ReadBinding(j, b); !error.empty()) return "input: action " + a.key() + ": " + error;
					bindings.push_back(std::move(b));
				}
				staged.Actions[a.key()] = std::move(bindings);
			}
		}
		actions = std::move(staged);
		return "";
	}

	std::string LoadInputActionsFile(const std::string& path, InputActions& actions)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "";
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadInputActions(ss.str(), actions);
		return error.empty() ? "" : path + ": " + error;
	}
}
