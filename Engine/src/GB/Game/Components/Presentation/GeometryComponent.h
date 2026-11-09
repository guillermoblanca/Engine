#pragma once
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Game/CharacterController.h"
#include "GB/Game/GBObject.h"
#include "GB/Renderer/ModelGpu.h"
#include "GB/Renderer/Renderer.h"
#include <memory>

namespace GB
{
	// The shape of an actor: the model (an asset that other actors share), its own ModelInstance, the scale that fits it to the actor's height, and
	// the matrix that puts it where the body stands. The animation, the props and the ragdoll work on it. An actor without a model has none.
	class GeometryComponent : public GBObjectComponent
	{
	public:
		explicit GeometryComponent(const CharacterController& controller) : m_Controller(controller) {}

		// False when there is no model. The model is scaled to `height` metres.
		bool Build(std::shared_ptr<Model> model, float height);

		const std::shared_ptr<Model>& GetModel() const { return m_Model; }
		const ModelInstance* Instance() const { return m_Instance.get(); }
		ModelInstance* Instance() { return m_Instance.get(); }
		float Scale() const { return m_Scale; }
		void ScaleBy(float factor) { m_Scale *= factor; }
		glm::mat4 RootMatrix() const;
		void AddToScene(Scene& scene) const { if (m_Instance) GB::AddToScene(*m_Instance, scene, RootMatrix(), m_Tints.empty() ? nullptr : &m_Tints); }
		// U4: a colour per material of the model (its clan's, its trade's): empty = as the model is
		void SetTints(std::vector<glm::vec3> tints) { m_Tints = std::move(tints); }
		const std::vector<glm::vec3>& Tints() const { return m_Tints; }

		// The last to go: the animation, the props and the ragdoll that read it end before.
		void OnDestroy(GBObject&) override { m_Instance.reset(); m_Model.reset(); }

	private:
		const CharacterController& m_Controller;
		float m_Scale = 1.0f;
		std::shared_ptr<Model> m_Model;
		std::unique_ptr<ModelInstance> m_Instance;
		std::vector<glm::vec3> m_Tints;
	};
}
