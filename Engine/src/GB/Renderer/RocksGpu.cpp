#include "GB/Renderer/RocksGpu.h"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	std::unique_ptr<RockGpu> RocksGpu::Upload(const Rock& source)
	{
		const RockParams& params = source.Params;
		auto rock = std::make_unique<RockGpu>();
		rock->Params = params;
		rock->Radius = source.Radius;
		rock->Height = source.Height;
		rock->TriangleCount = source.TriangleCount;
		rock->MeshPtr = std::make_unique<Mesh>(source.Geometry.Vertices, source.Geometry.Indices);
		Material& m = rock->Mat;
		m.Albedo = params.Color;
		m.Roughness = 0.88f;
		m.HeightScale = 0.0f;
		m.CavityStrength = 0.0f;
		m.Grain = 0.8f;
		m.GrainScale = 2.4f / std::max(params.Size, 0.3f);
		m.Moss = params.Moss;
		m.Sand = params.Sand;
		return rock;
	}

	void RockGpu::AddToScene(Scene& scene, const glm::vec3& position, float yaw, float scale, float sink, float waterLine) const
	{
		RenderObject o;
		o.MeshPtr = MeshPtr.get();
		o.Mat = Mat;
		o.Mat.BaseY = position.y - sink;
		o.Mat.WaterLine = waterLine;
		o.BoundingCenter = { 0.0f, Height * 0.5f, 0.0f };
		o.BoundingRadius = 1.05f * glm::length(glm::vec2(Radius, Height * 0.5f));
		o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position - glm::vec3(0.0f, sink, 0.0f)), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		scene.Objects.push_back(o);
	}
}
