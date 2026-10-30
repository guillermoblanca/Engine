#include "GB/Game/Components/Presentation/GeometryComponent.h"
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	bool GeometryComponent::Build(std::shared_ptr<Model> model, float height)
	{
		if (!model) return false;
		const float modelHeight = model->GetBoundsMax().y - model->GetBoundsMin().y;
		m_Scale = modelHeight > 1e-3f ? height / modelHeight : 1.0f;
		m_Model = std::move(model);
		m_Instance = std::make_unique<ModelInstance>(*m_Model);
		return true;
	}

	glm::mat4 GeometryComponent::RootMatrix() const
	{
		return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_Controller.GetPosition()), m_Controller.GetYaw(), glm::vec3(0, 1, 0)), glm::vec3(m_Scale));
	}
}
