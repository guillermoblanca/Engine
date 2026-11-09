#pragma once
#include "GB/Game/Components/ActorStateSaver.h"
#include "GB/Game/Components/Presentation/AnimationComponent.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// What an actor holds in its hands and on its back ("box", "bag", "can", "shovel", "seeds", "tool", or a look of the visuals): the hands reach the
	// grips with IK and the prop follows its socket. It works on the animation's sockets. It does not draw: the visual layer reads ForEachDrawn
	// (GB/Visual/ActorRender.h), so this stays plain data with no GPU type in it.
	class PropsComponent : public GBObjectComponent, public IStateSaver
	{
	public:
		PropsComponent(GeometryComponent& geometry, AnimationComponent& animation) : m_Geometry(geometry), m_Animation(animation) {}

		bool Attach(const std::string& name);
		// A prop drawn by a look of the visuals (GB/Visual): `look` is handed to the prop drawer, `socket` is where it hangs ("hand_r", "hip_r",
		// "back"...; "carry" holds it in both arms between the palms) and `offset` places it in the socket's frame. The same name replaces the prop.
		bool AttachLook(const std::string& name, const std::string& socket, const std::string& look, const glm::mat4& offset);
		void Detach(const std::string& name);   // empty: all of them
		bool Equip(const std::string& name, bool on);   // also changes the walk for the box and the wheelbarrow
		std::vector<std::string> Names() const { return m_Animation.Anim() ? m_Animation.Anim()->PropNames() : std::vector<std::string>(); }
		std::string LookOf(const std::string& name) const { const auto it = m_Looks.find(name); return it == m_Looks.end() ? std::string() : it->second.Look; }
		std::string SocketOf(const std::string& name) const { const auto it = m_Looks.find(name); return it == m_Looks.end() ? std::string() : it->second.Socket; }

		void SetVisible(bool on) { m_Visible = on; }   // H16: far away they are a few pixels; they stay attached and come back when it is nearer
		bool Visible() const { return m_Visible; }
		// The props to draw now, in the animation's prop order (nothing when hidden or without an animation): each one's look ("" = none: a
		// coloured box), its world matrix, and the box's size and colour (the box is also what a look that cannot be drawn falls back to).
		using DrawVisitor = std::function<void(const std::string& look, const glm::mat4& world, const glm::vec3& boxSize, const glm::vec3& boxColor)>;
		void ForEachDrawn(const DrawVisitor& visit) const;

		// After the pose of the frame: a box waits for its overlay to finish before the grips are measured.
		void ResolvePending();

		void CaptureState(Save::ActorState& state) const override { state.Props = Names(); }

	private:
		struct PropLook { glm::vec3 Size{ 0.3f }; glm::vec3 Color{ 0.6f, 0.42f, 0.25f }; std::string Look, Socket; };

		GeometryComponent& m_Geometry;
		AnimationComponent& m_Animation;
		std::map<std::string, PropLook> m_Looks;
		bool m_Visible = true;
		std::string m_Pending;   // waits for the box overlay to finish before measuring the grips
	};
}
