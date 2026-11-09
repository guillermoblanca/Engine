#pragma once
#include "GB/Game/ActorWorld.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Scene.h"
#include <functional>
#include <memory>
#include <string>

namespace GB
{
	// Draws the actors of the game (GB/Game/Actor.h), which hold only data: the posed model of each one (its ModelInstance with its root matrix
	// and its tints; the model's GPU copy is ModelGpu::Of, kept by the model) and the props in its hands and on its back. A prop with a look of the
	// visuals goes to the look drawer (TribeVisuals::PropDrawer); without one, or when the drawer cannot draw it, it is a coloured box.
	// Owns the box's GPU mesh (made on the first box): destroy it while the OpenGL context is alive. Drawing actors without props needs no context.
	class ActorRenderer
	{
	public:
		// Draws a look at a prop's world matrix into the scene. False = draw the box.
		using PropDrawer = std::function<bool(const std::string& look, const glm::mat4& world, Scene& scene)>;
		void SetPropDrawer(PropDrawer drawer) { m_PropDrawer = std::move(drawer); }

		// One actor: its model first, then its props in the animation's prop order.
		void AddToScene(const Actor& actor, Scene& scene) const;
		// Every actor of the world in name order, but the suspended ones (Paused: not drawn).
		void AddToScene(const ActorWorld& world, Scene& scene) const;

	private:
		PropDrawer m_PropDrawer;
		mutable std::unique_ptr<Mesh> m_Box;
	};
}
