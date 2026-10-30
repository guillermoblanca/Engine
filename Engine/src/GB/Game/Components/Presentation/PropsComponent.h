#pragma once
#include "GB/Game/Components/ActorStateSaver.h"
#include "GB/Game/Components/Presentation/AnimationComponent.h"
#include "GB/Game/GBObject.h"
#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// What an actor holds in its hands and on its back ("box", "bag", "can", "shovel", "seeds", "tool", or a look of the visuals): the hands reach the
	// grips with IK and the prop follows its socket. It works on the animation's sockets and draws with the geometry's matrix.
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

		// Draws the looks (without one, a prop is a coloured box): the look, the prop's world matrix and the scene. False = draw the box.
		using Drawer = std::function<bool(const std::string& look, const glm::mat4& world, Scene& scene)>;
		void SetDrawer(Drawer drawer) { m_Drawer = std::move(drawer); }
		void SetVisible(bool on) { m_Visible = on; }   // H16: far away they are a few pixels; they stay attached and come back when it is nearer
		void AddToScene(Scene& scene) const;

		// After the pose of the frame: a box waits for its overlay to finish before the grips are measured.
		void ResolvePending();

		void CaptureState(Save::ActorState& state) const override { state.Props = Names(); }

	private:
		struct PropLook { glm::vec3 Size{ 0.3f }; glm::vec3 Color{ 0.6f, 0.42f, 0.25f }; std::string Look, Socket; };

		GeometryComponent& m_Geometry;
		AnimationComponent& m_Animation;
		Drawer m_Drawer;
		std::map<std::string, PropLook> m_Looks;
		mutable std::unique_ptr<Mesh> m_Mesh;
		bool m_Visible = true;
		std::string m_Pending;   // waits for the box overlay to finish before measuring the grips
	};
}
