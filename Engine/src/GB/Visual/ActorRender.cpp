#include "GB/Visual/ActorRender.h"
#include "GB/Renderer/ModelGpu.h"
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	void ActorRenderer::AddToScene(const Actor& actor, Scene& scene) const
	{
		if (const GeometryComponent* geometry = actor.Geometry(); geometry && geometry->Instance())
			GB::AddToScene(*geometry->Instance(), scene, geometry->RootMatrix(), geometry->Tints().empty() ? nullptr : &geometry->Tints());
		if (const PropsComponent* props = actor.Props())
			props->ForEachDrawn([&](const std::string& look, const glm::mat4& world, const glm::vec3& boxSize, const glm::vec3& boxColor)
			{
				if (!look.empty() && m_PropDrawer && m_PropDrawer(look, world, scene)) return;
				if (!m_Box) m_Box = std::make_unique<Mesh>(Mesh::CreateCube(1.0f));
				RenderObject obj;
				obj.MeshPtr = m_Box.get();
				obj.Mat.Albedo = boxColor;
				obj.Mat.Roughness = 0.7f;
				obj.Transform = glm::scale(world, boxSize);
				scene.Objects.push_back(std::move(obj));
			});
	}

	void ActorRenderer::AddToScene(const ActorWorld& world, Scene& scene) const
	{
		for (const auto& [name, actor] : world.All())
			if (!actor->Paused()) AddToScene(*actor, scene);
	}
}
