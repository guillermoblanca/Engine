#pragma once
#include "Texture.h"
#include <glm/glm.hpp>

namespace GB
{
	enum class AlphaMode { Opaque, Mask, Blend };

	// Metallic/roughness PBR material (constant parameters; textures come with the glTF importer).
	struct Material
	{
		glm::vec3 Albedo{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		glm::vec3 Emissive{ 0.0f };

		const Texture2D* AlbedoMap = nullptr;
		const Texture2D* NormalMap = nullptr;
		const Texture2D* HeightMap = nullptr;
		float UVScale = 1.0f;
		float HeightScale = 0.1f;
		float NormalStrength = 1.0f;
		bool NormalFlipX = false;
		bool NormalFlipY = false;
		float CavityStrength = 0.6f;

		const Texture2D* MetallicRoughnessMap = nullptr;
		const Texture2D* OcclusionMap = nullptr;
		float OcclusionStrength = 1.0f;
		const Texture2D* EmissiveMap = nullptr;

		const Texture2D* MacroMap = nullptr;
		glm::vec4 MacroRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		const Texture2D* SurfaceNormalMap = nullptr;

		float TrailDepth = 0.0f;
		glm::vec3 TrailTint{ 1.0f };
		float TrailNormalBoost = 1.0f;

		float WindSway = 0.0f;
		float WindHeight = 1.0f;
		float WindFlutter = 0.0f;
		bool FoliageNormals = false;

		float Grain = 0.0f;
		float GrainScale = 3.0f;
		float Moss = 0.0f;
		glm::vec3 MossColor{ 0.20f, 0.36f, 0.10f };
		float Sand = 0.0f;
		glm::vec3 SandColor{ 0.76f, 0.66f, 0.45f };
		float BaseY = 0.0f;
		float WaterLine = -1e9f;

		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		float Opacity = 1.0f;
		bool DoubleSided = false;
	};
}
